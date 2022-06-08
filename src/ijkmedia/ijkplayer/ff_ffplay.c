/*
 * ffplay_def.c
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2013 Zhang Rui <bbcallen@gmail.com>
 *
 * This file is part of ijkPlayer.
 *
 * ijkPlayer is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * ijkPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with ijkPlayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "ff_ffplay.h"
#include "cc_video_saver.h"
#include <math.h>
#include "ff_cmdutils.h"
#include "ff_fferror.h"
#include "ff_ffpipeline.h"
#include "ff_ffpipenode.h"
#include "ijkmeta.h"
#include "../ijksdl/ffmpeg/ijksdl_image_convert.h"
#include <stdatomic.h>
#if CONFIG_AVFILTER
# include "libavcodec/avcodec.h"
# include "libavfilter/buffersink.h"
# include "libavfilter/buffersrc.h"
#endif
#ifdef __APPLE__
#include "h264_sps_parser.h"
#include "ijksoundtouch_wrap.h"
#endif
// FIXME: 9 work around NDKr8e or gcc4.7 bug
// isnan() may not recognize some double NAN, so we test both double and float
#if defined(__ANDROID__)
#ifdef isnan
#undef isnan
#endif
#define isnan(x) (isnan((double)(x)) || isnanf((float)(x)))
#endif

#if defined(__ANDROID__)
#define printf(...) ALOGD(__VA_ARGS__)
#endif

#define FFP_XPS_PERIOD (3)
// #define FFP_SHOW_FPS
// #define FFP_SHOW_VDPS
// #define FFP_SHOW_AUDIO_DELAY
// #define FFP_SHOW_DEMUX_CACHE
// #define FFP_SHOW_BUF_POS
// #define FFP_SHOW_PKT_RECYCLE

// #define FFP_NOTIFY_BUF_TIME
// #define FFP_NOTIFY_BUF_BYTES

#define FFP_IO_STAT_STEP (50 * 1024)

#ifdef FFP_SHOW_VDPS
static int g_vdps_counter = 0;
static int64_t g_vdps_total_time = 0;
#endif

static int ffp_format_control_message(struct AVFormatContext *s, int type,
                                      void *data, size_t data_size);

#if CONFIG_AVFILTER
static inline
int cmp_audio_fmts(enum AVSampleFormat fmt1, int64_t channel_count1,
                   enum AVSampleFormat fmt2, int64_t channel_count2)
{
    /* If channel count == 1, planar and non-planar formats are the same */
    if (channel_count1 == 1 && channel_count2 == 1)
        return av_get_packed_sample_fmt(fmt1) != av_get_packed_sample_fmt(fmt2);
    else
        return channel_count1 != channel_count2 || fmt1 != fmt2;
}

static inline
int64_t get_valid_channel_layout(int64_t channel_layout, int channels)
{
    if (channel_layout && av_get_channel_layout_nb_channels(channel_layout) == channels)
        return channel_layout;
    else
        return 0;
}
#endif

#define FFP_BUF_MSG_PERIOD (3)

static AVPacket flush_pkt;

// FFP_MERGE: cmp_audio_fmts
// FFP_MERGE: get_valid_channel_layout

static void free_picture(Frame *vp);

static int packet_queue_put_private(PacketQueue *q, AVPacket *pkt)
{
    MyAVPacketList *pkt1;

    if (q->abort_request)
       return -1;

#ifdef FFP_MERGE
    pkt1 = av_malloc(sizeof(MyAVPacketList));
#else
    pkt1 = q->recycle_pkt;
    if (pkt1) {
        q->recycle_pkt = pkt1->next;
        q->recycle_count++;
    } else {
        q->alloc_count++;
        pkt1 = av_malloc(sizeof(MyAVPacketList));
    }
#ifdef FFP_SHOW_PKT_RECYCLE
    int total_count = q->recycle_count + q->alloc_count;
    if (!(total_count % 50)) {
        ALOGE("pkt-recycle \t%d + \t%d = \t%d\n", q->recycle_count, q->alloc_count, total_count);
    }
#endif
#endif
    if (!pkt1)
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;
    if (pkt == &flush_pkt)
        q->serial++;
    pkt1->serial = q->serial;

    if (!q->last_pkt)
        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size + sizeof(*pkt1);
    if (pkt1->pkt.duration > 0) {
        q->duration += pkt1->pkt.duration;
    }
    /* XXX: should duplicate packet data in DV case */
    CCSDL_CondSignal(q->cond);
    return 0;
}

static int64_t packet_queue_get_duration(PacketQueue *q) {
    if (q->duration > 0) {
        return q->duration;
    } else if (q->first_pkt && q->last_pkt && q->first_pkt != q->last_pkt) {
        return q->last_pkt->pkt.pts - q->first_pkt->pkt.pts;
    }
    return 0;
}

static int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
    int ret;

    /* duplicate the packet */
    if (pkt != &flush_pkt && av_dup_packet(pkt) < 0)
        return -1;

    CCSDL_LockMutex(q->mutex);
    ret = packet_queue_put_private(q, pkt);
    CCSDL_UnlockMutex(q->mutex);

    if (pkt != &flush_pkt && ret < 0)
        av_free_packet(pkt);

    return ret;
}

static int packet_queue_put_nullpacket(PacketQueue *q, int stream_index)
{
    AVPacket pkt1, *pkt = &pkt1;
    av_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
    pkt->stream_index = stream_index;
    return packet_queue_put(q, pkt);
}

/* packet queue handling */
static void packet_queue_init(PacketQueue *q)
{
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = CCSDL_CreateMutex();
    q->cond = CCSDL_CreateCond();
    q->abort_request = 1;
}

static void packet_queue_flush(PacketQueue *q)
{
    MyAVPacketList *pkt, *pkt1;

    CCSDL_LockMutex(q->mutex);
    for (pkt = q->first_pkt; pkt; pkt = pkt1) {
        pkt1 = pkt->next;
        av_free_packet(&pkt->pkt);
#ifdef FFP_MERGE
        av_freep(&pkt);
#else
        pkt->next = q->recycle_pkt;
        q->recycle_pkt = pkt;
#endif
    }
    q->last_pkt = NULL;
    q->first_pkt = NULL;
    q->nb_packets = 0;
    q->size = 0;
    q->duration = 0;
    CCSDL_UnlockMutex(q->mutex);
}

static void packet_queue_repush(PacketQueue *q)
{
    MyAVPacketList *pkt, *pkt1;
    
    int ret;
    CCSDL_LockMutex(q->mutex);
    for (pkt = q->first_pkt; pkt; pkt = pkt1) {
        pkt1 = pkt->next;
         q->first_pkt = pkt->next;
        if (!q->first_pkt)
            q->last_pkt = NULL;
        
//        if (&(pkt->pkt) == &flush_pkt)
//            break;
    
        if(ffp_is_flush_packet(&pkt->pkt))
            break;
        q->nb_packets--;
        q->size -= pkt->pkt.size + sizeof(*pkt);
        if (pkt->pkt.duration > 0)
            q->duration -= pkt->pkt.duration;
        
        pkt->serial = q->serial;
        ret = packet_queue_put_private(q, &pkt->pkt);
   
        pkt->next = q->recycle_pkt;
        q->recycle_pkt = pkt;
        
    }
    CCSDL_UnlockMutex(q->mutex);
}

static void packet_queue_destroy(PacketQueue *q)
{
    packet_queue_flush(q);

    CCSDL_LockMutex(q->mutex);
    while(q->recycle_pkt) {
        MyAVPacketList *pkt = q->recycle_pkt;
        if (pkt)
            q->recycle_pkt = pkt->next;
        av_freep(&pkt);
    }
    CCSDL_UnlockMutex(q->mutex);

    CCSDL_DestroyMutex(q->mutex);
    CCSDL_DestroyCond(q->cond);
}

static void packet_queue_abort(PacketQueue *q)
{
    CCSDL_LockMutex(q->mutex);

    q->abort_request = 1;

    CCSDL_CondSignal(q->cond);

    CCSDL_UnlockMutex(q->mutex);
}

static void packet_queue_start(PacketQueue *q)
{
    CCSDL_LockMutex(q->mutex);
    q->abort_request = 0;
    packet_queue_put_private(q, &flush_pkt);
    CCSDL_UnlockMutex(q->mutex);
}

static int frame_queue_nb_remaining(FrameQueue *f);
/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
static int packet_queue_get_l(FFPlayer *ffp, PacketQueue *q, AVPacket *pkt, int block, int *serial, bool is_audio_queue)
{
    MyAVPacketList *pkt1;
    int ret;

    for (;;) {
        if (q->abort_request) {
            ret = -1;
            break;
        }

        pkt1 = q->first_pkt;
        if (pkt1) {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;
            q->size -= pkt1->pkt.size + sizeof(*pkt1);
            if (pkt1->pkt.duration > 0)
                q->duration -= pkt1->pkt.duration;
            *pkt = pkt1->pkt;
            if (serial)
                *serial = pkt1->serial;
#ifdef FFP_MERGE
            av_free(pkt1);
#else
            pkt1->next = q->recycle_pkt;
            q->recycle_pkt = pkt1;
#endif
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            CCSDL_CondWaitTimeout(q->cond, q->mutex, 20);
            if (is_audio_queue) {
                if (!ffp->eof && !ffp->is->buffering_on && frame_queue_nb_remaining(&ffp->is->sampq) <= 0) {
                    ffp->is->stat_info.buffer_count++;
                    ffp_toggle_buffering(ffp, 1);
                    int64_t stuck_time = av_gettime_relative();
                    ffp->is->last_buffer_time = stuck_time;
                    ffp->is->last_stuck_time = stuck_time;
                }
            }
        }
    }
    return ret;
}

/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
static int packet_queue_get(FFPlayer *ffp, PacketQueue *q, AVPacket *pkt, int block, int *serial, bool is_audio_queue)
{
    int ret = 0;
    
    CCSDL_LockMutex(q->mutex);
    ret = packet_queue_get_l(ffp, q, pkt, block, serial, is_audio_queue);
    CCSDL_UnlockMutex(q->mutex);
    
    return ret;
}

static int packet_queue_video_cleanup(FFPlayer *ffp, PacketQueue *q, const int64_t buffer_ts, int64_t* pts)
{
    int buffer_packet_count = 0;
    int max_buffer_packet_count = 0;
    
    MyAVPacketList* item = NULL;
    int index = 0;
    int last_key_frame_index = -1;
    AVPacket tmp_packet;
    int serial;
    int remain_count = 0;
    
    CCSDL_LockMutex(q->mutex);
    
    *pts = 0;
    
    int64_t duration = q->duration;
    if (duration == 0 && q->first_pkt && q->last_pkt)
    {
        duration = q->last_pkt->pkt.pts - q->first_pkt->pkt.pts;
        struct AVRational ms_time_base;
        ms_time_base.den = 1000;
        ms_time_base.num = 1;
        duration = av_rescale_q(duration, ffp->is->video_st->time_base, ms_time_base);
    }
    
    if (*pts > 0 || q->nb_packets > 0 && buffer_ts > 0 && duration > buffer_ts) {
        buffer_packet_count = (int)(buffer_ts / (duration / q->nb_packets)) + 1;
        max_buffer_packet_count = buffer_packet_count * 3 / 2;
        
        if (*pts > 0 || q->nb_packets > max_buffer_packet_count) {
            //find the laest best position key frame ( I frame )
            item = q->first_pkt;
            bool (*func)(const AVPacket* pkt) = NULL;
#ifdef __APPLE__
            bool isHevc = ffp->is->viddec.avctx->codec_id == AV_CODEC_ID_HEVC;
            if(isHevc)
                func = ff_avpacket_hevc_is_i;
            else
                func = ff_avpacket_is_idr;
#endif
            if (*pts == 0) {
                while(item) {
                    bool isIdrOrKey = item->pkt.flags & AV_PKT_FLAG_KEY;
                    if(func) {
                        isIdrOrKey = func(&item->pkt);
                    }
                    if (isIdrOrKey) {
                        remain_count = q->nb_packets - index;
                        
                        if (remain_count > buffer_packet_count * 4 / 5) {
                            last_key_frame_index = index;
                        }
                        
                        if (remain_count <= buffer_packet_count) {
                            break;
                        }
                    }
                    index++;
                    item = item->next;
                }
            }
            else
            {
                while(item) {
                    const AVPacket* pkt = &item->pkt;
                    bool isIdrOrKey = item->pkt.flags & AV_PKT_FLAG_KEY;
                    if(func) {
                        isIdrOrKey = func(pkt);
                    }
                    if (pkt->pts < *pts && isIdrOrKey)
                    {
                        last_key_frame_index = index;
                    }
                    if (pkt->pts >= *pts) {
                        break;
                    }
                    index++;
                    item = item->next;
                }
            }
            
            unsigned char* video_new_extra_data = NULL;
            int size_data_size = 0;
            
            if (last_key_frame_index > 0) {
                ALOGW("video cleanup. nb_packets:%d, last_key:%d, remain:%d, buffer_packet_count:%d, buffer_ts:%lld\n", q->nb_packets, last_key_frame_index, q->nb_packets - last_key_frame_index, buffer_packet_count, buffer_ts);
                
                //clear over frames
                for(index = 0; index < last_key_frame_index; index++) {
                    if (packet_queue_get_l(ffp, q, &tmp_packet, 0, &serial, false) > 0) {
                        uint8_t *size_data = av_packet_get_side_data(&tmp_packet, AV_PKT_DATA_NEW_EXTRADATA, &size_data_size);
                        if (size_data && size_data_size > 7) {
                            video_new_extra_data = av_memdup(size_data, size_data_size);
                              ALOGF("[WHC] packet_queue_video_cleanup save new_extra_data pre_index %d\n",index);
                        }
                        av_packet_unref(&tmp_packet);
                    }
                }
            }
            
            if (q->first_pkt) {
                *pts = q->first_pkt->pkt.pts;
                AVPacket *pkt = &q->first_pkt->pkt;
                ALOGF("[WHC] packet_queue_video_cleanup now first pkt pts %lld \n",pkt->pts);
                if(video_new_extra_data) {
                    ALOGF("[WHC] packet_queue_video_cleanup add new extra data in key_frame_index %d \n",last_key_frame_index);
                    int new_extradata_size = size_data_size;
                    uint8_t *side = av_packet_new_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA, new_extradata_size);
                    if (side) {
                        memcpy(side, video_new_extra_data, new_extradata_size);
                    }
                }
            }
            if(video_new_extra_data)
                free(video_new_extra_data);
            video_new_extra_data = NULL;
        }else {
            ALOGF("[clean fail] packet not enough, nb_packets(%d) max_buffer_packet_count(%d) buffer_ts(%lld) duration(%lld)", q->nb_packets, max_buffer_packet_count, buffer_ts, duration);
        }
    }else {
        ALOGF("[clean fail] ts not enough, nb_packets(%d) buffer_ts(%lld) duration(%lld)", q->nb_packets, buffer_ts, duration);
    }
    CCSDL_UnlockMutex(q->mutex);
    if ((last_key_frame_index != -1) && (last_key_frame_index != 0)) {
        return 1;
    }
    return 0;
}

static int packet_queue_audio_cleanup(FFPlayer *ffp, PacketQueue *q, const int64_t pts)
{
    MyAVPacketList* item = NULL;
    int index = 0;
    int pos = -1;
    AVPacket tmp_packet;
    int serial;
    
    CCSDL_LockMutex(q->mutex);
    item = q->first_pkt;
    while(item) {
        const AVPacket* pkt = &item->pkt;
        if (pkt->pts > pts) {
            pos = index;
            break;
        }
        index++;
        item = item->next;
    }
    
    if (pos > 0) {
        pos -= 1;
    }
    
    for(index = 0; index < pos; index++) {
        if (packet_queue_get_l(ffp, q, &tmp_packet, 0, &serial, true) > 0) {
            av_free_packet(&tmp_packet);
        }
    }
    CCSDL_UnlockMutex(q->mutex);
    return 0;
}

static int packet_queue_get_or_buffering(FFPlayer *ffp, PacketQueue *q, bool is_audio_queue, AVPacket *pkt, int *serial, int *finished)
{
    assert(finished);
    
    while (1) {
        int new_packet = packet_queue_get(ffp, q, pkt, 0, serial, is_audio_queue);
        if (new_packet < 0)
            return -1;
        else if (new_packet == 0) {
            new_packet = packet_queue_get(ffp, q, pkt, 1, serial, is_audio_queue);
            if (new_packet < 0)
                return -1;
        }

        if (*finished == *serial) {
            av_free_packet(pkt);
            continue;
        }
        else
            break;
    }

    return 1;
}

static void decoder_init(Decoder *d, AVCodecContext *avctx, PacketQueue *queue, CCSDL_cond *empty_queue_cond) {
    memset(d, 0, sizeof(Decoder));
    d->avctx = avctx;
    d->queue = queue;
    d->empty_queue_cond = empty_queue_cond;
    d->start_pts = AV_NOPTS_VALUE;
    
    d->first_frame_decoded_time = CCSDL_GetTickHR();
    d->first_frame_decoded = 0;
}

static int decoder_decode_frame(FFPlayer *ffp, Decoder *d, AVFrame *frame, AVSubtitle *sub) {
    int got_frame = 0;

    do {
        int ret = -1;

        if (d->queue->abort_request)
            return -1;

        if (!d->packet_pending || d->queue->serial != d->pkt_serial) {
            AVPacket pkt;
            do {
                if (d->queue->nb_packets == 0)
                    CCSDL_CondSignal(d->empty_queue_cond);
                if (packet_queue_get_or_buffering(ffp, d->queue, d->avctx->codec_type == AVMEDIA_TYPE_AUDIO, &pkt, &d->pkt_serial, &d->finished) < 0)
                    return -1;

                if (pkt.data == flush_pkt.data) {
                    avcodec_flush_buffers(d->avctx);
                    d->finished = 0;
                    d->next_pts = d->start_pts;
                    d->next_pts_tb = d->start_pts_tb;
                }
//                bool a = pkt.data == flush_pkt.data;
//                bool b = d->queue->serial != d->pkt_serial;
                //d->pkt_serial = d->queue->serial;
//                ALOGI("before decoding flush get pkt video nb %d  a %d b %d c %d d %d\n",ffp->is->videoq.nb_packets, a, b, d->queue->serial,d->pkt_serial );
            } while (pkt.data == flush_pkt.data || d->queue->serial != d->pkt_serial);
            av_free_packet(&d->pkt);
            d->pkt_temp = d->pkt = pkt;
            d->packet_pending = 1;
        }

        switch (d->avctx->codec_type) {
            case AVMEDIA_TYPE_VIDEO: {
                
#ifdef FFP_SHOW_VDPS
                int64_t start = CCSDL_GetTickHR();
#endif
                //ALOGI("before decoding %lld %lld \n", d->pkt_temp.pts,d->pkt_temp.dts);
                ret = avcodec_decode_video2(d->avctx, frame, &got_frame, &d->pkt_temp);
                //ALOGE("after decoding %lld, got_frame = %d, input (%lld %lld), output(%lld %lld %lld)\n", (av_gettime_relative()- ffp->is->stream_open_time)/1000, got_frame, d->pkt_temp.dts, d->pkt_temp.pts, frame->pkt_dts, frame->pkt_pts, frame->pts);
#ifdef FFP_SHOW_VDPS
                int64_t dur = CCSDL_GetTickHR() - start;
                g_vdps_total_time += dur;
                g_vdps_counter++;
                int64_t avg_frame_time = 0;
                if (g_vdps_counter > 0)
                    avg_frame_time = g_vdps_total_time / g_vdps_counter;
                double fps = 0;
                if (avg_frame_time > 0)
                    fps = 1.0f / avg_frame_time * 1000;
                if (dur >= 30) {
                    ALOGE("vdps: [%f][%d] %"PRId64" ms/frame, vdps=%f, +%"PRId64"\n",
                          frame->pts, g_vdps_counter, (int64_t)avg_frame_time, fps, dur);
                }
                if (g_vdps_total_time >= FFP_XPS_PERIOD) {
                    g_vdps_total_time -= avg_frame_time;
                    g_vdps_counter--;
                }
#endif
                if (got_frame) {
                    if (ffp->decoder_reorder_pts == -1) {
                        frame->pts = av_frame_get_best_effort_timestamp(frame);
                    } else if (ffp->decoder_reorder_pts) {
                        frame->pts = frame->pkt_pts;
                    } else {
                        frame->pts = frame->pkt_dts;
                    }
//                    ALOGI("after decoding %lld, got_frame = %d, pts %d\n", (av_gettime_relative()- ffp->is->stream_open_time)/1000, got_frame, frame->pts);
                }
                }
                break;
            case AVMEDIA_TYPE_AUDIO:
                ret = avcodec_decode_audio4(d->avctx, frame, &got_frame, &d->pkt_temp);
                if (got_frame) {
                    AVRational tb = (AVRational){1, frame->sample_rate};
                    if (frame->pts != AV_NOPTS_VALUE)
                        frame->pts = av_rescale_q(frame->pts, d->avctx->time_base, tb);
                    else if (frame->pkt_pts != AV_NOPTS_VALUE)
                        frame->pts = av_rescale_q(frame->pkt_pts, av_codec_get_pkt_timebase(d->avctx), tb);
                    else if (d->next_pts != AV_NOPTS_VALUE)
                        frame->pts = av_rescale_q(d->next_pts, d->next_pts_tb, tb);
                    if (frame->pts != AV_NOPTS_VALUE) {
                        d->next_pts = frame->pts + frame->nb_samples;
                        d->next_pts_tb = tb;
                    }
                }
                break;
            // FFP_MERGE: case AVMEDIA_TYPE_SUBTITLE:
            case AVMEDIA_TYPE_SUBTITLE:
                ret = avcodec_decode_subtitle2(d->avctx, sub, &got_frame, &d->pkt_temp);
                break;
            default:
                break;
        }

        if (ret < 0) {
            d->packet_pending = 0;
        } else {
            d->pkt_temp.dts =
            d->pkt_temp.pts = AV_NOPTS_VALUE;
            if (d->pkt_temp.data) {
                if (d->avctx->codec_type != AVMEDIA_TYPE_AUDIO)
                    ret = d->pkt_temp.size;
                d->pkt_temp.data += ret;
                d->pkt_temp.size -= ret;
                if (d->pkt_temp.size <= 0)
                    d->packet_pending = 0;
            } else {
                if (!got_frame) {
                    d->packet_pending = 0;
                    d->finished = d->pkt_serial;
                }
            }
        }
    } while (!got_frame && !d->finished);

    return got_frame;
}

static void decoder_destroy(Decoder *d) {
    av_packet_unref(&d->pkt);
    avcodec_free_context(&d->avctx);
    avcodec_close(d->avctx);
}

static void frame_queue_unref_item(Frame *vp)
{
    av_frame_unref(vp->frame);
    CCSDL_VoutUnrefYUVOverlay(vp->bmp);
//#ifdef FFP_MERGE
    avsubtitle_free(&vp->sub);
//#endif
}

static int frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size, int keep_last)
{
    int i;
    memset(f, 0, sizeof(FrameQueue));
    if (!(f->mutex = CCSDL_CreateMutex()))
        return AVERROR(ENOMEM);
    if (!(f->cond = CCSDL_CreateCond()))
        return AVERROR(ENOMEM);
    f->pktq = pktq;
    f->max_size = FFMIN(max_size, FRAME_QUEUE_SIZE);
    f->keep_last = !!keep_last;
    for (i = 0; i < f->max_size; i++)
        if (!(f->queue[i].frame = av_frame_alloc()))
            return AVERROR(ENOMEM);
    return 0;
}

static void frame_queue_destory(FrameQueue *f)
{
    int i;
    for (i = 0; i < f->max_size; i++) {
        Frame *vp = &f->queue[i];
        frame_queue_unref_item(vp);
        av_frame_free(&vp->frame);
        free_picture(vp);
    }
    CCSDL_DestroyMutex(f->mutex);
    CCSDL_DestroyCond(f->cond);
}

static void frame_queue_signal(FrameQueue *f)
{
    CCSDL_LockMutex(f->mutex);
    CCSDL_CondSignal(f->cond);
    CCSDL_UnlockMutex(f->mutex);
}

static Frame *frame_queue_peek(FrameQueue *f)
{
    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}

static Frame *frame_queue_peek_next(FrameQueue *f)
{
    return &f->queue[(f->rindex + f->rindex_shown + 1) % f->max_size];
}

static Frame *frame_queue_peek_last(FrameQueue *f)
{
    return &f->queue[f->rindex];
}

static Frame *frame_queue_peek_writable(FrameQueue *f)
{
    /* wait until we have space to put a new frame */
    CCSDL_LockMutex(f->mutex);
    while (f->size >= f->max_size &&
           !f->pktq->abort_request) {
        CCSDL_CondWait(f->cond, f->mutex);
    }
    CCSDL_UnlockMutex(f->mutex);

    if (f->pktq->abort_request)
        return NULL;

    return &f->queue[f->windex];
}

