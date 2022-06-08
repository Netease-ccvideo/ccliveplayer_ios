/*
 * IJKAVMoviePlayerController.m
 *
 * Copyright (c) 2014 Zhang Rui <bbcallen@gmail.com>
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

/*
 File: AVPlayerDemoPlaybackViewController.m
 Abstract: UIViewController managing a playback view, thumbnail view, and associated playback UI.
 Version: 1.3
 
 Disclaimer: IMPORTANT:  This Apple software is supplied to you by Apple
 Inc. ("Apple") in consideration of your agreement to the following
 terms, and your use, installation, modification or redistribution of
 this Apple software constitutes acceptance of these terms.  If you do
 not agree with these terms, please do not use, install, modify or
 redistribute this Apple software.
 
 In consideration of your agreement to abide by the following terms, and
 subject to these terms, Apple grants you a personal, non-exclusive
 license, under Apple's copyrights in this original Apple software (the
 "Apple Software"), to use, reproduce, modify and redistribute the Apple
 Software, with or without modifications, in source and/or binary forms;
 provided that if you redistribute the Apple Software in its entirety and
 without modifications, you must retain this notice and the following
 text and disclaimers in all such redistributions of the Apple Software.
 Neither the name, trademarks, service marks or logos of Apple Inc. may
 be used to endorse or promote products derived from the Apple Software
 without specific prior written permission from Apple.  Except as
 expressly stated in this notice, no other rights or licenses, express or
 implied, are granted by Apple herein, including but not limited to any
 patent rights that may be infringed by your derivative works or by other
 works in which the Apple Software may be incorporated.
 
 The Apple Software is provided by Apple on an "AS IS" basis.  APPLE
 MAKES NO WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION
 THE IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS
 FOR A PARTICULAR PURPOSE, REGARDING THE APPLE SOFTWARE OR ITS USE AND
 OPERATION ALONE OR IN COMBINATION WITH YOUR PRODUCTS.
 
 IN NO EVENT SHALL APPLE BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL
 OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) ARISING IN ANY WAY OUT OF THE USE, REPRODUCTION,
 MODIFICATION AND/OR DISTRIBUTION OF THE APPLE SOFTWARE, HOWEVER CAUSED
 AND WHETHER UNDER THEORY OF CONTRACT, TORT (INCLUDING NEGLIGENCE),
 STRICT LIABILITY OR OTHERWISE, EVEN IF APPLE HAS BEEN ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
 
 Copyright (C) 2014 Apple Inc. All Rights Reserved.
 
 */

#import "IJKAVMoviePlayerController.h"
#import "IJKAVPlayerLayerView.h"
#import "IJKAudioKit.h"
#import "IJKMediaModule.h"
#import "IJKMediaUtils.h"
#import "IJKKVOController.h"
#include "ijksdl/ios/ijksdl_ios.h"
#import <AVFoundation/AVFoundation.h>
#import "CCNetConfig.h"
#import <AVKit/AVKit.h>
#import "PlayerConfig.h"

// avoid float equal compare
inline static bool isFloatZero(float value)
{
    return fabsf(value) <= 0.00001f;
}

// resume play after stall
static const float kMaxHighWaterMarkMilli   = 15 * 1000;

static NSString *kErrorDomain = @"IJKAVMoviePlayer";
static const NSInteger kEC_CurrentPlayerItemIsNil   = 5001;
static const NSInteger kEC_PlayerItemCancelled      = 5002;

static void *KVO_AVPlayer_rate          = &KVO_AVPlayer_rate;
static void *KVO_AVPlayer_currentItem   = &KVO_AVPlayer_currentItem;
static void *KVO_AVPlayer_airplay   = &KVO_AVPlayer_airplay;

static void *KVO_AVPlayerItem_state                     = &KVO_AVPlayerItem_state;
static void *KVO_AVPlayerItem_loadedTimeRanges          = &KVO_AVPlayerItem_loadedTimeRanges;
static void *KVO_AVPlayerItem_playbackLikelyToKeepUp    = &KVO_AVPlayerItem_playbackLikelyToKeepUp;
static void *KVO_AVPlayerItem_playbackBufferFull        = &KVO_AVPlayerItem_playbackBufferFull;
static void *KVO_AVPlayerItem_playbackBufferEmpty       = &KVO_AVPlayerItem_playbackBufferEmpty;

static const NSInteger kVideoPlayerReadyToPlayTimeOut = 12;

@interface IJKAVMoviePlayerController() <IJKAudioSessionDelegate, AVPlayerItemOutputPullDelegate>

// Redeclare property
@property(nonatomic, readwrite) UIView *view;

@property(nonatomic, readwrite) NSTimeInterval duration;
@property(nonatomic, readwrite) NSTimeInterval playableDuration;
@property(nonatomic, readwrite) NSInteger bufferingProgress;

@property(nonatomic, readwrite)  BOOL isPreparedToPlay;

@property(nonatomic, assign) MPMoviePlaybackState prePlaybackState;

@property(nonatomic, assign) BOOL firstBufferingReady;

@property(nonatomic, assign) NSUInteger playResumeTime;

@property(nonatomic, assign) BOOL audioMute;

@property(nonatomic, assign) BOOL enableAirPlay;

@property(nonatomic, assign) NSUInteger recoverTime;

@property(nonatomic, assign) NSUInteger failRetryTimes;

@end

@implementation IJKAVMoviePlayerController  {
    NSURL           *_playUrl;
    AVURLAsset      *_playAsset;
    
    AVPlayerItemVideoOutput* _playerVideoOutput;
    dispatch_queue_t _playerVideoOutputQueue;
    AVPlayerItem    *_playerItem;
    AVPlayer        *_player;
    //    IJKAVPlayerLayerView * _avView;
    //
    IJKKVOController *_playerKVO;
    IJKKVOController *_playerItemKVO;
    
    id _timeObserver;
    dispatch_once_t _readyToPlayToken;
    // while AVPlayer is prerolling, it could resume itself.
    // foring start could
    BOOL _isPrerolling;
    
    NSTimeInterval _seekingTime;
    BOOL _isSeeking;
    BOOL _isError;
    BOOL _isCompleted;
    BOOL _isShutdown;
    
    BOOL _pauseInBackground;
    
    BOOL _playbackLikelyToKeeyUp;
    BOOL _playbackBufferEmpty;
    BOOL _playbackBufferFull;
    
    float _playbackRate;
    
    float _playbackVolume;
    
    NSMutableArray *_registeredNotifications;
    
    BOOL _playingBeforeInterruption;
    
    PlayerConfig _playerConfig;
    BOOL _liveVideo;
    BOOL _playerConfigInited;
    NSInteger _loadingTimeOut;
    
    CADisplayLink *_displayLink;
    FrameProcessor *_fp;
}

