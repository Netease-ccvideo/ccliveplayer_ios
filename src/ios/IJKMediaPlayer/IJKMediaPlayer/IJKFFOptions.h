//
//  IJKFFOptions.h
//  IJKMediaPlayer
//
//  Created by ZhangRui on 13-10-17.
//  Copyright (c) 2013年 bilibili. All rights reserved.
//

#import <Foundation/Foundation.h>

typedef enum IJKAVDiscard{
    /* We leave some space between them for extensions (drop some
     * keyframes for intra-only or drop just some bidir frames). */
    IJK_AVDISCARD_NONE    =-16, ///< discard nothing
    IJK_AVDISCARD_DEFAULT =  0, ///< discard useless packets like 0 size packets in avi
    IJK_AVDISCARD_NONREF  =  8, ///< discard all non reference
    IJK_AVDISCARD_BIDIR   = 16, ///< discard all bidirectional frames
    IJK_AVDISCARD_NONKEY  = 32, ///< discard all frames except keyframes
    IJK_AVDISCARD_ALL     = 48, ///< discard all
} IJKAVDiscard;

typedef struct IjkMediaPlayer IjkMediaPlayer;

@interface IJKFFOptions : NSObject

+(IJKFFOptions *)optionsByDefault;

-(void)applyTo:(IjkMediaPlayer *)mediaPlayer;

- (void)enableProtectModeForBD;

@property(nonatomic) IJKAVDiscard skipLoopFilter;
@property(nonatomic) IJKAVDiscard skipFrame;

@property(nonatomic) int frameBufferCount;
@property(nonatomic) int maxFps;
@property(nonatomic) int frameDrop;
@property(nonatomic) BOOL pauseInBackground;
@property(nonatomic) int probesize;
@property(nonatomic) int analyzeduration;
@property(nonatomic) int vtb_max_frame_width;
@property(nonatomic) int loop;
@property(nonatomic, strong) NSString* userAgent;

@property(nonatomic, strong) NSString* audioLanguage;
@property(nonatomic, strong) NSString* subtitleLanguage;

@property(nonatomic) int64_t timeout; ///< read/write timeout, -1 for infinite, in microseconds

@property(nonatomic, assign) BOOL videoToolbox;

@property(nonatomic, assign) BOOL enableAccurateSeek;


@property(nonatomic, assign) BOOL enableSmoothLoop;

@property(nonatomic, assign) BOOL enableSubtitle;

@property(nonatomic, assign) BOOL enableMaxProbesize;

@property(nonatomic, assign) BOOL enableSoundtouch;


@end