static Frame *frame_queue_peek_readable(FrameQueue *f)
{
    /* Don't block here or ios audio output may fail */
    
    Frame *frame = NULL;
    CCSDL_LockMutex(f->mutex);
    if (!f->pktq->abort_request && f->size - f->rindex_shown > 0) {
        frame = &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
    }
    CCSDL_UnlockMutex(f->mutex);
    return frame;
    
//    while (f->size - f->rindex_shown <= 0 &&
//           !f->pktq->abort_request) {
//        CCSDL_CondWait(f->cond, f->mutex);
//    }
//    CCSDL_UnlockMutex(f->mutex);
//
//    if (f->pktq->abort_request)
//        return NULL;
//
//    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}
Frame *frame_queue_peek_readable_block(FrameQueue *f)
{
    /* wait until we have a readable a new frame */
    CCSDL_LockMutex(f->mutex);
    while (f->size - f->rindex_shown <= 0 &&
           !f->pktq->abort_request) {
        CCSDL_CondWait(f->cond, f->mutex);
    }
    CCSDL_UnlockMutex(f->mutex);

    if (f->pktq->abort_request)
        return NULL;

    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}
static void frame_queue_push(FrameQueue *f)
{
    if (++f->windex == f->max_size)
        f->windex = 0;
    CCSDL_LockMutex(f->mutex);
    f->size++;
    CCSDL_CondSignal(f->cond);
    CCSDL_UnlockMutex(f->mutex);
}

static void frame_queue_next(FrameQueue *f)
{
    if (f->keep_last && !f->rindex_shown) {
        f->rindex_shown = 1;
        return;
    }
    frame_queue_unref_item(&f->queue[f->rindex]);
    if (++f->rindex == f->max_size)
        f->rindex = 0;
    CCSDL_LockMutex(f->mutex);
    f->size--;
    CCSDL_CondSignal(f->cond);
    CCSDL_UnlockMutex(f->mutex);
}

/* jump back to the previous frame if available by resetting rindex_shown */
static int frame_queue_prev(FrameQueue *f)
{
    int ret = f->rindex_shown;
    f->rindex_shown = 0;
    return ret;
}

/* return the number of undisplayed frames in the queue */
static int frame_queue_nb_remaining(FrameQueue *f)
{
    return f->size - f->rindex_shown;
}

/* return last shown position */
#ifdef FFP_MERGE
static int64_t frame_queue_last_pos(FrameQueue *f)
{
    Frame *fp = &f->queue[f->rindex];
    if (f->rindex_shown && fp->serial == f->pktq->serial)
        return fp->pos;
    else
        return -1;
}
#endif

// FFP_MERGE: fill_rectangle
// FFP_MERGE: fill_border
// FFP_MERGE: ALPHA_BLEND
// FFP_MERGE: RGBA_IN
// FFP_MERGE: YUVA_IN
// FFP_MERGE: YUVA_OUT
// FFP_MERGE: BPP
// FFP_MERGE: blend_subrect

static void free_picture(Frame *vp)
{
    if (vp->bmp) {
        CCSDL_VoutFreeYUVOverlay(vp->bmp);
        vp->bmp = NULL;
    }
}

static int parse_ass_subtitle(const char *ass, char *output)
{
    char *tok = NULL;
    tok = strchr(ass, ':'); if (tok) tok += 1; // skip event
    tok = strchr(tok, ','); if (tok) tok += 1; // skip layer
    tok = strchr(tok, ','); if (tok) tok += 1; // skip start_time
    tok = strchr(tok, ','); if (tok) tok += 1; // skip end_time
    tok = strchr(tok, ','); if (tok) tok += 1; // skip style
    tok = strchr(tok, ','); if (tok) tok += 1; // skip name
    tok = strchr(tok, ','); if (tok) tok += 1; // skip margin_l
    tok = strchr(tok, ','); if (tok) tok += 1; // skip margin_r
    tok = strchr(tok, ','); if (tok) tok += 1; // skip margin_v
    tok = strchr(tok, ','); if (tok) tok += 1; // skip effect
    if (tok) {
        char *text = tok;
        int idx = 0;
        do {
            char *found = strstr(text, "\\N");
            if (found) {
                int n = found - text;
                memcpy(output+idx, text, n);
                output[idx + n] = '\n';
                idx = n + 1;
                text = found + 2;
            }
            else {
                int left_text_len = strlen(text);
                memcpy(output+idx, text, left_text_len);
                if (output[idx + left_text_len - 1] == '\n')
                    output[idx + left_text_len - 1] = '\0';
                else
                    output[idx + left_text_len] = '\0';
                break;
            }
        } while(1);
        return strlen(output) + 1;
    }
    return 0;
}

// FFP_MERGE: calculate_display_rect
// FFP_MERGE: video_image_display

#ifdef FFP_SHOW_FPS
static int g_fps_counter = 0;
static int64_t g_fps_total_time = 0;
#endif
static void video_image_display2(FFPlayer *ffp)
{
    VideoState *is = ffp->is;
    Frame *vp;
//    DisplayFrame displayFrame;
//    CCSDL_VoutOverlay *pic;

    vp = frame_queue_peek(&is->pictq);
    if (vp->bmp) {
#ifdef FFP_SHOW_FPS
        
        int64_t start = CCSDL_GetTickHR();
#endif
//        if (is->onDisplayFrameCb)
//        {
//            pic = vp->bmp;
//            displayFrame.data = pic->pixels[0];
//            if(pic->format == CCSDL_FCC_I420)
//                displayFrame.format =  FRAME_FORMAT_YUV;
//            else if(pic->format == CCSDL_FCC_RV32)
//                displayFrame.format =  FRAME_FORMAT_BGR;
//            else
//                displayFrame.format =  FRAME_FORMAT_NONE;
//            displayFrame.width = pic->w;
//            displayFrame.height = pic->h;
//            displayFrame.pitch = pic->pitches[0];
//            displayFrame.bits = displayFrame.format == FRAME_FORMAT_YUV? 8:32;
//            is->onDisplayFrameCb(&displayFrame, is->display_frame_obj);
//        }
        Frame *sp = NULL;
        if (is->subtitle_st) {
            if (frame_queue_nb_remaining(&is->subpq) > 0) {
                sp = frame_queue_peek(&is->subpq);
                if (vp->pts >= sp->pts + ((float) sp->sub.start_display_time / 1000)) {
                    if (!sp->uploaded) {
                        if (sp->sub.num_rects > 0) {
                            char buffered_text[4096];
                            if (sp->sub.rects[0]->text) {
                                strncpy(buffered_text, sp->sub.rects[0]->text, 4096);
                            }
                            else if (sp->sub.rects[0]->ass) {
                                parse_ass_subtitle(sp->sub.rects[0]->ass, buffered_text);
                            }
                            ffp_notify_msg4(ffp, FFP_MSG_TIMED_TEXT, 0, 0, buffered_text, sizeof(buffered_text));
                        }
                        sp->uploaded = 1;
                    }
                }
            }
        }
        //ALOGI("stream_open--->before CCSDL_VoutDisplayYUVOverlay = %lld ms\n", (av_gettime_relative() - ffp->is->stream_open_time) / 1000);
        if (ffp->show_log > 0) {
            ffp->show_log--;
            ALOGI("[dir] display video frame %f at %lld\n", vp->pts, av_gettime_relative() / 1000);
        }
        int display_ret = CCSDL_VoutDisplayYUVOverlay(ffp->vout, vp->bmp);
        //ALOGI("stream_open--->CCSDL_VoutDisplayYUVOverlay = %lld ms\n", (av_gettime_relative() - ffp->is->stream_open_time) / 1000);
#ifdef FFP_SHOW_FPS
        int64_t dur = CCSDL_GetTickHR() - start;
        g_fps_total_time += dur;
        g_fps_counter++;
        int64_t avg_frame_time = 0;
        if (g_fps_counter > 0)
            avg_frame_time = g_fps_total_time / g_fps_counter;
        double fps = 0;
        if (avg_frame_time > 0)
            fps = 1.0f / avg_frame_time * 1000;
        ALOGE("fps:  [%f][%d] %"PRId64" ms/frame, fps=%f, +%"PRId64"\n",
            vp->pts, g_fps_counter, (int64_t)avg_frame_time, fps, dur);
        if (g_fps_total_time >= FFP_XPS_PERIOD) {
            g_fps_total_time -= avg_frame_time;
            g_fps_counter--;
        }
#endif
        if (display_ret >= 0) {
            if (!ffp->display_ready) {
                __sync_fetch_and_or(&ffp->display_ready, 1);
                ffp_notify_msg1(ffp, FFP_MSG_RESTORE_VIDEO_PLAY);
                if (!ffp->first_video_frame_rendered) {
                    ffp->first_video_frame_rendered = 1;
                    if(!isnan(vp->pts))
                        ffp->first_video_pts = vp->pts;
                    ffp_notify_msg1(ffp, FFP_MSG_VIDEO_RENDERING_START);
                }
//                ALOGI("stream_open--->display first frame time = %lld ms\n", (av_gettime_relative() - ffp->is->stream_open_time) / 1000);
            }
        }
        
        if (is->latest_video_seek_load_serial == vp->serial) {
            int latest_video_seek_load_serial = __atomic_exchange_n(&(is->latest_video_seek_load_serial), -1, memory_order_seq_cst);
            if (latest_video_seek_load_serial == vp->serial) {
                ALOGF("accurate_seek post msg video render start \n");
                if (ffp->av_sync_type == AV_SYNC_VIDEO_MASTER) {
                    ffp_notify_msg2(ffp, FFP_MSG_VIDEO_SEEK_RENDERING_START, 1);
                } else {
                    ffp_notify_msg2(ffp, FFP_MSG_VIDEO_SEEK_RENDERING_START, 0);
                }
            }
        }
    }
}

//simulate onPreRenderbuffer onPostRenderbuffer event
static void video_no_image_display(FFPlayer *ffp)
{
    VideoState *is = ffp->is;
    if (is->video_st) {
        Frame *lastvp = frame_queue_peek_last(&is->pictq);
        if (lastvp->bmp) {
            ALOGI("[dir] video_no_image_display frame pts %f\n",lastvp->pts);
            CCSDL_VoutDisplayYUVOverlay(ffp->vout, lastvp->bmp);
        }
    }
}

// FFP_MERGE: compute_mod
// FFP_MERGE: video_audio_display

static void stream_close(VideoState *is)
{
    /* XXX: use a special url_shutdown call to abort parse cleanly */
    is->abort_request = 1;
    packet_queue_abort(&is->videoq);
    packet_queue_abort(&is->audioq);
    ALOGW("begin wait for read_tid\n");
    CCSDL_WaitThread(is->read_tid, NULL);
    ALOGW("end wait for read_tid\n");
    ALOGW("begin wait for video_refresh_tid\n");
    CCSDL_WaitThread(is->video_refresh_tid, NULL);
    ALOGW("end wait for video_refresh_tid\n");

    packet_queue_destroy(&is->videoq);
    packet_queue_destroy(&is->audioq);
//#ifdef FFP_MERGE
    packet_queue_destroy(&is->subtitleq);
//#endif

    /* free all pictures */
    frame_queue_destory(&is->pictq);
    if (is->capture_frame_data) {
        av_free(is->capture_frame_data);
        is->capture_frame_data = NULL;
    }
//#ifdef FFP_MERGE
    frame_queue_destory(&is->subpq);
//#endif
    frame_queue_destory(&is->sampq);
    
    CCSDL_DestroyCond(is->audio_accurate_seek_cond);
    CCSDL_DestroyCond(is->video_accurate_seek_cond);
    CCSDL_DestroyMutex(is->accurate_seek_mutex);
    
    CCSDL_DestroyCond(is->continue_read_thread);
    CCSDL_DestroyMutex(is->play_mutex);
#if !CONFIG_AVFILTER
    sws_freeContext(is->img_convert_ctx);
#endif
    if (is->handle != NULL) {
        ALOGI("[soundtouch] soundtouch handle destory %p is %p\n", is->handle, is);
        ijk_soundtouch_destroy(is->handle);
        is->handle = NULL;
        if (is->audio_new_buf) {
            ALOGI("[soundtouch] free audio_new_buf\n");
            av_free(is->audio_new_buf);
        }
    }
    av_free(is);
}

// FFP_MERGE: do_exit
// FFP_MERGE: sigterm_handler
// FFP_MERGE: video_open
// FFP_MERGE: video_display

/* display the current picture, if any */
static void video_display2(FFPlayer *ffp)
{
    ffp->last_display = ffp_get_current_position_l(ffp);
    VideoState *is = ffp->is;
    if (is->video_st)
        video_image_display2(ffp);
}

static double get_clock(Clock *c)
{
    if (*c->queue_serial != c->serial)
        return NAN;
    if (c->paused) {
        return c->pts;
    } else {
        double time = av_gettime_relative() / 1000000.0;
        return c->pts_drift + time - (time - c->last_updated) * (1.0 - c->speed);
    }
}

static void set_clock_at(Clock *c, double pts, int serial, double time)
{
    c->pts = pts;
    c->last_updated = time;
    c->pts_drift = c->pts - time;
    c->serial = serial;
}

static void set_clock(Clock *c, double pts, int serial)
{
    double time = av_gettime_relative() / 1000000.0;
    set_clock_at(c, pts, serial, time);
}

static void set_clock_speed(Clock *c, double speed)
{
    set_clock(c, get_clock(c), c->serial);
    c->speed = speed;
}

static void init_clock(Clock *c, int *queue_serial)
{
    c->speed = 1.0;
    c->paused = 0;
    c->queue_serial = queue_serial;
    set_clock(c, NAN, -1);
}

static void sync_clock_to_slave(Clock *c, Clock *slave)
{
    double clock = get_clock(c);
    double slave_clock = get_clock(slave);
    if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
        set_clock(c, slave_clock, slave->serial);
}

static int get_master_sync_type(VideoState *is) {
    if (is->av_sync_type == AV_SYNC_VIDEO_MASTER) {
        if (is->video_st)
            return AV_SYNC_VIDEO_MASTER;
        else
            return AV_SYNC_AUDIO_MASTER;
    } else if (is->av_sync_type == AV_SYNC_AUDIO_MASTER) {
        if (is->audio_st)
            return AV_SYNC_AUDIO_MASTER;
        else
            return AV_SYNC_EXTERNAL_CLOCK;
    } else {
        return AV_SYNC_EXTERNAL_CLOCK;
    }
}

/* get the current master clock value */
static double get_master_clock(VideoState *is)
{
    double val;

    switch (get_master_sync_type(is)) {
        case AV_SYNC_VIDEO_MASTER:
            val = get_clock(&is->vidclk);
            break;
        case AV_SYNC_AUDIO_MASTER:
            val = get_clock(&is->audclk);
            break;
        default:
            val = get_clock(&is->extclk);
            break;
    }
    return val;
}

static void check_external_clock_speed(VideoState *is) {
   if ((is->video_stream >= 0 && is->videoq.nb_packets <= MIN_FRAMES / 2) ||
       (is->audio_stream >= 0 && is->audioq.nb_packets <= MIN_FRAMES / 2)) {
       set_clock_speed(&is->extclk, FFMAX(EXTERNAL_CLOCK_SPEED_MIN, is->extclk.speed - EXTERNAL_CLOCK_SPEED_STEP));
   } else if ((is->video_stream < 0 || is->videoq.nb_packets > MIN_FRAMES * 2) &&
              (is->audio_stream < 0 || is->audioq.nb_packets > MIN_FRAMES * 2)) {
       set_clock_speed(&is->extclk, FFMIN(EXTERNAL_CLOCK_SPEED_MAX, is->extclk.speed + EXTERNAL_CLOCK_SPEED_STEP));
   } else {
       double speed = is->extclk.speed;
       if (speed != 1.0)
           set_clock_speed(&is->extclk, speed + EXTERNAL_CLOCK_SPEED_STEP * (1.0 - speed) / fabs(1.0 - speed));
   }
}

/* seek in the stream */
static void stream_seek(VideoState *is, int64_t pos, int64_t rel, int seek_by_bytes)
{
    if (!is->seek_req) {
        is->seek_pos = pos;
        is->seek_rel = rel;
        is->seek_flags &= ~AVSEEK_FLAG_BYTE;
        if (seek_by_bytes)
            is->seek_flags |= AVSEEK_FLAG_BYTE;
        is->seek_req = 1;
        CCSDL_CondSignal(is->continue_read_thread);
    }
}

/* pause or resume the video */
static void stream_toggle_pause_l(FFPlayer *ffp, int pause_on)
{
    VideoState *is = ffp->is;
    if (is->paused && !pause_on) {
        double time = av_gettime_relative() / 1000000.0;
        if(!ffp->smooth_loop_req && (time - is->frame_timer) < AV_SYNC_THRESHOLD_MAX){
            is->frame_timer += time + is->vidclk.pts_drift - is->vidclk.pts;
        }

#ifdef FFP_MERGE
        if (is->read_pause_return != AVERROR(ENOSYS)) {
            is->vidclk.paused = 0;
        }
#endif
        set_clock(&is->vidclk, get_clock(&is->vidclk), is->vidclk.serial);
        set_clock(&is->audclk, get_clock(&is->audclk), is->audclk.serial); //new ijkplayer
        // ALOGE("stream_toggle_pause_l: pause -> start\n");
    } else {
        // ALOGE("stream_toggle_pause_l: %d\n", pause_on);
    }
    set_clock(&is->extclk, get_clock(&is->extclk), is->extclk.serial);
    is->paused = is->audclk.paused = is->vidclk.paused = is->extclk.paused = pause_on;

    if (is->step && (is->pause_req || is->buffering_on)) {
            is->paused = is->vidclk.paused = is->extclk.paused = pause_on;
        } else {
            is->paused = is->audclk.paused = is->vidclk.paused = is->extclk.paused = pause_on;
            CCSDL_AoutPauseAudio(ffp->aout, pause_on);
        }
}

static void stream_update_pause_l(FFPlayer *ffp)
{
    VideoState *is = ffp->is;
    // ALOGE("stream_update_pause_l: (!%d && (%d || %d)\n", is->step, is->pause_req, is->buffering_on);
    if (!is->step && (is->pause_req || is->buffering_on)) {
        // ALOGE("stream_update_pause_l: 1\n");
        stream_toggle_pause_l(ffp, 1);
    } else {
        // ALOGE("stream_update_pause_l: 0\n");
        stream_toggle_pause_l(ffp, 0);
    }
}

static void toggle_pause_l(FFPlayer *ffp, int pause_on)
{
    // ALOGE("toggle_pause_l\n");
    VideoState *is = ffp->is;
    is->pause_req = pause_on;
    if (pause_on)
        is->stat_info.play_state = STATE_PAUSE;
    else
        is->stat_info.play_state = STATE_PLAYING;
    ffp->auto_start = !pause_on;
    stream_update_pause_l(ffp);
    is->step = 0;
}

static void toggle_pause(FFPlayer *ffp, int pause_on)
{
    CCSDL_LockMutex(ffp->is->play_mutex);
    toggle_pause_l(ffp, pause_on);
    CCSDL_UnlockMutex(ffp->is->play_mutex);
}

static void step_to_next_frame_l(FFPlayer *ffp)
{
    VideoState *is = ffp->is;
    /* if the stream is paused unpause it, then step */
    // ALOGE("step_to_next_frame\n");
    ALOGI("[dir] step_to_next_frame_l\n");
    if (is->paused)
        stream_toggle_pause_l(ffp, 0);
    is->step = 1;
}

static double compute_target_delay(double delay, VideoState *is)
{
    double sync_threshold, diff = 0.0f;

    /* update delay to follow master synchronisation source */
    if (get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER) {
        /* if video is slave, we try to correct big delays by
           duplicating or deleting a frame */
        diff = get_clock(&is->vidclk) - get_master_clock(is);

        /* skip or repeat frame. We take into account the
           delay to compute the threshold. I still don't know
           if it is the best guess */
        sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
        if (!isnan(diff) && fabs(diff) < is->max_frame_duration) {
            if (diff <= -sync_threshold)
                delay = FFMAX(0, delay + diff);
            else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
                delay = delay + diff;
            else if (diff >= sync_threshold){
                ALOGI("[dir] diff = %f - %f = %f\n",get_clock(&is->vidclk),get_master_clock(is),diff);
                delay = 2 * delay;
            }
        }
    }
    
    if(fabs(diff) > 3) {
        ALOGF("video: delay=%0.3f A-V=%f\n", delay, -diff);
    }
#ifdef FFP_SHOW_AUDIO_DELAY
    av_log(NULL, AV_LOG_ERROR, "video: delay=%0.3f A-V=%f\n",
           delay, -diff);
#endif
    
    return delay;
}

static double vp_duration(VideoState *is, Frame *vp, Frame *nextvp) {
    if (vp->serial == nextvp->serial) {
        double duration = nextvp->pts - vp->pts;
        if (isnan(duration) || duration <= 0 || duration > is->max_frame_duration)
            return vp->duration;
        else
            return duration;
    } else {
        return 0.0;
    }
}

static void update_video_pts(VideoState *is, double pts, int64_t pos, int serial) {
    /* update current video pts */
    set_clock(&is->vidclk, pts, serial);
    sync_clock_to_slave(&is->extclk, &is->vidclk);
}

/* called to display each frame */
static void video_refresh(FFPlayer *opaque, double *remaining_time)
{
    FFPlayer *ffp = opaque;
    VideoState *is = ffp->is;
    double time;

#ifdef FFP_MERGE
    Frame *sp, *sp2;
#endif

    if (!is->paused && get_master_sync_type(is) == AV_SYNC_EXTERNAL_CLOCK && is->realtime)
        check_external_clock_speed(is);

    if (!ffp->display_disable && is->show_mode != SHOW_MODE_VIDEO && is->audio_st) {
        time = av_gettime_relative() / 1000000.0;
        if (is->force_refresh || is->last_vis_time + ffp->rdftspeed < time) {
            is->last_refresh_time = av_gettime_relative();
            video_display2(ffp);
            is->last_vis_time = time;
        }
        *remaining_time = FFMIN(*remaining_time, is->last_vis_time + ffp->rdftspeed - time);
    }

    if (is->video_st) {
        int redisplay = 0;
        if (is->force_refresh)
            redisplay = frame_queue_prev(&is->pictq);
retry:
        if (frame_queue_nb_remaining(&is->pictq) == 0) {
            // nothing to do, no picture to display in the queue
            int64_t now = av_gettime_relative();
            if ((!is->step && !ffp->changedir_req) && (now - is->last_refresh_time) / 1000 > DEFAULT_VIDEO_REFRESH_INTERVAL) {
                is->last_refresh_time = now;
                video_no_image_display(ffp);
            }
        } else {
            double last_duration, duration, delay;
            Frame *vp, *lastvp;

            /* dequeue the picture */
            lastvp = frame_queue_peek_last(&is->pictq);
            vp = frame_queue_peek(&is->pictq);

            if (vp->serial != is->videoq.serial) {
                frame_queue_next(&is->pictq);
                redisplay = 0;
                goto retry;
            }

            if (lastvp->serial != vp->serial && !redisplay)
                is->frame_timer = av_gettime_relative() / 1000000.0;
            
            // display the first several frames as walk-around
            if (is->paused && !is->force_videodisplay)
                goto display;

            /* compute nominal last_duration */
            last_duration = vp_duration(is, lastvp, vp);
            if (redisplay)
                delay = 0.0;
            else
                delay = compute_target_delay(last_duration, is);

            time= av_gettime_relative()/1000000.0;
            if (isnan(is->frame_timer) || time < is->frame_timer)
                is->frame_timer = time;
            if (time < is->frame_timer + delay && !redisplay) {
                *remaining_time = FFMIN(is->frame_timer + delay - time, *remaining_time);
                return;
            }

            is->frame_timer += delay;
            if (delay > 0 && time - is->frame_timer > AV_SYNC_THRESHOLD_MAX)
                is->frame_timer = time;

            CCSDL_LockMutex(is->pictq.mutex);
            if(ffp->smooth_loop_req && !isnan(vp->pts) && vp->pts == ffp->first_video_pts){
                set_clock(&is->extclk, vp->pts, 0);
                ffp->smooth_loop_req = 0;
            }
            if (!redisplay && !isnan(vp->pts))
                update_video_pts(is, vp->pts, vp->pos, vp->serial);
            CCSDL_UnlockMutex(is->pictq.mutex);

            if (frame_queue_nb_remaining(&is->pictq) > 1) {
                Frame *nextvp = frame_queue_peek_next(&is->pictq);
                duration = vp_duration(is, vp, nextvp);
                if(!is->step && (redisplay || ffp->framedrop > 0 || (ffp->framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) && time > is->frame_timer + duration) {
                    if (!redisplay)
                        is->frame_drops_late++;
                    frame_queue_next(&is->pictq);
                    redisplay = 0;
                    goto retry;
                }
            }

            // FFP_MERGE: if (is->subtitle_st) { {...}
            Frame *sp, *sp2;
            if (is->subtitle_st) {
                while (frame_queue_nb_remaining(&is->subpq) > 0) {
                    sp = frame_queue_peek(&is->subpq);

                    if (frame_queue_nb_remaining(&is->subpq) > 1)
                        sp2 = frame_queue_peek_next(&is->subpq);
                    else
                        sp2 = NULL;

                    if (sp->serial != is->subtitleq.serial
                            || (is->vidclk.pts > (sp->pts + ((float) sp->sub.end_display_time / 1000)))
                            || (sp2 && is->vidclk.pts > (sp2->pts + ((float) sp2->sub.start_display_time / 1000))))
                    {
                        if (sp->uploaded) {
                            ffp_notify_msg4(ffp, FFP_MSG_TIMED_TEXT, 0, 0, "", 1);
                        }
                        frame_queue_next(&is->subpq);
                    } else {
                        break;
                    }
                }
            }
            
display:
            if (!ffp->display_disable && is->show_mode == SHOW_MODE_VIDEO) {
                is->last_refresh_time = av_gettime_relative();
                video_display2(ffp);
            }
            
            frame_queue_next(&is->pictq);
            CCSDL_LockMutex(ffp->is->play_mutex);
            if (is->step) {
                is->step = 0;
                ALOGI("[dir] video refresh step = 0\n");
                if (!is->paused)
                    stream_update_pause_l(ffp);
            }
            CCSDL_UnlockMutex(ffp->is->play_mutex);
        }
    }
    is->force_refresh = 0;
    if (ffp->show_status) {
        static int64_t last_time;
        int64_t cur_time;
        int aqsize, vqsize, sqsize __unused;
        double av_diff;

        cur_time = av_gettime_relative();
        if (!last_time || (cur_time - last_time) >= 30000) {
            aqsize = 0;
            vqsize = 0;
            sqsize = 0;
            if (is->audio_st)
                aqsize = is->audioq.size;
            if (is->video_st)
                vqsize = is->videoq.size;
#ifdef FFP_MERGE
            if (is->subtitle_st)
                sqsize = is->subtitleq.size;
#else
            sqsize = 0;
#endif
            av_diff = 0;
            if (is->audio_st && is->video_st)
                av_diff = get_clock(&is->audclk) - get_clock(&is->vidclk);
            else if (is->video_st)
                av_diff = get_master_clock(is) - get_clock(&is->vidclk);
            else if (is->audio_st)
                av_diff = get_master_clock(is) - get_clock(&is->audclk);
//            av_log(NULL, AV_LOG_INFO,
//                   "%7.2f %s:%7.3f fd=%4d aq=%5dKB vq=%5dKB sq=%5dB f=%"PRId64"/%"PRId64"   \r",
//                   get_master_clock(is),
//                   (is->audio_st && is->video_st) ? "A-V" : (is->video_st ? "M-V" : (is->audio_st ? "M-A" : "   ")),
//                   av_diff,
//                   is->frame_drops_early + is->frame_drops_late,
//                   aqsize / 1024,
//                   vqsize / 1024,
//                   sqsize,
//                   is->video_st ? is->video_st->codec->pts_correction_num_faulty_dts : 0,
//                   is->video_st ? is->video_st->codec->pts_correction_num_faulty_pts : 0);
            fflush(stdout);
            last_time = cur_time;
        }
    }
}

// TODO: 9 alloc_picture in video_refresh_thread if overlay referenced by vout
/* allocate a picture (needs to do that in main thread to avoid
   potential locking problems */
static void alloc_picture(FFPlayer *ffp, int frame_format)
{
    VideoState *is = ffp->is;
    Frame *vp;
#ifdef FFP_MERGE
    int64_t bufferdiff;
#endif
    
    vp = &is->pictq.queue[is->pictq.windex];
    
    free_picture(vp);
    
#ifdef FFP_MERGE
    video_open(ffp, 0, vp);
#endif
    
    CCSDL_VoutSetOverlayFormat(ffp->vout, ffp->overlay_format);
    vp->bmp = CCSDL_Vout_CreateOverlay(vp->width, vp->height,
                                     frame_format,
                                     ffp->vout, ffp->crop,
                                     ffp->surface_width, ffp->surface_height, ffp->rotate);
#ifdef FFP_MERGE
    bufferdiff = vp->bmp ? FFMAX(vp->bmp->pixels[0], vp->bmp->pixels[1]) - FFMIN(vp->bmp->pixels[0], vp->bmp->pixels[1]) : 0;
    if (!vp->bmp || vp->bmp->pitches[0] < vp->width || bufferdiff < (int64_t)vp->height * vp->bmp->pitches[0])
#else
    /* RV16, RV32 contains only one plane */
        if (!vp->bmp || (!vp->bmp->is_private && vp->bmp->pitches[0] < vp->width))
#endif
        {
            /* SDL allocates a buffer smaller than requested if the video
             * overlay hardware is unable to support the requested size. */
            av_log(NULL, AV_LOG_FATAL,
                   "Error: the video system does not support an image\n"
                   "size of %dx%d pixels. Try using -lowres or -vf \"scale=w:h\"\n"
                   "to reduce the image size.\n", vp->width, vp->height );
            free_picture(vp);
        }
    
    CCSDL_LockMutex(is->pictq.mutex);
    vp->allocated = 1;
    CCSDL_CondSignal(is->pictq.cond);
    CCSDL_UnlockMutex(is->pictq.mutex);
}

#ifdef FFP_MERGE
static void duplicate_right_border_pixels(CCSDL_Overlay *bmp) {
    int i, width, height;
    Uint8 *p, *maxp;
    for (i = 0; i < 3; i++) {
        width  = bmp->w;
        height = bmp->h;
        if (i > 0) {
            width  >>= 1;
            height >>= 1;
        }
        if (bmp->pitches[i] > width) {
            maxp = bmp->pixels[i] + bmp->pitches[i] * height - 1;
            for (p = bmp->pixels[i] + width - 1; p < maxp; p += bmp->pitches[i])
                *(p+1) = *p;
        }
    }
}
#endif

static int accurate_video_seek_if_need(FFPlayer *ffp, double pts)
{
    VideoState *is = ffp->is;
    int video_accurate_seek_fail = 0;
    int64_t video_seek_pos = 0;
    int64_t now = 0;
    int64_t deviation = 0;
    
    int64_t deviation2 = 0;
    int64_t deviation3 = 0;
    
    if (ffp->enable_accurate_seek && is->video_accurate_seek_req && !is->seek_req) {
        if (!isnan(pts)) {
            video_seek_pos = is->seek_pos;
            is->accurate_seek_vframe_pts = pts * 1000 * 1000;
            deviation = llabs((int64_t)(pts * 1000 * 1000) - is->seek_pos);
            if ((pts * 1000 * 1000 < is->seek_pos) || deviation > MAX_DEVIATION) {
                now = av_gettime_relative() / 1000;
                if (is->drop_vframe_count == 0) {
                    CCSDL_LockMutex(is->accurate_seek_mutex);
                    if (is->accurate_seek_start_time <= 0 && (is->audio_stream < 0 || is->audio_accurate_seek_req)) {
                        is->accurate_seek_start_time = now;
                    }
                    CCSDL_UnlockMutex(is->accurate_seek_mutex);
                    av_log(NULL, AV_LOG_INFO, "[dir] video accurate_seek start, is->seek_pos=%lld, pts=%lf, is->accurate_seek_time = %lld\n", is->seek_pos, pts, is->accurate_seek_start_time);
                }
                is->drop_vframe_count++;
                
                while (is->audio_accurate_seek_req && !is->abort_request) {
                    int64_t apts = is->accurate_seek_aframe_pts ;
                    deviation2 = apts - pts * 1000 * 1000;
                    deviation3 = apts - is->seek_pos;
                    
                    if (deviation2 > -100 * 1000 && deviation3 < 0) {
                        break;
                    } else {
                        av_usleep(20 * 1000);
                    }
                    now = av_gettime_relative() / 1000;
                    if ((now - is->accurate_seek_start_time) > ffp->accurate_seek_timeout) {
                        break;
                    }
                }
                
                if ((now - is->accurate_seek_start_time) <= ffp->accurate_seek_timeout) {
                    return 1;  // drop some old frame when do accurate seek
                } else {
                    av_log(NULL, AV_LOG_WARNING, "video accurate_seek is error, is->drop_vframe_count=%d, now = %lld, pts = %lf\n", is->drop_vframe_count, now, pts);
                    video_accurate_seek_fail = 1;  // if KEY_FRAME interval too big, disable accurate seek
                }
            } else {
                
                av_log(NULL, AV_LOG_INFO, "[dir] video accurate_seek is ok, is->drop_vframe_count =%d, is->seek_pos=%lld, pts=%lf\n", is->drop_vframe_count, is->seek_pos, pts);
                if (video_seek_pos == is->seek_pos) {
                    is->drop_vframe_count       = 0;
                    CCSDL_LockMutex(is->accurate_seek_mutex);
                    is->video_accurate_seek_req = 0;
                    CCSDL_CondSignal(is->audio_accurate_seek_cond);
                    if (video_seek_pos == is->seek_pos && is->audio_accurate_seek_req && !is->abort_request) {
                        CCSDL_CondWaitTimeout(is->video_accurate_seek_cond, is->accurate_seek_mutex, ffp->accurate_seek_timeout);
                    } else {
                        ALOGF("accurate_seek complete pts %d from video\n",(int)(pts * 1000));
                        ffp_notify_msg2(ffp, FFP_MSG_ACCURATE_SEEK_COMPLETE, (int)(pts * 1000));
                        ALOGI("[dir] video seek complete\n");
                    }
                    if (video_seek_pos != is->seek_pos && !is->abort_request) {
                        is->video_accurate_seek_req = 1;
                        CCSDL_UnlockMutex(is->accurate_seek_mutex);
                        return 1;
                    }
                    
                    CCSDL_UnlockMutex(is->accurate_seek_mutex);
                }
            }
        } else {
            video_accurate_seek_fail = 1;
        }
        
        if (video_accurate_seek_fail) {
            is->drop_vframe_count = 0;
            CCSDL_LockMutex(is->accurate_seek_mutex);
            is->video_accurate_seek_req = 0;
            CCSDL_CondSignal(is->audio_accurate_seek_cond);
            if (is->audio_accurate_seek_req && !is->abort_request) {
                CCSDL_CondWaitTimeout(is->video_accurate_seek_cond, is->accurate_seek_mutex, ffp->accurate_seek_timeout);
            } else {
                if (!isnan(pts)) {
                    ALOGF("accurate_seek complete pts %d from video\n",(int)(pts * 1000));
                    ffp_notify_msg2(ffp, FFP_MSG_ACCURATE_SEEK_COMPLETE, (int)(pts * 1000));
                    ALOGI("[dir] video seek fail\n");
                } else {
                    ALOGF("accurate_seek complete pts 0 from video\n");
                    ffp_notify_msg2(ffp, FFP_MSG_ACCURATE_SEEK_COMPLETE, 0);
                    ALOGI("[dir] audio seek fail\n");
                }
            }
            CCSDL_UnlockMutex(is->accurate_seek_mutex);
        }
        is->accurate_seek_start_time = 0;
        video_accurate_seek_fail = 0;
        is->accurate_seek_vframe_pts = 0;
    }
    return 0;
}


static int queue_picture(FFPlayer *ffp, AVFrame *src_frame, double pts, double duration, int64_t pos, int serial)
{
    VideoState *is = ffp->is;
    Frame *vp;
    
    if (ffp->show_log > 0)
        ALOGI("[dir] queue picture %f\n", pts);
    if(!ffp->smooth_loop_req && accurate_video_seek_if_need(ffp, pts) == 1) {
        return 1;
    }
    
#if defined(DEBUG_SYNC) && 0
    printf("frame_type=%c pts=%0.3f\n",
           av_get_picture_type_char(src_frame->pict_type), pts);
#endif
    
    if (!(vp = frame_queue_peek_writable(&is->pictq)))
        return -1;
    
    vp->sar = src_frame->sample_aspect_ratio;
    
    /* alloc or resize hardware picture buffer */
    if (!vp->bmp || vp->reallocate || !vp->allocated ||
        vp->width  != src_frame->width ||
        vp->height != src_frame->height || vp->frame_format != src_frame->format) {
        
        if ((vp->width > 0 && vp->width != src_frame->width) || (vp->height > 0 && vp->height != src_frame->height))
            ffp_notify_msg3(ffp, FFP_MSG_VIDEO_SIZE_CHANGED, src_frame->width, src_frame->height);
        
        vp->allocated  = 0;
        vp->reallocate = 0;
        vp->width = src_frame->width;
        vp->height = src_frame->height;
        vp->frame_format = src_frame->format;
        /* the allocation must be done in the main thread to avoid
         locking problems. */
//        switch (src_frame->format) {
//            case CCSDL_FCC__AMC:
//            case CCSDL_FCC__VTB:
//                alloc_picture(ffp, src_frame->format);
//                break;
//            default:
//                alloc_picture(ffp, ffp->overlay_format);
//                break;
//        }
        alloc_picture(ffp, src_frame->format);
        
        if (is->videoq.abort_request)
            return -1;
    }
    
    /* if the frame is not skipped, then display it */
    if (vp->bmp) {
        /* get a pointer on the bitmap */
        CCSDL_VoutLockYUVOverlay(vp->bmp);
        
        if (CCSDL_VoutFillFrameYUVOverlay(vp->bmp, src_frame) < 0) {
            av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
            exit(1);
        }
        /* update the bitmap content */
        CCSDL_VoutUnlockYUVOverlay(vp->bmp);
        
        vp->pts = pts;
        vp->duration = duration;
        vp->pos = pos;
        vp->serial = serial;
        
        /* now we can update the picture count */
        frame_queue_push(&is->pictq);
        if (!is->viddec.first_frame_decoded) {
            ALOGD("Video: first frame decoded\n");
            is->viddec.first_frame_decoded_time = CCSDL_GetTickHR();
            is->viddec.first_frame_decoded = 1;
        }
    }
    return 0;
}

static int get_video_frame(FFPlayer *ffp, AVFrame *frame)
{
    VideoState *is = ffp->is;
    
    int got_picture;

    if (ffp->is->abort_request) {
        return -1;
    }
//    if (ffp->display_disable) {
//        CCSDL_Delay(10);
//        return 0;
//    }
    if ((got_picture = decoder_decode_frame(ffp, &is->viddec, frame, NULL)) < 0)
        return -1;

    if (got_picture) {
        if (is->first_frame_time <= 0) {
            is->first_frame_time = av_gettime_relative();
            ALOGI("stream_open--->decode first frame time = %lld ms, video packet count = %d\n", (is->first_frame_time - is->stream_open_time) / 1000, is->videoq.nb_packets);
        }
        double dpts = NAN;

        if (frame->pts != AV_NOPTS_VALUE)
            dpts = av_q2d(is->video_st->time_base) * frame->pts;

        frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(is->ic, is->video_st, frame);

        if (ffp->framedrop>0 || (ffp->framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) {
            if (frame->pts != AV_NOPTS_VALUE) {
                double diff = dpts - get_master_clock(is);
                if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD &&
                    diff - is->frame_last_filter_delay < 0 &&
                    is->viddec.pkt_serial == is->vidclk.serial &&
                    is->videoq.nb_packets) {
                    is->frame_drops_early++;
                    is->continuous_frame_drops_early++;
                    if (is->continuous_frame_drops_early > ffp->framedrop) {
                        is->continuous_frame_drops_early = 0;
                    } else {
                        av_frame_unref(frame);
                        got_picture = 0;
                    }
                }
            }
        }
    }
    return got_picture;
}

#if CONFIG_AVFILTER
static int configure_filtergraph(AVFilterGraph *graph, const char *filtergraph,
                                 AVFilterContext *source_ctx, AVFilterContext *sink_ctx)
{
    int ret, i;
    int nb_filters = graph->nb_filters;
    AVFilterInOut *outputs = NULL, *inputs = NULL;

    if (filtergraph) {
        outputs = avfilter_inout_alloc();
        inputs  = avfilter_inout_alloc();
        if (!outputs || !inputs) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        outputs->name       = av_strdup("in");
        outputs->filter_ctx = source_ctx;
        outputs->pad_idx    = 0;
        outputs->next       = NULL;

        inputs->name        = av_strdup("out");
        inputs->filter_ctx  = sink_ctx;
        inputs->pad_idx     = 0;
        inputs->next        = NULL;

        if ((ret = avfilter_graph_parse_ptr(graph, filtergraph, &inputs, &outputs, NULL)) < 0)
            goto fail;
    } else {
        if ((ret = avfilter_link(source_ctx, 0, sink_ctx, 0)) < 0)
            goto fail;
    }

    /* Reorder the filters to ensure that inputs of the custom filters are merged first */
    for (i = 0; i < graph->nb_filters - nb_filters; i++)
        FFSWAP(AVFilterContext*, graph->filters[i], graph->filters[i + nb_filters]);

    ret = avfilter_graph_config(graph, NULL);
fail:
    avfilter_inout_free(&outputs);
    avfilter_inout_free(&inputs);
    return ret;
}

static int configure_video_filters(FFPlayer *ffp, AVFilterGraph *graph, VideoState *is, const char *vfilters, AVFrame *frame)
{
    ALOGF("[filters] configure_video_filters vfilters %s \n", vfilters);
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };
    char sws_flags_str[128];
    char buffersrc_args[256];
    int ret;
    AVFilterContext *filt_src = NULL, *filt_out = NULL, *last_filter = NULL;
    AVCodecContext *codec = is->video_st->codec;
    AVRational fr = av_guess_frame_rate(is->ic, is->video_st, NULL);
    
    AVDictionaryEntry *e = NULL;
    
    while ((e = av_dict_get(ffp->sws_opts, "", e, AV_DICT_IGNORE_SUFFIX))) {
        if (!strcmp(e->key, "sws_flags")) {
            av_strlcatf(sws_flags_str, sizeof(sws_flags_str), "%s=%s:", "flags", e->value);
        } else
            av_strlcatf(sws_flags_str, sizeof(sws_flags_str), "%s=%s:", e->key, e->value);
    }
    if (strlen(sws_flags_str))
        sws_flags_str[strlen(sws_flags_str)-1] = '\0';
    
    graph->scale_sws_opts = av_strdup(sws_flags_str);
    
    snprintf(buffersrc_args, sizeof(buffersrc_args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             frame->width, frame->height, frame->format,
             is->video_st->time_base.num, is->video_st->time_base.den,
             codec->sample_aspect_ratio.num, FFMAX(codec->sample_aspect_ratio.den, 1));
    if (fr.num && fr.den)
        av_strlcatf(buffersrc_args, sizeof(buffersrc_args), ":frame_rate=%d/%d", fr.num, fr.den);
    
    if ((ret = avfilter_graph_create_filter(&filt_src,
                                            avfilter_get_by_name("buffer"),
                                            "ffplay_buffer", buffersrc_args, NULL,
                                            graph)) < 0){
        ALOGF("[filters] configure_video_filters avfilter_graph_create_filter buffer fail \n");
        goto fail;
    }
    
    ret = avfilter_graph_create_filter(&filt_out,
                                       avfilter_get_by_name("buffersink"),
                                       "ffplay_buffersink", NULL, NULL, graph);
    if (ret < 0) {
        ALOGF("[filters] configure_video_filters avfilter_graph_create_filter buffersink fail \n");
        goto fail;
    }
    
    if ((ret = av_opt_set_int_list(filt_out, "pix_fmts", pix_fmts,  AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0) {
        ALOGF("[filters] configure_video_filters av_opt_set_int_list pix_fmts fail \n");
        goto fail;
    }
    
    last_filter = filt_out;
    
    /* Note: this macro adds a filter before the lastly added filter, so the
     * processing order of the filters is in reverse */
#define INSERT_FILT(name, arg) do {                                         \
AVFilterContext *filt_ctx;                                              \
\
ret = avfilter_graph_create_filter(&filt_ctx,                           \
avfilter_get_by_name(name),          \
"ffplay_" name, arg, NULL, graph);   \
if (ret < 0)                                                            \
goto fail;                                                          \
\
ret = avfilter_link(filt_ctx, 0, last_filter, 0);                       \
if (ret < 0)                                                            \
goto fail;                                                          \
\
last_filter = filt_ctx;                                                 \
} while (0)
    
    /* SDL YUV code is not handling odd width/height for some driver
     * combinations, therefore we crop the picture to an even width/height. */
    //    INSERT_FILT("crop", "floor(in_w/2)*2:floor(in_h/2)*2");
    //
    //    if (autorotate) {
    //        AVDictionaryEntry *rotate_tag = av_dict_get(is->video_st->metadata, "rotate", NULL, 0);
    //        if (rotate_tag && *rotate_tag->value && strcmp(rotate_tag->value, "0")) {
    //            if (!strcmp(rotate_tag->value, "90")) {
    //                INSERT_FILT("transpose", "clock");
    //            } else if (!strcmp(rotate_tag->value, "180")) {
    //                INSERT_FILT("hflip", NULL);
    //                INSERT_FILT("vflip", NULL);
    //            } else if (!strcmp(rotate_tag->value, "270")) {
    //                INSERT_FILT("transpose", "cclock");
    //            } else {
    //                char rotate_buf[64];
    //                snprintf(rotate_buf, sizeof(rotate_buf), "%s*PI/180", rotate_tag->value);
    //                INSERT_FILT("rotate", rotate_buf);
    //            }
    //        }
    //    }
#ifdef FFP_AVFILTER_PLAYBACK_RATE
    if (fabsf(ffp->pf_playback_rate) > 0.00001 &&
        fabsf(ffp->pf_playback_rate - 1.0f) > 0.00001) {
        char setpts_buf[256];
        float rate = 1.0f / ffp->pf_playback_rate;
        rate = av_clipf_c(rate, 0.5f, 2.0f);
        ALOGF("[filters] vf_rate=%f(1/%f)\n", ffp->pf_playback_rate, rate);
        snprintf(setpts_buf, sizeof(setpts_buf), "%f*PTS", rate);
        INSERT_FILT("setpts", setpts_buf);
        ALOGF("[filters] setpts %s\n",setpts_buf);
    }
#endif
    ALOGF("[filters] vfilters=%s\n", vfilters);
    if ((ret = configure_filtergraph(graph, vfilters, filt_src, last_filter)) < 0) {
        ALOGF("[filters] configure_video_filters fail %d \n",ret);
        goto fail;
    }
    
    is->in_video_filter  = filt_src;
    is->out_video_filter = filt_out;
    ALOGF("[filters] configure_video_filters done \n");
fail:
    if(ret < 0)
        ALOGF("[filters] configure_video_filters fail -- \n");
    return ret;
}

static int configure_audio_filters(FFPlayer *ffp, VideoState *is, const char *afilters, int force_output_format)
{
    ALOGF("[filters] configure_audio_filters afilters %s\n", afilters);
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE };
    int sample_rates[2] = { 0, -1 };
    int64_t channel_layouts[2] = { 0, -1 };
    int channels[2] = { 0, -1 };
    AVFilterContext *filt_asrc = NULL, *filt_asink = NULL;
    char aresample_swr_opts[512] = "";
    AVDictionaryEntry *e = NULL;
    char asrc_args[256];
    int ret;
    char afilters_args[4096];
    
    avfilter_graph_free(&is->agraph);
    if (!(is->agraph = avfilter_graph_alloc()))
        return AVERROR(ENOMEM);
    
    while ((e = av_dict_get(ffp->swr_opts, "", e, AV_DICT_IGNORE_SUFFIX)))
        av_strlcatf(aresample_swr_opts, sizeof(aresample_swr_opts), "%s=%s:", e->key, e->value);
    if (strlen(aresample_swr_opts))
        aresample_swr_opts[strlen(aresample_swr_opts)-1] = '\0';
    av_opt_set(is->agraph, "aresample_swr_opts", aresample_swr_opts, 0);
    
    ret = snprintf(asrc_args, sizeof(asrc_args),
                   "sample_rate=%d:sample_fmt=%s:channels=%d:time_base=%d/%d",
                   is->audio_filter_src.freq, av_get_sample_fmt_name(is->audio_filter_src.fmt),
                   is->audio_filter_src.channels,
                   1, is->audio_filter_src.freq);
    if (is->audio_filter_src.channel_layout)
        snprintf(asrc_args + ret, sizeof(asrc_args) - ret,
                 ":channel_layout=0x%"PRIx64,  is->audio_filter_src.channel_layout);
    
    ret = avfilter_graph_create_filter(&filt_asrc,
                                       avfilter_get_by_name("abuffer"), "ffplay_abuffer",
                                       asrc_args, NULL, is->agraph);
    if (ret < 0) {
        ALOGF("[filters] configure_audio_filters avfilter_graph_create_filter abuffer fail \n");
        goto end;
    }
    
    
    ret = avfilter_graph_create_filter(&filt_asink,
                                       avfilter_get_by_name("abuffersink"), "ffplay_abuffersink",
                                       NULL, NULL, is->agraph);
    if (ret < 0) {
        ALOGF("[filters] configure_audio_filters avfilter_graph_create_filter abuffersink fail \n");
        goto end;
    }
    
    if ((ret = av_opt_set_int_list(filt_asink, "sample_fmts", sample_fmts,  AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0) {
        ALOGF("[filters] configure_audio_filters av_opt_set_int_list sample_fmts fail \n");
        goto end;
    }
    if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN)) < 0) {
        ALOGF("[filters] configure_audio_filters av_opt_set_int all_channel_counts fail \n");
        goto end;
    }
    
    if (force_output_format) {
        channel_layouts[0] = is->audio_tgt.channel_layout;
        channels       [0] = is->audio_tgt.channels;
        sample_rates   [0] = is->audio_tgt.freq;
        if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 0, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "channel_layouts", channel_layouts,  -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "channel_counts" , channels       ,  -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "sample_rates"   , sample_rates   ,  -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
    }
    
    afilters_args[0] = 0;
    if (afilters)
        snprintf(afilters_args, sizeof(afilters_args), "%s", afilters);
    
#ifdef FFP_AVFILTER_PLAYBACK_RATE
    if (fabsf(ffp->pf_playback_rate) > 0.00001 &&
        fabsf(ffp->pf_playback_rate - 1.0f) > 0.00001) {
        if (afilters_args[0])
            av_strlcatf(afilters_args, sizeof(afilters_args), ",");
        
        ALOGF("[filters] af_rate=%f\n", ffp->pf_playback_rate);
        av_strlcatf(afilters_args, sizeof(afilters_args), "atempo=%f", ffp->pf_playback_rate);
    }
#endif
    ALOGF("[filters] afilters_args=%s\n", afilters_args);
    if ((ret = configure_filtergraph(is->agraph, afilters_args[0] ? afilters_args : NULL, filt_asrc, filt_asink)) < 0) {
        ALOGF("[filters] configure filtergraph failed %d \n",ret);
        goto end;
    }
    
    is->in_audio_filter  = filt_asrc;
    is->out_audio_filter = filt_asink;
    
end:
    if (ret < 0) {
        ALOGF("[filters] configure_audio_filters fail -- \n");
        avfilter_graph_free(&is->agraph);
    }
    return ret;
}
#endif  /* CONFIG_AVFILTER */

static int accurate_audio_seek_if_need(FFPlayer *ffp, AVFrame *frame, AVRational tb)
{
    VideoState *is = ffp->is;
    int audio_accurate_seek_fail = 0;
    int64_t audio_seek_pos = 0;
    double frame_pts = 0;
    double audio_clock = 0;
    int64_t now = 0;
    double samples_duration = 0;
    int64_t deviation = 0;
    int64_t deviation2 = 0;
    int64_t deviation3 = 0;
    if (ffp->enable_accurate_seek && is->audio_accurate_seek_req && !is->seek_req) {
        frame_pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
        now = av_gettime_relative() / 1000;
        if (!isnan(frame_pts)) {
            samples_duration = (double) frame->nb_samples / frame->sample_rate;
            audio_clock = frame_pts + samples_duration;
            is->accurate_seek_aframe_pts = audio_clock * 1000 * 1000;
            audio_seek_pos = is->seek_pos + ffp->seek_offset;
            deviation = llabs((int64_t)(audio_clock * 1000 * 1000) - is->seek_pos - ffp->seek_offset);
            if ((audio_clock * 1000 * 1000 < is->seek_pos + ffp->seek_offset) || deviation > MAX_DEVIATION) {
                if (is->drop_aframe_count == 0) {
                    CCSDL_LockMutex(is->accurate_seek_mutex);
                    if (is->accurate_seek_start_time <= 0 && (is->video_stream < 0 || is->video_accurate_seek_req)) {
                        is->accurate_seek_start_time = now;
                    }
                    CCSDL_UnlockMutex(is->accurate_seek_mutex);
                    av_log(NULL, AV_LOG_INFO, "[dir] audio accurate_seek start, is->seek_pos=%lld, audio_clock=%lf, is->accurate_seek_start_time = %lld\n", is->seek_pos, audio_clock, is->accurate_seek_start_time);
                }
                is->drop_aframe_count++;
                ALOGI("[dir] drop_aframe_count++\n");
                while (is->video_accurate_seek_req && !is->abort_request) {
                    int64_t vpts = is->accurate_seek_vframe_pts;
                    deviation2 = vpts  - audio_clock * 1000 * 1000;
                    deviation3 = vpts  - is->seek_pos;
//                    if (deviation2 > -100 * 1000 && deviation3 < 0) {
                    if (deviation2 > -100 * 1000 - ffp->seek_offset && deviation3 < 0) {
                        break;
                    } else {
                        ALOGI("[dir] av_usleep\n");
                        av_usleep(20 * 1000);
                    }
                    now = av_gettime_relative() / 1000;
                    if ((now - is->accurate_seek_start_time) > ffp->accurate_seek_timeout) {
                        break;
                    }
                }
                
                if(!is->video_accurate_seek_req && is->video_stream >= 0 && audio_clock * 1000 * 1000 - ffp->seek_offset> is->accurate_seek_vframe_pts) {
                    audio_accurate_seek_fail = 1;
                } else {
                    now = av_gettime_relative() / 1000;
                    if ((now - is->accurate_seek_start_time) <= ffp->accurate_seek_timeout) {
                        av_frame_unref(frame);
                        return 1; // drop some old frame when do accurate seek
                    } else {
                        audio_accurate_seek_fail = 1;
                    }
                }
            } else {
                if (audio_seek_pos == is->seek_pos + ffp->seek_offset) {
                    av_log(NULL, AV_LOG_INFO, "[dir] audio accurate_seek is ok, is->drop_aframe_count=%d, audio_clock = %lf\n", is->drop_aframe_count, audio_clock);
                    is->drop_aframe_count       = 0;
                    CCSDL_LockMutex(is->accurate_seek_mutex);
                    is->audio_accurate_seek_req = 0;
                    CCSDL_CondSignal(is->video_accurate_seek_cond);
                    if (audio_seek_pos == is->seek_pos + ffp->seek_offset && is->video_accurate_seek_req && !is->abort_request) {
                        CCSDL_CondWaitTimeout(is->audio_accurate_seek_cond, is->accurate_seek_mutex, ffp->accurate_seek_timeout);
                    } else {
                        ALOGI("[dir] accurate_seek complete pts %d from audio\n",(int)(audio_clock * 1000));
                        ffp_notify_msg2(ffp, FFP_MSG_ACCURATE_SEEK_COMPLETE, (int)(audio_clock * 1000));
                    }
                    
                    if (audio_seek_pos != is->seek_pos + ffp->seek_offset && !is->abort_request) {
                        is->audio_accurate_seek_req = 1;
                        CCSDL_UnlockMutex(is->accurate_seek_mutex);
                        av_frame_unref(frame);
                        return 1;
                    }
                    
                    CCSDL_UnlockMutex(is->accurate_seek_mutex);
                }
            }
        } else {
            audio_accurate_seek_fail = 1;
        }
        if (audio_accurate_seek_fail) {
            av_log(NULL, AV_LOG_INFO, "audio accurate_seek is error, is->drop_aframe_count=%d, now = %lld, audio_clock = %lf\n", is->drop_aframe_count, now, audio_clock);
            is->drop_aframe_count       = 0;
            CCSDL_LockMutex(is->accurate_seek_mutex);
            is->audio_accurate_seek_req = 0;
            CCSDL_CondSignal(is->video_accurate_seek_cond);
            if (is->video_accurate_seek_req && !is->abort_request) {
                CCSDL_CondWaitTimeout(is->audio_accurate_seek_cond, is->accurate_seek_mutex, ffp->accurate_seek_timeout);
            } else {
                ALOGI("[dir] accurate_seek complete pts %d from audio\n",(int)(audio_clock * 1000));
                ffp_notify_msg2(ffp, FFP_MSG_ACCURATE_SEEK_COMPLETE, (int)(audio_clock * 1000));
            }
            CCSDL_UnlockMutex(is->accurate_seek_mutex);
        }
        is->accurate_seek_start_time = 0;
        audio_accurate_seek_fail = 0;
    }

    return 0;
}

static int audio_thread(void *arg)
{
    FFPlayer *ffp = arg;
    VideoState *is = ffp->is;
    AVFrame *frame = av_frame_alloc();
    Frame *af;
#if CONFIG_AVFILTER
    int last_serial = -1;
    int64_t dec_channel_layout;
    int reconfigure;
#endif
    int got_frame = 0;
    AVRational tb;
    int ret = 0;

    if (!frame)
        return AVERROR(ENOMEM);

    do {
        if ((got_frame = decoder_decode_frame(ffp, &is->auddec, frame, NULL)) < 0)
            goto the_end;

        if (got_frame) {
                tb = (AVRational){1, frame->sample_rate};
            
            if(accurate_audio_seek_if_need(ffp, frame, tb) == 1) {
                continue;
            }
#if CONFIG_AVFILTER
                dec_channel_layout = get_valid_channel_layout(frame->channel_layout, av_frame_get_channels(frame));

                reconfigure =
                    cmp_audio_fmts(is->audio_filter_src.fmt, is->audio_filter_src.channels,
                                   frame->format, av_frame_get_channels(frame))    ||
                    is->audio_filter_src.channel_layout != dec_channel_layout ||
                    is->audio_filter_src.freq           != frame->sample_rate ||+                    is->auddec.pkt_serial               != last_serial;

                if (reconfigure) {
                    CCSDL_LockMutex(ffp->af_mutex);
                    ffp->af_changed = 0;
                    char buf1[1024], buf2[1024];
                    av_get_channel_layout_string(buf1, sizeof(buf1), -1, is->audio_filter_src.channel_layout);
                    av_get_channel_layout_string(buf2, sizeof(buf2), -1, dec_channel_layout);
                    av_log(NULL, AV_LOG_DEBUG,
                           "Audio frame changed from rate:%d ch:%d fmt:%s layout:%s serial:%d to rate:%d ch:%d fmt:%s layout:%s serial:%d\n",
                           is->audio_filter_src.freq, is->audio_filter_src.channels, av_get_sample_fmt_name(is->audio_filter_src.fmt), buf1, last_serial,
                           frame->sample_rate, av_frame_get_channels(frame), av_get_sample_fmt_name(frame->format), buf2, is->auddec.pkt_serial);

                    is->audio_filter_src.fmt            = frame->format;
                    is->audio_filter_src.channels       = av_frame_get_channels(frame);
                    is->audio_filter_src.channel_layout = dec_channel_layout;
                    is->audio_filter_src.freq           = frame->sample_rate;
                    last_serial                         = is->auddec.pkt_serial;

                    ALOGF("[filters] audio_thread goto configure_audio_filters \n");
                    if ((ret = configure_audio_filters(ffp, is, ffp->afilters, 1)) < 0) {
                        ALOGF("[filters] audio_thread configure_audio_filters fail \n");
                        CCSDL_UnlockMutex(ffp->af_mutex);
                        goto the_end;
                    }
                    ALOGF("[filters] audio_thread configure_audio_filters done \n");
                    CCSDL_UnlockMutex(ffp->af_mutex);
                }

            if ((ret = av_buffersrc_add_frame(is->in_audio_filter, frame)) < 0)
                goto the_end;

            while ((ret = av_buffersink_get_frame_flags(is->out_audio_filter, frame, 0)) >= 0) {
                tb = is->out_audio_filter->inputs[0]->time_base;
#endif
                if (!(af = frame_queue_peek_writable(&is->sampq)))
                    goto the_end;

                af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : (frame->pts == 0 ? frame->pkt_pts : frame->pts) * av_q2d(tb);
                af->pos = av_frame_get_pkt_pos(frame);
                af->serial = is->auddec.pkt_serial;
                af->duration = av_q2d((AVRational){frame->nb_samples, frame->sample_rate});
//                ALOGI(" audio pts %lld dpts %f s\n",frame->pts, af->pts);
                av_frame_move_ref(af->frame, frame);
                frame_queue_push(&is->sampq);

#if CONFIG_AVFILTER
                if (is->audioq.serial != is->auddec.pkt_serial)
                    break;
            }
            if (ret == AVERROR_EOF)
                is->auddec.finished = is->auddec.pkt_serial;
#endif
        }
    } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
 the_end:
#if CONFIG_AVFILTER
    avfilter_graph_free(&is->agraph);
#endif
    av_frame_free(&frame);
    return ret;
}

static int ffplay_video_thread(void *arg)
{
    FFPlayer *ffp = arg;
    VideoState *is = ffp->is;
    AVFrame *frame = av_frame_alloc();
    double pts;
    double duration;
    int ret;
    AVRational tb = is->video_st->time_base;
    AVRational frame_rate = av_guess_frame_rate(is->ic, is->video_st, NULL);

#if CONFIG_AVFILTER
    AVFilterGraph *graph = avfilter_graph_alloc();
    AVFilterContext *filt_out = NULL, *filt_in = NULL;
    int last_w = 0;
    int last_h = 0;
    enum AVPixelFormat last_format = -2;
    int last_serial = -1;
    int last_vfilter_idx = 0;
    if (!graph) {
        av_frame_free(&frame);
        return AVERROR(ENOMEM);
    }
    if (!frame) {
#if CONFIG_AVFILTER
        avfilter_graph_free(&graph);
#endif
        return AVERROR(ENOMEM);
    }
#else
//     ffp_notify_msg2(ffp, FFP_MSG_VIDEO_ROTATION_CHANGED, ffp_get_video_rotate_degrees(ffp));
#endif

    for (;;) {
        ret = get_video_frame(ffp, frame);
        if (ret < 0)
            goto the_end;
        if (!ret)
            continue;

#if CONFIG_AVFILTER
        if (   last_w != frame->width
            || last_h != frame->height
            || last_format != frame->format
            || ffp->vf_changed
            || last_serial != is->viddec.pkt_serial
            || last_vfilter_idx != is->vfilter_idx) {
            CCSDL_LockMutex(ffp->vf_mutex);
            ffp->vf_changed = 0;
            av_log(NULL, AV_LOG_DEBUG,
                   "Video frame changed from size:%dx%d format:%s serial:%d to size:%dx%d format:%s serial:%d\n",
                   last_w, last_h,
                   (const char *)av_x_if_null(av_get_pix_fmt_name(last_format), "none"), last_serial,
                   frame->width, frame->height,
                   (const char *)av_x_if_null(av_get_pix_fmt_name(frame->format), "none"), is->viddec.pkt_serial);
            avfilter_graph_free(&graph);
            graph = avfilter_graph_alloc();
            ALOGF("[filters] ffplay_video_thread goto configure_video_filters\n");
            if ((ret = configure_video_filters(ffp, graph, is, ffp->vfilters_list ? ffp->vfilters_list[is->vfilter_idx] : NULL, frame)) < 0) {
                CCSDL_UnlockMutex(ffp->vf_mutex);
                ALOGF("[filters] ffplay_video_thread configure_video_filters fail \n");
                goto the_end;
            }
            filt_in  = is->in_video_filter;
            filt_out = is->out_video_filter;
            last_w = frame->width;
            last_h = frame->height;
            last_format = frame->format;
            last_serial = is->viddec.pkt_serial;
            last_vfilter_idx = is->vfilter_idx;
            frame_rate = filt_out->inputs[0]->frame_rate;
            CCSDL_UnlockMutex(ffp->vf_mutex);
        }

        ret = av_buffersrc_add_frame(filt_in, frame);
        if (ret < 0)
            goto the_end;

        while (ret >= 0) {
            is->frame_last_returned_time = av_gettime_relative() / 1000000.0;

            ret = av_buffersink_get_frame_flags(filt_out, frame, 0);
            if (ret < 0) {
                if (ret == AVERROR_EOF)
                     ffp->is->viddec.finished = is->viddec.pkt_serial;
                ret = 0;
                break;
            }

            is->frame_last_filter_delay = av_gettime_relative() / 1000000.0 - is->frame_last_returned_time;
            if (fabs(is->frame_last_filter_delay) > AV_NOSYNC_THRESHOLD / 10.0)
                is->frame_last_filter_delay = 0;
            tb = filt_out->inputs[0]->time_base;
#endif
            duration = (frame_rate.num && frame_rate.den ? av_q2d((AVRational){frame_rate.den, frame_rate.num}) : 0);
            pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
//            ALOGI("video pts %lld dpts %f s duration %f video frame queue %d audio frame queue %d video nb %d audio nb %d\n", frame->pts, pts, duration, frame_queue_nb_remaining(&is->pictq), frame_queue_nb_remaining(&is->sampq), is->videoq.nb_packets,is->audioq.nb_packets);

            ret = queue_picture(ffp, frame, pts, duration, av_frame_get_pkt_pos(frame), is->viddec.pkt_serial);
            av_frame_unref(frame);
#if CONFIG_AVFILTER
        }
#endif

        if (ret < 0)
            goto the_end;
    }
 the_end:
#if CONFIG_AVFILTER
    avfilter_graph_free(&graph);
#endif
    av_frame_free(&frame);
    return 0;
}

static int decoder_start(Decoder *d, int (*fn)(void *), void *arg, const char *name)
{
    packet_queue_start(d->queue);
    d->decoder_tid = CCSDL_CreateThreadEx(&d->_decoder_tid, fn, arg, name);
    if (!d->decoder_tid) {
        av_log(NULL, AV_LOG_ERROR, "CCSDL_CreateThread(): %s\n", CCSDL_GetError());
        return AVERROR(ENOMEM);
    }
    return 0;
}

static void decoder_abort(Decoder *d, FrameQueue *fq)
{
    packet_queue_abort(d->queue);
    frame_queue_signal(fq);
    CCSDL_WaitThread(d->decoder_tid, NULL);
    d->decoder_tid = NULL;
    packet_queue_flush(d->queue);
}

static int video_thread(void *arg)
{
    FFPlayer *ffp = (FFPlayer *)arg;
    int       ret = 0;

    if (ffp->node_vdec) {
        ret = ffpipenode_run_sync(ffp->node_vdec);
    }
#ifdef __APPLE__
    if(ret == EIJK_VTB_FAILED) {//硬解解码失败
        ALOGF("vtb decode failed use normal decoder \n");
        ffpipenode_free_p(&ffp->node_vdec);
        ffp->node_vdec = NULL;
        ffp->videotoolbox = 0;
        ffp->node_vdec = ffpipeline_open_video_decoder(ffp->pipeline, ffp);
        if (!ffp->node_vdec) {
            ALOGF("ffpipeline_open_video_decoder re alloc fail \n");
            ffp_notify_msg1(ffp, FFP_MSG_ERROR);
            return ret;
        }
        if ((ret = decoder_start(&ffp->is->viddec, video_thread, ffp, "ff_normal_video_dec")) < 0) {
            ALOGF("decoder_start normal video_dec fail \n");
            ffp_notify_msg1(ffp, FFP_MSG_ERROR);
        }
    }
#endif

    return ret;
}

static int subtitle_thread(void *arg)
{
    FFPlayer *ffp = arg;
    VideoState *is = ffp->is;
    Frame *sp;
    int got_subtitle;
    double pts;

    for (;;) {
        if (!(sp = frame_queue_peek_writable(&is->subpq)))
            return 0;

        if ((got_subtitle = decoder_decode_frame(ffp, &is->subdec, NULL, &sp->sub)) < 0)
            break;

        pts = 0;
#ifdef FFP_MERGE
        if (got_subtitle && sp->sub.format == 0) {
#else
        if (got_subtitle) {
#endif
            if (sp->sub.pts != AV_NOPTS_VALUE)
                pts = sp->sub.pts / (double)AV_TIME_BASE;
            sp->pts = pts;
            sp->serial = is->subdec.pkt_serial;
            sp->width = is->subdec.avctx->width;
            sp->height = is->subdec.avctx->height;
            sp->uploaded = 0;

            /* now we can update the picture count */
            frame_queue_push(&is->subpq);
#ifdef FFP_MERGE
        } else if (got_subtitle) {
            avsubtitle_free(&sp->sub);
#endif
        }
    }
    return 0;
}

// FFP_MERGE: subtitle_thread

/* copy samples for viewing in editor window */
static void update_sample_display(VideoState *is, short *samples, int samples_size)
{
    int size, len;

    size = samples_size / sizeof(short);
    while (size > 0) {
        len = SAMPLE_ARRAY_SIZE - is->sample_array_index;
        if (len > size)
            len = size;
        memcpy(is->sample_array + is->sample_array_index, samples, len * sizeof(short));
        samples += len;
        is->sample_array_index += len;
        if (is->sample_array_index >= SAMPLE_ARRAY_SIZE)
            is->sample_array_index = 0;
        size -= len;
    }
}

/* return the wanted number of samples to get better sync if sync_type is video
 * or external master clock */
static int synchronize_audio(VideoState *is, int nb_samples)
{
    int wanted_nb_samples = nb_samples;

    /* if not master, then we try to remove or add samples to correct the clock */
    if (get_master_sync_type(is) != AV_SYNC_AUDIO_MASTER) {
        double diff, avg_diff;
        int min_nb_samples, max_nb_samples;

        diff = get_clock(&is->audclk) - get_master_clock(is);

        if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD) {
            is->audio_diff_cum = diff + is->audio_diff_avg_coef * is->audio_diff_cum;
            if (is->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
                /* not enough measures to have a correct estimate */
                is->audio_diff_avg_count++;
            } else {
                /* estimate the A-V difference */
                avg_diff = is->audio_diff_cum * (1.0 - is->audio_diff_avg_coef);

                if (fabs(avg_diff) >= is->audio_diff_threshold) {
                    wanted_nb_samples = nb_samples + (int)(diff * is->audio_src.freq);
                    min_nb_samples = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    max_nb_samples = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    wanted_nb_samples = FFMIN(FFMAX(wanted_nb_samples, min_nb_samples), max_nb_samples);
                }
                av_dlog(NULL, "diff=%f adiff=%f sample_diff=%d apts=%0.3f %f\n",
                        diff, avg_diff, wanted_nb_samples - nb_samples,
                        is->audio_clock, is->audio_diff_threshold);
            }
        } else {
            /* too big difference : may be initial PTS errors, so
               reset A-V filter */
            is->audio_diff_avg_count = 0;
            is->audio_diff_cum       = 0;
        }
    }

    return wanted_nb_samples;
}

/**
 * Decode one audio frame and return its uncompressed size.
 *
 * The processed audio frame is decoded, converted if required, and
 * stored in is->audio_buf, with size in bytes given by the return
 * value.
 */
static int audio_decode_frame(FFPlayer *ffp)
{
    VideoState *is = ffp->is;
    int data_size, resampled_data_size;
    int64_t dec_channel_layout;
    av_unused double audio_clock0;
    int wanted_nb_samples;
    Frame *af;

    int translate_time = 1;
    
    if (is->paused)
        return -1;
reload:
    do {
        if (!(af = frame_queue_peek_readable_block(&is->sampq))){
            return -1;
        }
            
        frame_queue_next(&is->sampq);
    } while (af->serial != is->audioq.serial);

    {
        data_size = av_samples_get_buffer_size(NULL, av_frame_get_channels(af->frame),
                                               af->frame->nb_samples,
                                               af->frame->format, 1);

        dec_channel_layout =
            (af->frame->channel_layout && av_frame_get_channels(af->frame) == av_get_channel_layout_nb_channels(af->frame->channel_layout)) ?
            af->frame->channel_layout : av_get_default_channel_layout(av_frame_get_channels(af->frame));
        wanted_nb_samples = synchronize_audio(is, af->frame->nb_samples);

        if (af->frame->format        != is->audio_src.fmt            ||
            dec_channel_layout       != is->audio_src.channel_layout ||
            af->frame->sample_rate   != is->audio_src.freq           ||
            (wanted_nb_samples       != af->frame->nb_samples && !is->swr_ctx)) {
            swr_free(&is->swr_ctx);
            is->swr_ctx = swr_alloc_set_opts(NULL,
                                             is->audio_tgt.channel_layout, is->audio_tgt.fmt, is->audio_tgt.freq,
                                             dec_channel_layout,           af->frame->format, af->frame->sample_rate,
                                             0, NULL);
            if (!is->swr_ctx || swr_init(is->swr_ctx) < 0) {
                av_log(NULL, AV_LOG_ERROR,
                       "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                        af->frame->sample_rate, av_get_sample_fmt_name(af->frame->format), av_frame_get_channels(af->frame),
                        is->audio_tgt.freq, av_get_sample_fmt_name(is->audio_tgt.fmt), is->audio_tgt.channels);
                swr_free(&is->swr_ctx);
                return -1;
            }
            is->audio_src.channel_layout = dec_channel_layout;
            is->audio_src.channels       = av_frame_get_channels(af->frame);
            is->audio_src.freq = af->frame->sample_rate;
            is->audio_src.fmt = af->frame->format;
        }

        if (is->swr_ctx) {
            const uint8_t **in = (const uint8_t **)af->frame->extended_data;
            uint8_t **out = &is->audio_buf1;
            int out_count = (int)((int64_t)wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate + 256);
            int out_size  = av_samples_get_buffer_size(NULL, is->audio_tgt.channels, out_count, is->audio_tgt.fmt, 0);
            int len2;
            if (out_size < 0) {
                av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
                return -1;
            }
            if (wanted_nb_samples != af->frame->nb_samples) {
                if (swr_set_compensation(is->swr_ctx, (wanted_nb_samples - af->frame->nb_samples) * is->audio_tgt.freq / af->frame->sample_rate,
                                            wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate) < 0) {
                    av_log(NULL, AV_LOG_ERROR, "swr_set_compensation() failed\n");
                    return -1;
                }
            }
            av_fast_malloc(&is->audio_buf1, &is->audio_buf1_size, out_size);
            if (!is->audio_buf1)
                return AVERROR(ENOMEM);
            len2 = swr_convert(is->swr_ctx, out, out_count, in, af->frame->nb_samples);
            if (len2 < 0) {
                av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
                return -1;
            }
            if (len2 == out_count) {
                av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
                if (swr_init(is->swr_ctx) < 0)
                    swr_free(&is->swr_ctx);
            }
            is->audio_buf = is->audio_buf1;
            int bytes_per_sample = av_get_bytes_per_sample(is->audio_tgt.fmt);
            resampled_data_size = len2 * is->audio_tgt.channels * bytes_per_sample;
//        #if defined(__ANDROID__)
            if (ffp->soundtouch_enable && ffp->pf_playback_rate != 1.0f && ffp->pf_playback_rate != 0.0f && !is->abort_request) {
#if defined(__ANDROID__)
                av_fast_malloc(&is->audio_new_buf, &is->audio_new_buf_size, out_size * translate_time);
#else
                //ijk_soundtouch_translate中每次接收转换后的采样数最大值是 sample_rate/n_channel，换算成字节数为 sample_rate * bytes_per_sample
                int audioBuf_size = FFMAX(out_size * translate_time, af->frame->sample_rate * bytes_per_sample);
                av_fast_malloc(&is->audio_new_buf, &is->audio_new_buf_size, audioBuf_size);
#endif
                for (int i = 0; i < (resampled_data_size / 2); i++)
                {
                    is->audio_new_buf[i] = (is->audio_buf1[i * 2] | (is->audio_buf1[i * 2 + 1] << 8));
                }

                int ret_len = ijk_soundtouch_translate(is->handle, is->audio_new_buf, (float)(ffp->pf_playback_rate), (float)(1.0f/ffp->pf_playback_rate),
                                                       resampled_data_size / 2, bytes_per_sample, is->audio_tgt.channels, af->frame->sample_rate);
                if (ret_len > 0) {
                    is->audio_buf = (uint8_t*)is->audio_new_buf;
                    resampled_data_size = ret_len;
                } else {
                    translate_time++;
                    goto reload;
                }
            }
//        #endif
        } else {
            is->audio_buf = af->frame->data[0];
            resampled_data_size = data_size;
        }

        audio_clock0 = is->audio_clock;
        /* update the audio clock with the pts */
        if (!isnan(af->pts))
            is->audio_clock = af->pts + (double) af->frame->nb_samples / af->frame->sample_rate;
        else
            is->audio_clock = NAN;
        is->audio_clock_serial = af->serial;
#ifdef FFP_SHOW_AUDIO_DELAY
#ifdef DEBUG
        {
            static double last_clock;
            printf("audio: delay=%0.3f clock=%0.3f clock0=%0.3f\n",
                   is->audio_clock - last_clock,
                   is->audio_clock, audio_clock0);
            last_clock = is->audio_clock;
        }
#endif
#endif
    }
    if (!is->auddec.first_frame_decoded) {
        ALOGD("avcodec/Audio: first frame decoded\n");
        is->auddec.first_frame_decoded_time = CCSDL_GetTickHR();
        is->auddec.first_frame_decoded = 1;
    }
    if (!ffp->first_audio_frame_rendered) {
        ffp->first_audio_frame_rendered = 1;
        ffp_notify_msg1(ffp, FFP_MSG_AUDIO_RENDERING_START);
    }
    return resampled_data_size;
}

/* prepare a new audio buffer */
static void sdl_audio_callback(void *opaque, Uint8 *stream, int len)
{
    FFPlayer *ffp = opaque;
    VideoState *is = ffp->is;
    int audio_size, len1;

    ffp->audio_callback_time = av_gettime_relative();
    if (ffp->pf_playback_rate_changed) {
        ffp->pf_playback_rate_changed = 0;
//#if defined(__ANDROID__)
        if (!ffp->soundtouch_enable) {
            SDL_AoutSetPlaybackRate(ffp->aout, ffp->pf_playback_rate);
        }
//#else
//#endif
    }
    if (ffp->pf_playback_volume_changed) {
        ffp->pf_playback_volume_changed = 0;
        SDL_AoutSetPlaybackVolume(ffp->aout, ffp->pf_playback_volume);
    }
    
    if(is->audio_tgt.frame_size <= 0 || is->audio_tgt.bytes_per_sec <= 0) {
        ALOGF("frame_size == 0 || bytes_per_sec == 0\n");
        return;
    }
    
    while (len > 0) {
        if (is->audio_buf_index >= is->audio_buf_size) {
           audio_size = audio_decode_frame(ffp);
           if (audio_size < 0) {
                /* if error, just output silence */
               is->audio_buf      = is->silence_buf;
               is->audio_buf_size = sizeof(is->silence_buf) / is->audio_tgt.frame_size * is->audio_tgt.frame_size;
           } else {
               if (is->show_mode != SHOW_MODE_VIDEO)
                   update_sample_display(is, (int16_t *)is->audio_buf, audio_size);
               is->audio_buf_size = audio_size;
           }
           is->audio_buf_index = 0;
        }
        if (is->auddec.pkt_serial != is->audioq.serial) {
            // ALOGE("aout_cb: flush\n");
            is->audio_buf_index = is->audio_buf_size;
            memset(stream, 0, len);
            // stream += len;
            // len = 0;
            CCSDL_AoutFlushAudio(ffp->aout);
            break;
        }
        len1 = is->audio_buf_size - is->audio_buf_index;
        if (len1 > len)
            len1 = len;
        if(!ffp->muted) {
            memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
        } else {
            memset(stream, 0, len1);
        }
        len -= len1;
        stream += len1;
        is->audio_buf_index += len1;
    }
    is->audio_write_buf_size = is->audio_buf_size - is->audio_buf_index;
    /* Let's assume the audio driver that is used by SDL has two periods. */
    if (!isnan(is->audio_clock)) {
        double dPts = is->audio_clock - (double)(is->audio_write_buf_size) / is->audio_tgt.bytes_per_sec;
        is->audio_cur_pts = dPts;
        is->stat_info.play_time = dPts;
        set_clock_at(&is->audclk, dPts - CCSDL_AoutGetLatencySeconds(ffp->aout), is->audio_clock_serial, ffp->audio_callback_time / 1000000.0);
        sync_clock_to_slave(&is->extclk, &is->audclk);
    }
    
    if (is->latest_audio_seek_load_serial == is->audio_clock_serial) {
         int latest_audio_seek_load_serial = __atomic_exchange_n(&(is->latest_audio_seek_load_serial), -1, memory_order_seq_cst);
         if (latest_audio_seek_load_serial == is->audio_clock_serial) {
             ALOGF("accurate_seek post msg audio render start \n");
             if (ffp->av_sync_type == AV_SYNC_AUDIO_MASTER) {
                 ffp_notify_msg2(ffp, FFP_MSG_AUDIO_SEEK_RENDERING_START, 1);
             } else {
                 ffp_notify_msg2(ffp, FFP_MSG_AUDIO_SEEK_RENDERING_START, 0);
             }
         }
     }
}

static int audio_open(FFPlayer *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params)
{
    FFPlayer *ffp = opaque;
    CCSDL_AudioSpec wanted_spec, spec;
    const char *env;
    static const int next_nb_channels[] = {0, 0, 1, 6, 2, 6, 4, 6};
#ifdef FFP_MERGE
    static const int next_sample_rates[] = {0, 44100, 48000, 96000, 192000};
#endif
    static const int next_sample_rates[] = {6000, 11025, 12000, 22050, 24000, 44100, 48000};
    int next_sample_rate_idx = FF_ARRAY_ELEMS(next_sample_rates) - 1;

    env = CCSDL_getenv("CCSDL_AUDIO_CHANNELS");
    if (env) {
        wanted_nb_channels = atoi(env);
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
    }
    if (!wanted_channel_layout || wanted_nb_channels != av_get_channel_layout_nb_channels(wanted_channel_layout)) {
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
        wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
    }
    wanted_nb_channels = av_get_channel_layout_nb_channels(wanted_channel_layout);
    wanted_spec.channels = wanted_nb_channels;
    wanted_spec.freq = wanted_sample_rate;
    if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0) {
        av_log(NULL, AV_LOG_ERROR, "Invalid sample rate or channel count!\n");
        return -1;
    }
    while (next_sample_rate_idx && next_sample_rates[next_sample_rate_idx] >= wanted_spec.freq)
        next_sample_rate_idx--;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.silence = 0;
    wanted_spec.samples = FFMAX(CCSDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wanted_spec.freq / CCSDL_AUDIO_MAX_CALLBACKS_PER_SEC));
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = opaque;
    int max_audio_open_times = 3;
    while (CCSDL_AoutOpenAudio(ffp->aout, &wanted_spec, &spec) < 0) {
        if (ffp->is->abort_request) {
            ALOGI("CCSDL_AoutOpenAudio ing --> abort_request \n");
            return -1;
        }
        if(!max_audio_open_times--) {
            ALOGI("CCSDL_AoutOpenAudio failed return \n");
            return -1;
        }

        av_log(NULL, AV_LOG_WARNING, "CCSDL_OpenAudio (%d channels, %d Hz): %s\n",
               wanted_spec.channels, wanted_spec.freq, CCSDL_GetError());
        wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];
        if (!wanted_spec.channels) {
            wanted_spec.freq = next_sample_rates[next_sample_rate_idx--];
            wanted_spec.channels = wanted_nb_channels;
            if (!wanted_spec.freq) {
                av_log(NULL, AV_LOG_ERROR,
                       "No more combinations to try, audio open failed\n");
                return -1;
            }
        }
        wanted_channel_layout = av_get_default_channel_layout(wanted_spec.channels);
    }
    if (spec.format != AUDIO_S16SYS) {
        av_log(NULL, AV_LOG_ERROR,
               "SDL advised audio format %d is not supported!\n", spec.format);
        return -1;
    }
    if (spec.channels != wanted_spec.channels) {
        wanted_channel_layout = av_get_default_channel_layout(spec.channels);
        if (!wanted_channel_layout) {
            av_log(NULL, AV_LOG_ERROR,
                   "SDL advised channel count %d is not supported!\n", spec.channels);
            return -1;
        }
    }

    audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
    audio_hw_params->freq = spec.freq;
    audio_hw_params->channel_layout = wanted_channel_layout;
    audio_hw_params->channels =  spec.channels;
    audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->channels, 1, audio_hw_params->fmt, 1);
    audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
    if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
        av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
        return -1;
    }

    CCSDL_AoutSetDefaultLatencySeconds(ffp->aout, ((double)(2 * spec.size)) / audio_hw_params->bytes_per_sec);
    return spec.size;
}