@synthesize view                        = _view;
@synthesize currentPlaybackTime         = _currentPlaybackTime;
@synthesize duration                    = _duration;
@synthesize playableDuration            = _playableDuration;
@synthesize bufferingProgress           = _bufferingProgress;

@synthesize numberOfBytesTransferred    = _numberOfBytesTransferred;

@synthesize playbackState               = _playbackState;
@synthesize loadState                   = _loadState;

@synthesize scalingMode                 = _scalingMode;
@synthesize shouldAutoplay              = _shouldAutoplay;

@synthesize controlStyle = _controlStyle;
@synthesize isPreparedToPlay = _isPreparedToPlay;
@synthesize enableAutoIdleTimer = _enableAutoIdleTimer;
@synthesize videoSize = _videoSize;

static IJKAVMoviePlayerController* instance;

- (id)initWithContentURL:(NSURL *)aUrl withLoadingTimeout:(NSUInteger)timeout
{
    self = [super init];
    if (self != nil) {
        self.scalingMode = MPMovieScalingModeAspectFit;
        self.shouldAutoplay = NO;
        
        _playUrl = aUrl;
        
        //        _avView = [[IJKAVPlayerLayerView alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
        //        self.view = _avView;
        
        // TODO:
        [[IJKAudioKit sharedInstance] setupAudioSessionWithMediaPlay:withMediaPlay];
        
        _isPrerolling           = NO;
        
        _isSeeking              = NO;
        _isError                = NO;
        _isCompleted            = NO;
        self.bufferingProgress  = 0;
        
        _playbackLikelyToKeeyUp = NO;
        _playbackBufferEmpty    = YES;
        _playbackBufferFull     = NO;
        
        _playbackRate           = 1.0f;
        
        _audioMute              = NO;
        _playbackVolume         = 1.0f;
        
        _registeredNotifications = [[NSMutableArray alloc] init];
        
        // init extra
        [self setScreenOn:YES];
        
        [self registerApplicationObservers];
        
        _pauseInBackground = NO;
        
        _controlStyle = MPMovieControlStyleNone;
        _scalingMode = MPMovieScalingModeAspectFit;
        
        _shouldAutoplay = YES;
        
        _recoverTime = 0;
        
        _loadingTimeOut = timeout > 0 ? timeout : kVideoPlayerReadyToPlayTimeOut;
        
        _failRetryTimes = 1;
        
        _playerVideoOutput = nil;
        
        _loop = 1;
        
        ALOGI("AVPlayer init with url = %s %f\n",[[aUrl absoluteString] UTF8String], [[NSDate date] timeIntervalSince1970]);
    }
    return self;
}

+ (id)getInstance:(NSString *)aUrl
{
    if (instance == nil) {
        instance = [[IJKAVMoviePlayerController alloc] initWithContentURLString:aUrl withLoadingTimeout:kVideoPlayerReadyToPlayTimeOut];
    } else {
        instance = [instance initWithContentURLString:aUrl withLoadingTimeout:kVideoPlayerReadyToPlayTimeOut];
    }
    return instance;
}

- (id)initWithContentURLString:(NSString *)aUrl withLoadingTimeout:(NSUInteger)timeout
{
    NSURL *url;
    if (aUrl == nil) {
        aUrl = @"";
    }
    aUrl = [aUrl stringByReplacingPercentEscapesUsingEncoding:NSUTF8StringEncoding];
    
    if ([aUrl rangeOfString:@"/"].location == 0) {
        //本地
        url = [NSURL fileURLWithPath:aUrl];
    }
    else {
        url = [NSURL URLWithString:aUrl];
    }
    
    self = [self initWithContentURL:url withLoadingTimeout:timeout];
    if (self != nil) {
        
    }
    return self;
}

- (void)setScreenOn: (BOOL)on
{
    [IJKMediaModule sharedModule].mediaModuleIdleTimerDisabled = on;
}

- (void)dealloc
{
    ALOGI("avplayer dealloc \n");
}

- (void)prepareToPlay
{
    AVURLAsset *asset = NULL;
    NSArray *requestedKeys = @[@"playable"];
    asset = [AVURLAsset URLAssetWithURL:_playUrl options:nil];
    _playAsset = asset;
    [self performSelector:@selector(playerReadyToPlayTimeOut) withObject:nil afterDelay:3.0f];
    NSDictionary *attr = @{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)};
    _playerVideoOutput = [[AVPlayerItemVideoOutput alloc] initWithPixelBufferAttributes:attr];
    _playerVideoOutputQueue = dispatch_queue_create("mlive.ccplayer.playerVideoOutputQueue", DISPATCH_QUEUE_SERIAL);
    [_playerVideoOutput setDelegate:self queue:_playerVideoOutputQueue];
    [self startProcessPlayItem];
    
    [asset loadValuesAsynchronouslyForKeys:requestedKeys
                         completionHandler:^{
                             dispatch_async( dispatch_get_main_queue(), ^{
                                 [self didPrepareToPlayAsset:asset withKeys:requestedKeys];
                             });
                         }];
}

- (void)configPlayerUrl
{
    NSLog(@"configPlayerUrl");
    
    if(_isShutdown)
        return;
    
    if (_playerItem != nil) {
        [_playerItem cancelPendingSeeks];
    }
    
    [_playerItemKVO safelyRemoveAllObservers];
    [[NSNotificationCenter defaultCenter] removeObserver:self
                                                    name:nil
                                                  object:_playerItem];
    
    [_playerKVO safelyRemoveAllObservers];
    
    [self unregisterApplicationObservers];
    
//    _readyToPlayToken = 0;
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL:_playUrl options:nil];
    NSArray *requestedKeys = @[@"playable"];
    _playAsset = asset;
    [self performSelector:@selector(playerReadyToPlayTimeOut) withObject:nil afterDelay:_loadingTimeOut];
    [asset loadValuesAsynchronouslyForKeys:requestedKeys
                         completionHandler:^{
                             dispatch_async( dispatch_get_main_queue(), ^{
                                 [self didPrepareToPlayAsset:asset withKeys:requestedKeys];
                             });
                         }];
    
}

