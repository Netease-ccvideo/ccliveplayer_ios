//
//  cc_video_saver.c
//  IJKMediaPlayer
//
//  Created by 何钰堂s on 2020/12/24.
//  Copyright © 2020 bilibili. All rights reserved.
//

#include "cc_video_saver.h"
#include "ff_ffplay_def.h"
#import <CoreFoundation/CoreFoundation.h>

// 用于临时测试，使用c api获取沙盒文件路径
//const char * generate_tmp_path(){
//    CFURLRef home_url = CFCopyHomeDirectoryURL();
//    CFStringRef cfhome_path = CFURLGetString(home_url);
//
//    char * file_path = malloc(500);
//    memset(file_path, 0, 500);
//
//    char * tmp_file_path = malloc(500);
//    memset(tmp_file_path, 0, 500);
//
//    long home_path_size = CFStringGetLength(cfhome_path) + 1;
//    char * home_path = malloc(home_path_size);
//    memset(home_path, 0, home_path_size);
//    CFStringGetCString(cfhome_path, home_path, home_path_size, kCFStringEncodingUTF8);
//
//    strcat(tmp_file_path, home_path);
//    strcat(tmp_file_path, "/Documents/save_tmp.mp4");
//    strncpy(file_path, tmp_file_path+7, strlen(tmp_file_path)-7);
//
//    free(tmp_file_path);
//    free(home_path);
//    CFRelease(home_url);
//    return file_path;
//}


void init_fail(CCVideoSaver *saver){
    AVFormatContext *output_ctx = saver->output_ctx;
    ffp_notify_msg2(saver->ffp, FFP_MSG_VIDEO_SAVE, SaverEndInitialFail);
    if (output_ctx) {
        avformat_close_input(&output_ctx);
        saver->output_ctx = nil;
    }
    saver->save_statu = SaverStatuFail;
    int ret = remove(saver->file_path);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "CCVideoSaver - remove unfinish file fail %d\n", ret);
    }
}

void cc_packet_rescale_ts(AVPacket *inPkt, AVPacket *oPkt, AVRational src_tb, AVRational dst_tb){
    if (inPkt->pts != AV_NOPTS_VALUE)
        oPkt->pts = av_rescale_q(inPkt->pts, src_tb, dst_tb);
    if (inPkt->dts != AV_NOPTS_VALUE)
        oPkt->dts = av_rescale_q(inPkt->dts, src_tb, dst_tb);

    if (inPkt->duration > 0)
        oPkt->duration = av_rescale_q(inPkt->duration, src_tb, dst_tb);
}

void init_video_saver(CCVideoSaver *saver, AVFormatContext *input_ctx){
    if (!saver->file_path) {
        return;
    }
    saver->save_statu = SaverStatuUnfinish;
    saver->input_ctx = input_ctx;
    int ret = avformat_alloc_output_context2(&saver->output_ctx, NULL, NULL, saver->file_path);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "CCVideoSaver - Open output context failed %d\n", ret);
        init_fail(saver);
        return;
    }
    
    ret = avio_open2(&saver->output_ctx->pb, saver->file_path, AVIO_FLAG_READ_WRITE, NULL, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "CCVideoSaver - Open avio failed %d\n", ret);
        init_fail(saver);
        return;
    }
    
    for (int i=0; i<saver->input_ctx->nb_streams; i++) {
        AVCodec *codec = avcodec_find_decoder(saver->input_ctx->streams[i]->codecpar->codec_id);
        if (!codec) {
            continue;
        }
        AVStream *stream = avformat_new_stream(saver->output_ctx, codec);
        AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
        
        ret = avcodec_parameters_to_context(codec_ctx, saver->input_ctx->streams[i]->codecpar);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Failed to copy in_stream codecpar to codec context %d\n", ret);
            init_fail(saver);
            return;
        }
        codec_ctx->codec_tag = 0;
        if (saver->output_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
//
        ret = avcodec_parameters_from_context(stream->codecpar, codec_ctx);
        avcodec_close(codec_ctx);
        avcodec_free_context(&codec_ctx);
    
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Failed to copy codec context to out_stream codecpar context %d\n", ret);
            init_fail(saver);
            return;
        }
    }

    ret = avformat_write_header(saver->output_ctx, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "CCVideoSaver - format write header failed %d\n\n", ret);
        init_fail(saver);
    }
}

void set_save_path(CCVideoSaver *saver, const char *file_path){
    if (!file_path) {
        av_log(NULL, AV_LOG_ERROR, "CCVideoSaver - save path nil\n");
        return;
    }
    if (!access(file_path, 0)) {
        av_log(NULL, AV_LOG_ERROR, "CCVideoSaver - save path already exist\n");
        return;
    }
    saver->file_path = file_path;
}