/* open a given stream. Return 0 if OK */
static int stream_component_open(FFPlayer *ffp, int stream_index)
{
    VideoState *is = ffp->is;
    AVFormatContext *ic = is->ic;
    AVCodecContext *avctx;
    AVCodec *codec;
    const char *forced_codec_name = NULL;
    AVDictionary *opts;
    AVDictionaryEntry *t = NULL;
    int sample_rate, nb_channels;
    int64_t channel_layout;
    int ret = 0;
    int stream_lowres = ffp->lowres;

    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return -1;
//    avctx = ic->streams[stream_index]->codec;
     avctx = avcodec_alloc_context3(NULL);
    ret = avcodec_parameters_to_context(avctx, ic->streams[stream_index]->codecpar);
    if (ret < 0)
        goto fail;
    
    av_codec_set_pkt_timebase(avctx, ic->streams[stream_index]->time_base);

    codec = avcodec_find_decoder(avctx->codec_id);
    
    switch (avctx->codec_type) {
        case AVMEDIA_TYPE_AUDIO   : is->last_audio_stream    = stream_index; forced_codec_name = ffp->audio_codec_name; break;
//         FFP_MERGE: case AVMEDIA_TYPE_SUBTITLE:
        case AVMEDIA_TYPE_SUBTITLE: is->last_subtitle_stream = stream_index; forced_codec_name = ffp->subtitle_codec_name; break;
        case AVMEDIA_TYPE_VIDEO   : is->last_video_stream    = stream_index; forced_codec_name = ffp->video_codec_name; break;
        default: break;
    }
    if (forced_codec_name)
        codec = avcodec_find_decoder_by_name(forced_codec_name);
    if (!codec) {
        // FIXME: 9 report unknown codec id/name
        if (forced_codec_name) av_log(NULL, AV_LOG_WARNING,
                                      "No codec could be found with name '%s'\n", forced_codec_name);
        else                   av_log(NULL, AV_LOG_WARNING,
                                      "No codec could be found with id %d\n", avctx->codec_id);
        return -1;
    }

    avctx->codec_id = codec->id;
    if(stream_lowres > av_codec_get_max_lowres(codec)){
        av_log(avctx, AV_LOG_WARNING, "The maximum value for lowres supported by the decoder is %d\n",
                av_codec_get_max_lowres(codec));
        stream_lowres = av_codec_get_max_lowres(codec);
    }
    av_codec_set_lowres(avctx, stream_lowres);

    if(stream_lowres) avctx->flags |= CODEC_FLAG_EMU_EDGE;
    if (ffp->fast)    avctx->flags2 |= CODEC_FLAG2_FAST;
    if(codec->capabilities & CODEC_CAP_DR1)
        avctx->flags |= CODEC_FLAG_EMU_EDGE;

    avctx->flags |= CODEC_FLAG_LOW_DELAY;
    opts = filter_codec_opts(ffp->codec_opts, avctx->codec_id, ic, ic->streams[stream_index], codec);
    if (!av_dict_get(opts, "threads", NULL, 0))
        av_dict_set(&opts, "threads", "auto", 0);
    if (stream_lowres)
        av_dict_set_int(&opts, "lowres", stream_lowres, 0);
    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO || avctx->codec_type == AVMEDIA_TYPE_AUDIO)
        av_dict_set(&opts, "refcounted_frames", "1", 0);
    //if (avctx->codec_type != AVMEDIA_TYPE_VIDEO) {
    if ((ret = avcodec_open2(avctx, codec, &opts)) < 0) {
        goto fail;
    }
    //}
    if ((t = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
#ifdef FFP_MERGE
        ret = AVERROR_OPTION_NOT_FOUND;
        goto fail;
#endif
    }

    ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;
    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
#if CONFIG_AVFILTER
        {
            AVFilterLink *link;

            is->audio_filter_src.freq           = avctx->sample_rate;
            is->audio_filter_src.channels       = avctx->channels;
            is->audio_filter_src.channel_layout = get_valid_channel_layout(avctx->channel_layout, avctx->channels);
            is->audio_filter_src.fmt            = avctx->sample_fmt;
            CCSDL_LockMutex(ffp->af_mutex);
            ALOGF("[filters] stream_component_open goto configure_audio_filters \n");
            if ((ret = configure_audio_filters(ffp, is, ffp->afilters, 0)) < 0) {
                CCSDL_UnlockMutex(ffp->af_mutex);
                ALOGF("[filters] stream_component_open configure_audio_filters fail \n");
                goto fail;
            }
            ALOGF("[filters] stream_component_open configure_audio_filters done \n");
            ffp->af_changed = 0;
            CCSDL_UnlockMutex(ffp->af_mutex);
            link = is->out_audio_filter->inputs[0];
            sample_rate    = link->sample_rate;
            nb_channels    = link->channels;
            channel_layout = link->channel_layout;
        }
#else
        sample_rate    = avctx->sample_rate;
        nb_channels    = avctx->channels;
        channel_layout = avctx->channel_layout;
#endif

        /* prepare audio output */
        if ((ret = audio_open(ffp, channel_layout, nb_channels, sample_rate, &is->audio_tgt)) < 0)
            goto fail;
        ffp_set_audio_codec_info(ffp, AVCODEC_MODULE_NAME, avcodec_get_name(avctx->codec_id));
        is->audio_hw_buf_size = ret;
        is->audio_src = is->audio_tgt;
        is->audio_buf_size  = 0;
        is->audio_buf_index = 0;

        /* init averaging filter */
        is->audio_diff_avg_coef  = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
        is->audio_diff_avg_count = 0;
        /* since we do not have a precise anough audio fifo fullness,
           we correct audio sync only if larger than this threshold */
        is->audio_diff_threshold = 2.0 * is->audio_hw_buf_size / is->audio_tgt.bytes_per_sec;

        is->audio_stream = stream_index;
        is->audio_st = ic->streams[stream_index];

        packet_queue_start(&is->audioq);
        decoder_init(&is->auddec, avctx, &is->audioq, is->continue_read_thread);
        if ((is->ic->iformat->flags & (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK)) && !is->ic->iformat->read_seek) {
            is->auddec.start_pts = is->audio_st->start_time;
            is->auddec.start_pts_tb = is->audio_st->time_base;
        }
        if ((ret = decoder_start(&is->auddec, audio_thread, ffp, "ff_audio_dec")) < 0) {
            goto out;
        }
        CCSDL_AoutPauseAudio(ffp->aout, 0);
        break;
    case AVMEDIA_TYPE_VIDEO:
        is->video_stream = stream_index;
        is->video_st = ic->streams[stream_index];
        packet_queue_start(&is->videoq);
        decoder_init(&is->viddec, avctx, &is->videoq, is->continue_read_thread);
        ffp->node_vdec = ffpipeline_open_video_decoder(ffp->pipeline, ffp);
        if (!ffp->node_vdec)
            goto fail;
        if ((ret = decoder_start(&is->viddec, video_thread, ffp, "ff_video_dec")) < 0) {
            goto out;
        }
        is->queue_attachments_req = 1;

        if(is->video_st->avg_frame_rate.den && is->video_st->avg_frame_rate.num) {
            double fps = av_q2d(is->video_st->avg_frame_rate);
            is->stat_info.video_framerate = (int)fps;
            if (fps > ffp->max_fps && fps < 100.0) {
                is->is_video_high_fps = 1;
                ALOGI("fps: %lf (too high)\n", fps);
            } else {
                ALOGI("fps: %lf (normal)\n", fps);
            }
        }
        if(is->video_st->r_frame_rate.den && is->video_st->r_frame_rate.num) {
            double tbr = av_q2d(is->video_st->r_frame_rate);
            if (tbr > ffp->max_fps && tbr < 100.0) {
                is->is_video_high_fps = 1;
                ALOGI("fps: %lf (too high)\n", tbr);
            } else {
                ALOGI("fps: %lf (normal)\n", tbr);
            }
        }

        if (is->is_video_high_fps) {
            avctx->skip_frame       = FFMAX(avctx->skip_frame, AVDISCARD_NONREF);
            avctx->skip_loop_filter = FFMAX(avctx->skip_loop_filter, AVDISCARD_NONREF);
            avctx->skip_idct        = FFMAX(avctx->skip_loop_filter, AVDISCARD_NONREF);
        }
        int rotate = ffp_get_video_rotate_degrees(ffp);
        ffp->rotate = rotate;
        break;
    // FFP_MERGE: case AVMEDIA_TYPE_SUBTITLE:
    case AVMEDIA_TYPE_SUBTITLE:
        if (ffp->subtitle_disable) break;

        is->subtitle_stream = stream_index;
        is->subtitle_st = ic->streams[stream_index];
        packet_queue_start(&is->subtitleq);

        ffp_set_subtitle_codec_info(ffp, AVCODEC_MODULE_NAME, avcodec_get_name(avctx->codec_id));

        decoder_init(&is->subdec, avctx, &is->subtitleq, is->continue_read_thread);
        if ((ret = decoder_start(&is->subdec, subtitle_thread, ffp, "ff_subtitle_dec")) < 0)
            goto out;
        ALOGI("start subtitle stream decode thread\n");
        break;
    default:
        break;
    }
    goto out;