- (void)play
{
    ALOGI(" --play-- \n");
    if (_isCompleted)
    {
        _isCompleted = NO;
        [_player seekToTime:kCMTimeZero];
    }
    
    [_player play];
}

- (void)pause
{
    _isPrerolling = NO;
    [_player pause];
}

- (void)stop
{
    [_player pause];
    [self setScreenOn:NO];
    _isCompleted = YES;
}

- (BOOL)isPlaying
{
    if (!isFloatZero(_player.rate)) {
        return YES;
    } else {
        if (_isPrerolling) {
            return YES;
        } else {
            return NO;
        }
    }
}

- (void)shutdown
{
    ALOGI(" avplayer --shutdown-- \n");
    [self cancleAllTimeOut];
    
    _isShutdown = YES;
    
    [self stop];
    
    if(_displayLink != nil) {
        [_displayLink setPaused:YES];
        [_displayLink invalidate]; // remove from all run loops
        _displayLink = nil;
    }
    
    if (_playerItem != nil) {
        [_playerItem cancelPendingSeeks];
    }
    [_playerItem removeOutput:_playerVideoOutput];
    _playerVideoOutput = nil;
    
    [_playerItemKVO safelyRemoveAllObservers];
    [[NSNotificationCenter defaultCenter] removeObserver:self
                                                    name:nil
                                                  object:_playerItem];
    
    [_playerKVO safelyRemoveAllObservers];
    
    [self unregisterApplicationObservers];
    
    //    if (_avView != nil) {
    //        [_avView setPlayer:nil];
    //    }
    
    self.view = nil;
    ALOGI(" avplayer --shutdown done-- \n");
    
}

- (void)setPlayControlParameters:(BOOL)canFwd fwdNew:(BOOL)forwardNew bufferTimeMsec:(int)bufferTime
                  fwdExtTimeMsec:(int)fwdExtTime firstjitter:(int)firstjitter minJitterMsec:(int)minJitter maxJitterMsec:(int)maxJitter {
    
    _liveVideo = canFwd;
}

- (UIImage *)thumbnailImageAtCurrentTime
{
    AVAssetImageGenerator *imageGenerator = [AVAssetImageGenerator assetImageGeneratorWithAsset:_playAsset];
    NSError *error = nil;
    CMTime time = CMTimeMakeWithSeconds(self.currentPlaybackTime, 1);
    CMTime actualTime;
    CGImageRef cgImage = [imageGenerator copyCGImageAtTime:time actualTime:&actualTime error:&error];
    UIImage *image = [UIImage imageWithCGImage:cgImage];
    return image;
}

- (void)setCurrentPlaybackTime:(NSTimeInterval)aCurrentPlaybackTime
{
    if (!_player)
        return;
    
    _seekingTime = aCurrentPlaybackTime;
    _isSeeking = YES;
    _bufferingProgress = 0;
    [self didPlaybackStateChange];
    [self didLoadStateChange];
    if (_isPrerolling) {
        [_player pause];
    }
    
    [_player seekToTime:CMTimeMakeWithSeconds(aCurrentPlaybackTime, NSEC_PER_SEC)
      completionHandler:^(BOOL finished) {
          dispatch_async(dispatch_get_main_queue(), ^{
              _isSeeking = NO;
              if (_isPrerolling) {
                  [_player play];
              }
              [self didPlaybackStateChange];
              [self didLoadStateChange];
          });
      }];
}

- (NSTimeInterval)currentPlaybackTime
{
    if (!_player)
        return 0.0f;
    
    if (_isSeeking)
        return _seekingTime;
    
    return CMTimeGetSeconds([_player currentTime]);
}

-(int64_t)numberOfBytesTransferred
{
#if 0
    if (_player == nil)
        return 0;
    
    AVPlayerItem *playerItem = [_player currentItem];
    if (playerItem == nil)
        return 0;
    
    NSArray *events = playerItem.accessLog.events;
    if (events != nil && events.count > 0) {
        MPMovieAccessLogEvent *currentEvent = [events objectAtIndex:events.count -1];
        return currentEvent.numberOfBytesTransferred;
    }
#endif
    return 0;
}

- (MPMoviePlaybackState)playbackState
{
    if (!_player)
        return MPMoviePlaybackStateStopped;
    
    MPMoviePlaybackState mpState = MPMoviePlaybackStateStopped;
    if (_isCompleted) {
        mpState = MPMoviePlaybackStateStopped;
    } else if (_isSeeking) {
        mpState = MPMoviePlaybackStateSeekingForward;
    } else if ([self isPlaying]) {
        mpState = MPMoviePlaybackStatePlaying;
    } else {
        mpState = MPMoviePlaybackStatePaused;
    }
    ALOGI("playback state = %ld \n",(long)mpState);
    return mpState;
}

- (MPMovieLoadState)loadState
{
    if (_player == nil)
        return MPMovieLoadStateUnknown;
    
    if (_isSeeking)
        return MPMovieLoadStateStalled;
    
    AVPlayerItem *playerItem = [_player currentItem];
    if (playerItem == nil)
        return MPMovieLoadStateUnknown;
    
    if (_player != nil && !isFloatZero(_player.rate)) {
        ALOGI("loadState: playing \n");
        return MPMovieLoadStatePlayable | MPMovieLoadStatePlaythroughOK;
    } else if ([playerItem isPlaybackBufferFull]) {
        ALOGI("loadState: isPlaybackBufferFull \n");
        return MPMovieLoadStatePlayable | MPMovieLoadStatePlaythroughOK;
    } else if ([playerItem isPlaybackLikelyToKeepUp]) {
        ALOGI("loadState: isPlaybackLikelyToKeepUp \n");
        return MPMovieLoadStatePlayable | MPMovieLoadStatePlaythroughOK;
    } else if ([playerItem isPlaybackBufferEmpty]) {
        ALOGI("loadState: isPlaybackBufferEmpty \n");
        return MPMovieLoadStateStalled;
    } else {
        ALOGI("loadState: unknown \n");
        return MPMovieLoadStateUnknown;
    }
}


