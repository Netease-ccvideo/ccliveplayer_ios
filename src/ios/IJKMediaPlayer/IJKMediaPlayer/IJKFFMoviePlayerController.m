/*
 * IJKFFMoviePlayerController.m
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

#import "IJKFFMoviePlayerController.h"
#import "IJKFFMoviePlayerDef.h"
#import "IJKMediaPlayback.h"
#import "IJKMediaModule.h"
#import "IJKFFMrl.h"
#import "IJKAudioKit.h"
//#import "ijkavfmsg.h"
#include "string.h"
#import "IJKMuteAudioManager.h"
#import "CCNetConfig.h"

void _runPlayerOnMainThread(void (^block)(), BOOL async) {
    if ([[NSThread currentThread] isMainThread]) {
        block();
    }
    else if (async) {
        dispatch_async(dispatch_get_main_queue(), ^{
            block();
        });
    }
    else {
        dispatch_sync(dispatch_get_main_queue(), ^{
            block();
        });
    }
}

@interface IJKFFMoviePlayerController() <IJKAudioSessionDelegate>

@property(nonatomic, readonly) NSDictionary *mediaMeta;
@property(nonatomic, readonly) NSDictionary *videoMeta;
@property(nonatomic, readonly) NSDictionary *audioMeta;

@end

@implementation IJKFFMoviePlayerController {
    IJKFFMrl *_ffMrl;
    id<IJKMediaSegmentResolver> _segmentResolver;

    IjkMediaPlayer *_mediaPlayer;
    IJKSDLGLView *_glView;
    IJKFFMoviePlayerMessagePool *_msgPool;
    EAGLSharegroup* _sharegroup;

    NSInteger _videoWidth;
    NSInteger _videoHeight;
    NSInteger _sampleAspectRatioNumerator;
    NSInteger _sampleAspectRatioDenominator;

    BOOL      _seeking;
    NSInteger _bufferingTime;
    NSInteger _bufferingPercent;
    
    BOOL _keepScreenOnWhilePlaying;
    BOOL _pauseInBackground;

    NSMutableArray *_registeredNotifications;
    
    BOOL liveVideo;
    BOOL _isVideoToolboxOpen;
    
    BOOL _playingBeforeInterruption;
    
    PlayerConfig _playerConfig;
    float   _player_volume;
    BOOL _isMuted;
    
    NSString *videoLinkUrl;
    NSString *roomHeartkUrl;
    NSString *statBaseUrl;
    BOOL _isExternalPause;
    
    int _degrees;
}

@synthesize view = _view;
@synthesize currentPlaybackTime;
@synthesize duration;
@synthesize playableDuration;
@synthesize bufferingProgress = _bufferingProgress;

@synthesize numberOfBytesTransferred = _numberOfBytesTransferred;

@synthesize isPreparedToPlay = _isPreparedToPlay;
@synthesize playbackState = _playbackState;
@synthesize loadState = _loadState;

@synthesize naturalSize = _naturalSize;
@synthesize videoSize = _videoSize;
@synthesize isSeekBuffering = _isSeekBuffering;
@synthesize isAudioSync = _isAudioSync;
@synthesize isVideoSync = _isVideoSync;

@synthesize controlStyle = _controlStyle;
@synthesize scalingMode = _scalingMode;
@synthesize shouldAutoplay = _shouldAutoplay;
@synthesize enableAutoIdleTimer = _enableAutoIdleTimer;

@synthesize mediaMeta = _mediaMeta;
@synthesize videoMeta = _videoMeta;
@synthesize audioMeta = _audioMeta;

static MediaDataSourceRead _mediaDataReadBlock;

#define FFP_IO_STAT_STEP (50 * 1024)

// as an example
void IJKFFIOStatDebugCallback(const char *url, int type, int bytes)
{
    static int64_t s_ff_io_stat_check_points = 0;
    static int64_t s_ff_io_stat_bytes = 0;
    if (!url)
        return;

    if (type != IJKMP_IO_STAT_READ)
        return;

    if (!av_strstart(url, "http:", NULL))
        return;

    s_ff_io_stat_bytes += bytes;
    if (s_ff_io_stat_bytes < s_ff_io_stat_check_points ||
        s_ff_io_stat_bytes > s_ff_io_stat_check_points + FFP_IO_STAT_STEP) {
        s_ff_io_stat_check_points = s_ff_io_stat_bytes;
        NSLog(@"io-stat: %s, +%d = %"PRId64"\n", url, bytes, s_ff_io_stat_bytes);
    }
}

void IJKFFIOStatRegister(void (*cb)(const char *url, int type, int bytes))
{
    ijkmp_io_stat_register(cb);
}

void IJKFFIOStatCompleteDebugCallback(const char *url,
                                      int64_t read_bytes, int64_t total_size,
                                      int64_t elpased_time, int64_t total_duration)
{
    if (!url)
        return;

    if (!av_strstart(url, "http:", NULL))
        return;

    NSLog(@"io-stat-complete: %s, %"PRId64"/%"PRId64", %"PRId64"/%"PRId64"\n",
          url, read_bytes, total_size, elpased_time, total_duration);
}

void IJKFFIOStatCompleteRegister(void (*cb)(const char *url,
                                            int64_t read_bytes, int64_t total_size,
                                            int64_t elpased_time, int64_t total_duration))
{
    ijkmp_io_stat_complete_register(cb);
}

- (id)initWithContentURL:(NSURL *)aUrl sharegroup:(EAGLSharegroup*)sharegroup withOptions:(IJKFFOptions *)options crop:(int)crop
{
    return [self initWithContentURL:aUrl
                         sharegroup:sharegroup
                        withOptions:options
                withSegmentResolver:nil
                               crop:crop];
}

- (id)initWithContentURL:(NSURL *)aUrl
              sharegroup:(EAGLSharegroup*)sharegroup
             withOptions:(IJKFFOptions *)options
     withSegmentResolver:(id<IJKMediaSegmentResolver>)segmentResolver
                    crop:(int)crop
{
    if (aUrl == nil)
        return nil;

    return [self initWithContentURLString:[aUrl absoluteString]
                               sharegroup:sharegroup
                              withOptions:options
                      withSegmentResolver:segmentResolver
                                     crop:crop];
}

- (id)initWithContentURLString:(NSString *)aUrlString
                    sharegroup:(EAGLSharegroup*)sharegroup
                   withOptions:(IJKFFOptions *)options
           withSegmentResolver:(id<IJKMediaSegmentResolver>)segmentResolver
                          crop:(int) crop
{
    if (aUrlString == nil)
        return nil;

    self = [super init];
    if (self) {
        ijkmp_global_init();
        _sharegroup = sharegroup;
		
		[self muteAudio:NO];
		
        // IJKFFIOStatRegister(IJKFFIOStatDebugCallback);
        // IJKFFIOStatCompleteRegister(IJKFFIOStatCompleteDebugCallback);

        // init fields
        _controlStyle = MPMovieControlStyleNone;
        _scalingMode = MPMovieScalingModeAspectFit;
        _shouldAutoplay = YES;
        _isMuted = NO;
        _enableAutoIdleTimer = NO;
        // init media resource
        NSString *decodedUrl = [aUrlString stringByReplacingPercentEscapesUsingEncoding:NSUTF8StringEncoding];
        _ffMrl = [[IJKFFMrl alloc] initWithMrl:decodedUrl];
        _segmentResolver = segmentResolver;
        _mediaMeta = [[NSDictionary alloc] init];

        // init player
        _mediaPlayer = ijkmp_ios_create(media_player_msg_loop, crop);
        _msgPool = [[IJKFFMoviePlayerMessagePool alloc] init];

        ijkmp_set_weak_thiz(_mediaPlayer, (__bridge_retained void *) self);
        ijkmp_set_format_callback(_mediaPlayer, format_control_message, (__bridge void *) self);
        ijkmp_set_should_auto_start(_mediaPlayer, _shouldAutoplay ? 1 : 0);
        
        // init video sink
        [self createGlView];
        
        //int chroma = CCSDL_FCC_RV24;
        int chroma = CCSDL_FCC_I420;
        ijkmp_set_overlay_format(_mediaPlayer, chroma);

        // init audio sink
        [[IJKAudioKit sharedInstance] setupAudioSessionWithMediaPlay:withMediaPlay];

        // apply ffmpeg options
        [options applyTo:_mediaPlayer];
        _pauseInBackground = options.pauseInBackground;

        // init extra
        _keepScreenOnWhilePlaying = YES;
        [self setScreenOn:YES];

        _player_volume = 1.0f;
        
        _isVideoToolboxOpen = NO;
        _isExternalPause = NO;
        
        _registeredNotifications = [[NSMutableArray alloc] init];
        [self registerApplicationObservers];
        
    }
    return self;
}

- (void)setSavePath:(NSString *)savePath{
    _savePath = savePath;
    ijkmp_set_video_saver(_mediaPlayer, [_savePath UTF8String]);
}

- (void)createGlView
{
    if (_mediaPlayer == nil) {
        return;
    }
    
    if (_glView != nil) {
        return;
    }
    
    if ([[UIApplication sharedApplication] applicationState] == UIApplicationStateActive) {
        _glView = [[IJKSDLGLView alloc] initWithFrame:[[UIScreen mainScreen] bounds] sharegroup:_sharegroup];
        _view   = _glView;
        ijkmp_ios_set_glview(_mediaPlayer, _glView);
        
        [self setScalingMode:_scalingMode];
        
        //notify
        [[NSNotificationCenter defaultCenter] postNotificationName:IJKMoviePlayerPlaybackVideoViewCreatedNotification object:self];
    }
}

- (void)setScreenOn: (BOOL)on
{
    if(!_enableAutoIdleTimer) return;
    
     [IJKMediaModule sharedModule].mediaModuleIdleTimerDisabled = on;
     [UIApplication sharedApplication].idleTimerDisabled = on;
}

- (void)dealloc
{
    [_ffMrl removeTempFiles];
}

- (void)setShouldAutoplay:(BOOL)shouldAutoplay
{
    NSLog(@"autoplay is %d",shouldAutoplay);
    _shouldAutoplay = shouldAutoplay;
    
    if (!_mediaPlayer)
        return;
    
    ijkmp_set_should_auto_start(_mediaPlayer, _shouldAutoplay ? 1 : 0);
}

- (BOOL)shouldAutoplay
{
    return _shouldAutoplay;
}

- (void)prepareToPlay
{
    if (!_mediaPlayer)
        return;

    [self setScreenOn:_keepScreenOnWhilePlaying];

    ijkmp_set_data_source(_mediaPlayer, [_ffMrl.resolvedMrl UTF8String]);
    ijkmp_set_format_option(_mediaPlayer, "safe", "0"); // for concat demuxer
    ijkmp_prepare_async(_mediaPlayer);
    ijkmp_mute_audio(_mediaPlayer, _isMuted);
    
}

- (void)play
{
    if (!_mediaPlayer)
        return;

    [self setScreenOn:_keepScreenOnWhilePlaying];
    _isExternalPause = NO;
    ijkmp_start(_mediaPlayer);
}

- (void)pause
{
    if (!_mediaPlayer)
        return;
    _isExternalPause = YES;
    ijkmp_pause(_mediaPlayer);
}

-(void)setCropMode:(BOOL)cropMode
{
    if (!_mediaPlayer)
        return;
    
    ijkmp_set_crop_mode(_mediaPlayer, cropMode, 0, 0);
    
    //这里用来触发一次updateVertices
    [self setScalingMode:_scalingMode];
}

- (void)stop
{
    if (!_mediaPlayer)
        return;

    [self setScreenOn:NO];

    int res = jikmp_check_stop(_mediaPlayer);
    if (res == -1) {
        [[NSNotificationCenter defaultCenter] postNotificationName:IJKMoviePlayerVideoSaveNotification object:self userInfo:@{@"video_save_ret":@-1}];
    }
    ijkmp_stop(_mediaPlayer);
}

- (BOOL)isPlaying
{
    if (!_mediaPlayer)
        return NO;

    return ijkmp_is_playing(_mediaPlayer);
}

- (int)changeDir:(int)direct
{
    if (!_mediaPlayer)
        return -1;
    long video_duration = ijkmp_get_duration(_mediaPlayer);
    long cur = ijkmp_get_current_position(_mediaPlayer);
    long msec = video_duration - cur;
    if((direct == 2 && msec > video_duration/2) || (direct == 1 && msec < video_duration/2)){
        // 前半段，逆向播放切换; 后半段，正向播放切换
        return ijkmp_change_play_direction(_mediaPlayer, direct);
    }else{
        // 不切换方向
        return -1;
    }
}
- (void)setPauseInBackground:(BOOL)pause
{
    _pauseInBackground = pause;
}

+ (void)setLogReport:(BOOL)preferLogReport
{
    ijkmp_global_set_log_report(preferLogReport ? 1 : 0);
}

- (void)setMediaDataSourceReadblock:(MediaDataSourceRead)mediaDataReadBlock{
    _mediaDataReadBlock = mediaDataReadBlock;
}

+ (void)setLogLevel:(IJKLogLevel)logLevel
{
    ijk_logLevel = logLevel;
    ijkmp_global_set_log_level(logLevel);
}

+ (void)setupAudioSessionWithMediaPlay:(BOOL)mediaPlay
{
    ALOGF("IJKFFMoviePlayerController setupAudioSessionWithMediaPlay %d",mediaPlay);
    withMediaPlay = mediaPlay;
    [[IJKAudioKit sharedInstance] setupAudioSessionWithMediaPlay:withMediaPlay];
}

- (void)shutdown
{
    ALOGW("-- player shutdown begin -- \n");
    if (!_mediaPlayer)
        return;

    [self unregisterApplicationObservers];
    [self setScreenOn:NO];
    
    [self performSelectorInBackground:@selector(shutdownWaitStop:) withObject:self];
    
    _glView = NULL;
    _view = NULL;
    
}

- (void)setVideoEnable:(BOOL)enabled
{
    if (!_mediaPlayer)
        return;
    
    if (!enabled) {
        ijkmp_pausedisplay(_mediaPlayer);
    } else
    {
        ijkmp_resumedisplay(_mediaPlayer);
    }
}

- (void)shutdownWaitStop:(IJKFFMoviePlayerController *) mySelf
{
    if (!_mediaPlayer)
        return;
    
    ijkmp_stop(_mediaPlayer);
    
    ijkmp_shutdown(_mediaPlayer);
    
    [self performSelectorOnMainThread:@selector(shutdownClose:) withObject:self waitUntilDone:YES];
//	dispatch_async(dispatch_get_main_queue(), ^{
//		[self shutdownClose:self];
//	});
	
}


- (void)shutdownClose:(IJKFFMoviePlayerController *) mySelf
{
    if (!_mediaPlayer)
        return;

//    ijkmp_shutdown(_mediaPlayer);
    ijkmp_dec_ref_p(&_mediaPlayer);
//    ijkmp_async_release(_mediaPlayer);
    ALOGW("-- player shutdown done -- \n");
}

- (MPMoviePlaybackState)playbackState
{
    if (!_mediaPlayer)
        return NO;

    MPMoviePlaybackState mpState = MPMoviePlaybackStateStopped;
    int state = ijkmp_get_state(_mediaPlayer);
    switch (state) {
        case MP_STATE_STOPPED:
        case MP_STATE_COMPLETED:
        case MP_STATE_ERROR:
        case MP_STATE_END:
            mpState = MPMoviePlaybackStateStopped;
            break;
        case MP_STATE_IDLE:
        case MP_STATE_INITIALIZED:
        case MP_STATE_ASYNC_PREPARING:
        case MP_STATE_PAUSED:
            mpState = MPMoviePlaybackStatePaused;
            break;
        case MP_STATE_PREPARED:
        case MP_STATE_STARTED: {
            if (_seeking)
                mpState = MPMoviePlaybackStateSeekingForward;
            else
                mpState = MPMoviePlaybackStatePlaying;
            break;
        }
    }
    // MPMoviePlaybackStatePlaying,
    // MPMoviePlaybackStatePaused,
    // MPMoviePlaybackStateStopped,
    // MPMoviePlaybackStateInterrupted,
    // MPMoviePlaybackStateSeekingForward,
    // MPMoviePlaybackStateSeekingBackward
    return mpState;
}

- (void)setCurrentPlaybackTime:(NSTimeInterval)aCurrentPlaybackTime
{
    if (!_mediaPlayer)
        return;

    _seeking = YES;
    [[NSNotificationCenter defaultCenter]
     postNotificationName:IJKMoviePlayerPlaybackStateDidChangeNotification
     object:self];
    NSLog(@"[seek] setCurrentPlaybackTime %f",aCurrentPlaybackTime);
    
    ijkmp_seek_to(_mediaPlayer, aCurrentPlaybackTime * 1000);
}

- (NSTimeInterval)currentPlaybackTime
{
    if (!_mediaPlayer)
        return 0.0f;

    NSTimeInterval ret = ijkmp_get_current_position(_mediaPlayer);
    return ret / 1000;
}

- (NSTimeInterval)duration
{
    if (!_mediaPlayer)
        return 0.0f;

    NSTimeInterval ret = ijkmp_get_duration(_mediaPlayer);
    return ret / 1000;
}

- (NSTimeInterval)playableDuration
{
    if (!_mediaPlayer)
        return 0.0f;

    NSTimeInterval ret = ijkmp_get_playable_duration(_mediaPlayer);
    return ret / 1000;
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

// deprecated, for MPMoviePlayerController compatiable
- (UIImage *)thumbnailImageAtTime:(NSTimeInterval)playbackTime timeOption:(MPMovieTimeOption)option
{
    return nil;
}

- (UIImage *)thumbnailImageAtCurrentTime
{
    if ([_view isKindOfClass:[IJKSDLGLView class]]) {
        IJKSDLGLView *glView = (IJKSDLGLView *)_view;
        return [glView snapshot];
    }

    return nil;
}

- (CGFloat)fpsAtOutput
{
    return _glView.fps;
}

inline static void fillMetaInternal(NSMutableDictionary *meta, IjkMediaMeta *rawMeta, const char *name, NSString *defaultValue)
{
    if (!meta || !rawMeta || !name)
        return;

    NSString *key = [NSString stringWithUTF8String:name];
    const char *value = ijkmeta_get_string_l(rawMeta, name);
    if (value) {
        [meta setObject:[NSString stringWithUTF8String:value] forKey:key];
    } else if (defaultValue){
        [meta setObject:defaultValue forKey:key];
    } else {
        [meta removeObjectForKey:key];
    }
}

- (void)postEvent: (IJKFFMoviePlayerMessage *)msg
{
    if (!msg)
        return;

    AVMessage *avmsg = &msg->_msg;
    switch (avmsg->what) {
        case FFP_MSG_FLUSH:
            break;
        case FFP_MSG_ERROR: {
            NSLog(@"FFP_MSG_ERROR: %d", avmsg->arg1);
            [self setScreenOn:NO];
             _runPlayerOnMainThread(^{
     
                 [[NSNotificationCenter defaultCenter]
                  postNotificationName:IJKMoviePlayerPlaybackStateDidChangeNotification
                  object:self];
                 
                 [[NSNotificationCenter defaultCenter]
                  postNotificationName:IJKMoviePlayerPlaybackDidFinishNotification
                  object:self
                  userInfo:@{
                             MPMoviePlayerPlaybackDidFinishReasonUserInfoKey: @(MPMovieFinishReasonPlaybackError),
                             @"error": @(avmsg->arg1)}];
             },YES);
            ALOGI("!!!post IJKMoviePlayerPlaybackDidFinishNotification!!! \n");
            break;
        }
        case FFP_MSG_PREPARED: {
            NSLog(@"FFP_MSG_PREPARED:");

            IjkMediaMeta *rawMeta = ijkmp_get_meta_l(_mediaPlayer);
            if (rawMeta) {
                ijkmeta_lock(rawMeta);

                NSMutableDictionary *newMediaMeta = [[NSMutableDictionary alloc] init];

                fillMetaInternal(newMediaMeta, rawMeta, IJKM_KEY_FORMAT, nil);
                fillMetaInternal(newMediaMeta, rawMeta, IJKM_KEY_DURATION_US, nil);
                fillMetaInternal(newMediaMeta, rawMeta, IJKM_KEY_START_US, nil);
                fillMetaInternal(newMediaMeta, rawMeta, IJKM_KEY_BITRATE, nil);

                fillMetaInternal(newMediaMeta, rawMeta, IJKM_KEY_VIDEO_STREAM, nil);
                fillMetaInternal(newMediaMeta, rawMeta, IJKM_KEY_AUDIO_STREAM, nil);

                int64_t video_stream = ijkmeta_get_int64_l(rawMeta, IJKM_KEY_VIDEO_STREAM, -1);
                int64_t audio_stream = ijkmeta_get_int64_l(rawMeta, IJKM_KEY_AUDIO_STREAM, -1);

                NSMutableArray *streams = [[NSMutableArray alloc] init];

                size_t count = ijkmeta_get_children_count_l(rawMeta);
                for(size_t i = 0; i < count; ++i) {
                    IjkMediaMeta *streamRawMeta = ijkmeta_get_child_l(rawMeta, i);
                    NSMutableDictionary *streamMeta = [[NSMutableDictionary alloc] init];

                    if (streamRawMeta) {
                        fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_TYPE, k_IJKM_VAL_TYPE__UNKNOWN);
                        const char *type = ijkmeta_get_string_l(streamRawMeta, IJKM_KEY_TYPE);
                        if (type) {
                            fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_CODEC_NAME, nil);
                            fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_CODEC_PROFILE, nil);
                            fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_CODEC_LONG_NAME, nil);
                            fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_BITRATE, nil);

                            if (0 == strcmp(type, IJKM_VAL_TYPE__VIDEO)) {
                                fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_WIDTH, nil);
                                fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_HEIGHT, nil);
                                fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_FPS_NUM, nil);
                                fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_FPS_DEN, nil);
                                fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_TBR_NUM, nil);
                                fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_TBR_DEN, nil);
                                fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_SAR_NUM, nil);
                                fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_SAR_DEN, nil);

                                if (video_stream == i) {
                                    _videoMeta = streamMeta;

                                    int64_t fps_num = ijkmeta_get_int64_l(streamRawMeta, IJKM_KEY_FPS_NUM, 0);
                                    int64_t fps_den = ijkmeta_get_int64_l(streamRawMeta, IJKM_KEY_FPS_DEN, 0);
                                    if (fps_num > 0 && fps_den > 0) {
                                        _fpsInMeta = ((CGFloat)(fps_num)) / fps_den;
                                        NSLog(@"fps in meta %f\n", _fpsInMeta);
                                    }
                                }

                            } else if (0 == strcmp(type, IJKM_VAL_TYPE__AUDIO)) {
                                fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_SAMPLE_RATE, nil);
                                fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_CHANNEL_LAYOUT, nil);

                                if (audio_stream == i) {
                                    _audioMeta = streamMeta;
                                }

                            }
                        }
                    }

                    [streams addObject:streamMeta];
                }

                [newMediaMeta setObject:streams forKey:kk_IJKM_KEY_STREAMS];

                ijkmeta_unlock(rawMeta);
                _mediaMeta = newMediaMeta;
            }

            _isPreparedToPlay = YES;
            _loadState = MPMovieLoadStatePlayable | MPMovieLoadStatePlaythroughOK;
            
            _runPlayerOnMainThread(^{
                [[NSNotificationCenter defaultCenter] postNotificationName:IJKMediaPlaybackIsPreparedToPlayDidChangeNotification object:self];
                
                [[NSNotificationCenter defaultCenter]
                 postNotificationName:IJKMoviePlayerLoadStateDidChangeNotification
                 object:self];
            }, YES);

            break;
        }
        case FFP_MSG_COMPLETED: {

            [self setScreenOn:NO];
            _runPlayerOnMainThread(^{
                [[NSNotificationCenter defaultCenter]
                 postNotificationName:IJKMoviePlayerPlaybackStateDidChangeNotification
                 object:self];
                
                [[NSNotificationCenter defaultCenter]
                 postNotificationName:IJKMoviePlayerPlaybackDidFinishNotification
                 object:self
                 userInfo:@{MPMoviePlayerPlaybackDidFinishReasonUserInfoKey: @(MPMovieFinishReasonPlaybackEnded)}];
            }, YES);
         
            break;
        }
        case FFP_MSG_VIDEO_SIZE_CHANGED:
        {
            NSLog(@"FFP_MSG_VIDEO_SIZE_CHANGED: %d, %d", avmsg->arg1, avmsg->arg2);
            if (avmsg->arg1 > 0)
                _videoWidth = avmsg->arg1;
            if (avmsg->arg2 > 0)
                _videoHeight = avmsg->arg2;
			
			_videoSize = CGSizeMake(avmsg->arg1, avmsg->arg2);
			
            // TODO: notify size changed
            _runPlayerOnMainThread(^{
                [[NSNotificationCenter defaultCenter] postNotificationName:IJKMoviePlayerPlaybackVideoSizeChangedNotification object:self userInfo:@{@"width":@(avmsg->arg1), @"height":@(avmsg->arg2)}];
            }, YES);
        }
            break;
        case FFP_MSG_SAR_CHANGED:
            NSLog(@"FFP_MSG_SAR_CHANGED: %d, %d", avmsg->arg1, avmsg->arg2);
            if (avmsg->arg1 > 0)
                _sampleAspectRatioNumerator = avmsg->arg1;
            if (avmsg->arg2 > 0)
                _sampleAspectRatioDenominator = avmsg->arg2;
            break;
            
        case FFP_MSG_BUFFERING_START: {
            NSLog(@"FFP_MSG_BUFFERING_START:");
            _loadState = MPMovieLoadStateStalled;
            __weak typeof(self) weakSelf = self;
            _runPlayerOnMainThread(^{
                __strong typeof(weakSelf) strongSelf = weakSelf;
                strongSelf->_isSeekBuffering = avmsg->arg1;
                [[NSNotificationCenter defaultCenter]
                 postNotificationName:IJKMoviePlayerLoadStateDidChangeNotification
                 object:self];
                strongSelf->_isSeekBuffering = 0;
            }, YES);

            break;
        }
        case FFP_MSG_BUFFERING_END: {
            NSLog(@"FFP_MSG_BUFFERING_END:");
            _loadState = MPMovieLoadStatePlayable | MPMovieLoadStatePlaythroughOK;
            __weak typeof(self) weakSelf = self;
            _runPlayerOnMainThread(^{
                __strong typeof(weakSelf) strongSelf = weakSelf;
                strongSelf->_isSeekBuffering = avmsg->arg1;
                [[NSNotificationCenter defaultCenter]
                 postNotificationName:IJKMoviePlayerLoadStateDidChangeNotification
                 object:self];
                [[NSNotificationCenter defaultCenter]
                 postNotificationName:IJKMoviePlayerPlaybackStateDidChangeNotification
                 object:self];
                strongSelf->_isSeekBuffering = 0;
            }, YES);
            break;
        }
        case FFP_MSG_BUFFERING_UPDATE: {
            _bufferingPercent = avmsg->arg1;
            NSLog(@"FFP_MSG_BUFFERING_UPDATE:%ld\n",(long)_bufferingPercent);
            _runPlayerOnMainThread(^{
                [[NSNotificationCenter defaultCenter] postNotificationName:IJKMoviePlayerPlaybackBufferingUpdateNotification object:self userInfo:[NSDictionary dictionaryWithObjectsAndKeys:@(_bufferingPercent), @"percent", nil]];
            }, YES);
        }
            break;
        case FFP_MSG_BUFFERING_BYTES_UPDATE:
            // NSLog(@"FFP_MSG_BUFFERING_BYTES_UPDATE: %d", avmsg->arg1);
            break;
        case FFP_MSG_BUFFERING_TIME_UPDATE:
            _bufferingTime       = avmsg->arg1;
            // NSLog(@"FFP_MSG_BUFFERING_TIME_UPDATE: %d", avmsg->arg1);
            break;
        case FFP_MSG_PLAYBACK_STATE_CHANGED:
        {
            _runPlayerOnMainThread(^{
                [[NSNotificationCenter defaultCenter]
                 postNotificationName:IJKMoviePlayerPlaybackStateDidChangeNotification
                 object:self];
                if(self.playbackState == MPMoviePlaybackStatePlaying) {
                    [[IJKAudioKit sharedInstance] listenCategory];
                }
            }, YES);
        }
            break;
        case FFP_MSG_SEEK_COMPLETE: {
            NSLog(@"FFP_MSG_SEEK_COMPLETE:");
            _seeking = NO;
            _runPlayerOnMainThread(^{
                [[NSNotificationCenter defaultCenter]
                 postNotificationName:IJKMoviePlayerPlaybackSeekCompletedNotification
                 object:self];
            }, YES);
            break;
        }
        case FFP_MSG_ACCURATE_SEEK_COMPLETE: {
            NSLog(@"FFP_MSG_ACCURATE_SEEK_COMPLETE:\n");
            if(_mediaPlayer){
                ijkmp_accurate_seek_complete(_mediaPlayer);
            }
            _runPlayerOnMainThread(^{
                [[NSNotificationCenter defaultCenter]
                    postNotificationName:IJKMPMoviePlayerAccurateSeekCompleteNotification object:self userInfo:@{@"accurate_seek_complete_pos": @(avmsg->arg1)}];
            }, YES);
            break;
        }
        case FFP_MSG_VIDEO_SEEK_RENDERING_START: {
            NSLog(@"FFP_MSG_VIDEO_SEEK_RENDERING_START:\n");
            __weak typeof(self) weakSelf = self;
            _runPlayerOnMainThread(^{
                __strong typeof(weakSelf) strongSelf = weakSelf;
                strongSelf->_isVideoSync = avmsg->arg1;
                [[NSNotificationCenter defaultCenter] postNotificationName:IJKMPMoviePlayerSeekVideoStartNotification object:self];
                strongSelf->_isVideoSync = 0;
            }, YES);
            break;
        }
        case FFP_MSG_AUDIO_SEEK_RENDERING_START: {
            NSLog(@"FFP_MSG_AUDIO_SEEK_RENDERING_START:\n");
            __weak typeof(self) weakSelf = self;
            _runPlayerOnMainThread(^{
                __strong typeof(weakSelf) strongSelf = weakSelf;
                strongSelf->_isAudioSync = avmsg->arg1;
                [[NSNotificationCenter defaultCenter] postNotificationName:IJKMPMoviePlayerSeekAudioStartNotification
                            object:self];
                strongSelf->_isAudioSync = 0;
            }, YES);
            break;
        }
        case FFP_MSG_VIDEO_DECODER_OPEN: {
            _isVideoToolboxOpen = avmsg->arg1;
            NSLog(@"FFP_MSG_VIDEO_DECODER_OPEN: %@\n", _isVideoToolboxOpen ? @"true" : @"false");
            _runPlayerOnMainThread(^{
                [[NSNotificationCenter defaultCenter]
                 postNotificationName:IJKMPMoviePlayerVideoDecoderOpenNotification
                 object:self];
            }, YES);

            break;
        }
        case FFP_MSG_RESTORE_VIDEO_PLAY: {
            _runPlayerOnMainThread(^{
                [[NSNotificationCenter defaultCenter] postNotificationName:IJKMoviePlayerPlaybackRestoreVideoPlay object:self];
            }, YES);

            break;
        }
        case FFP_MSG_FIRST_BUFFERING_READY: {
            _runPlayerOnMainThread(^{
            }, YES);

            break;
        }
        case FFP_MSG_VIDEO_ROTATION_CHANGED: {
            _degrees = avmsg->arg1;
            if (_degrees != 0) {
                NSLog(@"FFP_MSG_VIDEO_ROTATION_CHANGED: %d\n", _degrees);
                _runPlayerOnMainThread(^{
                    [[NSNotificationCenter defaultCenter] postNotificationName:IJKMoviePlayerRotateChangedNotification object:self userInfo:[NSDictionary dictionaryWithObjectsAndKeys:@(_degrees), @"degrees", nil]];
                }, YES);
            }
            break;
        }
        
        case FFP_MSG_TIMED_TEXT:{
            _runPlayerOnMainThread(^{
                NSString *subtitle = [NSString stringWithUTF8String:(char*) avmsg->obj];
                [[NSNotificationCenter defaultCenter] postNotificationName:IJKMoviePlayerSubtitleChangedNotification object:self userInfo:[NSDictionary dictionaryWithObjectsAndKeys:subtitle, @"subtitle", nil]];
            }, YES);
            break;
        }
        case FFP_MSG_VIDEO_SAVE:{
            _runPlayerOnMainThread(^{
                [[NSNotificationCenter defaultCenter] postNotificationName:IJKMoviePlayerVideoSaveNotification
                                                                    object:self
                                                                  userInfo:@{@"video_save_ret": [NSNumber numberWithInt:avmsg->arg1]}];
            }, YES);
            break;
        }
        default:
            // NSLog(@"unknown FFP_MSG_xxx(%d)", avmsg->what);
            break;
    }

    [_msgPool recycle:msg];
}

- (IJKFFMoviePlayerMessage *) obtainMessage {
    return [_msgPool obtain];
}

inline static IJKFFMoviePlayerController *ffplayerRetain(void *arg) {
    return (__bridge_transfer IJKFFMoviePlayerController *) arg;
}

int media_player_msg_loop(void* arg)
{
    @autoreleasepool {
        IjkMediaPlayer *mp = (IjkMediaPlayer*)arg;
        __weak IJKFFMoviePlayerController *ffpController = ffplayerRetain(ijkmp_set_weak_thiz(mp, NULL));

        while (ffpController) {
            @autoreleasepool {
                IJKFFMoviePlayerMessage *msg = [ffpController obtainMessage];
                
                if (!msg){
                    NSLog(@"msg break msg");
                    break;
                }

                int retval = ijkmp_get_msg(mp, &msg->_msg, 1);
                if (retval < 0){
                    NSLog(@"msg break retval");
                    break;
                }

                // block-get should never return 0
                assert(retval > 0);

                [ffpController performSelectorOnMainThread:@selector(postEvent:) withObject:msg waitUntilDone:NO];
            }
        }

        // retained in prepare_async, before CCSDL_CreateThreadEx
        ijkmp_dec_ref_p(&mp);
        return 0;
    }
}

#pragma mark av_format_control_message

int onControlResolveSegment(IJKFFMoviePlayerController *mpc, int type, void *data, size_t data_size)
{
    if (mpc == nil)
        return -1;

//    IJKFormatSegmentContext *fsc = (IJKFormatSegmentContext*)data;
//    if (fsc == NULL || sizeof(IJKFormatSegmentContext) != data_size) {
//        NSLog(@"IJKAVF_CM_RESOLVE_SEGMENT: invalid call\n");
//        return -1;
//    }
//
//    NSString *url = [mpc->_segmentResolver urlOfSegment:fsc->position];
//    if (url == nil)
//        return -1;
//
//    const char *rawUrl = [url UTF8String];
//    if (url == NULL)
//        return -1;
//
//    fsc->url = strdup(rawUrl);
//    if (fsc->url == NULL)
//        return -1;
//
//    fsc->url_free = free;
    return 0;
}

// NOTE: support to be called from read_thread
int format_control_message(void *opaque, int type, void *data, size_t data_size)
{
//    IJKFFMoviePlayerController *mpc = (__bridge IJKFFMoviePlayerController*)opaque;
//
//    switch (type) {
//        case IJKAVF_CM_RESOLVE_SEGMENT:
//            return onControlResolveSegment(mpc, type, data, data_size);
//        default: {
//            return -1;
//        }
//    }
    return -1;
}

#pragma mark IJKAudioSessionDelegate

- (void)ijkAudioBeginInterruption
{
    [[IJKAudioKit sharedInstance] setActive:NO error:nil];
    [self pause];
}

- (void)ijkAudioEndInterruption
{
    if(_isExternalPause) return;
    [[IJKAudioKit sharedInstance] setActive:YES error:nil];
    [self play];
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
    NSLog(@"IJKFFMoviePlayerController unregisterApplicationObservers");
    for (NSString *name in _registeredNotifications) {
        [[NSNotificationCenter defaultCenter] removeObserver:self
                                                        name:name
                                                      object:nil];
    }
    [_registeredNotifications removeAllObjects];
}

- (void)applicationWillEnterForeground
{
    NSLog(@"IJKFFMoviePlayerController:applicationWillEnterForeground: %d", (int)[UIApplication sharedApplication].applicationState);
}

- (void)applicationDidBecomeActive
{
    NSLog(@"IJKFFMoviePlayerController:applicationDidBecomeActive: %d", (int)[UIApplication sharedApplication].applicationState);
    [self createGlView];
    [self setVideoEnable:YES];
    if(!_isExternalPause) {
        [self play];
    }

}

- (void)applicationWillResignActive
{
    NSLog(@"IJKFFMoviePlayerController:applicationWillResignActive: %d", (int)[UIApplication sharedApplication].applicationState);
    dispatch_async(dispatch_get_main_queue(), ^{
        if (_pauseInBackground) {
            [self pause];
        }
    });
}

- (void)applicationDidEnterBackground
{
    NSLog(@"IJKFFMoviePlayerController:applicationDidEnterBackground: %d", (int)[UIApplication sharedApplication].applicationState);
    dispatch_async(dispatch_get_main_queue(), ^{
        if (_pauseInBackground) {
            [self pause];
        }
    });
    [self setVideoEnable:NO];
}

- (void)applicationWillTerminate
{
    NSLog(@"IJKFFMoviePlayerController:applicationWillTerminate: %d", (int)[UIApplication sharedApplication].applicationState);
    dispatch_async(dispatch_get_main_queue(), ^{
        if (_pauseInBackground) {
            [self pause];
        }
    });
}

- (void)audioSessionInterrupt:(NSNotification *)notification
{
    int reason = [[[notification userInfo] valueForKey:AVAudioSessionInterruptionTypeKey] intValue];
    switch (reason) {
        case AVAudioSessionInterruptionTypeBegan: {
            NSLog(@"IJKFFMoviePlayerController:audioSessionInterrupt: begin\n");
            switch (self.playbackState) {
                case MPMoviePlaybackStatePaused:
                case MPMoviePlaybackStateStopped:
                    _playingBeforeInterruption = NO;
                    break;
                default:
                    _playingBeforeInterruption = YES;
                    break;
            }
            NSLog(@"IJKFFMoviePlayerController:audioSessionInterrupt: _playingBeforeInterruption %d\n", _playingBeforeInterruption);
            [self pause];
            [[IJKAudioKit sharedInstance] setActive:NO error:nil];
            break;
        }
        case AVAudioSessionInterruptionTypeEnded: {
            NSLog(@"IJKFFMoviePlayerController:audioSessionInterrupt: end _playingBeforeInterruption %d\n",_playingBeforeInterruption);
            [[IJKAudioKit sharedInstance] setActive:YES error:nil];
            if (_playingBeforeInterruption) {
                [self play];
            }
            break;
        }
    }
}

- (void)muteAudio:(BOOL)mute {
//    [IJKMuteAudioManager sharedInstance].mute = mute;
    _isMuted = mute;
    if (!_mediaPlayer)
        return;
    ijkmp_mute_audio(_mediaPlayer, _isMuted);
}


- (void)setPlaybackRate:(float)playbackRate
{
    if (!_mediaPlayer)
        return;
    
    return ijkmp_set_playback_rate(_mediaPlayer, playbackRate);
}

- (float)playbackRate
{
    if (!_mediaPlayer)
        return 0.0f;
    
    return ijkmp_get_property_float(_mediaPlayer, FFP_PROP_FLOAT_PLAYBACK_RATE, 0.0f);
}

- (void)setPlaybackVolume:(float)volume
{
    if (!_mediaPlayer)
        return;
    return ijkmp_set_playback_volume(_mediaPlayer, volume);
}

- (float)playbackVolume
{
    if (!_mediaPlayer)
        return 0.0f;
    return ijkmp_get_property_float(_mediaPlayer, FFP_PROP_FLOAT_PLAYBACK_VOLUME, 1.0f);
}

- (void)setPlayControlParameters:(BOOL)canFwd fwdNew:(BOOL)forwardNew bufferTimeMsec:(int)bufferTime
                  fwdExtTimeMsec:(int)fwdExtTime minJitterMsec:(int)minJitter maxJitterMsec:(int)maxJitter {
    ijkmp_set_play_control_parameters(_mediaPlayer, canFwd, forwardNew, bufferTime, fwdExtTime, minJitter, maxJitter);
    liveVideo = canFwd;
}

- (void)setRadicalRealTimeFlag:(BOOL)radicalRealTime
{
    ijkmp_set_radical_real_time(_mediaPlayer, radicalRealTime);
}

- (void)CheckAndSetAudioIs:(BOOL)active {
    [[IJKAudioKit sharedInstance] setActive:active error:nil];
    if(active)
        [self play];
    else
        [self pause];
}

- (void)setFrameProcessor:(FrameProcessor *)fp
{
    ijkmp_ios_set_frame_processor(_mediaPlayer, fp);
}

- (void)setDisplayFrameCb:(OnDisplayFrameCb)handle withObj:(void *)obj
{
    ijkmp_set_display_frame_cb(_mediaPlayer, handle, obj);
}

- (BOOL) isVideoToolBoxOpen {
    return _isVideoToolboxOpen;
}

- (int)setPlayerConfig:(PlayerConfig *)config
{
    _playerConfig = *config;
    return 0;
}

- (pthread_t)getReadThreadId{
    return ijkmp_get_read_thread_id(_mediaPlayer);
}

void ijkmp_ijkmediadatasource_read_data(unsigned char *data, int size, int fd){
    if (_mediaDataReadBlock) {
        _mediaDataReadBlock(data, size, fd);
    }else{
        ALOGE("_mediaDataReadBlock is NULL");
    }
}
@end