fail:
    avcodec_free_context(&avctx);
    
out:
    av_dict_free(&opts);
    return ret;
}

static void stream_component_close(FFPlayer *ffp, int stream_index)
{
    VideoState *is = ffp->is;
    AVFormatContext *ic = is->ic;
    AVCodecContext *avctx;

    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return;
    avctx = ic->streams[stream_index]->codec;

    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        decoder_abort(&is->auddec, &is->sampq);
        CCSDL_AoutCloseAudio(ffp->aout);

        decoder_destroy(&is->auddec);
        packet_queue_flush(&is->audioq);
        swr_free(&is->swr_ctx);
        av_freep(&is->audio_buf1);
        is->audio_buf1_size = 0;
        is->audio_buf = NULL;

#ifdef FFP_MERGE
        if (is->rdft) {
            av_rdft_end(is->rdft);
            av_freep(&is->rdft_data);
            is->rdft = NULL;
            is->rdft_bits = 0;
        }
#endif
        break;
    case AVMEDIA_TYPE_VIDEO:
        ALOGW("begin wait for video_tid\n");
        decoder_abort(&is->viddec, &is->pictq);
        ALOGW("end wait for video_tid\n");

        decoder_destroy(&is->viddec);
        packet_queue_flush(&is->videoq);
        break;
    // FFP_MERGE: case AVMEDIA_TYPE_SUBTITLE:
    case AVMEDIA_TYPE_SUBTITLE:
        decoder_abort(&is->subdec, &is->subpq);
        decoder_destroy(&is->subdec);
        packet_queue_flush(&is->subtitleq);
        break;
    default:
        break;
    }

    ic->streams[stream_index]->discard = AVDISCARD_ALL;
    avcodec_close(avctx);
    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        is->audio_st = NULL;
        is->audio_stream = -1;
        break;
    case AVMEDIA_TYPE_VIDEO:
        is->video_st = NULL;
        is->video_stream = -1;
        break;
    // FFP_MERGE: case AVMEDIA_TYPE_SUBTITLE:
    case AVMEDIA_TYPE_SUBTITLE:
        is->subtitle_st = NULL;
        is->subtitle_stream = -1;
    default:
        break;
    }
}