-(void)setPlaybackRate:(float)playbackRate
{
    _playbackRate = playbackRate;
    if (_player != nil && !isFloatZero(_player.rate)) {
        _player.rate = _playbackRate;
    }
}

-(float)playbackRate
{
    return _playbackRate;
}

-(void)setPlaybackVolume:(float)playbackVolume
{
    _playbackVolume = playbackVolume;
    if (_player != nil && _player.volume != playbackVolume) {
        _player.volume = playbackVolume;
    }
}

-(float)playbackVolume
{
    return _playbackVolume;
}

- (void)didPrepareToPlayAsset:(AVURLAsset *)asset withKeys:(NSArray *)requestedKeys
{
    if (_isShutdown)
        return;
    
    /* Make sure that the value of each key has loaded successfully. */
    for (NSString *thisKey in requestedKeys)
    {
        NSError *error = nil;
        AVKeyValueStatus keyStatus = [asset statusOfValueForKey:thisKey error:&error];
        if (keyStatus == AVKeyValueStatusFailed)
        {
            [self assetFailedToPrepareForPlayback:error];
            return;
        } else if (keyStatus == AVKeyValueStatusCancelled) {
            // TODO [AVAsset cancelLoading]
            error = [self createErrorWithCode:kEC_PlayerItemCancelled
                                  description:@"player item cancelled"
                                       reason:nil];
            [self assetFailedToPrepareForPlayback:error];
            return;
        }
        
    }
    
    /* Use the AVAsset playable property to detect whether the asset can be played. */
    if (!asset.playable)
    {
        NSError *assetCannotBePlayedError = [NSError errorWithDomain:@"AVMoviePlayer"
                                                                code:0
                                                            userInfo:nil];
        
        [self assetFailedToPrepareForPlayback:assetCannotBePlayedError];
        return;
    }
    
    /* At this point we're ready to set up for playback of the asset. */
    
    /* Stop observing our prior AVPlayerItem, if we have one. */
    [_playerItemKVO safelyRemoveAllObservers];
    [[NSNotificationCenter defaultCenter] removeObserver:self
                                                    name:nil
                                                  object:_playerItem];
    
    /* Create a new instance of AVPlayerItem from the now successfully loaded AVAsset. */
    _playerItem = [AVPlayerItem playerItemWithAsset:asset];
    [_playerItem addOutput:_playerVideoOutput];
    CGFloat fps = 20.0f;
    NSArray<AVAssetTrack *> *videoTracks = [_playAsset tracksWithMediaType:AVMediaTypeVideo];
    if (videoTracks == nil || videoTracks.count <= 0) {
    } else {
        fps = [[videoTracks objectAtIndex:0] nominalFrameRate];
    }
    [_playerVideoOutput requestNotificationOfMediaDataChangeWithAdvanceInterval:1 / fps];//fixme one frame duration
    
    _playerItemKVO = [[IJKKVOController alloc] initWithTarget:_playerItem];
    [self registerApplicationObservers];
    /* Observe the player item "status" key to determine when it is ready to play. */
    [_playerItemKVO safelyAddObserver:self
                           forKeyPath:@"status"
                              options:NSKeyValueObservingOptionInitial | NSKeyValueObservingOptionNew
                              context:KVO_AVPlayerItem_state];
    
    [_playerItemKVO safelyAddObserver:self
                           forKeyPath:@"loadedTimeRanges"
                              options:NSKeyValueObservingOptionInitial | NSKeyValueObservingOptionNew
                              context:KVO_AVPlayerItem_loadedTimeRanges];
    
    [_playerItemKVO safelyAddObserver:self
                           forKeyPath:@"playbackLikelyToKeepUp"
                              options:NSKeyValueObservingOptionInitial | NSKeyValueObservingOptionNew
                              context:KVO_AVPlayerItem_playbackLikelyToKeepUp];
    
    [_playerItemKVO safelyAddObserver:self
                           forKeyPath:@"playbackBufferEmpty"
                              options:NSKeyValueObservingOptionInitial | NSKeyValueObservingOptionNew
                              context:KVO_AVPlayerItem_playbackBufferEmpty];
    
    [_playerItemKVO safelyAddObserver:self
                           forKeyPath:@"playbackBufferFull"
                              options:NSKeyValueObservingOptionInitial | NSKeyValueObservingOptionNew
                              context:KVO_AVPlayerItem_playbackBufferFull];
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(playerItemDidReachEnd:)
                                                 name:AVPlayerItemDidPlayToEndTimeNotification
                                               object:_playerItem];
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(playerItemFailedToPlayToEndTime:)
                                                 name:AVPlayerItemFailedToPlayToEndTimeNotification
                                               object:_playerItem];
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(playerItemStalled:)
                                                 name:AVPlayerItemPlaybackStalledNotification
                                               object:_playerItem];
    
    _isCompleted = NO;
    
    /* Create new player, if we don't already have one. */
    if (!_player)
    {
        /* Get a new AVPlayer initialized to play the specified player item. */
        _player = [AVPlayer playerWithPlayerItem:_playerItem];
        [_player setAllowsExternalPlayback:_enableAirPlay];
        [_player setUsesExternalPlaybackWhileExternalScreenIsActive: _enableAirPlay];
        
        //[self setPlaybackVolume:(_audioMute ? 0.0f : 1.0f)];
        
        // _player.automaticallyWaitsToMinimizeStalling = YES;
        
        _playerKVO = [[IJKKVOController alloc] initWithTarget:_player];
        
        /* Observe the AVPlayer "currentItem" property to find out when any
         AVPlayer replaceCurrentItemWithPlayerItem: replacement will/did
         occur.*/
        [_playerKVO safelyAddObserver:self
                           forKeyPath:@"currentItem"
                              options:NSKeyValueObservingOptionInitial | NSKeyValueObservingOptionNew
                              context:KVO_AVPlayer_currentItem];
        
        /* Observe the AVPlayer "rate" property to update the scrubber control. */
        [_playerKVO safelyAddObserver:self
                           forKeyPath:@"rate"
                              options:NSKeyValueObservingOptionInitial | NSKeyValueObservingOptionNew
                              context:KVO_AVPlayer_rate];
        
        //        [_playerKVO safelyAddObserver:self
        //                           forKeyPath:@"airPlayVideoActive"
        //                              options:NSKeyValueObservingOptionInitial | NSKeyValueObservingOptionNew
        //                              context:KVO_AVPlayer_airplay];
    }
    
    /* Make our new AVPlayerItem the AVPlayer's current item. */
    if (_player.currentItem != _playerItem)
    {
        /* Replace the player item with a new player item. The item replacement occurs
         asynchronously; observe the currentItem property to find out when the
         replacement will/did occur
         
         If needed, configure player item here (example: adding outputs, setting text style rules,
         selecting media options) before associating it with a player
         */
        [_player replaceCurrentItemWithPlayerItem:_playerItem];
        
        // TODO: notify state change
    }
    
    // TODO: set time to 0;
}

