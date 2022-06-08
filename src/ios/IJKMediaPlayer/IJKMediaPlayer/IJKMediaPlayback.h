/*
 * IJKMediaPlayback.h
 *
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

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <MediaPlayer/MediaPlayer.h>
#import "FrameProcessor.h"
#import "PlayerConfig.h"

//call back function
#ifndef HAVE_DISPLAY_FRAME_CB
#define HAVE_DISPLAY_FRAME_CB
enum FrameFormat
{
    FRAME_FORMAT_NONE = -1,
    FRAME_FORMAT_YUV = 0,
    FRAME_FORMAT_RGB = 1,
    FRAME_FORMAT_BGR = 2,
};

typedef struct DisplayFrame {
    void *data;
    int format;
    int width;
    int height;
    int pitch;
    int bits;
} DisplayFrame;

// callback function to get display frame
typedef void (*OnDisplayFrameCb)(DisplayFrame *frame, void *obj);
#endif

typedef enum IJKLogLevel {
    k_IJK_LOG_UNKNOWN = 0,
    k_IJK_LOG_DEFAULT = 1,
    
    k_IJK_LOG_VERBOSE = 2,
    k_IJK_LOG_DEBUG   = 3,
    k_IJK_LOG_INFO    = 4,
    k_IJK_LOG_WARN    = 5,
    k_IJK_LOG_ERROR   = 6,
    k_IJK_LOG_FATAL   = 7,
    k_IJK_LOG_SILENT  = 8,
} IJKLogLevel;

@protocol IJKMediaPlayback;

#pragma mark IJKMediaPlayback

@protocol IJKMediaPlayback <NSObject>

- (void)prepareToPlay;
- (void)play;
- (void)pause;
- (void)stop;
- (BOOL)isPlaying;
- (void)shutdown;
- (void)setVideoEnable:(BOOL)enable;
- (void)setPlayControlParameters:(BOOL)canFwd fwdNew:(BOOL)forwardNew bufferTimeMsec:(int)bufferTime
    fwdExtTimeMsec:(int)fwdExtTime minJitterMsec:(int)minJitter maxJitterMsec:(int)maxJitter;
- (void)setFrameProcessor:(FrameProcessor*)fp;
- (void)setDisplayFrameCb:(OnDisplayFrameCb)handle withObj:(void *)obj;
- (void)setPlayerConfig:(PlayerConfig*)config;
- (void)setPauseInBackground:(BOOL)pause;
- (void)muteAudio:(BOOL)mute;
- (void)setVolume:(float)volume;

+ (void)setupAudioSessionWithMediaPlay:(BOOL)mediaPlay;

@property(nonatomic, readonly)  UIView *view;
@property(nonatomic)            NSTimeInterval currentPlaybackTime;
@property(nonatomic, readonly)  NSTimeInterval duration;
@property(nonatomic, readonly)  NSTimeInterval playableDuration;
@property(nonatomic, readonly)  NSInteger bufferingProgress;

@property(nonatomic, readonly)  BOOL isPreparedToPlay;
@property(nonatomic, readonly)  MPMoviePlaybackState playbackState;
@property(nonatomic, readonly)  MPMovieLoadState loadState;
@property(nonatomic, readonly) int isSeekBuffering;
@property(nonatomic, readonly) int isAudioSync;
@property(nonatomic, readonly) int isVideoSync;

@property(nonatomic, readonly) CGSize naturalSize;
@property (nonatomic, readonly) CGSize videoSize;

@property(nonatomic, readonly) int64_t numberOfBytesTransferred;

- (UIImage *)thumbnailImageAtCurrentTime;

@property(nonatomic) MPMovieControlStyle controlStyle;
@property(nonatomic) MPMovieScalingMode scalingMode;
@property(nonatomic) BOOL shouldAutoplay;
@property(nonatomic) BOOL enableAutoIdleTimer;

@property (nonatomic) float playbackRate;
@property (nonatomic) float playbackVolume;
#pragma mark Notifications

#ifdef __cplusplus
#define IJK_EXTERN extern "C" __attribute__((visibility ("default")))
#else
#define IJK_EXTERN extern __attribute__((visibility ("default")))
#endif

IJK_EXTERN NSString *const IJKMediaPlaybackIsPreparedToPlayDidChangeNotification;

IJK_EXTERN NSString *const IJKMoviePlayerLoadStateDidChangeNotification;
IJK_EXTERN NSString *const IJKMoviePlayerPlaybackDidFinishNotification;
IJK_EXTERN NSString *const IJKMoviePlayerPlaybackStateDidChangeNotification;
IJK_EXTERN NSString *const IJKMoviePlayerPlaybackRestoreVideoPlay;
IJK_EXTERN NSString *const IJKMoviePlayerAudioBeginInterruptionNotification;
IJK_EXTERN NSString *const IJKMoviePlayerPlaybackVideoViewCreatedNotification;
IJK_EXTERN NSString *const IJKMoviePlayerPlaybackBufferingUpdateNotification;
IJK_EXTERN NSString *const IJKMoviePlayerPlaybackVideoSizeChangedNotification;
IJK_EXTERN NSString *const IJKMoviePlayerPlaybackSeekCompletedNotification;
IJK_EXTERN NSString *const IJKMPMoviePlayerDidSeekCompleteNotification;
IJK_EXTERN NSString *const IJKMPMoviePlayerAccurateSeekCompleteNotification;
IJK_EXTERN NSString *const IJKMPMoviePlayerSeekAudioStartNotification;
IJK_EXTERN NSString *const IJKMPMoviePlayerSeekVideoStartNotification;
IJK_EXTERN NSString *const IJKMPMoviePlayerVideoDecoderOpenNotification;
IJK_EXTERN NSString *const IJKMoviePlayerRotateChangedNotification;
IJK_EXTERN NSString *const IJKMoviePlayerSubtitleChangedNotification;
IJK_EXTERN NSString *const IJKMoviePlayerVideoSaveNotification;

@end

#pragma mark IJKMediaResource

@protocol IJKMediaSegmentResolver <NSObject>

- (NSString *)urlOfSegment:(int)segmentPosition;

@end