static int decode_interrupt_cb(void *ctx)
{
    VideoState *is = ctx;
    return is->abort_request || (!is->prepared && (is->open_input_time > 0 && av_gettime_relative() - is->open_input_time >= 10 * 1000 * 1000));
}

static int is_realtime(AVFormatContext *s)
{
    if(   !strcmp(s->iformat->name, "rtp")
       || !strcmp(s->iformat->name, "rtsp")
       || !strcmp(s->iformat->name, "sdp")
    )
        return 1;

    if(s->pb && (   !strncmp(s->filename, "rtp:", 4)
                 || !strncmp(s->filename, "udp:", 4)
                )
    )
        return 1;
    return 0;
}

void calc_download_bps(VideoState *is, AVPacket *pkt) //xinyz
{
    static int sec_cnt = 0;              // 秒计数
    static int sum_download_per_min = 0; // 每秒下载数和，累积一分钟
    int64_t cur_time = av_gettime_relative();
    is->cur_recv_size += pkt->size;
    
    if (is->last_time / 1000000 != cur_time / 1000000)
    {
        is->stat_info.download_bps = (int)(is->cur_recv_size - is->last_recv_size);
        is->last_time = cur_time;
        is->last_recv_size = is->cur_recv_size;
        // 统计一分钟数据量
        sec_cnt++;
        if (sec_cnt > 60) {
            is->stat_info.download_per_min = sum_download_per_min;
            sec_cnt = 0;
            sum_download_per_min = 0;
            ALOGE("loadbytes stat---> download_per_min:%d\n", is->stat_info.download_per_min);
        }
        sum_download_per_min += is->stat_info.download_bps;
    }

}


//jitter buffer related
static void init_jitter_calculator(JitterCalculator *jc, int period_ms) {
    printf("%s period=%d\n", __func__, period_ms);
    jc->max_jitter = 0;
    jc->second_max_jitter = 0;
    jc->forward_period = period_ms;
    jc->forward_time = 0;
}