- (void)didPlaybackStateChange
{
    if (_playbackState != self.playbackState) {
        _playbackState = self.playbackState;
        
        ALOGI("playbackState = %ld \n",(long)_playbackState);
        [[NSNotificationCenter defaultCenter]
         postNotificationName:IJKMoviePlayerPlaybackStateDidChangeNotification
         object:self];
        
        if(_prePlaybackState != _playbackState && _playbackState == MPMoviePlaybackStatePlaying) {
            [[NSNotificationCenter defaultCenter]
             postNotificationName:IJKMoviePlayerPlaybackRestoreVideoPlay
             object:self];
            [self recoverForPlay:_recoverTime++];
            
        } else if(_prePlaybackState != _playbackState && _playbackState == MPMoviePlaybackStatePaused) {
            _recoverTime = 0;
        }
        _prePlaybackState = _playbackState;
    }
    
}

- (void) recoverForPlay:(NSUInteger)count {
    if(count >= 2) {
        return;
    }
    NSTimeInterval interval = [self currentPlaybackTime];
    [self setCurrentPlaybackTime:interval];
    
}

- (void)fetchLoadStateFromItem:(AVPlayerItem*)playerItem
{
    if (playerItem == nil)
        return;
    
    _playbackLikelyToKeeyUp = playerItem.isPlaybackLikelyToKeepUp;
    _playbackBufferEmpty    = playerItem.isPlaybackBufferEmpty;
    _playbackBufferFull     = playerItem.isPlaybackBufferFull;
}

- (void)didLoadStateChange
{
    // NOTE: do not force play after stall,
    // which may cause AVPlayer get into wrong state
    //
    // Rely on AVPlayer's auto resume.
    
    [[NSNotificationCenter defaultCenter]
     postNotificationName:IJKMoviePlayerLoadStateDidChangeNotification
     object:self];
}

- (void)didPlayableDurationUpdate
{
    NSTimeInterval currentPlaybackTime = self.currentPlaybackTime;
    int playableDurationMilli    = (int)(self.playableDuration * 1000);
    int currentPlaybackTimeMilli = (int)(currentPlaybackTime * 1000);
    
    int bufferedDurationMilli = playableDurationMilli - currentPlaybackTimeMilli;
    if (bufferedDurationMilli > 0) {
        self.bufferingProgress = bufferedDurationMilli * 100 / kMaxHighWaterMarkMilli;
        
        if (self.bufferingProgress > 100) {
            if(![_playerItem isPlaybackLikelyToKeepUp] && _firstBufferingReady && ![self isPlaying]) {
                ALOGI("AVPlayer bufferingProgress = %zd \n",self.bufferingProgress);
                [[NSNotificationCenter defaultCenter] postNotificationName:IJKMoviePlayerPlaybackBufferingUpdateNotification object:self userInfo:[NSDictionary dictionaryWithObjectsAndKeys:@(100), @"percent", nil]];
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                if (self.bufferingProgress > 100) {
                    if ([self isPlaying]) {
                        _player.rate = _playbackRate;
                        _firstBufferingReady = YES;
                    }
                }
            });
        } else {
            if(![_playerItem isPlaybackLikelyToKeepUp] && _firstBufferingReady && ![self isPlaying]) {
                ALOGI("AVPlayer bufferingProgress = %zd \n",self.bufferingProgress);
                [[NSNotificationCenter defaultCenter] postNotificationName:IJKMoviePlayerPlaybackBufferingUpdateNotification object:self userInfo:[NSDictionary dictionaryWithObjectsAndKeys:@(self.bufferingProgress), @"percent", nil]];
            }
        }
        
    }
    //    NSLog(@"KVO_AVPlayerItem_loadedTimeRanges: %d / %d\n",
    //          bufferedDurationMilli,
    //          (int)kMaxHighWaterMarkMilli);
}

- (void)onError:(NSError *)error
{
    [self cancleAllTimeOut];
    
    _isError = YES;
    
    __block NSError *blockError = error;
    
    ALOGI("AVPlayer: onError %zd\n",blockError.code);
    dispatch_async(dispatch_get_main_queue(), ^{
        [self didPlaybackStateChange];
        [self didLoadStateChange];
        [self setScreenOn:NO];
        
        if (blockError == nil) {
            blockError = [[NSError alloc] initWithDomain:@"Stalled Time out" code:0 userInfo:nil];
        }
        
        [[NSNotificationCenter defaultCenter]
         postNotificationName:IJKMoviePlayerPlaybackDidFinishNotification
         object:self
         userInfo:@{
                    MPMoviePlayerPlaybackDidFinishReasonUserInfoKey: @(MPMovieFinishReasonPlaybackError),
                    @"error": blockError
                    }];
        
    });
}

- (void)assetFailedToPrepareForPlayback:(NSError *)error
{
    if (_isShutdown)
        return;
    
    if(_failRetryTimes) {
        NSLog(@"assetFailedToPrepareForPlayback retry configPlayerUrl");
        [self cancleAllTimeOut];
        [self configPlayerUrl];
        _failRetryTimes--;
    }
    else if(_failRetryTimes == 0) {
        [self onError:error];
    }
}

- (void)playerItemStalled:(NSNotification *)notification {
    
    if(CMTIME_COMPARE_INLINE(_player.currentTime, >, kCMTimeZero) &&
       CMTIME_COMPARE_INLINE(_player.currentTime, !=, _player.currentItem.duration)) {
        [self playerHanging];
    }
}