void save_packet(CCVideoSaver *saver, AVPacket *in_pkt){
    if (!saver->file_path || (saver->save_statu != SaverStatuUnfinish)) {
        return;
    }

    AVFrame frame;
    AVPacket out_pkt;
    AVStream *input_stream = saver->input_ctx->streams[in_pkt->stream_index];
    AVStream *output_stream = saver->output_ctx->streams[in_pkt->stream_index];
    av_init_packet(&out_pkt);
    frame.key_frame = in_pkt->flags & AV_PKT_FLAG_KEY;
    
    cc_packet_rescale_ts(in_pkt, &out_pkt, input_stream->time_base, output_stream->time_base);
    out_pkt.stream_index = output_stream->index;
    out_pkt.flags = in_pkt->flags;
    out_pkt.data = in_pkt->data;
    out_pkt.size = in_pkt->size;
    
    int ret = av_interleaved_write_frame(saver->output_ctx, &out_pkt);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "CCVideoSaver - write stream err %d\n", ret);
    }
    
    output_stream->nb_frames++;
    av_packet_unref(&out_pkt);
}

void free_output_context(CCVideoSaver *saver){
    if (!saver->output_ctx) {
        return;
    }
    if (!(saver->output_ctx->oformat->flags & AVFMT_NOFILE) && saver->output_ctx->pb) {
        avio_close(saver->output_ctx->pb);
        saver->output_ctx->pb = NULL;
        
        //ffmpeg bug：avformat_new_stream 内存泄漏解决方式
        for (int i=0; i<saver->output_ctx->nb_streams; i++) {
            AVStream *st = saver->output_ctx->streams[i];
            if(st->codec->priv_data && st->codec->codec && st->codec->codec->priv_class){
                av_opt_free(st->codec->priv_data);
            }
            av_freep(&st->codec->priv_data);
        }
    }
    avformat_close_input(&(saver->output_ctx));
    saver->output_ctx = nil;
}

void save_fail(CCVideoSaver *saver){
    free_output_context(saver);
    saver->save_statu = SaverStatuFail;
}

void save_finish(CCVideoSaver *saver){
    if (!saver->file_path || (saver->save_statu != SaverStatuUnfinish)) {
        return;
    }
    
    int ret = 0;
    if (saver->output_ctx) {
        ret = av_write_trailer(saver->output_ctx);
        if (ret < 0) {
            ffp_notify_msg2(saver->ffp, FFP_MSG_VIDEO_SAVE, SaverEndWriteTrailerFail);
            save_fail(saver);
            return;
        }
    }
    
    if (saver->save_statu == SaverStatuFail && !access(saver->file_path, 0)) {
        ret = remove(saver->file_path);
        if (ret < 0) {
            ffp_notify_msg2(saver->ffp, FFP_MSG_VIDEO_SAVE, SaverEndRemoveFileFail);
        }
        return;
    }
    
    free_output_context(saver);
    saver->save_statu = SaverStatuSuccess;
    ffp_notify_msg2(saver->ffp, FFP_MSG_VIDEO_SAVE, 0);
}

void destory_video_saver(CCVideoSaver **ssaver){
    CCVideoSaver *saver = *ssaver;
    if (saver->file_path) {
        free_output_context(saver);
    }
    free(saver);
    *ssaver = nil;
}

void seek_handler(CCVideoSaver *saver){
    if (saver->save_statu == SaverStatuUnfinish) {
        saver->save_statu = SaverStatuFail;
        ffp_notify_msg2(saver->ffp, FFP_MSG_VIDEO_SAVE, SaverEndSeekFail);
    }
}

int stop_handler(CCVideoSaver *saver){
    if (saver->save_statu == SaverStatuUnstart) {
        return -2;
    }
    if (saver->save_statu == SaverStatuUnfinish) {
        saver->save_statu = SaverStatuFail;
        save_fail(saver);
        return -1;
    }
    return 0;
}


CCVideoSaver * new_video_saver(FFPlayer *ffp){
    CCVideoSaver *saver = malloc(sizeof(CCVideoSaver));
    memset(saver, 0, sizeof(CCVideoSaver));
    saver->ffp = ffp;
    saver->init_video_saver = init_video_saver;
    saver->save_packet = save_packet;
    saver->save_finish = save_finish;
    saver->save_statu = SaverStatuUnstart;
    saver->set_save_path = set_save_path;
    saver->seek_handler = seek_handler;
    saver->stop_handler = stop_handler;
    return saver;
}