static int update_jitter_calculator(JitterCalculator *jc, int jitter, int64_t packet_recv_time) {
    if (jitter >= jc->max_jitter) {
        jc->max_jitter = jitter;
    } else if (jitter >= jc->second_max_jitter) {
        jc->second_max_jitter = jitter;
    }
    if (jc->forward_time == 0) {
        jc->forward_time = packet_recv_time + 5 * 1000;//first forward will occur in 5 seconds
        return 0;
    }
    if (packet_recv_time > jc->forward_time) {
        int target = jc->second_max_jitter;
        jc->max_jitter = 0;
        jc->second_max_jitter = 0;
        jc->forward_time = packet_recv_time + (jc->forward_period > 0 ? jc->forward_period : 10 * 1000);
        return target;
    }
    return 0;
}



    
/* this thread gets the stream from the disk or the network */
static int read_thread(void *arg)
{
    FFPlayer *ffp = arg;
    ffp->read_thread_id = pthread_self();
    VideoState *is = ffp->is;
    AVFormatContext *ic = NULL;
    int err, i, ret __unused;
    int st_index[AVMEDIA_TYPE_NB];
    AVPacket pkt1, *pkt = &pkt1;
//    int eof = 0;
    int64_t stream_start_time;
    int completed = 0;
    int pkt_in_play_range = 0;
    AVDictionaryEntry *t;
    AVDictionary **opts;
    int orig_nb_streams;
    CCSDL_mutex *wait_mutex = CCSDL_CreateMutex();
    int scan_all_pmts_set = 0;
    int last_error = 0;
//    int64_t video_packet_count = 0;
//    int64_t audio_packet_count = 0;
    ffp->max_video_buffer_packet_nb = 20 * 15;
//    int64_t cur_audio_time;
//    int64_t audio_frame_time;
    int open_retry_num = 0;
    int open_input_retry_num = 0;
    int max_open_input_stream_retry_num = 5;
    int primary_open_input_retry_delay = 1000;
    int open_input_retry_delay_advance = 500;

    ffp_toggle_buffering(ffp, 1);
    int pre_percent = 0;
    int64_t audio_packet_count = 0;

    int64_t last_video_packet_recv_time = 0;

open_retry:
    memset(st_index, -1, sizeof(st_index));
    is->last_video_stream = is->video_stream = -1;
    is->last_audio_stream = is->audio_stream = -1;
//#ifdef FFP_MERGE
    is->last_subtitle_stream = is->subtitle_stream = -1;
//#endif

    ic = avformat_alloc_context();
    ic->interrupt_callback.callback = decode_interrupt_cb;
    ic->interrupt_callback.opaque = is;
    if (!av_dict_get(ffp->format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
        av_dict_set(&ffp->format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        scan_all_pmts_set = 1;
        av_dict_set(&ffp->format_opts, "formatprobesize", "4096", 0);
    }
    
    char probesize[32] = {0};
    char analyzeduration[32] = {0};

    size_t len = 0;
    t = av_dict_get(ffp->format_opts, "probesize", NULL, AV_DICT_MATCH_CASE);
    if(t && t->value && (len = strlen(t->value)) > 0) {
        memcpy(probesize, t->value, len < 32 ? len : 32);
    }
    t = av_dict_get(ffp->format_opts, "analyzeduration", NULL, AV_DICT_MATCH_CASE);
    if(t && t->value && (len = strlen(t->value)) > 0) {
        memcpy(analyzeduration, t->value, len < 32 ? len : 32);
    }
    
    if (ffp->format_control_message) {
        av_format_set_control_message_cb(ic, ffp_format_control_message);
        av_format_set_opaque(ic, ffp);
    }
    
    if (strlen(probesize) > 0){
        av_dict_set(&ffp->format_opts, "probesize", probesize, 0);
    }else{
        ic->probesize = 100000;
    }
        
    if (strlen(analyzeduration) > 0){
        av_dict_set(&ffp->format_opts, "analyzeduration", analyzeduration, 0);
    }else{
        ic->max_analyze_duration = 200000;
    }
        
    
    int64_t time_begin_open_input = av_gettime_relative();
    
    if (is->open_input_time <= 0) {
        is->open_input_time = av_gettime_relative();
    }
    
    err = avformat_open_input(&ic, is->filename, is->iformat, &ffp->format_opts);
    ffp->video_saver->init_video_saver(ffp->video_saver, ic);
    
    int64_t time_end_open_input = av_gettime_relative();
    ALOGI("stream_open--->avformat_open_input = %lld ms, avformat_open_input spends time = %lld ms\n",
        (time_end_open_input - ffp->is->stream_open_time) / 1000, (time_end_open_input - time_begin_open_input) / 1000);
    if (err < 0) {
        if (open_input_retry_num >= max_open_input_stream_retry_num) {
            ALOGE("open input error %d:%s max retry reached\n", err, av_err2str(err));
            print_error(is->filename, err);
            last_error = err;
            ret = -1;
            goto fail;
        } else {
            ALOGE("open input error %d:%s\n", err, av_err2str(err));
            int open_input_retry_delay = primary_open_input_retry_delay + open_input_retry_delay_advance * open_input_retry_num;
            int sleeped = 0;
            while (!is->abort_request && sleeped < open_input_retry_delay) {
                CCSDL_Delay(50);
                sleeped += 50;
            }
            if (!is->abort_request) {
                if (ic) {
                    avformat_close_input(&ic);
                    ic = NULL;
                }
                open_input_retry_num++;
                is->stat_info.reconnect_count = open_input_retry_num;
                goto open_retry;
            } else {
                print_error(is->filename, err);
                last_error = err;
                ret = -1;
                goto fail;
            }
        }
    }

//#if defined(__ANDROID__)
    // get the rediret url and time
    if (ic->pb->redirect_url[0] != 0)
    {
        strncpy(is->stat_info.redirect_url, ic->pb->redirect_url, REDIRECT_URL_LEN-1);
        is->stat_info.redirect_url[REDIRECT_URL_LEN-1] = 0;
        is->stat_info.redirect_time = ic->pb->redirect_time / 1000000.0;
        const char *proto_prefix = "http://";
        char *prefix = strstr(is->stat_info.redirect_url, proto_prefix);
        if (prefix) {
            //parse server ip
            char *pos_ip_start = is->stat_info.redirect_url + strlen(proto_prefix);
            char *pos_ip_end = strchr(pos_ip_start, '/');
            if (pos_ip_end) {
                int ip_length = 0;
                char *colon = strchr(pos_ip_start, ':');
                if (colon && colon < pos_ip_end) {
                    ip_length = (int)(colon - pos_ip_start);
                } else {
                    ip_length = (int)(pos_ip_end - pos_ip_start);
                }
                if (ip_length < SERVER_IP_LEN - 1) {
                    strncpy(is->stat_info.server_ip, pos_ip_start, ip_length);
                }
            }
            //parse stream id
            char *pos_question = strchr(is->stat_info.redirect_url, '?');
            if (pos_question) {
                char *pos_last_slash = strrchr(is->stat_info.redirect_url, '/');
                if (pos_last_slash && pos_last_slash < pos_question) {
                    char *pos_stream_id_start = pos_last_slash + 1;
                    char *pos_postfix = strchr(pos_stream_id_start, '.');
                    char *pos_stream_id_end = (pos_postfix > pos_stream_id_start && pos_postfix < pos_question) ? pos_postfix : pos_question;
                    int stream_id_length = (int)(pos_stream_id_end - pos_stream_id_start);
                    if (stream_id_length < STREAM_ID_LEN - 1) {
                        strncpy(is->stat_info.stream_id, pos_stream_id_start, stream_id_length);
                    }
                }
            }
        }
        
    }
//#endif

    if (scan_all_pmts_set)
        av_dict_set(&ffp->format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);

    if ((t = av_dict_get(ffp->format_opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
#ifdef FFP_MERGE
        ret = AVERROR_OPTION_NOT_FOUND;
        goto fail;
#endif
    }
    is->ic = ic;

    if (ffp->genpts)
        ic->flags |= AVFMT_FLAG_GENPTS;

    av_format_inject_global_side_data(ic);

    opts = setup_find_stream_info_opts(ic, ffp->codec_opts);
    orig_nb_streams = ic->nb_streams;

    int64_t time_begin_find_stream_info = av_gettime_relative();
    err = avformat_find_stream_info(ic, opts);
    int64_t time_end_find_stream_info = av_gettime_relative();
    ALOGI("stream_open--->avformat_find_stream_info = %lld ms, avformat_find_stream_info spends time = %lld ms\n",
        (time_end_find_stream_info - ffp->is->stream_open_time) / 1000,
        (time_end_find_stream_info - time_begin_find_stream_info) / 1000);

//    for (i = 0; i < orig_nb_streams; i++)
//        av_dict_free(&opts[i]);
    
    int audioIndex = -1;
    int subtitleIndex = -1;
    ALOGI("subtitle language %s audio language: %s\n", ffp->subtitle_language, ffp->audio_language);
    for (i = 0; i < orig_nb_streams; i++) {
        av_dict_free(&opts[i]);
        int type = ic->streams[i]->codec->codec_type;
        AVDictionaryEntry *lang = av_dict_get(ic->streams[i]->metadata, "language", NULL, 0);
        if (lang)
        {
//            ALOGI("meta: language %s type: %d\n", lang->value, type);
            if (ffp->audio_language && type == AVMEDIA_TYPE_AUDIO) {
                if (!strcmp(ffp->audio_language, lang->value))
                {
                    audioIndex = i;
                    ALOGI("audio select stream %d lang=\"%s\"\n", i, lang->value);
                }
            }
            else if (ffp->subtitle_language && type == AVMEDIA_TYPE_SUBTITLE) {
                if (!strcmp(ffp->subtitle_language, lang->value))
                {
                    subtitleIndex = i;
                    ALOGI("subtitle select stream %d lang=\"%s\"\n", i, lang->value);
                }
            }
        }
    }
    av_freep(&opts);

    if (err < 0) {
        if (open_input_retry_num >= max_open_input_stream_retry_num) {
            ALOGE("find stream info error %d:%s max retry reached\n", err, av_err2str(err));
            //finding stream info error
            last_error = FFP_MSG_ERROR_FIND_STREAM_INFO;
            av_log(NULL, AV_LOG_WARNING,
                   "%s: could not find codec parameters\n", is->filename);
            ret = -1;
            goto fail;
        } else {
            ALOGE("find stream info error %d:%s\n", err, av_err2str(err));
            int open_input_retry_delay = primary_open_input_retry_delay + open_input_retry_delay_advance * open_input_retry_num;
            int sleeped = 0;
            while (!is->abort_request && sleeped < open_input_retry_delay) {
                CCSDL_Delay(50);
                sleeped += 50;
            }
            if (!is->abort_request) {
                if (ic) {
                    avformat_close_input(&ic);
                    ic = NULL;
                }
                open_input_retry_num++;
                is->stat_info.reconnect_count = open_input_retry_num;
                goto open_retry;
            } else {
                print_error(is->filename, err);
                last_error = err;
                ret = -1;
                goto fail;
            }
        }
    }

    if (ic->pb)
        ic->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end

    if (ffp->seek_by_bytes < 0)
        ffp->seek_by_bytes = !!(ic->iformat->flags & AVFMT_TS_DISCONT) && strcmp("ogg", ic->iformat->name);

    is->max_frame_duration = (ic->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;
    ALOGI("max_frame_duration: %.3f\n", is->max_frame_duration);

#ifdef FFP_MERGE
    if (!window_title && (t = av_dict_get(ic->metadata, "title", NULL, 0)))
        window_title = av_asprintf("%s - %s", t->value, input_filename);

#endif
    /* if seeking requested, we execute it */
    if (ffp->start_time != AV_NOPTS_VALUE) {
        int64_t timestamp;

        timestamp = ffp->start_time;
        /* add the stream start time */
        if (ic->start_time != AV_NOPTS_VALUE)
            timestamp += ic->start_time;
        ret = avformat_seek_file(ic, -1, INT64_MIN, timestamp, INT64_MAX, 0);
        if (ret < 0) {
            av_log(NULL, AV_LOG_WARNING, "%s: could not seek to position %0.3f\n",
                    is->filename, (double)timestamp / AV_TIME_BASE);
        }
    }

    is->realtime = is_realtime(ic);
    if (true || ffp->show_status)
        av_dump_format(ic, 0, is->filename, 0);

    int audio_stream_count = 0;
    int video_stream_count = 0;
    int h264_stream_count = 0;
    int first_h264_stream = -1;
    for (i = 0; i < ic->nb_streams; i++) {
        AVStream *st = ic->streams[i];
        enum AVMediaType type = st->codec->codec_type;
        st->discard = AVDISCARD_ALL;
        if (ffp->wanted_stream_spec[type] && st_index[type] == -1)
            if (avformat_match_stream_specifier(ic, st, ffp->wanted_stream_spec[type]) > 0)
                st_index[type] = i;

        // choose first h264
        AVCodecContext *codec = ic->streams[i]->codec;
        if (codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_count++;
            if (codec->codec_id == AV_CODEC_ID_H264) {
                h264_stream_count++;
                if (first_h264_stream < 0)
                    first_h264_stream = i;
            }
        } else if (codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_count++;
        }
    }
    if (video_stream_count > 1 && st_index[AVMEDIA_TYPE_VIDEO] < 0) {
        st_index[AVMEDIA_TYPE_VIDEO] = first_h264_stream;
        av_log(NULL, AV_LOG_WARNING, "multiple video stream found, prefer first h264 stream: %d\n", first_h264_stream);
    }
    if (!ffp->video_disable)
        st_index[AVMEDIA_TYPE_VIDEO] =
            av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO,
                                st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);
    if (!ffp->audio_disable)
    {
        if (audioIndex >= 0) {
            st_index[AVMEDIA_TYPE_AUDIO] = audioIndex;
        }else
        {
            st_index[AVMEDIA_TYPE_AUDIO] =
            av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO,
                                st_index[AVMEDIA_TYPE_AUDIO],
                                st_index[AVMEDIA_TYPE_VIDEO],
                                NULL, 0);
        }
        ALOGI("select stream audio index: %d, set audio index:%d, prosize: %d\n", st_index[AVMEDIA_TYPE_AUDIO], audioIndex, ic->probesize);
    }
        
//#ifdef FFP_MERGE
    if (!ffp->video_disable && !ffp->subtitle_disable)
    {
       if (subtitleIndex >= 0) {
           st_index[AVMEDIA_TYPE_SUBTITLE] = subtitleIndex;
       }else
       {
           st_index[AVMEDIA_TYPE_SUBTITLE] =
           av_find_best_stream(ic, AVMEDIA_TYPE_SUBTITLE,
                               st_index[AVMEDIA_TYPE_SUBTITLE],
                               (st_index[AVMEDIA_TYPE_AUDIO] >= 0 ?
                                st_index[AVMEDIA_TYPE_AUDIO] :
                                st_index[AVMEDIA_TYPE_VIDEO]),
                               NULL, 0);
       }
        ALOGI("select stream subtitle index: %d, set subtitle index:%d\n", st_index[AVMEDIA_TYPE_SUBTITLE], subtitleIndex);
    }
//#endif
    ijkmeta_set_avformat_context_l(ffp->meta, ic);
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0)
        ijkmeta_set_int64_l(ffp->meta, IJKM_KEY_VIDEO_STREAM, st_index[AVMEDIA_TYPE_VIDEO]);
    if (st_index[AVMEDIA_TYPE_AUDIO] >= 0)
        ijkmeta_set_int64_l(ffp->meta, IJKM_KEY_AUDIO_STREAM, st_index[AVMEDIA_TYPE_AUDIO]);
    if (st_index[AVMEDIA_TYPE_SUBTITLE] >= 0)
        ijkmeta_set_int64_l(ffp->meta, IJKM_KEY_TIMEDTEXT_STREAM, st_index[AVMEDIA_TYPE_SUBTITLE]);

    is->show_mode = ffp->show_mode;
#ifdef FFP_MERGE // bbc: dunno if we need this
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        AVStream *st = ic->streams[st_index[AVMEDIA_TYPE_VIDEO]];
        AVCodecContext *avctx = st->codec;
        AVRational sar = av_guess_sample_aspect_ratio(ic, st, NULL);
        if (avctx->width)
            set_default_window_size(avctx->width, avctx->height, sar);
    }
#endif

    /* open the streams */
    if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {
        stream_component_open(ffp, st_index[AVMEDIA_TYPE_AUDIO]);
        ALOGI("stream_open--->audio stream_component_open time = %lld ms\n", (av_gettime_relative() - ffp->is->stream_open_time) / 1000);
    }

    ret = -1;
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        AVCodecContext *vcodec = ic->streams[st_index[AVMEDIA_TYPE_VIDEO]]->codec;
        is->stat_info.video_bitrate = vcodec->bit_rate;
        is->stat_info.video_width = vcodec->width;
        is->stat_info.video_height = vcodec->height;
        double video_frame_rate = av_q2d(vcodec->framerate);
        if (!isnan(video_frame_rate) && video_frame_rate > 0) {
            ffp->max_video_buffer_packet_nb = 15 * video_frame_rate;
        }
        
        // retry avformat_open_input() when video codec is not initiated correctly
        if (ic->streams[st_index[AVMEDIA_TYPE_VIDEO]]->codec->width <= 0)
        {
            if (open_retry_num > 3) // retry 3 times
                goto fail;
            if (is->audio_stream >= 0)
                stream_component_close(ffp, is->audio_stream);
            if (ic) {
                avformat_close_input(&is->ic);
                is->ic = NULL;
            }
            open_retry_num++;
            ALOGW("Retry avformat_open_input() when video codec is not initiated correctly\n");
            goto open_retry;
        }
        
        ret = stream_component_open(ffp, st_index[AVMEDIA_TYPE_VIDEO]);
        ALOGI("stream_open--->video stream_component_open time = %lld ms\n", (av_gettime_relative() - ffp->is->stream_open_time) / 1000);
    }
    if (is->show_mode == SHOW_MODE_NONE)
        is->show_mode = ret >= 0 ? SHOW_MODE_VIDEO : SHOW_MODE_RDFT;

//#ifdef FFP_MERGE
    if (st_index[AVMEDIA_TYPE_SUBTITLE] >= 0) {
        stream_component_open(ffp, st_index[AVMEDIA_TYPE_SUBTITLE]);
    }
//#endif

    if (is->video_stream < 0 && is->audio_stream < 0) {
        av_log(NULL, AV_LOG_FATAL, "Failed to open file '%s' or configure filtergraph\n",
               is->filename);
        //open stream component failure.
        last_error = FFP_MSG_ERROR_OPEN_STREAM_COMPONENT;
        ret = -1;
        goto fail;
    }
    if (is->audio_stream >= 0) {
        is->audioq.is_buffer_indicator = 1;
        is->buffer_indicator_queue = &is->audioq;
    } else if (is->video_stream >= 0) {
        is->videoq.is_buffer_indicator = 1;
        is->buffer_indicator_queue = &is->videoq;
    } else {
        assert("invalid streams");
    }

    if (ffp->infinite_buffer < 0 && is->realtime)
        ffp->infinite_buffer = 1;

    if (!ffp->start_on_prepared)
        toggle_pause(ffp, 1);
    
    ALOGI("stream_open--->FFP_MSG_PREPARED time = %lld ms\n", (av_gettime_relative() - ffp->is->stream_open_time) / 1000);
    ffp->prepared = true;
    is->prepared = 1;
    ffp_notify_msg1(ffp, FFP_MSG_PREPARED);
    if (is->video_st && is->video_st->codec) {
        AVCodecContext *avctx = is->video_st->codec;
        ffp_notify_msg3(ffp, FFP_MSG_VIDEO_SIZE_CHANGED, avctx->width, avctx->height);
        ffp_notify_msg3(ffp, FFP_MSG_SAR_CHANGED, avctx->sample_aspect_ratio.num, avctx->sample_aspect_ratio.den);
    }
    if (!ffp->start_on_prepared) {
        while (is->pause_req && !is->abort_request) {
            CCSDL_Delay(20);
        }
    }
    if (ffp->auto_start) {
        ffp_notify_msg1(ffp, FFP_REQ_START);
        ffp->auto_start = 0;
    }
    
    is->force_initdisplay = 1;
    //ffp->buffering_target_duration_ms = 20; //set the first buffering time
    int first_audio_frame = 1;
    int first_video_frame = 1;
    
    init_jitter_calculator(&is->jitter_calculator, 5 * 1000);
    
    for (;;) {
        if (is->abort_request)
            break;
#ifdef FFP_MERGE
        if (is->paused != is->last_paused) {
            is->last_paused = is->paused;
            if (is->paused)
                is->read_pause_return = av_read_pause(ic);
            else
                av_read_play(ic);
        }
#endif
#if CONFIG_RTSP_DEMUXER || CONFIG_MMSH_PROTOCOL
        if (is->paused &&
                (!strcmp(ic->iformat->name, "rtsp") ||
                 (ic->pb && !strncmp(ffp->input_filename, "mmsh:", 5)))) {
            /* wait 10 ms to avoid trying to get another packet */
            /* XXX: horrible */
            CCSDL_Delay(10);
            continue;
        }
#endif
        if (is->seek_req) {
            ALOGI("[dir] seek start\n");

            
            int64_t seek_target = is->seek_pos;
            int64_t seek_min    = is->seek_rel > 0 ? seek_target - is->seek_rel + 2: INT64_MIN;
            int64_t seek_max    = is->seek_rel < 0 ? seek_target - is->seek_rel - 2: INT64_MAX;
// FIXME the +-2 is due to rounding being not done in the correct direction in generation
//      of the seek_pos/seek_rel variables
            
            
            ffp_toggle_buffering(ffp, 1);
            ffp_notify_msg3(ffp, FFP_MSG_BUFFERING_UPDATE, 0, 0);
            ffp->video_saver->seek_handler(ffp->video_saver);
            
            ret = avformat_seek_file(is->ic, -1, seek_min, seek_target, seek_max, is->seek_flags);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR,
                       "%s: error while seeking\n", is->ic->filename);
            } else {
                if (is->audio_stream >= 0) {
                    packet_queue_flush(&is->audioq);
                    packet_queue_put(&is->audioq, &flush_pkt);
                    CCSDL_AoutFlushAudio(ffp->aout);
//                    packet_queue_repush(&is->audioq);
                }
//#ifdef FFP_MERGE
                if (is->subtitle_stream >= 0) {
                    packet_queue_flush(&is->subtitleq);
                    packet_queue_put(&is->subtitleq, &flush_pkt);
                }
//#endif
                if (is->video_stream >= 0) {
                    if (ffp->node_vdec) {
                        ffpipenode_flush(ffp->node_vdec);
                    }
//                    packet_queue_flush(&is->videoq);
                    if(ffp->smooth_loop_req){
                        packet_queue_put(&is->videoq, &flush_pkt);
                        is->videoq.serial--;
                    }else{
                        packet_queue_flush(&is->videoq);
                        packet_queue_put(&is->videoq, &flush_pkt);
                        ALOGI("[dir] packet_queue_flush(&is->videoq);\n");
                    }
//                    packet_queue_repush(&is->videoq);
                    
                }
                if (is->seek_flags & AVSEEK_FLAG_BYTE) {
                   set_clock(&is->extclk, NAN, 0);
                } else {
                    if(!ffp->smooth_loop_req){
                        set_clock(&is->extclk, seek_target / (double)AV_TIME_BASE, 0);
                    }
                }
                is->latest_video_seek_load_serial = is->videoq.serial;
                is->latest_audio_seek_load_serial = is->audioq.serial;
                is->latest_seek_load_start_at = av_gettime();
            }
            ALOGI("[dir] seek end\n");
            is->seek_req = 0;
            is->queue_attachments_req = 1;
            ffp->eof = 0;
#ifdef FFP_MERGE
            if (is->paused)
                step_to_next_frame(is);
#endif
            completed = 0;
            CCSDL_LockMutex(ffp->is->play_mutex);
            if (ffp->auto_start) {
                // ALOGE("seek: auto_start\n");
                is->pause_req = 0;
                is->buffering_on = 1;
                ffp->auto_start = 0;
                stream_update_pause_l(ffp);
            }

            if(ffp->changedir_req)
                is->step = 1;
            if (is->pause_req)
                step_to_next_frame_l(ffp);
            CCSDL_UnlockMutex(ffp->is->play_mutex);
            
            if (ffp->enable_accurate_seek) {
                is->drop_aframe_count = 0;
                is->drop_vframe_count = 0;
                CCSDL_LockMutex(is->accurate_seek_mutex);
                if (is->video_stream >= 0) {
                    is->video_accurate_seek_req = 1;
                }
                if (is->audio_stream >= 0) {
                    is->audio_accurate_seek_req = 1;
                }
                CCSDL_CondSignal(is->audio_accurate_seek_cond);
                CCSDL_CondSignal(is->video_accurate_seek_cond);
                CCSDL_UnlockMutex(is->accurate_seek_mutex);
            }
//            ffp->changedir_req = false;
            ffp_notify_msg3(ffp, FFP_MSG_SEEK_COMPLETE, (int)fftime_to_milliseconds(seek_target), ret);
            ffp_toggle_buffering(ffp, 1);
//            if(!is->seek_ahead)
//                ffp_toggle_buffering(ffp, 1);
            is->seek_ahead = 0;
        }
        if (is->queue_attachments_req) {
            if (is->video_st && (is->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
                AVPacket copy;
                if ((ret = av_copy_packet(&copy, &is->video_st->attached_pic)) < 0)
                    goto fail;
                packet_queue_put(&is->videoq, &copy);
                packet_queue_put_nullpacket(&is->videoq, is->video_stream);
            }
            is->queue_attachments_req = 0;
        }

        /* if the queue are full, no need to read more */
        if (ffp->infinite_buffer<1 && !is->seek_req &&
//#ifdef FFP_MERGE
              (is->audioq.size + is->videoq.size + is->subtitleq.size > MAX_QUEUE_SIZE
//#else
//              (is->audioq.size + is->videoq.size > ffp->max_buffer_size
//#endif
            || (   (is->audioq   .nb_packets > MIN_FRAMES || is->audio_stream < 0 || is->audioq.abort_request)
                && (is->videoq   .nb_packets > MIN_FRAMES || is->video_stream < 0 || is->videoq.abort_request
                    || (is->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC))
//#ifdef FFP_MERGE
                && (is->subtitleq.nb_packets > MIN_FRAMES || is->subtitle_stream < 0 || is->subtitleq.abort_request))))
//#else
//                )))
//#endif
        {
            if (ffp->is->buffering_on && is->audioq.size + is->videoq.size > ffp->max_buffer_size) {
                float dur = packet_queue_get_duration(&is->videoq) * av_q2d(is->video_st->time_base) * 1000;
                
                ALOGW("too much data: audio packet count=%d size=%d, video packet count=%d size=%d duration %d %f\n", is->audioq.nb_packets, is->audioq.size, is->videoq.nb_packets, is->videoq.size, packet_queue_get_duration(&is->videoq), dur);
                int64_t pts = 0;
                if (packet_queue_video_cleanup(ffp, &is->videoq, ffp->buffering_target_duration_ms, &pts)) {
                    if (is && is->audio_st && is->video_st) {
                        pts = av_rescale_q(pts, is->video_st->time_base, is->audio_st->time_base);
                    }
                    packet_queue_audio_cleanup(ffp, &is->audioq, pts);

                }
            } else {
                /* wait 10 ms */
                CCSDL_LockMutex(wait_mutex);
                CCSDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
                CCSDL_UnlockMutex(wait_mutex);
                continue;
            }
        }
        if(!ffp->realtime_play) {
            if((!is->paused && ffp->eof) && !is->seek_req) {
                int nb = ffp->is->videoq.nb_packets;
                if(nb <= 5) {
                    if (ffp->loop != 1 && (!ffp->loop || --ffp->loop)) {
                        is->seek_ahead = 1;
                        if(ffp->enable_smooth_loop)
                            ffp->smooth_loop_req = 1;
                        stream_seek(is, ffp->start_time != AV_NOPTS_VALUE ? ffp->start_time : 0, 0, 0);
                    }
                }
            }
        }
       
        if ((!is->paused || completed) &&
            (!is->audio_st || (is->auddec.finished == is->audioq.serial && frame_queue_nb_remaining(&is->sampq) == 0)) &&
            (!is->video_st || (ffp->display_disable || (is->viddec.finished == is->videoq.serial && frame_queue_nb_remaining(&is->pictq) == 0)))) {
            if (ffp->loop != 1 && (!ffp->loop || --ffp->loop)) {
                stream_seek(is, ffp->start_time != AV_NOPTS_VALUE ? ffp->start_time : 0, 0, 0);
            } else if (ffp->autoexit) {
                ret = AVERROR_EOF;
                goto fail;
            } else {
                if (completed) {
                    ALOGE("complete: eof\n");
                    CCSDL_LockMutex(wait_mutex);
                    // infinite wait may block shutdown
                    while(!is->abort_request && !is->seek_req)
                        CCSDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 100);
                    CCSDL_UnlockMutex(wait_mutex);
                    if (!is->abort_request)
                        continue;
                } else {
                    completed = 1;
                    ffp->auto_start = 0;

                    // TODO: 0 it's a bit early to notify complete here
                    ALOGE("completed: (error=%d)\n", ffp->error);
                    ffp_toggle_buffering(ffp, 0);
                    toggle_pause(ffp, 1);
                    if (ffp->error) {
                        ffp_notify_msg2(ffp, FFP_MSG_ERROR, ffp->error);
                    } else {
                        ffp_notify_msg1(ffp, FFP_MSG_COMPLETED);
                    }
                }
            }
        }

        if (!ffp->eof) {
            ret = av_read_frame(ic, pkt);
            ffp->video_saver->save_packet(ffp->video_saver ,pkt);
            
            calc_download_bps(is, pkt);
            if (ret < 0) {
                ALOGE("av_read_frame error = %s \n", av_err2str(ret));
                if ((ret == AVERROR_EOF || avio_feof(ic->pb)) && !ffp->eof) {
                    ALOGE("!!!EOF!!!");
                
                    ffp->video_saver->save_finish(ffp->video_saver);
                    
                    if (is->video_stream >= 0)
                        packet_queue_put_nullpacket(&is->videoq, is->video_stream);
                    if (is->audio_stream >= 0)
                        packet_queue_put_nullpacket(&is->audioq, is->audio_stream);
//#ifdef FFP_MERGE
                    if (is->subtitle_stream >= 0)
                        packet_queue_put_nullpacket(&is->subtitleq, is->subtitle_stream);
//#endif
                    ffp->eof = 1;
                }
                if (ic->pb && ic->pb->error) {
                    if (!ffp->eof) {
                        if (is->video_stream >= 0)
                            packet_queue_put_nullpacket(&is->videoq, is->video_stream);
                        if (is->audio_stream >= 0)
                            packet_queue_put_nullpacket(&is->audioq, is->audio_stream);
                        if (is->subtitle_stream >= 0)
                            packet_queue_put_nullpacket(&is->subtitleq, is->subtitle_stream);
                    }
#ifdef FFP_MERGE
                    if (is->subtitle_stream >= 0)
                        packet_queue_put_nullpacket(&is->subtitleq, is->subtitle_stream);
#endif
                    ffp->eof = 1;
                    ffp->error = ic->pb->error;
                    ALOGE("av_read_frame pb error = %s \n", av_err2str(ffp->error));

                    // break;
                } else {
                    ffp->error = 0;
                }
                if (ffp->eof) {
                    ffp_toggle_buffering(ffp, 0);
                    CCSDL_Delay(100);
                }
                CCSDL_LockMutex(wait_mutex);
                CCSDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 100);
                CCSDL_UnlockMutex(wait_mutex);
                continue;
            } else {
                ffp->eof = 0;
            }

            //check buffering
            if (is->buffering_on) {
                int target_buffer = ffp->buffering_target_duration_ms > 0 ? ffp->buffering_target_duration_ms : JITTER_DEFAULT;
                int percent = 0;
                if (audio_stream_count > 0) {
                    percent = (packet_queue_get_duration(&is->audioq) / (float)target_buffer) * 100;
                } else {
                    percent = (packet_queue_get_duration(&is->videoq) / (float)target_buffer) * 100;
                }
                //flv header has audio stream, but no audio packets received at all. In this case, force ending buffering if more than 10s video received.
                if (audio_stream_count > 0
                    && audio_packet_count <= 0
                    && (packet_queue_get_duration(&is->videoq) > 10 * 1000 || is->videoq.nb_packets > ffp->max_video_buffer_packet_nb * 2 / 3)) {
                    percent = 100;
                }
                if (percent != pre_percent) {
                    if (ffp->display_ready && percent > pre_percent) {
                        ffp_notify_msg2(ffp, FFP_MSG_BUFFERING_UPDATE, percent > 100 ? 100 : percent);
                    }
                    if (percent >= 100) {
                        ALOGW("target_buffer=%d, audio buffer=%lld, video buffer=%lld\n",
                            target_buffer, packet_queue_get_duration(&is->audioq), packet_queue_get_duration(&is->videoq));
                        if (!is->first_buffering_ready) {
                            is->first_buffering_ready = 1;
                            is->first_buffering_ready_time = av_gettime_relative();
                            is->stat_info.first_buffering_time = (is->first_buffering_ready_time - is->stream_open_time) / 1000.0 / 1000.0;
                            ALOGW("stream_open--->first buffering time %.3f s\n", is->stat_info.first_buffering_time);
                            ffp_notify_msg1(ffp, FFP_MSG_FIRST_BUFFERING_READY);
                        }
                        ffp_toggle_buffering(ffp, 0);
                        if (is->last_buffer_time != 0) {
                            ffp->is->stat_info.buffer_pre = (av_gettime_relative() - ffp->is->last_buffer_time) / 1000000.0;
                            ffp->is->stat_info.buffer_sum += ffp->is->stat_info.buffer_pre;
                            ffp->is->last_buffer_time = 0;
                            ALOGW("stuck statistics: buffer_pre = %f, buffer_sum = %f\ns", ffp->is->stat_info.buffer_pre, ffp->is->stat_info.buffer_sum);
                        }
                    }
                }
                if (percent >= 100) {
                    pre_percent = 0;
                } else if (percent > pre_percent) {
                    pre_percent = percent;
                }
            }
            /* check if packet is in play range specified by user, then queue, otherwise discard */
            stream_start_time = ic->streams[pkt->stream_index]->start_time;
            pkt_in_play_range = ffp->duration == AV_NOPTS_VALUE ||
                (pkt->pts - (stream_start_time != AV_NOPTS_VALUE ? stream_start_time : 0)) *
                av_q2d(ic->streams[pkt->stream_index]->time_base) -
                (double)(ffp->start_time != AV_NOPTS_VALUE ? ffp->start_time : 0) / 1000000
                <= ((double)ffp->duration / 1000000);

            //force_videodisplay最多持续多久?
            if(is->force_videodisplay) {
                int64_t cur_st = av_gettime_relative();
                int64_t timeout = (cur_st - is->force_videodisplay_st) /1000;
                if(timeout >= 2 * 1000 && !ffp->audio_disable) {
                    is->force_videodisplay = 0;
                }
            }
            
            /* 当打开音频一直没有音频数据时 */
            if(first_audio_frame && is->get_first_video_frame_tick != 0) { //视频数据来了之后超过2s没有音频数据，则关闭音频
                int64_t cur_st = av_gettime_relative();
                int64_t timeout = (cur_st - is->get_first_video_frame_tick) /1000;
                if(timeout >= 2 * 1000 && !ffp->audio_disable) {
                    if (is->audio_stream >= 0) {
                        is->av_sync_type = AV_SYNC_EXTERNAL_CLOCK;
                    }
                    first_audio_frame = 0;
                }
            }
            if (pkt->stream_index == is->audio_stream && pkt_in_play_range) {
//                if (ffp->realtime_play) {
//                    ffp->realtime_audio_packet_count++;
//                    cur_audio_time = av_gettime_relative();
//                    if (ffp->realtime_last_audio_packet_time == 0)
//                        audio_frame_time = 0;
//                    else
//                        audio_frame_time = (cur_audio_time - ffp->realtime_last_audio_packet_time) / 1000;
//                    ffp->realtime_last_audio_packet_time = cur_audio_time;
//                }
                
                if (first_audio_frame)
                {
                    ALOGI("stream_open--->get first auido frame = %lld ms, size = %d\n", (av_gettime_relative() - is->stream_open_time)/ 1000, (int)pkt->size);
                    first_audio_frame = 0;
                    is->force_videodisplay = 0;
                }
                
                audio_packet_count++;
                
                packet_queue_put(&is->audioq, pkt);
                is->stat_info.buffer_len = pkt->pts / 1000.0 - is->audio_cur_pts;
//                printf("[audio packet] %lld duration=%lld avg=%lld\n", audio_packet_count, packet_queue_get_duration(&is->audioq), packet_queue_get_duration(&is->audioq) / is->audioq.nb_packets);
            } else if (pkt->stream_index == is->video_stream && pkt_in_play_range
                && !(is->video_st && (is->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC))) {
                int64_t video_packet_recv_time = av_gettime_relative();
                packet_queue_put(&is->videoq, pkt);
                
//                printf("[video packet] %lld duration=%lld avg=%lld\n", video_packet_count, packet_queue_get_duration(&is->videoq), packet_queue_get_duration(&is->videoq) / is->videoq.nb_packets);
                if(ffp->videotoolbox && pkt->duration > 0 &&  is->video_st->nb_frames > 0 && pkt->dts + pkt->duration == is->video_st->first_dts + is->video_st->nb_frames * pkt->duration){
                    for(int endIndex = 0; endIndex< ffp->max_ref_frames; endIndex++){
                        AVPacket *endpkt;
                        endpkt = av_packet_clone(pkt);
                        endpkt->pts = pkt->pts + (endIndex+1) * pkt->duration;
                        endpkt->dts = pkt->dts + (endIndex+1) * pkt->duration;
                        endpkt->duration = pkt->duration;
                        packet_queue_put(&is->videoq, endpkt);
                    }
                }
                
                if (first_video_frame)
                {
                    if (first_audio_frame) {
                        if(ffp->realtime_play)
                            is->force_videodisplay = 1;
                        is->force_videodisplay_st = av_gettime_relative();
                    }
                    is->get_first_video_frame_tick = video_packet_recv_time;
                    ALOGI("stream_open--->get first video frame = %lld ms\n", (video_packet_recv_time - is->stream_open_time)/ 1000);
                    first_video_frame = 0;
                }
                
                if (ffp->realtime_play) {
                    if (!ffp->radical_realtime) {
                        ffp->realtime_video_packet_count++;
                        if (ffp->realtime_video_packet_count % FORWARD_CHECK_PACKET_COUNT == 0
                            && !ffp->is->buffering_on
                            && (ffp->is->last_stuck_time == 0 || (av_gettime_relative() - ffp->is->last_stuck_time) / 1000 > SAFE_INTERVAL_SINCE_STUCK)) {
                            
                            bool turnDown = adjust_buffering_target_duration(ffp, false);
                            if (turnDown) {
                                //                                ALOGD("try forward: target_dur=(%d), packet queue duration=(%lld)", ffp->buffering_target_duration_ms, is->videoq.duration);
                                int64_t pts = 0;
                                if (packet_queue_video_cleanup(ffp, &is->videoq, ffp->buffering_target_duration_ms, &pts)) {
                                    if (is && is->audio_st && is->video_st) {
                                        pts = av_rescale_q(pts, is->video_st->time_base, is->audio_st->time_base);
                                    }
                                    packet_queue_audio_cleanup(ffp, &is->audioq, pts);
                                    is->stat_info.forward_count++;
                                }
                            }
                        }
                    } else {
                        int video_jitter = 0;
                        int forward = 0;
                        if (last_video_packet_recv_time != 0) {
                            video_jitter = (int)(video_packet_recv_time - last_video_packet_recv_time);
                        }
                        last_video_packet_recv_time = video_packet_recv_time;
                        if (video_jitter > 0) {
                            forward = update_jitter_calculator(&is->jitter_calculator, video_jitter / 1000, video_packet_recv_time / 1000);
                        }
                        int min_video_buffer = 200;
                        int max_video_buffer = 500;
                        if (forward > 0) {
                            int cur_video_duration = packet_queue_get_duration(&is->videoq);
                            if (cur_video_duration <= max_video_buffer) {
                                ffp->buffering_target_duration_ms_limit = 0;
                            } else {
                                if (forward >= min_video_buffer && forward <= max_video_buffer) {
                                    ffp->buffering_target_duration_ms_limit = forward;
                                } else {
                                    ffp->buffering_target_duration_ms_limit = min_video_buffer;
                                }
                            }
                            printf("forward=%d, buffering_target_duration_ms_limit=%d, cur_video_duration=%d\n", forward, ffp->buffering_target_duration_ms_limit, cur_video_duration);
                        }
                        int64_t pts = 0;
                        if (ffp->buffering_target_duration_ms_limit > 0) {
                            if (packet_queue_video_cleanup(ffp, &is->videoq, ffp->buffering_target_duration_ms_limit, &pts)) {
                                ffp->buffering_target_duration_ms_limit = 0;
                                if (is && is->audio_st && is->video_st) {
                                    pts = av_rescale_q(pts, is->video_st->time_base, is->audio_st->time_base);
                                }
                                packet_queue_audio_cleanup(ffp, &is->audioq, pts);
                                is->stat_info.forward_count++;
                            }
                        }
                    }
                }

//#ifdef FFP_MERGE
            } else if (pkt->stream_index == is->subtitle_stream && pkt_in_play_range) {
                packet_queue_put(&is->subtitleq, pkt);
//#endif
            } else {
                av_free_packet(pkt);
            }

        } else {
            if (is->buffering_on) {
                ffp_toggle_buffering(ffp, 0);
            }
            CCSDL_Delay(100);
            CCSDL_LockMutex(wait_mutex);
            CCSDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
            CCSDL_UnlockMutex(wait_mutex);
        }
    }
    /* wait until the end */
    while (!is->abort_request) {
        CCSDL_Delay(100);
    }

    ret = 0;
 fail:
    /* close each stream */
    if (is->audio_stream >= 0)
        stream_component_close(ffp, is->audio_stream);
    if (is->video_stream >= 0)
        stream_component_close(ffp, is->video_stream);