- (void) resume {
    
    [_player pause];
    [_player play];
}

- (NSTimeInterval) availableDuration
{
    NSArray *timeRangeArray = [[_player currentItem] loadedTimeRanges];
    BOOL foundRange = NO;
    CMTimeRange aTimeRange = {0};
    
    if (timeRangeArray.count) {
        aTimeRange = [[timeRangeArray objectAtIndex:0] CMTimeRangeValue];
        
        foundRange = YES;
        
    }
    
    if (foundRange) {
        CMTime maxTime = CMTimeRangeGetEnd(aTimeRange);
        NSTimeInterval playableDuration = CMTimeGetSeconds(maxTime);
        return playableDuration;
    }
    return 0;
}

- (void)playerItemFailedToPlayToEndTime:(NSNotification *)notification
{
    if (_isShutdown)
        return;
    NSError *error = notification.userInfo[AVPlayerItemFailedToPlayToEndTimeErrorKey];
    //   AVFoundationErrorDomain Code=-11853 "Playlist not received"
    if(error.code != AVErrorFailedToParse) {
        [self onError:error];
    }
    
}

- (void)playerItemDidReachEnd:(NSNotification *)notification
{
    if (_isShutdown)
        return;
    
    _isCompleted = YES;
    
    [self cancleAllTimeOut];
    
    if([self isLocalPlayItem] && _loop != 1) {
        [self play];
        _loop -- ;
        _loop = MAX(_loop, 0);
        return;
    }
    
    dispatch_async(dispatch_get_main_queue(), ^{
        [self didPlaybackStateChange];
        [self didLoadStateChange];
        [self setScreenOn:NO];
        
        [[NSNotificationCenter defaultCenter]
         postNotificationName:IJKMoviePlayerPlaybackDidFinishNotification
         object:self
         userInfo:@{
                    MPMoviePlayerPlaybackDidFinishReasonUserInfoKey: @(MPMovieFinishReasonPlaybackEnded)
                    }];
    });
}


#pragma mark KVO

- (void)observeValueForKeyPath:(NSString*)path
                      ofObject:(id)object
                        change:(NSDictionary*)change
                       context:(void*)context
{
    if (_isShutdown)
        return;
    
    if (context == KVO_AVPlayerItem_state)
    {
        /* AVPlayerItem "status" property value observer. */
        AVPlayerItemStatus status = [[change objectForKey:NSKeyValueChangeNewKey] integerValue];
        switch (status)
        {
            case AVPlayerItemStatusUnknown:
            {
                /* Indicates that the status of the player is not yet known because
                 it has not tried to load new media resources for playback */
            }
                break;
                
            case AVPlayerItemStatusReadyToPlay:
            {
                /* Once the AVPlayerItem becomes ready to play, i.e.
                 [playerItem status] == AVPlayerItemStatusReadyToPlay,
                 its duration can be fetched from the item. */
                dispatch_once(&_readyToPlayToken, ^{
                
                    //                    [_avView setPlayer:_player];
                    
                    self.isPreparedToPlay = YES;
                    AVPlayerItem *playerItem = (AVPlayerItem *)object;
                    NSTimeInterval duration = CMTimeGetSeconds(playerItem.duration);
                    if (duration <= 0)
                        self.duration = 0.0f;
                    else
                        self.duration = duration;
                    
                    [[NSNotificationCenter defaultCenter]
                     postNotificationName:IJKMediaPlaybackIsPreparedToPlayDidChangeNotification
                     object:self];
                    
                    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(playerReadyToPlayTimeOut) object:nil];
                    _failRetryTimes = 0;
                    if (_shouldAutoplay && (!_pauseInBackground || [UIApplication sharedApplication].applicationState == UIApplicationStateActive))
                        [_player play];
                    
                    ALOGI(" --ready to play-- %f \n",[[NSDate date] timeIntervalSince1970]);
                    
                });
            }
                break;
                
            case AVPlayerItemStatusFailed:
            {
                AVPlayerItem *playerItem = (AVPlayerItem *)object;
                [self assetFailedToPrepareForPlayback:playerItem.error];
            }
                break;
        }
        
        [self didPlaybackStateChange];
        [self didLoadStateChange];
    }
    else if (context == KVO_AVPlayerItem_loadedTimeRanges)
    {
        AVPlayerItem *playerItem = (AVPlayerItem *)object;
        if (_player != nil && playerItem.status == AVPlayerItemStatusReadyToPlay) {
            NSArray *timeRangeArray = playerItem.loadedTimeRanges;
            CMTime currentTime = [_player currentTime];
            
            BOOL foundRange = NO;
            CMTimeRange aTimeRange = {0};
            
            if (timeRangeArray.count) {
                aTimeRange = [[timeRangeArray objectAtIndex:0] CMTimeRangeValue];
                if(CMTimeRangeContainsTime(aTimeRange, currentTime)) {
                    foundRange = YES;
                }
            }
            
            if (foundRange) {
                CMTime maxTime = CMTimeRangeGetEnd(aTimeRange);
                NSTimeInterval playableDuration = CMTimeGetSeconds(maxTime);
                if (playableDuration > 0) {
                    self.playableDuration = playableDuration;
                    [self didPlayableDurationUpdate];
                }
            }
        }
        else
        {
            self.playableDuration = 0;
        }
    }
    else if (context == KVO_AVPlayerItem_playbackLikelyToKeepUp) {
        AVPlayerItem *playerItem = (AVPlayerItem *)object;
        NSLog(@"KVO_AVPlayerItem_playbackLikelyToKeepUp: %@\n", playerItem.isPlaybackLikelyToKeepUp ? @"YES" : @"NO");
        _isPrerolling = NO;
        [self fetchLoadStateFromItem:playerItem];
        [self didLoadStateChange];
    }
    else if (context == KVO_AVPlayerItem_playbackBufferEmpty) {
        AVPlayerItem *playerItem = (AVPlayerItem *)object;
        BOOL isPlaybackBufferEmpty = playerItem.isPlaybackBufferEmpty;
        NSLog(@"KVO_AVPlayerItem_playbackBufferEmpty: %@\n", isPlaybackBufferEmpty ? @"YES" : @"NO");
        if (isPlaybackBufferEmpty)
            _isPrerolling = YES;
        [self fetchLoadStateFromItem:playerItem];
        [self didLoadStateChange];
    }
    else if (context == KVO_AVPlayerItem_playbackBufferFull) {
        AVPlayerItem *playerItem = (AVPlayerItem *)object;
        NSLog(@"KVO_AVPlayerItem_playbackBufferFull: %@\n", playerItem.isPlaybackBufferFull ? @"YES" : @"NO");
        [self fetchLoadStateFromItem:playerItem];
        [self didLoadStateChange];
    }
    else if (context == KVO_AVPlayer_rate)
    {
        ALOGI("rate changed = %f \n",_player.rate);
        if (_player != nil && !isFloatZero(_player.rate))
            _isPrerolling = NO;
        /* AVPlayer "rate" property value observer. */
        [self didPlaybackStateChange];
        [self didLoadStateChange];
    }
    else if (context == KVO_AVPlayer_currentItem)
    {
        _isPrerolling = NO;
        /* AVPlayer "currentItem" property observer.
         Called when the AVPlayer replaceCurrentItemWithPlayerItem:
         replacement will/did occur. */
        AVPlayerItem *newPlayerItem = [change objectForKey:NSKeyValueChangeNewKey];
        
        /* Is the new player item null? */
        if (newPlayerItem == (id)[NSNull null])
        {
            NSError *error = [self createErrorWithCode:kEC_CurrentPlayerItemIsNil
                                           description:@"current player item is nil"
                                                reason:nil];
            [self assetFailedToPrepareForPlayback:error];
        }
        else /* Replacement of player currentItem has occurred */
        {
            //            [_avView setPlayer:_player];
            
            [self didPlaybackStateChange];
            [self didLoadStateChange];
        }
    }
    else
    {
        [super observeValueForKeyPath:path ofObject:object change:change context:context];
    }
}

