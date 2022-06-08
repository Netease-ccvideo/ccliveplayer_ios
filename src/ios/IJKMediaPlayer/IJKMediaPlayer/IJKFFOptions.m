//
//  IJKFFOptions.m
//  IJKMediaPlayer
//
//  Created by ZhangRui on 13-10-17.
//  Copyright (c) 2013年 bilibili. All rights reserved.
//

#import "IJKFFOptions.h"
#include "ijkplayer/ios/ijkplayer_ios.h"

@implementation IJKFFOptions

+ (IJKFFOptions *)optionsByDefault
{
    IJKFFOptions *options = [[IJKFFOptions alloc] init];

    options.skipLoopFilter  = IJK_AVDISCARD_NONE;
    options.skipFrame       = IJK_AVDISCARD_NONE;

    options.frameBufferCount  = 3;
    options.maxFps            = 30;
    options.frameDrop         = 0;
    
    options.pauseInBackground = YES;
    options.timeout         = 20 * 1000 * 1000;
    
    options.probesize = 100000;
    options.analyzeduration = 200000;
    
    options.userAgent = @"ccplayersdk";

    options.vtb_max_frame_width = 0;
    
    options.loop = 1;
    options.enableAccurateSeek = 0;
    options.enableSmoothLoop = 0;
    options.enableSubtitle = 0;
    
    return options;
}

- (void)applyTo:(IjkMediaPlayer *)mediaPlayer
{
    [self logOptions];

    [self setCodecOption:@"skip_loop_filter"
               withInt64:self.skipLoopFilter
                      to:mediaPlayer];
    [self setCodecOption:@"skip_frame"
               withInt64:self.skipFrame
                      to:mediaPlayer];

    ijkmp_set_picture_queue_capicity(mediaPlayer, _frameBufferCount);
    ijkmp_set_max_fps(mediaPlayer, _maxFps);
    ijkmp_set_framedrop(mediaPlayer, _frameDrop);

    ijkmp_set_videotoolbox(mediaPlayer, _videoToolbox);
    
    ijkmp_set_enable_accurate_seek(mediaPlayer, _enableAccurateSeek);
    
    ijkmp_set_enable_smooth_loop(mediaPlayer, _enableSmoothLoop);
    
    ijkmp_set_vtb_max_frame_width(mediaPlayer, _vtb_max_frame_width);
    
    ijkmp_set_audio_language(mediaPlayer, [_audioLanguage UTF8String]);
    ijkmp_set_subtitle_language(mediaPlayer, [_subtitleLanguage UTF8String]);
    
    ijkmp_set_enable_subtitle(mediaPlayer, _enableSubtitle);
    
    ijkmp_set_enable_soundtouch(mediaPlayer, _enableSoundtouch);
    if(self.loop >=0) {
        ijkmp_set_loop(mediaPlayer, self.loop);
    }
    
    if (self.timeout > 0) {
        [self setFormatOption:@"timeout"
                    withInt64:self.timeout
                           to:mediaPlayer];
    }
    if ([self.userAgent isEqualToString:@""] == NO) {
        [self setFormatOption:@"user-agent" withString:self.userAgent to:mediaPlayer];
    }
    
    if(_enableMaxProbesize)
    {
        [self enableMaxAnalyzeDuration: mediaPlayer];
    }
}

- (void)logOptions
{
    NSMutableString *echo = [[NSMutableString alloc] init];
    [echo appendString:@"========================================\n"];
    [echo appendString:@"= FFmpeg options:\n"];
    [echo appendFormat:@"= skip_loop_filter: %@\n",   [IJKFFOptions getDiscardString:self.skipLoopFilter]];
    [echo appendFormat:@"= skipFrame:        %@\n",   [IJKFFOptions getDiscardString:self.skipFrame]];
    [echo appendFormat:@"= frameBufferCount: %d\n",   self.frameBufferCount];
    [echo appendFormat:@"= maxFps:           %d\n",   self.maxFps];
    [echo appendFormat:@"= timeout:          %lld\n", self.timeout];
    [echo appendString:@"========================================\n"];
    NSLog(@"%@", echo);
}

+ (NSString *)getDiscardString:(IJKAVDiscard)discard
{
    switch (discard) {
        case IJK_AVDISCARD_NONE:
            return @"avdiscard none";
        case IJK_AVDISCARD_DEFAULT:
            return @"avdiscard default";
        case IJK_AVDISCARD_NONREF:
            return @"avdiscard nonref";
        case IJK_AVDISCARD_BIDIR:
            return @"avdicard bidir;";
        case IJK_AVDISCARD_NONKEY:
            return @"avdicard nonkey";
        case IJK_AVDISCARD_ALL:
            return @"avdicard all;";
        default:
            return @"avdiscard unknown";
    }
}

- (void)setFormatOption:(NSString *)optionName
              withInt64:(int64_t)value
                     to:(IjkMediaPlayer *)mediaPlayer
{
    ijkmp_set_format_option(mediaPlayer,
                           [optionName UTF8String],
                           [[NSString stringWithFormat:@"%lld", value] UTF8String]);
}

- (void)setFormatOption:(NSString *)optionName
              withString:(NSString*)value
                     to:(IjkMediaPlayer *)mediaPlayer
{
    ijkmp_set_format_option(mediaPlayer,
                            [optionName UTF8String],
                            [value UTF8String]);
}


- (void)setCodecOption:(NSString *)optionName
             withInt64:(int64_t)value
                    to:(IjkMediaPlayer *)mediaPlayer
{
    ijkmp_set_codec_option(mediaPlayer,
                           [optionName UTF8String],
                           [[NSString stringWithFormat:@"%lld", value] UTF8String]);
}

- (void)enableProtectModeForBD {
    
    self.skipLoopFilter = IJK_AVDISCARD_ALL;
    self.skipFrame = IJK_AVDISCARD_NONREF;
    self.frameDrop = 2;
}

- (void)enableMaxAnalyzeDuration:(IjkMediaPlayer *)mediaPlayer {
    [self setFormatOption:@"probesize" withInt64:INT_MAX to:mediaPlayer];
    [self setFormatOption:@"analyzeduration" withInt64:0 to:mediaPlayer];
}

- (void) setVideoToolbox:(BOOL)videoToolbox
{
    _videoToolbox = videoToolbox;
}

- (void)setEnableAccurateSeek:(BOOL)enableAccurateSeek {
    _enableAccurateSeek = enableAccurateSeek;
}

- (void)setEnableSmoothLoop:(BOOL)enableSmoothLoop {
    _enableSmoothLoop = enableSmoothLoop;
}
- (void)setAudioLanguage:(NSString *)language
{
    _audioLanguage = language;
}
- (void)setSubtitleLanguage:(NSString *)language
{
    _subtitleLanguage = language;
}

@end
