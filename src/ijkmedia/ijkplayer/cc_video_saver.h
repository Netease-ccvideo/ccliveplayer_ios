//
//  cc_video_saver.h
//  IJKMediaPlayer
//
//  Created by 何钰堂s on 2020/12/24.
//  Copyright © 2020 bilibili. All rights reserved.
//

#ifndef cc_video_saver_h
#define cc_video_saver_h

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include <stdio.h>
#include "ff_ffplay_def.h"

typedef enum{
    SaverStatuUnfinish,
    SaverStatuSuccess,
    SaverStatuFail,
    SaverStatuUnstart
}SaverStatu;


typedef enum{
    SaverEndSucc = 0,
    SaverEndUnfinishFail = -1,
    SaverEndSeekFail = -2,
    SaverEndWriteTrailerFail = -3,
    SaverEndInitialFail = -4,
    SaverEndRemoveFileFail = -5,
}SaverEnd;

typedef struct CCVideoSaver CCVideoSaver;

struct CCVideoSaver{
    const char *file_path;
    FFPlayer   *ffp;
    SaverStatu  save_statu;
    
    AVFormatContext *input_ctx;
    AVFormatContext *output_ctx;
    
    void (*init_video_saver)(CCVideoSaver *saver, AVFormatContext *input_ctx);
    void (*set_save_path)(CCVideoSaver *saver, const char *save_path);
    void (*save_packet)(CCVideoSaver *saver, AVPacket *in_pkt);
    void (*save_finish)(CCVideoSaver *saver);
    void (*seek_handler)(CCVideoSaver *saver);
    int (*stop_handler)(CCVideoSaver *saver);
};


CCVideoSaver * new_video_saver(FFPlayer *ffp);
void destory_video_saver(CCVideoSaver **ssaver);

#endif /* cc_video_saver_h */