#pragma mark -- add by jlubobo

- (void) startProcessPlayItem {
    NSLog(@"-- avplayer -- startProcessPlayItem --");
    _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(displayLinkCallback:)];
    [_displayLink addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSRunLoopCommonModes];
    [_displayLink setPaused:YES];
}

- (void)displayLinkCallback:(CADisplayLink *)sender
{
    //    NSLog(@"-- avplayer -- displayLinkCallback --");
    CFTimeInterval nextVSync = ([sender timestamp] + [sender duration]);
    
    CMTime outputItemTime = [_playerVideoOutput itemTimeForHostTime:nextVSync];
    
    [self processPixelBufferAtTime:outputItemTime];
    
}

- (void)outputMediaDataWillChange:(AVPlayerItemOutput *)sender
{
    NSLog(@"-- avplayer -- setPaused NO --");
    [_displayLink setPaused:NO];
}

- (void)processPixelBufferAtTime:(CMTime)outputItemTime {
    
    if ([_playerVideoOutput hasNewPixelBufferForItemTime:outputItemTime]) {
        CVPixelBufferRef pixelBuffer = [_playerVideoOutput copyPixelBufferForItemTime:outputItemTime itemTimeForDisplay:NULL];
        if( pixelBuffer ) {
            if(_fp)
                _fp.onRenderAVFrame(pixelBuffer);
            //ALOGI("video pts = %lld \n", outputItemTime.value / outputItemTime.timescale);
            CFRelease(pixelBuffer);
        }
    }
}

- (void)playerHanging {
    
    if ([_playerItem isPlaybackLikelyToKeepUp]) {
        ALOGI("is keepup playerhanging return \n");
        _playResumeTime = 0;
        return;
    }
    if (_playResumeTime <= 15) {
        _playResumeTime++;
        ALOGI("playerhanging resume \n");
        [self resume];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            [self playerHanging];
        });
    } else {
        ALOGI("retry time out \n");
        //        [self onError:nil];
    }
}


- (NSError*)createErrorWithCode: (NSInteger)code
                    description: (NSString*)description
                         reason: (NSString*)reason
{
    NSError *error = [IJKMediaUtils createErrorWithDomain:kErrorDomain
                                                     code:code
                                              description:description
                                                   reason:reason];
    return error;
}

#pragma mark app state changed

- (void)registerApplicationObservers
{
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationWillEnterForeground)
                                                 name:UIApplicationWillEnterForegroundNotification
                                               object:nil];
    [_registeredNotifications addObject:UIApplicationWillEnterForegroundNotification];
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationDidBecomeActive)
                                                 name:UIApplicationDidBecomeActiveNotification
                                               object:nil];
    [_registeredNotifications addObject:UIApplicationDidBecomeActiveNotification];
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationWillResignActive)
                                                 name:UIApplicationWillResignActiveNotification
                                               object:nil];
    [_registeredNotifications addObject:UIApplicationWillResignActiveNotification];
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationDidEnterBackground)
                                                 name:UIApplicationDidEnterBackgroundNotification
                                               object:nil];
    [_registeredNotifications addObject:UIApplicationDidEnterBackgroundNotification];
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationWillTerminate)
                                                 name:UIApplicationWillTerminateNotification
                                               object:nil];
    [_registeredNotifications addObject:UIApplicationWillTerminateNotification];
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                             selector:@selector(audioSessionInterrupt:)
                                 name:AVAudioSessionInterruptionNotification
                               object:nil];
    
    [_registeredNotifications addObject:AVAudioSessionInterruptionNotification];
    
}

- (void)unregisterApplicationObservers
{
    for (NSString *name in _registeredNotifications) {
        [[NSNotificationCenter defaultCenter] removeObserver:self
                                                        name:name
                                                      object:nil];
    }
    [_registeredNotifications removeAllObjects];
}

-(BOOL)allowsMediaAirPlay
{
    if (!_player)
        return NO;
    return _player.allowsExternalPlayback;
}

-(void)setAllowsMediaAirPlay:(BOOL)b
{
    _enableAirPlay = b;
    
    if (!_player)
        return;
    _player.allowsExternalPlayback = b;
}

-(BOOL)airPlayMediaActive
{
    if (!_player)
        return NO;
    return _player.externalPlaybackActive;
}

- (CGSize)naturalSize
{
    if (_playAsset == nil)
        return CGSizeZero;
    
    NSArray<AVAssetTrack *> *videoTracks = [_playAsset tracksWithMediaType:AVMediaTypeVideo];
    if (videoTracks == nil || videoTracks.count <= 0)
        return CGSizeZero;
    
    return [videoTracks objectAtIndex:0].naturalSize;
}