//#ifdef FFP_MERGE
    if (is->subtitle_stream >= 0)
        stream_component_close(ffp, is->subtitle_stream);
//#endif
    if (ic) {
        avformat_close_input(&is->ic);
        is->ic = NULL;
    }

    if (!ffp->prepared || !is->abort_request) {
        ffp->last_error = last_error;
        ffp_notify_msg2(ffp, FFP_MSG_ERROR, last_error);
    }
    CCSDL_DestroyMutex(wait_mutex);
    return 0;
}

static int video_refresh_thread(void *arg);
static VideoState *stream_open(FFPlayer *ffp, const char *filename, AVInputFormat *iformat)
{
    assert(!ffp->is);
    VideoState *is;

    is = av_mallocz(sizeof(VideoState));
    if (!is)
        return NULL;
    int64_t stream_open_time = av_gettime_relative();
    ALOGI("stream_open");
    is->stream_open_time = stream_open_time;
    av_strlcpy(is->filename, filename, sizeof(is->filename));
    is->iformat = iformat;
    is->ytop    = 0;
    is->xleft   = 0;
    // initiate the statistic info
    is->stat_info.buffer_count = 0;
    is->stat_info.buffer_len = 0;
    is->stat_info.drop_frame = 0;
    is->stat_info.play_state = STATE_IDLE;
    is->stat_info.play_time = 0.0;
    is->stat_info.download_bps = 0;
    is->stat_info.download_per_min = 0;
    is->stat_info.buffer_pre = 0.0;
    is->stat_info.buffer_sum = 0.0;
    is->stat_info.forward_count = 0;
    is->stat_info.redirect_time = 0.0;
    is->stat_info.video_bitrate = 0;
    is->stat_info.video_width = 0;
    is->stat_info.video_height = 0;
    is->stat_info.first_buffering_time = 0;
    is->audio_cur_pts = 0;
    is->last_time = 0;
    is->last_recv_size = 0;
    is->cur_recv_size = 0;
    is->last_buffer_time = 0;
    is->prepared = 0;
    is->seek_ahead = 0;
    /* start video display */
    if (frame_queue_init(&is->pictq, &is->videoq, ffp->pictq_size, 1) < 0)
        goto fail;
//#ifdef FFP_MERGE
    if (frame_queue_init(&is->subpq, &is->subtitleq, SUBPICTURE_QUEUE_SIZE, 0) < 0)
        goto fail;
//#endif
    if (frame_queue_init(&is->sampq, &is->audioq, SAMPLE_QUEUE_SIZE, 1) < 0)
        goto fail;

    packet_queue_init(&is->videoq);
    packet_queue_init(&is->audioq);
//#ifdef FFP_MERGE
    packet_queue_init(&is->subtitleq);
//#endif

    is->continue_read_thread = CCSDL_CreateCond();

    if (!(is->video_accurate_seek_cond = CCSDL_CreateCond())) {
        av_log(NULL, AV_LOG_FATAL, "CCSDL_CreateCond(): %s\n", CCSDL_GetError());
        ffp->enable_accurate_seek = 0;
    }
    if (!(is->audio_accurate_seek_cond = CCSDL_CreateCond())) {
        av_log(NULL, AV_LOG_FATAL, "CCSDL_CreateCond(): %s\n", CCSDL_GetError());
        ffp->enable_accurate_seek = 0;
    }
       
    init_clock(&is->vidclk, &is->videoq.serial);
    init_clock(&is->audclk, &is->audioq.serial);
    init_clock(&is->extclk, &is->extclk.serial);
    is->audio_clock_serial = -1;
    is->av_sync_type = ffp->av_sync_type;

    is->play_mutex = CCSDL_CreateMutex();
    is->accurate_seek_mutex = CCSDL_CreateMutex();
    
    ffp->is = is;
    is->pause_req = !ffp->start_on_prepared;
    is->video_refresh_tid = CCSDL_CreateThreadEx(&is->_video_refresh_tid, video_refresh_thread, ffp, "ff_vout");
    if (!is->video_refresh_tid) {
        av_freep(&ffp->is);
        return NULL;
    }

    is->read_tid = CCSDL_CreateThreadEx(&is->_read_tid, read_thread, ffp, "ff_read");
    if (!is->read_tid) {
fail:
        is->abort_request = true;
        if (is->video_refresh_tid)
            CCSDL_WaitThread(is->video_refresh_tid, NULL);
        stream_close(is);
        return NULL;
    }
    
    if (ffp->soundtouch_enable) {
        is->handle = ijk_soundtouch_create();
        ALOGI("[soundtouch] soundtouch handle create %p ffp %p is %p\n", is->handle, ffp, is);
    }
    return is;
}

// FFP_MERGE: stream_cycle_channel
// FFP_MERGE: toggle_full_screen
// FFP_MERGE: toggle_audio_display
// FFP_MERGE: refresh_loop_wait_event
// FFP_MERGE: event_loop
// FFP_MERGE: opt_frame_size
// FFP_MERGE: opt_width
// FFP_MERGE: opt_height
// FFP_MERGE: opt_format
// FFP_MERGE: opt_frame_pix_fmt
// FFP_MERGE: opt_sync
// FFP_MERGE: opt_seek
// FFP_MERGE: opt_duration
// FFP_MERGE: opt_show_mode
// FFP_MERGE: opt_input_file
// FFP_MERGE: opt_codec
// FFP_MERGE: dummy
// FFP_MERGE: options
// FFP_MERGE: show_usage
// FFP_MERGE: show_help_default
static int ffplay_video_refresh_thread(void *arg)
{
    FFPlayer *ffp = arg;
    VideoState *is = ffp->is;
    double remaining_time = 0.0;
    
    while (!is->abort_request) {
        if (is->force_initdisplay)
        {
            // do CCSDL_VoutDisplayInit() here to accelarate first video frame
            CCSDL_VoutDisplayInit(ffp->vout);
            is->force_initdisplay = 0;
        }
        if (remaining_time > 0.0)
            av_usleep((int)(int64_t)(remaining_time * 1000000.0));
        remaining_time = REFRESH_RATE;
        if (is->show_mode != SHOW_MODE_NONE && (!is->paused || is->force_refresh || is->force_videodisplay))
        {
            video_refresh(ffp, &remaining_time);
        }
    }
    if (ffp->vout && ffp->vout->clear_buffer) {
        ffp->vout->clear_buffer(ffp->vout);
    }
    return 0;
}

static int video_refresh_thread(void *arg)
{
//    FFPlayer *ffp = arg;
//    VideoState *is = ffp->is;
//    double remaining_time = 0.0;
//    while (!is->abort_request) {
//        if (remaining_time > 0.0)
//            av_usleep((int)(int64_t)(remaining_time * 1000000.0));
//        remaining_time = REFRESH_RATE;
//        if (is->show_mode != SHOW_MODE_NONE && (!is->paused || is->force_refresh))
//            video_refresh(ffp, &remaining_time);
//    }
    ffp_video_refresh_thread(arg);
    
    return 0;
}

static int lockmgr(void **mtx, enum AVLockOp op)
{
    switch (op) {
    case AV_LOCK_CREATE:
        *mtx = CCSDL_CreateMutex();
        if (!*mtx)
            return 1;
        return 0;
    case AV_LOCK_OBTAIN:
        return !!CCSDL_LockMutex(*mtx);
    case AV_LOCK_RELEASE:
        return !!CCSDL_UnlockMutex(*mtx);
    case AV_LOCK_DESTROY:
        CCSDL_DestroyMutex(*mtx);
        return 0;
    }
    return 1;
}

// FFP_MERGE: main

/*****************************************************************************
 * end last line in ffplay.c
 ****************************************************************************/

static bool g_ffmpeg_global_inited = false;
static bool g_ffmpeg_global_use_log_report = false;

inline static int log_level_av_to_ijk(int av_level)
{
    int ijk_level = IJK_LOG_VERBOSE;
    if      (av_level <= AV_LOG_PANIC)      ijk_level = IJK_LOG_FATAL;
    else if (av_level <= AV_LOG_FATAL)      ijk_level = IJK_LOG_FATAL;
    else if (av_level <= AV_LOG_ERROR)      ijk_level = IJK_LOG_ERROR;
    else if (av_level <= AV_LOG_WARNING)    ijk_level = IJK_LOG_WARN;
    else if (av_level <= AV_LOG_INFO)       ijk_level = IJK_LOG_INFO;
    // AV_LOG_VERBOSE means detailed info
    else if (av_level <= AV_LOG_VERBOSE)    ijk_level = IJK_LOG_INFO;
    else if (av_level <= AV_LOG_DEBUG)      ijk_level = IJK_LOG_DEBUG;
    else                                    ijk_level = IJK_LOG_VERBOSE;
    return ijk_level;
}

inline static int log_level_ijk_to_av(int ijk_level)
{
    int av_level = IJK_LOG_VERBOSE;
    if      (ijk_level >= IJK_LOG_SILENT)   av_level = AV_LOG_QUIET;
    else if (ijk_level >= IJK_LOG_FATAL)    av_level = AV_LOG_FATAL;
    else if (ijk_level >= IJK_LOG_ERROR)    av_level = AV_LOG_ERROR;
    else if (ijk_level >= IJK_LOG_WARN)     av_level = AV_LOG_WARNING;
    else if (ijk_level >= IJK_LOG_INFO)     av_level = AV_LOG_INFO;
    // AV_LOG_VERBOSE means detailed info
    else if (ijk_level >= IJK_LOG_DEBUG)    av_level = AV_LOG_DEBUG;
    else if (ijk_level >= IJK_LOG_VERBOSE)  av_level = AV_LOG_VERBOSE;
    else if (ijk_level >= IJK_LOG_DEFAULT)  av_level = AV_LOG_VERBOSE;
    else if (ijk_level >= IJK_LOG_UNKNOWN)  av_level = AV_LOG_VERBOSE;
    else                                    av_level = AV_LOG_VERBOSE;
    return av_level;
}

static void ffp_log_callback_brief(void *ptr, int level, const char *fmt, va_list vl)
{
    int ffplv __unused = IJK_LOG_VERBOSE;
    if (level <= AV_LOG_ERROR)
        ffplv = IJK_LOG_ERROR;
    else if (level <= AV_LOG_WARNING)
        ffplv = IJK_LOG_WARN;
    else if (level <= AV_LOG_INFO)
        ffplv = IJK_LOG_INFO;
    else if (level <= AV_LOG_VERBOSE)
        ffplv = IJK_LOG_VERBOSE;
    else
        ffplv = IJK_LOG_DEBUG;

    if (level <= AV_LOG_INFO)
        VLOG(ffplv, IJK_LOG_TAG, fmt, vl);
}

static void ffp_log_callback_report(void *ptr, int level, const char *fmt, va_list vl)
{
    int ffplv __unused = IJK_LOG_VERBOSE;
    if (level <= AV_LOG_ERROR)
        ffplv = IJK_LOG_ERROR;
    else if (level <= AV_LOG_WARNING)
        ffplv = IJK_LOG_WARN;
    else if (level <= AV_LOG_INFO)
        ffplv = IJK_LOG_INFO;
    else if (level <= AV_LOG_VERBOSE)
        ffplv = IJK_LOG_VERBOSE;
    else
        ffplv = IJK_LOG_DEBUG;
    
    va_list vl2;
    char line[1024];
    static int print_prefix = 1;

    va_copy(vl2, vl);
    // av_log_default_callback(ptr, level, fmt, vl);
    av_log_format_line(ptr, level, fmt, vl2, line, sizeof(line), &print_prefix);
    va_end(vl2);

    ALOG(ffplv, IJK_LOG_TAG, "%s", line);
}


int ijkav_register_all(void);
void ffp_global_init()
{
    if (g_ffmpeg_global_inited)
        return;

    /* register all codecs, demux and protocols */
    avcodec_register_all();
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif
#if CONFIG_AVFILTER
    avfilter_register_all();
#endif
    av_register_all();
    
    ijkav_register_all();
    
    avformat_network_init();

    av_lockmgr_register(lockmgr);
    if (g_ffmpeg_global_use_log_report) {
        av_log_set_callback(ffp_log_callback_report);
    } else {
        av_log_set_callback(ffp_log_callback_brief);
    }

    av_init_packet(&flush_pkt);
    flush_pkt.data = (uint8_t *)&flush_pkt;

    g_ffmpeg_global_inited = true;
}

void ffp_global_uninit()
{
    if (!g_ffmpeg_global_inited)
        return;

    av_lockmgr_register(NULL);

#if CONFIG_AVFILTER
    avfilter_uninit();
//    av_freep(&vfilters);
#endif
    avformat_network_deinit();

    g_ffmpeg_global_inited = false;
}

void ffp_global_set_log_report(int use_report)
{
    g_ffmpeg_global_use_log_report = use_report;
    if (use_report) {
        av_log_set_callback(ffp_log_callback_report);
    } else {
        av_log_set_callback(ffp_log_callback_brief);
    }
}

void ffp_global_set_log_level(int log_level)
{
    int av_level = log_level_ijk_to_av(log_level);
    av_log_set_level(av_level);
}


void ffp_io_stat_register(void (*cb)(const char *url, int type, int bytes))
{
    // avijk_io_stat_register(cb);
}

void ffp_io_stat_complete_register(void (*cb)(const char *url,
                                              int64_t read_bytes, int64_t total_size,
                                              int64_t elpased_time, int64_t total_duration))
{
    // avijk_io_stat_complete_register(cb);
}
    
void cc_video_saver_set_path(FFPlayer *ffp ,const char * save_path){
    ffp->video_saver->set_save_path(ffp->video_saver, save_path);
}

FFPlayer *ffp_create(int crop)
{
    FFPlayer* ffp = (FFPlayer*) av_mallocz(sizeof(FFPlayer));
    if (!ffp)
        return NULL;

    msg_queue_init(&ffp->msg_queue);
#if CONFIG_AVFILTER
    ffp->af_mutex = CCSDL_CreateMutex();
    ffp->vf_mutex = CCSDL_CreateMutex();
#endif
    ffp_reset_internal(ffp);
    ffp->crop = crop;
    ffp->meta = ijkmeta_create();
    ffp->video_saver = new_video_saver(ffp);
    return ffp;
}

void ffp_destroy_language_p(char** language)
{
    if (!*language)
        return;
    free(*language);
    *language = NULL;
}
    
void ffp_destroy(FFPlayer *ffp)
{
    ALOGW("ffp_destroy\n");
    if (!ffp)
        return;

    destory_video_saver(&(ffp->video_saver));
    
    if (ffp && ffp->is) {
        av_log(NULL, AV_LOG_WARNING, "ffp_destroy_ffplayer: force stream_close()");
        stream_close(ffp->is);
        ffp->is = NULL;
    }

    CCSDL_VoutFreeP(&ffp->vout);
    CCSDL_AoutFreeP(&ffp->aout);
    ffpipenode_free_p(&ffp->node_vdec);
    ffpipeline_free_p(&ffp->pipeline);
    
    ijkmeta_destroy_p(&ffp->meta);
    ffp_reset_internal(ffp);
    msg_queue_destroy(&ffp->msg_queue);
    
    ffp_destroy_language_p(&ffp->subtitle_language);
    ffp_destroy_language_p(&ffp->audio_language);
#if CONFIG_AVFILTER
    CCSDL_DestroyMutexP(&ffp->af_mutex);
    CCSDL_DestroyMutexP(&ffp->vf_mutex);
#endif
    av_free(ffp);
    ALOGW("ffp_destroy done \n");
}
    
void ffp_destroy_p(FFPlayer **pffp)
{
    if (!pffp)
        return;

    ffp_destroy(*pffp);
    *pffp = NULL;
}

void ffp_set_format_callback(FFPlayer *ffp, ijk_format_control_message cb, void *opaque)
{
    ffp->format_control_message = cb;
    ffp->format_control_opaque  = opaque;
}

void ffp_set_format_option(FFPlayer *ffp, const char *name, const char *value)
{
    if (!ffp)
        return;

    av_dict_set(&ffp->format_opts, name, value, 0);
}

void ffp_set_codec_option(FFPlayer *ffp, const char *name, const char *value)
{
    if (!ffp)
        return;

    av_dict_set(&ffp->codec_opts, name, value, 0);
}

void ffp_set_sws_option(FFPlayer *ffp, const char *name, const char *value)
{
    if (!ffp)
        return;

    av_dict_set(&ffp->sws_opts, name, value, 0);
}

void ffp_set_overlay_format(FFPlayer *ffp, int chroma_fourcc)
{
    switch (chroma_fourcc) {
        case CCSDL_FCC_I420:
        case CCSDL_FCC_YV12:
        case CCSDL_FCC_RV16:
        case CCSDL_FCC_RV24:
        case CCSDL_FCC_RV32:
            ffp->overlay_format = chroma_fourcc;
            break;
        default:
            ALOGE("ffp_set_overlay_format: unknown chroma fourcc: %d\n", chroma_fourcc);
            break;
    }
}

void ffp_set_picture_queue_capicity(FFPlayer *ffp, int frame_count)
{
    ffp->pictq_size = frame_count;
}

void ffp_set_max_fps(FFPlayer *ffp, int max_fps)
{
    ffp->max_fps = max_fps;
}

void ffp_set_framedrop(FFPlayer *ffp, int framedrop)
{
    ffp->framedrop = framedrop;
}

void ffp_set_vtb_max_frame_width(FFPlayer *ffp, int max_frame_width)
{
    ffp->vtb_max_frame_width = max_frame_width;
}

int ffp_get_video_codec_info(FFPlayer *ffp, char **codec_info)
{
    if (!codec_info)
        return -1;

    // FIXME: not thread-safe
    if (ffp->video_codec_info) {
        *codec_info = strdup(ffp->video_codec_info);
    } else {
        *codec_info = NULL;
    }
    return 0;
}