- (void)setScalingMode: (MPMovieScalingMode) aScalingMode
{
    MPMovieScalingMode newScalingMode = aScalingMode;
    switch (aScalingMode) {
        case MPMovieScalingModeNone:
            [_view setContentMode:UIViewContentModeCenter];
            break;
        case MPMovieScalingModeAspectFit:
            [_view setContentMode:UIViewContentModeScaleAspectFit];
            break;
        case MPMovieScalingModeAspectFill:
            [_view setContentMode:UIViewContentModeScaleAspectFill];
            break;
        case MPMovieScalingModeFill:
            [_view setContentMode:UIViewContentModeScaleToFill];
            break;
        default:
            newScalingMode = _scalingMode;
    }
    
    _scalingMode = newScalingMode;
}

- (void)setPauseInBackground:(BOOL)pause
{
    _pauseInBackground = pause;
}

- (void)audioSessionInterrupt:(NSNotification *)notification
{
    int reason = [[[notification userInfo] valueForKey:AVAudioSessionInterruptionTypeKey] intValue];
    switch (reason) {
        case AVAudioSessionInterruptionTypeBegan: {
            NSLog(@"IJKAVMoviePlayerController:audioSessionInterrupt: begin\n");
            switch (self.playbackState) {
                case MPMoviePlaybackStatePaused:
                case MPMoviePlaybackStateStopped:
                    _playingBeforeInterruption = NO;
                    break;
                default:
                    _playingBeforeInterruption = YES;
                    break;
            }
            [self pause];
            [[IJKAudioKit sharedInstance] setActive:NO error:nil];
            break;
        }
        case AVAudioSessionInterruptionTypeEnded: {
            NSLog(@"IJKAVMoviePlayerController:audioSessionInterrupt: end\n");
            [[IJKAudioKit sharedInstance] setActive:YES error:nil];
            if (_playingBeforeInterruption) {
                [self play];
            }
            break;
        }
    }
}

#pragma mark IJKAudioSessionDelegate

- (void)ijkAudioBeginInterruption
{
    [[IJKAudioKit sharedInstance] setActive:NO error:nil];
    [self pause];
}

- (void)ijkAudioEndInterruption
{
    [[IJKAudioKit sharedInstance] setActive:YES error:nil];
    [self play];
}

- (void)applicationWillEnterForeground
{
    ALOGI("IJKAVMoviePlayerController:applicationWillEnterForeground: %d\n", (int)[UIApplication sharedApplication].applicationState);
}

- (void)applicationDidBecomeActive
{
    ALOGI("IJKAVMoviePlayerController:applicationDidBecomeActive: %d\n", (int)[UIApplication sharedApplication].applicationState);
    //    [_avView setPlayer:_player];
    
}

- (void)applicationWillResignActive
{
    ALOGI("IJKAVMoviePlayerController:applicationWillResignActive: %d\n", (int)[UIApplication sharedApplication].applicationState);
}

- (void)applicationDidEnterBackground
{
    ALOGI("IJKAVMoviePlayerController:applicationDidEnterBackground: %d\n", (int)[UIApplication sharedApplication].applicationState);
    if (_pauseInBackground && ![self airPlayMediaActive]) {
        [self pause];
    } else {
        if (![self airPlayMediaActive]) {
            //            [_avView setPlayer:nil];
            if (isIOS9OrLater()) {
                if ([self isPlaying]) {
                    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(2 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
                        [self play];
                    });
                }
            }
        }
    }
}

- (void)applicationWillTerminate
{
    ALOGI("IJKAVMoviePlayerController:applicationWillTerminate: %d\n", (int)[UIApplication sharedApplication].applicationState);
}

#pragma mark -- IJKMediaPlayback

- (void) setPlayerConfig:(PlayerConfig*)config {
    
    _playerConfig = *config;
    _playerConfigInited = YES;
    
}

- (void)setVideoEnable:(BOOL)enabled {
    ALOGI("videoEnable = %d but avplayer do nothing \n",enabled);
}


- (void)setDisplayFrameCb:(OnDisplayFrameCb)handle withObj:(void *)obj {
    
}


- (void)setFrameProcessor:(FrameProcessor *)fp {
    _fp = fp;
}


- (void)setPlayControlParameters:(BOOL)canFwd fwdNew:(BOOL)forwardNew bufferTimeMsec:(int)bufferTime fwdExtTimeMsec:(int)fwdExtTime minJitterMsec:(int)minJitter maxJitterMsec:(int)maxJitter {
    
}


- (void) setCropMode:(BOOL)cropMode {
    
    ALOGI("cropMode = %d but avplayer do nothing \n",cropMode);
}

#pragma mark -- info

+ (void)setLogLevel:(IJKLogLevel)logLevel
{
    ijk_logLevel = logLevel;
}

- (void)muteAudio:(BOOL)mute {
    
    _audioMute = mute;
    
    [self setPlaybackVolume: (mute ? 0.0f : 1.0f)];
    
}

- (void)setVolume:(float)volume {
    [self setPlaybackVolume:volume];
}

+ (void)setupAudioSessionWithMediaPlay:(BOOL)mediaPlay {
    withMediaPlay = mediaPlay;
}


#pragma mark - time Out

- (void)cancleAllTimeOut
{
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(playerReadyToPlayTimeOut) object:nil];
    [NSObject cancelPreviousPerformRequestsWithTarget:self];
}

-(void)playerReadyToPlayTimeOut
{
    if(_failRetryTimes) {
        NSLog(@"playerReadyToPlayTimeOut 1st retry configPlayerUrl");
        [self cancleAllTimeOut];
        [self configPlayerUrl];
        _failRetryTimes--;
    } else {
        NSLog(@"playerReadyToPlayTimeOut over");
        NSError *error = [[NSError alloc] initWithDomain:@"URLAsset Time out" code:0 userInfo:nil];
        [self onError:error];
    }
}

- (BOOL) isLocalPlayItem {
    if(!_playAsset) return NO;
    NSString *url = [[_playAsset URL] absoluteString];
    if([url hasPrefix:@"file:///"]) {
        return YES;
    }
    return NO;
}


@end