int ffp_get_audio_codec_info(FFPlayer *ffp, char **codec_info)
{
    if (!codec_info)
        return -1;

    // FIXME: not thread-safe
    if (ffp->audio_codec_info) {
        *codec_info = strdup(ffp->audio_codec_info);
    } else {
        *codec_info = NULL;
    }
    return 0;
}

int ffp_prepare_async_l(FFPlayer *ffp, const char *file_name)
{
    assert(ffp);
    assert(!ffp->is);
    assert(file_name);
    ALOGI("ffp_prepare_async_l begin\n");
    if (!ffp->aout) {
        ffp->aout = ffpipeline_open_audio_output(ffp->pipeline, ffp);
        if (!ffp->aout)
            return -1;
    }
    
//#if CONFIG_AVFILTER
//    if (ffp->vfilter0) {
//        GROW_ARRAY(ffp->vfilters_list, ffp->nb_vfilters);
//        ffp->vfilters_list[ffp->nb_vfilters - 1] = ffp->vfilter0;
//    }
//#endif

    VideoState *is = stream_open(ffp, file_name, NULL);
    if (!is) {
        av_log(NULL, AV_LOG_WARNING, "ffp_prepare_async_l: stream_open failed OOM");
        return EIJK_OUT_OF_MEMORY;
    }
    ALOGI("ffp_prepare_async_l done\n");
    ffp->is = is;
    return 0;
}

int ffp_start_from_l(FFPlayer *ffp, long msec)
{
    // ALOGE("ffp_start_at_l\n");
    assert(ffp);
    VideoState *is = ffp->is;
    if (!is)
        return EIJK_NULL_IS_PTR;
    
    if (ffp->loop != 1 && (!ffp->loop || --ffp->loop)) {
        ffp->auto_start = 1;
        ffp_toggle_buffering(ffp, 1);
        ffp_seek_to_l(ffp, msec);
    }
    return 0;
}

int ffp_start_l(FFPlayer *ffp)
{
    // ALOGE("ffp_start_l\n");
    assert(ffp);
    VideoState *is = ffp->is;
    if (!is)
        return EIJK_NULL_IS_PTR;

    toggle_pause(ffp, 0);
    return 0;
}

int ffp_pause_l(FFPlayer *ffp)
{
    // ALOGE("ffp_pause_l\n");
    assert(ffp);
    VideoState *is = ffp->is;
    if (!is)
        return EIJK_NULL_IS_PTR;

    toggle_pause(ffp, 1);
    return 0;
}

int ffp_stop_l(FFPlayer *ffp)
{
    assert(ffp);
    VideoState *is = ffp->is;
    if (is)
        is->abort_request = 1;

    msg_queue_abort(&ffp->msg_queue);
    if (ffp->enable_accurate_seek && is && is->accurate_seek_mutex
        && is->audio_accurate_seek_cond && is->video_accurate_seek_cond) {
        CCSDL_LockMutex(is->accurate_seek_mutex);
        is->audio_accurate_seek_req = 0;
        is->video_accurate_seek_req = 0;
        CCSDL_CondSignal(is->audio_accurate_seek_cond);
        CCSDL_CondSignal(is->video_accurate_seek_cond);
        CCSDL_UnlockMutex(is->accurate_seek_mutex);
    }
    
    return 0;
}

int ffp_wait_stop_l(FFPlayer *ffp)
{
    assert(ffp);

    if (ffp->is) {
        ffp_stop_l(ffp);
        stream_close(ffp->is);
        ffp->is = NULL;
    }
    return 0;
}

int ffp_seek_to_l(FFPlayer *ffp, long msec)
{
    assert(ffp);
    VideoState *is = ffp->is;
    if (!is)
        return EIJK_NULL_IS_PTR;

    if (is->ic == NULL)
        return EIJK_NULL_IS_PTR;
    
    int64_t seek_pos = milliseconds_to_fftime(msec);
    int64_t duration = milliseconds_to_fftime(ffp_get_duration_l(ffp));
    
    if (duration > 0 && seek_pos >= duration && ffp->enable_accurate_seek) {
        toggle_pause(ffp, 1);
        ffp_notify_msg1(ffp, FFP_MSG_COMPLETED);
        return 0;
    }
    
    int64_t start_time = is->ic->start_time;
    if (start_time > 0 && start_time != AV_NOPTS_VALUE)
        seek_pos += start_time;

    // FIXME: 9 seek by bytes
    // FIXME: 9 seek out of range
    // FIXME: 9 seekable
//    ALOGE("stream_seek %"PRId64"(%d) + %"PRId64", \n", seek_pos, (int)msec, start_time);
    stream_seek(is, seek_pos, 0, 0);
    return 0;
}

long ffp_get_current_position_l(FFPlayer *ffp)
{
    assert(ffp);
    VideoState *is = ffp->is;
    if (!is || !is->ic)
        return 0;

    int64_t start_time = is->ic->start_time;
    int64_t start_diff = 0;
    if (start_time > 0 && start_time != AV_NOPTS_VALUE)
        start_diff = fftime_to_milliseconds(start_time);

    int64_t pos = 0;
    double pos_clock = get_master_clock(is);
    if (isnan(pos_clock)) {
        // ALOGE("pos = seek_pos: %d\n", (int)is->seek_pos);
        pos = fftime_to_milliseconds(is->seek_pos);
    } else {
        // ALOGE("pos = pos_clock: %f\n", pos_clock);
        pos = pos_clock * 1000;
    }

    if (pos < 0 || pos < start_diff)
        return 0;

    int64_t adjust_pos = pos - start_diff;
    // ALOGE("pos=%ld\n", (long)adjust_pos);
    return (long)adjust_pos;
}

long ffp_get_duration_l(FFPlayer *ffp)
{
    assert(ffp);
    VideoState *is = ffp->is;
    if (!is || !is->ic)
        return 0;

    int64_t start_time = is->ic->start_time;
    int64_t start_diff = 0;
    if (start_time > 0 && start_time != AV_NOPTS_VALUE)
        start_diff = fftime_to_milliseconds(start_time);

    int64_t duration = fftime_to_milliseconds(is->ic->duration);
    if (duration < 0 || duration < start_diff)
        return 0;

    int64_t adjust_duration = duration - start_diff;
    // ALOGE("dur=%ld\n", (long)adjust_duration);
    return (long)adjust_duration;
}

long ffp_get_playable_duration_l(FFPlayer *ffp)
{
    assert(ffp);
    if (!ffp)
        return 0;

//    return (long)ffp->playable_duration_ms;
    if (ffp->is) {
        return ffp->is->videoq.duration;
    }
    return 0;
}

void ffp_packet_queue_init(PacketQueue *q)
{
    return packet_queue_init(q);
}

void ffp_packet_queue_destroy(PacketQueue *q)
{
    return packet_queue_destroy(q);
}

void ffp_packet_queue_abort(PacketQueue *q)
{
    return packet_queue_abort(q);
}

void ffp_packet_queue_start(PacketQueue *q)
{
    return packet_queue_start(q);
}

void ffp_packet_queue_flush(PacketQueue *q)
{
    return packet_queue_flush(q);
}

int ffp_packet_queue_get(FFPlayer *ffp, PacketQueue *q, AVPacket *pkt, int block, int *serial, bool is_audio_queue)
{
    return packet_queue_get(ffp, q, pkt, block, serial, is_audio_queue);
}

int ffp_packet_queue_get_or_buffering(FFPlayer *ffp, PacketQueue *q, bool is_audio_queue, AVPacket *pkt, int *serial, int *finished)
{
    return packet_queue_get_or_buffering(ffp, q, is_audio_queue, pkt, serial, finished);
}

int ffp_packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
    return packet_queue_put(q, pkt);
}

bool ffp_is_flush_packet(AVPacket *pkt)
{
    if (!pkt)
        return false;

    return pkt->data == flush_pkt.data;
}

Frame *ffp_frame_queue_peek_writable(FrameQueue *f)
{
    return frame_queue_peek_writable(f);
}

void ffp_frame_queue_push(FrameQueue *f)
{
    return frame_queue_push(f);
}

void ffp_alloc_picture(FFPlayer *ffp, Uint32 overlay_format)
{
    return alloc_picture(ffp, overlay_format);
}

int ffp_queue_picture(FFPlayer *ffp, AVFrame *src_frame, double pts, double duration, int64_t pos, int serial)
{
    return queue_picture(ffp, src_frame, pts, duration, pos, serial);
}

int ffp_get_master_sync_type(VideoState *is)
{
    return get_master_sync_type(is);
}

double ffp_get_master_clock(VideoState *is)
{
    return get_master_clock(is);
}

void ffp_toggle_buffering_l(FFPlayer *ffp, int buffering_on)
{
    VideoState *is = ffp->is;
    if (buffering_on && !is->buffering_on) {
        if (!ffp->radical_realtime)
            adjust_buffering_target_duration(ffp, true);
        ALOGD("player buffering start, target(%d), current:audio(%d) sample(%d) video(%d) picture(%d)\n", ffp->buffering_target_duration_ms, packet_queue_get_duration(&is->audioq), packet_queue_get_duration(&is->videoq), is->sampq.size, is->pictq.size);
        is->buffering_on = 1;
        is->stat_info.play_state = STATE_BUFFER;
        stream_update_pause_l(ffp);
        if (ffp->display_ready) {
            if (is->seek_req) {
                is->seek_buffering = 1;
                ffp_notify_msg2(ffp, FFP_MSG_BUFFERING_START, 1);
            } else {
                ffp_notify_msg2(ffp, FFP_MSG_BUFFERING_START, 0);
            }
//            ffp_notify_msg2(ffp, FFP_MSG_BUFFERING_UPDATE, 0);
        }
        
    } else if (!buffering_on && is->buffering_on){
        ALOGD("player buffering end, current:audio(%d) video(%d)\n", packet_queue_get_duration(&is->audioq), packet_queue_get_duration(&is->videoq));
        is->buffering_on = 0;
        is->stat_info.play_state = STATE_PLAYING;
        stream_update_pause_l(ffp);
        if (ffp->display_ready) {
            ffp_notify_msg2(ffp, FFP_MSG_BUFFERING_UPDATE, 100);
            if (is->seek_buffering) {
                is->seek_buffering = 0;
                ffp_notify_msg2(ffp, FFP_MSG_BUFFERING_END, 1);
            } else {
                ffp_notify_msg2(ffp, FFP_MSG_BUFFERING_END, 0);
            }
        }
    }
}

void ffp_toggle_buffering(FFPlayer *ffp, int start_buffering)
{
    CCSDL_LockMutex(ffp->is->play_mutex);
    ffp_toggle_buffering_l(ffp, start_buffering);
    CCSDL_UnlockMutex(ffp->is->play_mutex);
}

int ffp_video_thread(FFPlayer *ffp)
{
    return ffplay_video_thread(ffp);
}

int ffp_video_refresh_thread(FFPlayer *ffp)
{
    return ffplay_video_refresh_thread(ffp);
}

void ffp_set_video_codec_info(FFPlayer *ffp, const char *module, const char *codec)
{
    av_freep(&ffp->video_codec_info);
    ffp->video_codec_info = av_asprintf("%s, %s", module ? module : "", codec ? codec : "");
    ALOGI("VideoCodec: %s", ffp->video_codec_info);
}

void ffp_set_audio_codec_info(FFPlayer *ffp, const char *module, const char *codec)
{
    av_freep(&ffp->audio_codec_info);
    ffp->audio_codec_info = av_asprintf("%s, %s", module ? module : "", codec ? codec : "");
    ALOGI("AudioCodec: %s", ffp->audio_codec_info);
}
void ffp_set_subtitle_codec_info(FFPlayer *ffp, const char *module, const char *codec)
{
    av_freep(&ffp->subtitle_codec_info);
    ffp->subtitle_codec_info = av_asprintf("%s, %s", module ? module : "", codec ? codec : "");
    ALOGI("SubtitleCodec: %s\n", ffp->subtitle_codec_info);
}

void ffp_set_playback_rate(FFPlayer *ffp, float rate)
{
    if (!ffp)
        return;
    ALOGI("Playback rate: %f\n", rate);
    ffp->pf_playback_rate = rate;
#ifdef FFP_AVFILTER_PLAYBACK_RATE
    CCSDL_LockMutex(ffp->af_mutex);
    CCSDL_LockMutex(ffp->vf_mutex);
    ffp->vf_changed = 1;
    ffp->af_changed = 1;
    CCSDL_UnlockMutex(ffp->vf_mutex);
    CCSDL_UnlockMutex(ffp->af_mutex);
#else
    ffp->pf_playback_rate_changed = 1;
#endif
    
    
}

void ffp_set_playback_volume(FFPlayer *ffp, float volume)
{
    if (!ffp)
        return;
    ffp->pf_playback_volume = volume;
    ffp->pf_playback_volume_changed = 1;
}

int ffp_get_video_rotate_degrees(FFPlayer *ffp)
{
    VideoState *is = ffp->is;
    if (!is)
        return 0;
    
    int theta  = abs((int)((int64_t)round(fabs(get_rotation(is->video_st))) % 360));
    switch (theta) {
        case 0:
        case 90:
        case 180:
        case 270:
            break;
        case 360:
            theta = 0;
            break;
        default:
            ALOGW("Unknown rotate degress: %d\n", theta);
            theta = 0;
            break;
    }
    
    return theta;
}

float ffp_get_property_float(FFPlayer *ffp, int id, float default_value)
{
    switch (id) {
        case FFP_PROP_FLOAT_PLAYBACK_RATE:
            return ffp ? ffp->pf_playback_rate : default_value;
        case FFP_PROP_FLOAT_PLAYBACK_VOLUME:
            return ffp ? ffp->pf_playback_volume : default_value;
        default:
            return default_value;
    }
}

void ffp_set_property_float(FFPlayer *ffp, int id, float value)
{
    switch (id) {
        case FFP_PROP_FLOAT_PLAYBACK_RATE:
            ffp_set_playback_rate(ffp, value);
            break;
        case FFP_PROP_FLOAT_PLAYBACK_VOLUME:
            ffp_set_playback_volume(ffp, value);
            break;
        default:
            return;
    }
}

IjkMediaMeta *ffp_get_meta_l(FFPlayer *ffp)
{
    if (!ffp)
        return NULL;

    return ffp->meta;
}

StatInfo *ffp_get_stat_info(FFPlayer *ffp)
{
    if (!ffp)
        return NULL;
    ffp->is->stat_info.drop_frame = ffp->is->frame_drops_early + ffp->is->frame_drops_late;
    ffp->is->stat_info.audio_buffer_duration = packet_queue_get_duration(&ffp->is->audioq) / 1000.0f;
    ffp->is->stat_info.audio_buffer_packet_count = ffp->is->audioq.nb_packets;
    ffp->is->stat_info.video_buffer_duration = packet_queue_get_duration(&ffp->is->videoq) / 1000.0f;
    ffp->is->stat_info.video_buffer_packet_count = ffp->is->videoq.nb_packets;
    return &(ffp->is->stat_info);
}

static int ffp_format_control_message(struct AVFormatContext *s, int type,
                                      void *data, size_t data_size)
{
    if (s == NULL)
        return -1;

    FFPlayer *ffp = (FFPlayer *)s->opaque;
    if (ffp == NULL)
        return -1;

    if (!ffp->format_control_message)
        return -1;

    return ffp->format_control_message(ffp->format_control_opaque, type, data, data_size);
}

void jitter_queue_append_item(struct jitter_queue* q, int64_t msec, int64_t value)
{
    int idx;
    
    if ((NULL == q) || (NULL == q->buffer))
    {
        return;
    }
    
    q->buffer[q->cur_idx] = value;
    
    if (q->cur_idx == q->max_value1_idx || q->cur_idx == q->max_value2_idx)
    {
        q->max_value_1 = 0;
        q->max_value_2 = 0;
        q->max_value1_idx = -1;
        q->max_value2_idx = -1;
        for (idx = 0; idx < q->max_size; idx++)
        {
            if (q->buffer[idx] > q->max_value_1)
            {
                q->max_value_2 = q->max_value_1;
                q->max_value2_idx = q->max_value1_idx;
                q->max_value_1 = q->buffer[idx];
                q->max_value1_idx = idx;
            }
            else if(q->buffer[idx] > q->max_value_2)
            {
                q->max_value_2 = q->buffer[idx];
                q->max_value2_idx = idx;
            }
        }
    }
    else
    {
        if (value > q->max_value_1) {
            q->max_value_2 = q->max_value_1;
            q->max_value2_idx = q->max_value1_idx;
            q->max_value_1 = value;
            q->max_value1_idx = q->cur_idx;
        } else if (value > q->max_value_2) {
            q->max_value_2 = value;
            q->max_value2_idx = q->cur_idx;
        }
    }
    
    q->cur_idx++;
    q->cur_idx = q->cur_idx % q->max_size;
}
            
       
void jitter_queue_init(struct jitter_queue* q, int max_size)
{
    if (max_size > 0)
    {
        q->buffer = (int64_t *)av_malloc(max_size * sizeof(int64_t));
        memset(q->buffer, 0, max_size * sizeof(int64_t));
    }
    else
    {
        q->buffer = NULL;
    }
    q->cur_idx = 0;
    q->max_size = max_size;
    q->max_value_1 = 0;
    q->max_value_2 = 0;
    q->max_value1_idx = -1;
    q->max_value2_idx = -1;
}
            
void jitter_queue_reset(struct jitter_queue* q, int max_size)
{
    av_free(q->buffer);
    jitter_queue_init(q, max_size);
}
            
int64_t get_current_jitter_value(struct jitter_buffer *jitter) {
    if (jitter) {
        int64_t cur_jitter = jitter->jitter_q.max_value_2;
        if (cur_jitter < jitter->min_buffer_msec) {
            cur_jitter = jitter->min_buffer_msec;
        } else if (cur_jitter > jitter->max_buffer_msec) {
            cur_jitter = jitter->max_buffer_msec;
        }
        return cur_jitter;
    } else {
        return 0;
    }
}

void jitter_buffer_init(struct jitter_buffer* jitter, int min_msec, int max_msec, int buffer_msec, int cycle_nb_count)
{
    jitter->buffer_msec = buffer_msec;
    jitter->max_buffer_msec = max_msec;
    jitter->min_buffer_msec = min_msec;
    jitter->cycle_packet_nb_count = cycle_nb_count;
    jitter_queue_init(&jitter->jitter_q, cycle_nb_count);
}
    
    
bool adjust_buffering_target_duration(FFPlayer *ffp, bool turnUp) {
    int64_t old = ffp->buffering_target_duration_ms;
    
    if (turnUp) {
        //放大
        if (old >= ffp->buffering_target_duration_ms_max) {
            return false;
        }
        int64_t dist_sec = (av_gettime_relative() - ffp->is->last_stuck_time) / 1000 / 1000;
        if (ffp->is->last_stuck_time == 0) {
            dist_sec = 0;
        }
        
        if (dist_sec > 0) {
            double target = ffp->buffering_target_duration_ms;
            
            if (dist_sec < 10) {
                target *= 3.0f;
            } else if (dist_sec < 20) {
                target *= 2.5f;
            } else if (dist_sec < 30) {
                target *= 2.0f;
            } else if (dist_sec < 60) {
                target *= 1.5f;
            } else if (dist_sec < 90) {
                target *= 1.3f;
            } else if (dist_sec < 120) {
                target *= 1.2f;
            } else {
                target *= 1.1f;
            }
            
            ffp->buffering_target_duration_ms = target;
        }
    } else {
        //缩小
        int64_t now = av_gettime_relative();
        
        //在3 * 60秒内不能连续缩小
        if (((now - ffp->buffering_target_duration_change_time) / 1000) > (3 * 60 * 1000)) {
            if (old > ffp->buffering_target_duration_ms_min) {
                ffp->buffering_target_duration_ms *= BUFFERING_TARGET_REDUCTOR_RATIO;
            }
            ffp->buffering_target_duration_change_time = now;
        } else {
            return false;
        }
    }
    
    if (ffp->buffering_target_duration_ms < ffp->buffering_target_duration_ms_min) {
        ffp->buffering_target_duration_ms = ffp->buffering_target_duration_ms_min;
    }
    
    if (ffp->buffering_target_duration_ms > ffp->buffering_target_duration_ms_max) {
        ffp->buffering_target_duration_ms = ffp->buffering_target_duration_ms_max;
    }
    
    //更新统计信息
    ffp->is->stat_info.buffer_time = ffp->buffering_target_duration_ms;
    

    return true;
}
    
void ffp_set_crop_mode(FFPlayer *ffp, bool crop, int surface_width, int surface_height)
{
    VideoState *is = ffp->is;
    if (is) {
        CCSDL_LockMutex(ffp->vout->mutex);
        for (int i = 0; i < is->pictq.max_size; i++) {
            CCSDL_VoutOverlay *overlay = is->pictq.queue[i].bmp;
            if (overlay) {
                overlay->crop = crop;
                overlay->reset_padding = true;
                overlay->wanted_display_width = surface_width;
                overlay->wanted_display_height = surface_height;
            }
        }
        CCSDL_UnlockMutex(ffp->vout->mutex);
    }
    ffp->crop = crop;
    ffp->surface_width = surface_width;
    ffp->surface_height = surface_height;
}


static int capture_thread_l(void *arg)
{
    FFPlayer *ffp = arg;
    if (ffp) {
        if (ffp->is && ffp->is->capture_frame_data) {
            int offset = 0;
            uint8_t *data = ffp->is->capture_frame_data;
            for (int i = 0; i < ffp->is->capture_frame_size / 4; i++) {
                //bgra to argb
                uint8_t tmp = data[offset];
                data[offset] = data[offset + 2];
                data[offset + 2] = tmp;
                offset += 4;
            }
            ffp_notify_msg3(ffp, FFP_MSG_CAPTURE_COMPLETED, ffp->is->capture_frame_width, ffp->is->capture_frame_height);
            ffp->is->frame_capturing = 0;
        }
    }
    return 0;
}


static int ffp_capture_frame_func(void *arg) {
    FFPlayer *ffp = arg;
    if (!ffp->is->frame_capturing) {
        CCSDL_LockMutex(ffp->vout->mutex);
        ffp->is->frame_capturing = 1;
        CCSDL_VoutOverlay *overlay = ffp->is->pictq.queue[0].bmp;
        uint8_t *argbData = NULL;
        int pitch = 0;
        CCSDL_VoutFFmpeg_ConverI420ToARGB(overlay, &ffp->is->img_convert_ctx, ffp->sws_flags, &argbData, &pitch);
        int width = overlay->w;
        int height = overlay->h;
        int scaled_width = (width / 2) / 2 * 2;
        int scaled_height = (height / 2) / 2 * 2;
        ffp->is->capture_frame_width = scaled_width;
        ffp->is->capture_frame_height = scaled_height;
        int scaled_line_size = scaled_width * 4;
        int scaled_data_size = scaled_line_size * scaled_height;
        ffp->is->capture_scale_ratio = 2;
        if (!ffp->is->capture_frame_data) {
            ffp->is->capture_frame_size = scaled_data_size;
            ffp->is->capture_frame_data = av_mallocz(scaled_data_size);
        }
        ijk_argb_scale(argbData, pitch, width, height, ffp->is->capture_frame_data, scaled_line_size, scaled_width, scaled_height);
        if (ffp->is && ffp->is->capture_frame_data) {
            int offset = 0;
            uint8_t *data = ffp->is->capture_frame_data;
            for (int i = 0; i < ffp->is->capture_frame_size / 4; i++) {
                //bgra to argb
                uint8_t tmp = data[offset];
                data[offset] = data[offset + 2];
                data[offset + 2] = tmp;
                offset += 4;
            }
            ffp_notify_msg3(ffp, FFP_MSG_CAPTURE_COMPLETED, ffp->is->capture_frame_width, ffp->is->capture_frame_height);
            ffp->is->frame_capturing = 0;
        }
        CCSDL_UnlockMutex(ffp->vout->mutex);
    }
    return 0;
}

void ffp_capture_frame(FFPlayer *ffp)
{
    ffp->is->capture_thread = CCSDL_CreateThreadEx(&ffp->is->_capture_thread, ffp_capture_frame_func, ffp, "capture_thread");

/**
    CCSDL_LockMutex(ffp->vout->mutex);

    VideoState *is = ffp->is;
    if (is && is->stat_info.play_state == STATE_PLAYING) {
        if (!ffp->is->frame_capturing) {
            ffp->is->frame_capturing = 1;
            CCSDL_VoutOverlay *overlay = is->pictq.queue[0].bmp;
            int line_size = overlay->pitches[0];
            int width = overlay->w;
            int height = overlay->h;
            int scaled_width = (width / 2) / 2 * 2;
            int scaled_height = (height / 2) / 2 * 2;
            ffp->is->capture_frame_width = scaled_width;
            ffp->is->capture_frame_height = scaled_height;
            int scaled_line_size = scaled_width * 4;
            int scaled_data_size = scaled_line_size * scaled_height;
            ffp->is->capture_scale_ratio = 2;
            if (!ffp->is->capture_frame_data) {
                ffp->is->capture_frame_size = scaled_data_size;
                ffp->is->capture_frame_data = av_mallocz(scaled_data_size);
            }
            ijk_argb_scale(overlay->pixels[0], line_size, width, height, ffp->is->capture_frame_data, scaled_line_size, scaled_width, scaled_height);
            ffp->is->capture_thread = CCSDL_CreateThreadEx(&ffp->is->_capture_thread, capture_thread_l, ffp, "capture_thread");
        }
    } else {
        ffp_notify_msg3(ffp, FFP_MSG_CAPTURE_COMPLETED, 0, 0);
    }

    ffp_notify_msg3(ffp, FFP_MSG_CAPTURE_COMPLETED, 0, 0);
    CCSDL_UnlockMutex(ffp->vout->mutex);
**/
}

void ffp_set_display_frame_cb(FFPlayer *ffp, OnDisplayFrameCb handle, void *obj)
{
    CCSDL_LockMutex(ffp->vout->mutex);
    VideoState *is = ffp->is;
    is->onDisplayFrameCb = handle;
    is->display_frame_obj = obj;
    CCSDL_UnlockMutex(ffp->vout->mutex);
}
