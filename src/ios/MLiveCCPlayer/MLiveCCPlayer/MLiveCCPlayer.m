//
//  MLiveCCPlayer.m
//  MLiveCCPlayer
//
//  Created by cc on 16/9/8.
//  Copyright © 2016年 cc. All rights reserved.
//

#import "MLiveCCPlayer.h"
#import "IJKMediaPlayer/IJKMediaPlayer.h"
#import <OpenGLES/es2/gl.h>
#import "MLiveDefines.h"
#import "MLiveDeviceMgr.h"
#import "MLiveCCRenderVTB.h"
#import "MLiveCCRenderYUV.h"
#import <OpenGLES/ES2/glext.h>
#import "MLiveVideoFrame.h"
#import "CCMLiveMovieWriter.h"

#define PLAY_MOBILE_ERROR_NOMOBILEURL -10001
#define PLAY_MOBILE_ERROR_NOVIDEOURL -10002
#define PLAY_MOBILE_ERROR_IMMEDIATE_EXIT -10003
#define RENDER_BUFFER_LENGTH 6

NSString *const MLivePlayerIsPreparedToPlayNotification = @"MLivePlayerIsPreparedToPlayNotification";
NSString *const MLivePlayerDidFinishNotification = @"MLivePlayerDidFinishNotification";
NSString *const MLivePlayerStateDidChangeNotification = @"MLivePlayerStateDidChangeNotification";
NSString *const MLivePlayerRestoreVideoPlay = @"MLivePlayerRestoreVideoPlay";
NSString *const MLivePlayerBufferingUpdateNotification = @"MLivePlayerBufferingUpdateNotification";
NSString *const MLivePlayerVideoDecoderOpenNotification = @"MLivePlayerVideoDecoderOpenNotification";
NSString *const MLivePlayerSeekCompletedNotification = @"MLivePlayerSeekCompletedNotification";
NSString *const MLivePlayerVideoCacheNotification = @"MLivePlayerVideoCacheNotification";

void _runMLiveCCPlayerOnMainThread(void (^block)(), BOOL async) {
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

@interface MLiveCCPlayer()
{
    dispatch_queue_t _playQueue;
    NSString *_statLogExtraInfo;
    id<MLiveCCRender> _renderer;
    MLiveVideoFrame* _videoFrame;
    
    CVPixelBufferRef           _videoPixelBuffers[RENDER_BUFFER_LENGTH];
    int                        _videoPixelBufferIndex;
    
    BOOL                        _updatingTexure;
    NSInteger                   _gameUsedTexture;
    BOOL                        _smartFpsEnable;
    UInt64                      _lastPts;
    UInt64                      _lastUpdateInRenderTime;
    float                       _smartInterval;
    
    BOOL                        _displaySubtitle;
    NSString*                   _subtitleText;
    NSString*                   _videoWriterPath;
    MediaDataSourceReadBlock    _mediaDataReadBlock;
}

@property (atomic, retain) id<IJKMediaPlayback> player;
@property (nonatomic, strong) NSString *urs;
@property (nonatomic, strong) NSString *src;
@property (nonatomic, strong) NSString *uid;
@property (nonatomic, strong) NSString *sid;
@property (nonatomic, assign) BOOL devMode;
@property (nonatomic, assign) BOOL isClosed;
@property (nonatomic, strong) NSString *specializedHost;
@property (nonatomic, assign) BOOL mute;
@property(atomic,strong) NSRecursiveLock *frameLock;
@property(atomic, assign) BOOL framePaused;

@property (nonatomic, assign) CVPixelBufferPoolRef videoBufferPool;
@property (nonatomic, assign) CFDictionaryRef videoBufferPoolAuxAttributes;
@property (nonatomic, strong) NSDictionary *settingsDict;

@property (nonatomic, assign) BOOL useSubtitle;// 是否开启字幕流，默认NO
@property (nonatomic, strong) NSString* audioLanguage;// 设置播放音频语言
@property (nonatomic, strong) NSString* subtitleLanguage;// 设置播放字幕语言

@property (nonatomic, strong) CCMLiveMovieWriter *videoWriter; //测试编码
@end

@implementation MLiveCCPlayer

@synthesize volume = _volume;
@synthesize playbackRate = _playbackRate;

- (id) initWithOutType:(MLiveCCPlayerOutType)type
{
    if(self = [super init])
    {
        _volume = 1.0f;
        _pauseInBackground = YES;
        _devMode = NO;
        _videoWidth = 0;
        _videoHeight = 0;
        
        _outType = type;
        _texture = 0;
        _pixelBufferRef = NULL;
        
        _statLogExtraInfo = nil;
        
        _useHardDecoder = YES;
        _isClosed = YES;
        
        _radicalRealTime = NO;
        
        _useDefaultJitter = NO;
        
        _useCellJitter = NO;
        
        _liteMode = YES;
        
        _enableAutoIdelTimer = NO;
        
        _loop = 1;
        
        _playbackRate = 1.0f;
        
        _specializedHost = nil;
        
        _autoPlay = YES;
        
        _ignoreSilenceMode = YES;
        
        self.logEnable = NO;
        
        _mute = NO;
        
        _frameLock = [[NSRecursiveLock alloc] init];
        _framePaused = NO;
        
        _useRenderBuffer = NO;
        _videoPixelBufferIndex = 0;
        _videoBufferPool = NULL;
        _videoBufferPoolAuxAttributes = NULL;
        
        _useAccelerate = NO;
        _useAccurateSeek = NO;
        _enableSmoothLoop = NO;
        _retainVideoTexture = NO;
        
        _updatingTexure = NO;
        _gameUsedTexture = -1;
        _smartFpsEnable = NO;
        _smartInterval = 33;        // ms fps = 30
        _lastPts = 0;
        _lastUpdateInRenderTime = 0;
        
        _videoCache = nil;
        NSString *sn = [MLiveDeviceMgr sn];
        NSLog(@"[MLiveCCPlayer] version %s sn %@", SDK_VERSION, sn);
        
        _useSubtitle = NO;
        _displaySubtitle = YES;
        
        _useMovieWriter = NO;
        _useMaxProbesize = YES;
        _videoWriterPath =  [NSTemporaryDirectory() stringByAppendingPathComponent:@"ccplayerScreenCapture.mp4"];
    }
    return self;
    
}

- (id) init
{
    return [self initWithOutType:k_OUT_OPENGL_TEXTURE];
}

- (void) setLogEnable:(BOOL)logEnable {
    _logEnable = logEnable;
    if(_logEnable) {
        [self redirectLogsToFile];
    }
}

- (void)setMediaDataSourceReadBlock:(MediaDataSourceReadBlock)mediaDataReadBlock{
    _mediaDataReadBlock = mediaDataReadBlock;
}

- (void)redirectLogsToFile {
    
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    if (paths.count == 0)
        return;
    
    NSString *logFilePath = [[paths objectAtIndex:0] stringByAppendingPathComponent:@"cclive.log"];;
    
    freopen([logFilePath cStringUsingEncoding:NSASCIIStringEncoding], "a+", stdout);
    freopen([logFilePath cStringUsingEncoding:NSASCIIStringEncoding], "a+", stderr);
}
- (void) preparePlay {
    
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    _playQueue = dispatch_get_current_queue();
#pragma clang diagnostic pop
    
    if(_outType == k_OUT_OPENGL_TEXTURE)
    {
        if(_texture == 0) {
            GLint whichID = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &whichID);
            glGenTextures(1, &_texture);
            glBindTexture(GL_TEXTURE_2D, _texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, whichID);
        }
    }
    _updatingTexure = NO;
    _gameUsedTexture = -1;
    _lastUpdateInRenderTime = 0;
    _isClosed = NO;
}

- (void) close
{
    NSLog(@"[MLiveCCPlayer] close");
//    [self updateSubtitleByText:@""];
    [self closeExcludeOutBuffer];
    [self lockFameActive];
    
    glDeleteTextures(1, &_texture);
    _texture = 0;
    if (_videoWriter) {
        [_videoWriter StopWriter];
        _videoWriter = nil;
    }
    for (int i = 0; i< RENDER_BUFFER_LENGTH; i++) {
        if(_videoPixelBuffers[i]) {
            CFRelease(_videoPixelBuffers[i]);
            _videoPixelBuffers[i] = NULL;
        }
        if (_useRenderBuffer) {
            _pixelBufferRef = NULL;
        }
    }
    
    if (_videoBufferPool) {
        CFRelease(_videoBufferPool);
        _videoBufferPool = NULL;
    }
    
    if (_videoBufferPoolAuxAttributes) {
        CFRelease(_videoBufferPoolAuxAttributes);
        _videoBufferPoolAuxAttributes = NULL;
    }
    
    if(_pixelBufferRef != NULL) {
        CVPixelBufferRelease(_pixelBufferRef);
        _pixelBufferRef = NULL;
    }
    _subtitleText = nil;
    [self unlockFrameActive];
}

- (void) closeExcludeOutBuffer {
    NSLog(@"[MLiveCCPlayer] closeExcludeOutBuffer");
      [self lockFameActive];
      if(self.player != nil) {
          NSLog(@"[MLiveCCPlayer] shutdown");
          [self.player shutdown];
          _isClosed = YES;
      
          if([self.player isKindOfClass:[IJKFFMoviePlayerController class]]) {
              if(_videoFrame != NULL) {
                  free(_videoFrame);
              }
              if(_renderer) {
                  [_renderer clearBuffer];
                  _renderer = nil;
              }
          }
          [self removeMovieNotificationObservers];
          self.player = nil;
          _playQueue = nil;
          self.specializedHost = nil;
      }
      [self unlockFrameActive];
}

- (void) playWithUrl:(NSString*)videoUrl cachePath:(NSString *)cachePath{
    _videoCache = cachePath;
    [self playWithUrl:videoUrl];
}


- (void) playWithUrl:(NSString *)videourl {
    
    if(videourl == nil || [videourl isEqualToString:@""]) {
        return;
    }
    
    if(self.player != nil) {
        if(_retainVideoTexture) {
            [self closeExcludeOutBuffer];
        } else {
            [self close];
        }
    }
    
    if(self.player == nil) {
        
        [self preparePlay];
        
        _runMLiveCCPlayerOnMainThread(^{
            [self playUrl:videourl];
        }, YES);
    }
}

- (void) configPlayerSettings:(NSString*)settings
{
    if(settings && [settings isKindOfClass:[NSString class]]) {
        NSLog(@"player settings %@",settings);
        NSData* settingsData = [settings dataUsingEncoding:NSUTF8StringEncoding];
        if(settingsData)
            _settingsDict = [NSJSONSerialization JSONObjectWithData:settingsData options:0 error:nil];
    } else {
        _settingsDict = nil;
    }
    if(_settingsDict) {
        id volume = [_settingsDict objectForKey:@"volume"];
        if (volume) {
            _volume = [volume floatValue];
        }
        
        id playbackRate = [_settingsDict objectForKey:@"playbackRate"];
        if (playbackRate) {
            _playbackRate = [playbackRate floatValue];
        }
        
        id pauseInBackground = [_settingsDict objectForKey:@"pauseInBackground"];
        if (pauseInBackground) {
            _pauseInBackground = [pauseInBackground boolValue];
        }
        
        id radicalRealTime = [_settingsDict objectForKey:@"radicalRealTime"];
        if (radicalRealTime) {
            _radicalRealTime = [radicalRealTime boolValue];
        }
        
        id useDefaultJitter = [_settingsDict objectForKey:@"useDefaultJitter"];
        if (useDefaultJitter) {
            _useDefaultJitter = [useDefaultJitter boolValue];
        }
        
        id useCellJitter = [_settingsDict objectForKey:@"useCellJitter"];
        if (useCellJitter) {
            _useCellJitter = [useCellJitter boolValue];
        }
        
        id useHardDecoder = [_settingsDict objectForKey:@"useHardDecoder"];
        if(useHardDecoder) {
            _useHardDecoder = [useHardDecoder boolValue];
        }
        
        id enableAutoIdelTimer = [_settingsDict objectForKey:@"enableAutoIdelTimer"];
        if (enableAutoIdelTimer) {
            _enableAutoIdelTimer = [enableAutoIdelTimer boolValue];
        }
        
        id loop = [_settingsDict objectForKey:@"loop"];
        if (loop) {
            _loop = [loop intValue];
        }
        
        id liteMode = [_settingsDict objectForKey:@"liteMode"];
        if (liteMode) {
            _liteMode = [liteMode boolValue];
        }
        
        id autoPlay = [_settingsDict objectForKey:@"autoPlay"];
        if (autoPlay) {
            _autoPlay = [autoPlay boolValue];
        }
        
        id ignoreSilenceMode = [_settingsDict objectForKey:@"ignoreSilenceMode"];
        if (ignoreSilenceMode) {
            _ignoreSilenceMode = [ignoreSilenceMode boolValue];
        }
        
        id logEnable = [_settingsDict objectForKey:@"logEnable"];
        if (logEnable) {
           _logEnable = [logEnable boolValue];
        }
        
        id useRenderBuffer = [_settingsDict objectForKey:@"useRenderBuffer"];
        if (useRenderBuffer) {
           _useRenderBuffer = [useRenderBuffer boolValue];
        }
        
        id useAccelerate = [_settingsDict objectForKey:@"useAccelerate"];
        if (useAccelerate) {
            _useAccelerate = [useAccelerate boolValue];
        }
        
        id useAccurateSeek = [_settingsDict objectForKey:@"useAccurateSeek"];
        if (useAccurateSeek) {
            _useAccurateSeek = [useAccurateSeek boolValue];
        }
        
        id enableSmoothLoop = [_settingsDict objectForKey:@"enableSmoothLoop"];
        if (enableSmoothLoop) {
            _enableSmoothLoop = [enableSmoothLoop boolValue];
        }
        
        id retainVideoTexture = [_settingsDict objectForKey:@"retainVideoTexture"];
        if (retainVideoTexture) {
            _retainVideoTexture = [retainVideoTexture boolValue];
        }
        
        id useSubtitle = [_settingsDict objectForKey:@"useSubtitle"];
        if (useSubtitle) {
            _useSubtitle = [useSubtitle boolValue];
        }
        
        _audioLanguage = [_settingsDict objectForKey:@"audioLanguage"];
        _subtitleLanguage = [_settingsDict objectForKey:@"subtitleLanguage"];
    }
}

- (void) playWithVodUrl:(NSString*)vodurl withoffset:(NSInteger)offset length:(NSInteger)length {
    
    if(vodurl == nil || [vodurl isEqualToString:@""]) {
          return;
      }
    
      if(self.player != nil) {
          if(_retainVideoTexture) {
              [self closeExcludeOutBuffer];
          } else {
              [self close];
          }
      }
      
      if(self.player == nil) {
          
          [self preparePlay];
          NSString *ccVodUrl = [NSString stringWithFormat:@"%@&ccsliceoffset=%ld&ccslicelength=%ld", vodurl, (long)offset, length];
          _runMLiveCCPlayerOnMainThread(^{
              [self playUrl:ccVodUrl];
          }, YES);
      }
}

- (void) updateTexture {
    
    [self lockFameActive];
    
    if(_OnVideoUpdate_o) {
        _OnVideoUpdate_o(_videoFrame);
        [self unlockFrameActive];
        return;
    }
    
    if(self.useRenderBuffer) {
        if(_videoPixelBuffers[_videoPixelBufferIndex]) {
            _pixelBufferRef = _videoPixelBuffers[_videoPixelBufferIndex];
//            NSLog(@"[buffer] update %d pixel %p",_videoPixelBufferIndex, _pixelBufferRef);
        }
        
        _videoPixelBufferIndex = (_videoPixelBufferIndex + 1) % RENDER_BUFFER_LENGTH;
    }

    if(_pixelBufferRef != NULL) {
        if(!self.framePaused) {
            if(_outType == k_OUT_OPENGL_TEXTURE && _texture != 0)
            {
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                GLint whichID = 0;
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &whichID);
                glBindTexture(GL_TEXTURE_2D, _texture);
                int frameWidth = (int)CVPixelBufferGetWidth(_pixelBufferRef);
                int frameHeight = (int)CVPixelBufferGetHeight(_pixelBufferRef);
                if(kCVReturnSuccess == CVPixelBufferLockBaseAddress(_pixelBufferRef, 0))
                {
                    uint8_t *baseAddress = CVPixelBufferGetBaseAddress(_pixelBufferRef);
                    if(baseAddress != 0) {
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frameWidth, frameHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE, baseAddress);
                    }
                    CVPixelBufferUnlockBaseAddress(_pixelBufferRef, 0);
                }
                glBindTexture(GL_TEXTURE_2D, whichID);
            }
            
            if(_OnVideoUpdate != nil) _OnVideoUpdate();
            if (_videoWriter == nil && _useMovieWriter) {                
                NSURL *url = [NSURL fileURLWithPath:_videoWriterPath];
                int w = (int)CVPixelBufferGetWidth(_pixelBufferRef);
                int h = (int)CVPixelBufferGetHeight(_pixelBufferRef);
                _videoWriter = [[CCMLiveMovieWriter alloc] initWithURL:url quality:1 size:CGSizeMake(w, h)];
                [_videoWriter StartWriter];
            }
            if(_useMovieWriter){
                [_videoWriter EncodeVideoFrame:_pixelBufferRef];
            }
            
//            NSLog(@"[buffer] update pixel %p done", _pixelBufferRef);
        }
    }
   
    [self unlockFrameActive];
}

- (void)setMediaDataReadBlock:(MediaDataSourceReadBlock)mediaDataReadBlock{
    _mediaDataReadBlock = mediaDataReadBlock;
    if (self.player && [self.player isKindOfClass:[IJKFFMoviePlayerController class]]) {
        IJKFFMoviePlayerController *ffPlayer = self.player;
        [ffPlayer setMediaDataSourceReadblock:(MediaDataSourceRead)_mediaDataReadBlock];
    }
}

- (void) playUrl:(NSString*)videourl {
    NSLog(@"[MLiveCCPlayer] play videoUrl %@",videourl);
    
    [IJKFFMoviePlayerController setupAudioSessionWithMediaPlay:_ignoreSilenceMode];
    
    IJKFFOptions *opt = [IJKFFOptions optionsByDefault];
    opt.pauseInBackground = _pauseInBackground;
    opt.videoToolbox = _useHardDecoder;
    opt.loop = _loop;
    opt.enableAccurateSeek = _useAccurateSeek;
    opt.enableSmoothLoop = _enableSmoothLoop;
    opt.audioLanguage = _audioLanguage;
    opt.subtitleLanguage = _subtitleLanguage;
    opt.enableSubtitle = _useSubtitle;

    opt.enableMaxProbesize = _useMaxProbesize;
    opt.enableSoundtouch = _useSoundtouch;
    NSLog(@"[MLiveCCPlayer] play videoUrl %@ loop %lu useAccurateSeek %d maxprosize %d",videourl, (unsigned long)_loop, _useAccurateSeek, _useMaxProbesize);
     
    // "blueray", "ultra", "high", "standard"
//    BOOL isHighBitRate = ([_vbrname isEqualToString:@"blueray"] || [_vbrname isEqualToString: @"ultra"]);
    if(_liteMode) {
        NSLog(@"[MLiveCCPlayer] is low performance device and high bitrate --> discard some frames");
        [opt enableProtectModeForBD];
//        opt.vtb_max_frame_width = 1280;
    }
    self.player = [[IJKFFMoviePlayerController alloc] initWithContentURL:[NSURL URLWithString:videourl] sharegroup:nil withOptions:opt crop: 1];
    
    if (_mediaDataReadBlock) {
        [(IJKFFMoviePlayerController *)self.player setMediaDataSourceReadblock:_mediaDataReadBlock];
    }
    
    IJKFFMoviePlayerController *ffplayer = self.player;
    ffplayer.savePath = _videoCache;
    
    self.player.enableAutoIdleTimer = _enableAutoIdelTimer;
    
    [self installMovieNotificationObservers];
    
    [self.player muteAudio:_mute];
    
    [self.player setPlaybackVolume:_volume];
    
    [self.player setPlaybackRate:_playbackRate];
    
    
    FrameProcessor *fp = [[FrameProcessor alloc] init];

    fp.onRenderFrame = ^(MLiveCCVideoFrame *frame) {

        [self lockFameActive];
        if(_isClosed) {
            [self unlockFrameActive];
            return;
        }
        [self updateFrame:frame];
        [self unlockFrameActive];
    };

    [self.player setFrameProcessor:fp];
    
    [self.player setScalingMode:MPMovieScalingModeAspectFit];
    self.player.shouldAutoplay = _autoPlay;
    [self.player prepareToPlay];
}

#pragma mark -- play control
- (void) play {
    if(self.player != nil)
       [self.player play];
//    [self updateSubtitleByText:_subtitleText];
}

- (void) stop {
//    [self updateSubtitleByText:@""];
    if(self.player != nil)
        [self.player stop];
}

- (void) pause {
    if(self.player != nil)
        [self.player pause];
}

- (BOOL) isPlaying {
    if(self.player != nil)
        return [self.player isPlaying];
    else
        return NO;
}

- (void) muteAudio:(BOOL)mute {
    _mute = mute;
    NSLog(@"[MLiveCCPlayer] mute %d",_mute);
    if(self.player != nil)
        [self.player muteAudio:mute];
}

- (BOOL) changeDirection:(int) direct {
    BOOL ret = NO;
    if(self.player != nil){
        if([self.player isKindOfClass:[IJKFFMoviePlayerController class]]) {
            if ([(IJKFFMoviePlayerController*)self.player changeDir:direct] >= 0){
                NSLog(@"direction changed to %d",direct);
                ret = YES;
            } else{
                NSLog(@"keep current direction");
            }
        }
    }
    return ret;
}
- (void) reActiveAudio {
    if(self.player != nil) {
        if([self.player isKindOfClass:[IJKFFMoviePlayerController class]]) {
            [(IJKFFMoviePlayerController*)self.player CheckAndSetAudioIs:YES];
            NSLog(@"[MLiveCCPlayer] reActiveAudio");
        }
    }
}

#pragma mark -- play state

- (NSTimeInterval) playDuration {
    if(self.player != nil) {
        return [self.player duration];
    }
    return 0;
}

- (NSTimeInterval) curPlaybackTime {
    if(self.player != nil) {
        return [self.player currentPlaybackTime];
    }
    return 0;
}

- (void) setPlaybackTime:(NSTimeInterval)value {
    if(self.player != nil) {
        self.player.currentPlaybackTime = value;
    }
}
- (void)loadStateDidChange:(NSNotification*)notification
{
    //    MPMovieLoadStateUnknown        = 0,
    //    MPMovieLoadStatePlayable       = 1 << 0,
    //    MPMovieLoadStatePlaythroughOK  = 1 << 1, // Playback will be automatically started in this state when shouldAutoplay is YES
    //    MPMovieLoadStateStalled        = 1 << 2, // Playback will be automatically paused in this state, if started
    MPMovieLoadState loadState = _player.loadState;
    
    if ((loadState & MPMovieLoadStatePlaythroughOK) != 0) {
        NSLog(@"loadStateDidChange: MPMovieLoadStatePlaythroughOK: %d\n", (int)loadState);
    } else if ((loadState & MPMovieLoadStateStalled) != 0) {
        NSLog(@"loadStateDidChange: MPMovieLoadStateStalled: %d\n", (int)loadState);
    } else {
        NSLog(@"loadStateDidChange: ???: %d\n", (int)loadState);
    }
}

- (void) setVolume:(float)volume {
    _volume = volume;
//    [self.player setVolume:volume];
    NSLog(@"[MLiveCCPlayer] setVolume %f", volume);
    [self.player setPlaybackVolume:_volume];
}

- (float) volume {
    if(self.player != nil) {
        return [self.player playbackVolume];
    }
    return 1.0f;
}

- (void) setPlaybackRate:(float)playbackRate {
    _playbackRate = playbackRate;
    [self.player setPlaybackRate:_playbackRate];
}

- (float) playbackRate {
    if(self.player != nil) {
        return [self.player playbackRate];
    }
    return 1.0f;
}

- (void) setUseAccelerate:(BOOL)useAccelerate {
    _useAccelerate = useAccelerate;
    // less than 8.0 ?
    if([[[UIDevice currentDevice] systemVersion] compare:@"8.0" options:NSNumericSearch] == NSOrderedAscending) {
        _useAccelerate = NO;
    }
}

- (void) setRetainVideoTexture:(BOOL)retainVideoTexture {
    NSLog(@"[MLiveCCPlayer] setRetainVideoTexture %d", retainVideoTexture);
    _retainVideoTexture = retainVideoTexture;
}

- (void) setUseAccurateSeek:(BOOL)useAccurateSeek {
    _useAccurateSeek = useAccurateSeek;
    NSLog(@"[MLiveCCPlayer] setUseAccurateSeek %d", _useAccurateSeek);
}

- (void) setEnableSmoothLoop:(BOOL)enableSmoothLoop {
    _enableSmoothLoop = enableSmoothLoop;
    NSLog(@"[MLiveCCPlayer] setEnableSmoothLoop %d", _enableSmoothLoop);
}
- (void)moviePlayBackStateDidChange:(NSNotification*)notification
{
    //    MPMoviePlaybackStateStopped,
    //    MPMoviePlaybackStatePlaying,
    //    MPMoviePlaybackStatePaused,
    //    MPMoviePlaybackStateInterrupted,
    //    MPMoviePlaybackStateSeekingForward,
    //    MPMoviePlaybackStateSeekingBackward
    
    switch (_player.playbackState)
    {
        case MPMoviePlaybackStateStopped: {
            NSLog(@"moviePlayBackStateDidChange %d: stoped", (int)_player.playbackState);
            break;
        }
        case MPMoviePlaybackStatePlaying: {
            NSLog(@"moviePlayBackStateDidChange %d: playing", (int)_player.playbackState);
            break;
        }
        case MPMoviePlaybackStatePaused: {
            NSLog(@"moviePlayBackStateDidChange %d: paused", (int)_player.playbackState);
            break;
        }
        case MPMoviePlaybackStateInterrupted: {
            NSLog(@"moviePlayBackStateDidChange %d: interrupted", (int)_player.playbackState);
            break;
        }
        case MPMoviePlaybackStateSeekingForward:
        case MPMoviePlaybackStateSeekingBackward: {
            NSLog(@"moviePlayBackStateDidChange %d: seeking", (int)_player.playbackState);
            break;
        }
        default: {
            NSLog(@"moviePlayBackStateDidChange %d: unknown", (int)_player.playbackState);
            break;
        }
    }
    [[NSNotificationCenter defaultCenter] postNotificationName:MLivePlayerStateDidChangeNotification
                                                        object:self userInfo:@{@"playbackState": @(_player.playbackState)}];
}

- (void)moviePlayBackDidFinish:(NSNotification*)notification
{
    //    MPMovieFinishReasonPlaybackEnded,
    //    MPMovieFinishReasonPlaybackError,
    //    MPMovieFinishReasonUserExited

    int reason = [[[notification userInfo] valueForKey:MPMoviePlayerPlaybackDidFinishReasonUserInfoKey] intValue];
    id playerErr = [[notification userInfo] valueForKey:@"error"];
    int error = 0;
    if([playerErr isKindOfClass:[NSError class]]) {
        NSError *e = (NSError*)playerErr;
        error = e.code;
    } else
        error = [playerErr intValue];
    
    switch (reason)
    {
        case MPMovieFinishReasonPlaybackEnded:
            NSLog(@"[MLiveCCPlayer] playbackStateDidChange: MPMovieFinishReasonPlaybackEnded: %d\n", reason);
            break;
            
        case MPMovieFinishReasonUserExited:
            NSLog(@"[MLiveCCPlayer] playbackStateDidChange: MPMovieFinishReasonUserExited: %d\n", reason);
            break;
            
        case MPMovieFinishReasonPlaybackError:
            NSLog(@"[MLiveCCPlayer] playbackStateDidChange: MPMovieFinishReasonPlaybackError: %d code: %d\n", reason, error);
            break;
            
        default:
            NSLog(@"[MLiveCCPlayer] playbackPlayBackDidFinish: ???: %d\n", reason);
            break;
    }
    
}

- (void)mediaIsPreparedToPlayDidChange:(NSNotification*)notification
{
    NSLog(@"[MLiveCCPlayer] mediaIsPreparedToPlayDidChange\n");
    
    [[NSNotificationCenter defaultCenter] postNotificationName:MLivePlayerIsPreparedToPlayNotification
                                                        object:self];
}

- (void)moviePlayVideoViewCreated:(NSNotification*)notification
{
}

- (void)moviePlayBufferingUpdate:(NSNotification*)notification
{
    NSLog(@"[MLiveCCPlayer] moviePlayBufferingUpdate: percent = %@\n", [notification.userInfo valueForKey:@"percent"]);
    NSNumber *percent = [notification.userInfo valueForKey:@"percent"];

    [[NSNotificationCenter defaultCenter] postNotificationName:MLivePlayerBufferingUpdateNotification object:self userInfo:[NSDictionary dictionaryWithObjectsAndKeys:percent, @"percent", nil]];
}

- (void)moviePlayRestorePlay:(NSNotification*)notification
{
    [[NSNotificationCenter defaultCenter] postNotificationName:MLivePlayerRestoreVideoPlay object:self];
}

- (void)moviePlayDecoderOpen:(NSNotification*)notification
{
    [[NSNotificationCenter defaultCenter] postNotificationName:MLivePlayerVideoDecoderOpenNotification object:self];
}

- (void)moviePlaySeekCompleted:(NSNotification *)notification
{
    [[NSNotificationCenter defaultCenter] postNotificationName:MLivePlayerSeekCompletedNotification object:self];
}

- (void)moviePlayRotateChanged:(NSNotification *)notification
{
    NSLog(@"[MLiveCCPlayer] moviePlayRotateChanged: deg = %@\n", [notification.userInfo valueForKey:@"degrees"]);
    NSNumber *degrees = [notification.userInfo valueForKey:@"degrees"];
    
    
}

-(void) updateSubtitle:(NSNotification *)notification
{
    _subtitleText = [notification.userInfo valueForKey:@"subtitle"];
    [self updateSubtitleByText:_subtitleText];
}

-(void) updateSubtitleByText:(NSString *)subtitle
{
    if (!subtitle) {
        return;
    }
    if (_OnVideoSubtitle && _displaySubtitle) {
        if(_playQueue != nil) {
            dispatch_async(_playQueue, ^{
                _OnVideoSubtitle(subtitle);
            });
        }
    }
}


- (BOOL) isUsingHardDecoder {
    if(self.player) {
        if([self.player isKindOfClass:[IJKFFMoviePlayerController class]]) {
            BOOL isVideoToolBox =  [(IJKFFMoviePlayerController*)self.player isVideoToolBoxOpen];
            NSLog(@"[MLiveCCPlayer] is using hard decoder %d",isVideoToolBox);
            return isVideoToolBox;
        }
        else {
            return NO;
        }
    }
    else {
        return NO;
    }
}

/* Register observers for the various movie object notifications. */
-(void)installMovieNotificationObservers
{
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(loadStateDidChange:)
                                                 name:IJKMoviePlayerLoadStateDidChangeNotification
                                               object:self.player];
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(moviePlayBackDidFinish:)
                                                 name:IJKMoviePlayerPlaybackDidFinishNotification
                                               object:self.player];
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(mediaIsPreparedToPlayDidChange:)
                                                 name:IJKMediaPlaybackIsPreparedToPlayDidChangeNotification
                                               object:self.player];
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(moviePlayBackStateDidChange:)
                                                 name:IJKMoviePlayerPlaybackStateDidChangeNotification
                                               object:self.player];
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(moviePlayVideoViewCreated:)
                                                 name:IJKMoviePlayerPlaybackVideoViewCreatedNotification
                                               object:self.player];
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(moviePlayBufferingUpdate:)
                                                 name:IJKMoviePlayerPlaybackBufferingUpdateNotification
                                               object:self.player];
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(moviePlayRestorePlay:)
                                                 name:IJKMoviePlayerPlaybackRestoreVideoPlay
                                               object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(moviePlayDecoderOpen:)
                                                 name:IJKMPMoviePlayerVideoDecoderOpenNotification
                                               object:self.player];
    
    
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(moviePlaySeekCompleted:)
                                                 name:IJKMoviePlayerPlaybackSeekCompletedNotification
                                               object:self.player];
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                            selector:@selector(moviePlayRotateChanged:)
                                            name:IJKMoviePlayerRotateChangedNotification
                                            object:self.player];

    //application status
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationWillEnterForeground)
                                                 name:UIApplicationWillEnterForegroundNotification
                                               object:nil];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationDidBecomeActive)
                                                 name:UIApplicationDidBecomeActiveNotification
                                               object:nil];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationWillResignActive)
                                                 name:UIApplicationWillResignActiveNotification
                                               object:nil];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationDidEnterBackground)
                                                 name:UIApplicationDidEnterBackgroundNotification
                                               object:nil];
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationWillTerminate)
                                                 name:UIApplicationWillTerminateNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(updateSubtitle:)
                                                 name:IJKMoviePlayerSubtitleChangedNotification
                                               object:nil];

    
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(videoSaveNotify:)
                                                 name:IJKMoviePlayerVideoSaveNotification
                                               object:nil];
    
}
#pragma mark Remove Movie Notification Handlers

-(void)videoSaveNotify:(NSNotification *)notify{
    [[NSNotificationCenter defaultCenter] postNotificationName:MLivePlayerVideoCacheNotification object:notify.userInfo];
}

/* Remove the movie notification observers from the movie object. */
-(void)removeMovieNotificationObservers
{
    [[NSNotificationCenter defaultCenter]removeObserver:self name:IJKMoviePlayerLoadStateDidChangeNotification object:self.player];
    [[NSNotificationCenter defaultCenter]removeObserver:self name:IJKMoviePlayerPlaybackDidFinishNotification object:self.player];
    [[NSNotificationCenter defaultCenter]removeObserver:self name:IJKMediaPlaybackIsPreparedToPlayDidChangeNotification object:self.player];
    [[NSNotificationCenter defaultCenter]removeObserver:self name:IJKMoviePlayerPlaybackStateDidChangeNotification object:self.player];
    [[NSNotificationCenter defaultCenter]removeObserver:self name:IJKMoviePlayerPlaybackVideoViewCreatedNotification object:self.player];
    [[NSNotificationCenter defaultCenter]removeObserver:self name:IJKMoviePlayerPlaybackBufferingUpdateNotification object:self.player];
    [[NSNotificationCenter defaultCenter]removeObserver:self name:IJKMoviePlayerPlaybackRestoreVideoPlay object:self.player];
    [[NSNotificationCenter defaultCenter]removeObserver:self name:IJKMPMoviePlayerVideoDecoderOpenNotification object:self.player];
    [[NSNotificationCenter defaultCenter]removeObserver:self name:IJKMoviePlayerPlaybackSeekCompletedNotification object:self.player];

    [[NSNotificationCenter defaultCenter]removeObserver:self name:IJKMoviePlayerRotateChangedNotification object:self.player];
    [[NSNotificationCenter defaultCenter]removeObserver:self name:IJKMoviePlayerSubtitleChangedNotification object:self.player];
    [[NSNotificationCenter defaultCenter]removeObserver:self name:IJKMPMoviePlayerAccurateSeekCompleteNotification object:self.player];
    [[NSNotificationCenter defaultCenter] removeObserver:self name:IJKMoviePlayerVideoSaveNotification object:nil];
    //remove application status
    [[NSNotificationCenter defaultCenter]removeObserver:self name:UIApplicationWillEnterForegroundNotification object:nil];
    
    [[NSNotificationCenter defaultCenter] removeObserver:self name:UIApplicationDidBecomeActiveNotification object:nil];
    
    [[NSNotificationCenter defaultCenter] removeObserver:self name:UIApplicationWillResignActiveNotification object:nil];
    
    [[NSNotificationCenter defaultCenter] removeObserver:self name:UIApplicationDidEnterBackgroundNotification object:nil];
    
    [[NSNotificationCenter defaultCenter] removeObserver:self name:UIApplicationWillTerminateNotification object:nil];


}

-(void)setLogLevel:(MLiveCCPlayerLogLevel)level {
    [IJKFFMoviePlayerController setLogLevel:(IJKLogLevel)level];
}

- (BOOL)modelIsIPad {
    return ([[[UIDevice currentDevice] model] rangeOfString:@"iPad"].location != NSNotFound);
}

- (BOOL)modelIsIPod {
    return ([[[UIDevice currentDevice] model] rangeOfString:@"iPod touch"].location != NSNotFound);
}

- (NSString *)URLEncoding:(NSString *)input {
    if (!input) {
        return nil;
    }
    NSString * __autoreleasing outputStr = (__bridge NSString *)CFURLCreateStringByAddingPercentEscapes(NULL, (CFStringRef)input, NULL, CFSTR(":/?#[]@!$ &'()*+,;=\"<>%{}|\\^~`"), kCFStringEncodingUTF8);
    return outputStr;
}

#pragma mark -- update frame

- (void) updateFrame:(MLiveCCVideoFrame* )frame
{
    
    if(_updatingTexure ||  _gameUsedTexture == 0) {
        NSLog(@"[MLiveCCPlayer]: updateTextureInRenderQueue Busy\n");
        return;
    }
    if(_smartFpsEnable){
        UInt64 curTime = [[NSDate date] timeIntervalSince1970]*1000;

        if(curTime - _lastPts < _smartInterval){
            NSLog(@"[MLiveCCPlayer]: updateTextureInRenderQueue over fps %f \n", 1000 / _smartInterval);
            return;
        }
        _lastPts = curTime;
    }
    
    if(_OnVideoUpdate_o) {
        [self updateFrameByNone:frame];
    } else {
        [self updateFrameByRender:frame];
    }
}

- (void) displaySubtitle:(BOOL)enable
{
    if (!enable && _subtitleText) {
        [self updateSubtitleByText:@""];
        _displaySubtitle = NO;
    }else
    {
        _displaySubtitle = YES;
        [self updateSubtitleByText:_subtitleText];
    }
    
}

- (void) updateFrameByNone:(MLiveCCVideoFrame* )frame
{
    _updatingTexure = YES;
    
    if(_gameUsedTexture == 1) //说明外部启用了纹理渲染通知接口
        _gameUsedTexture = 0;
    
    if(_videoFrame != NULL) {
        free(_videoFrame);
        _videoFrame = NULL;
    }
    
    MLiveVideoFrame *out_frame = (MLiveVideoFrame*)malloc(sizeof(MLiveVideoFrame));
    if(!out_frame) return;
    memset(out_frame, 0, sizeof(MLiveVideoFrame));
    out_frame->w = frame->w;
    out_frame->h = frame->h;
    out_frame->format = frame->format;
    out_frame->planes = frame->planes;
    for (int i = 0; i < 8; ++i) {
        out_frame->pixels[i] = frame->pixels[i];
        out_frame->pitches[i] = frame->pitches[i];
    }
    
    _videoFrame = out_frame;
    
    if(_OnVideoUpdate_o) {
        if(_playQueue != nil) {
            dispatch_async(_playQueue, ^{
                [self updateTexture];
                _updatingTexure = NO;
            });
        }
        return;
    }
    
}

- (void) updateFrameByRender:(MLiveCCVideoFrame* )frame {
    
    uint32_t format =  frame->format;
    
    if (![self setupDisplay:frame]) {
        NSLog(@"[MLiveCCPlayer]: setupDisplay failed\n");
        return;
    }
    _updatingTexure = YES;
    if(_gameUsedTexture == 1) //说明外部启用了纹理渲染通知接口
        _gameUsedTexture = 0;
    if(self.useRenderBuffer) {
        [self preparePixelBufferPoolForFrame:frame];
        int index = (_videoPixelBufferIndex + 1) % RENDER_BUFFER_LENGTH;
        CVPixelBufferRef pixelBufferRef = _videoPixelBuffers[index];
//        NSLog(@"[buffer] modify %d pixel %p", index, _videoPixelBuffers[index]);
        switch (format) {
            case DECODE_FORMAT_I420:
                [_renderer genPixelBufferFrom:frame To:pixelBufferRef Accelerate:_useAccelerate];
                break;
            case DECODE_FORMAT_VTB:
                [_renderer genPixelBufferFrom:frame To:pixelBufferRef Accelerate:_useAccelerate];
                break;
            default:
                break;
        }
    } else {
//        NSLog(@"[buffer] modify pixel %p", _pixelBufferRef);
        [self preparePixelBufferForFrame:frame];
        switch (format) {
            case DECODE_FORMAT_I420:
                [_renderer genPixelBufferFrom:frame To:_pixelBufferRef Accelerate:_useAccelerate];
                break;
            case DECODE_FORMAT_VTB:
                [_renderer genPixelBufferFrom:frame To:_pixelBufferRef Accelerate:_useAccelerate];
                break;
            default:
                break;
        }
    }
    
    if(_playQueue != nil) {
        dispatch_async(_playQueue, ^{
            [self updateTexture];
            _updatingTexure = NO;
        });
    }
}

- (void) afterRenderVideoBuffer {
    
    _gameUsedTexture = 1;

}


- (BOOL)setupDisplay: (MLiveCCVideoFrame *) frame
{
    BOOL needReCreateRenderer = NO;
    if (_renderer && frame && _renderer.format != frame->format) {
        // TODO: if format changed
        needReCreateRenderer = YES;
    }
    
    if (_renderer == nil || needReCreateRenderer) {
        if (frame == nil) {
            return NO;
        } else if (frame->format == DECODE_FORMAT_VTB) {
            _renderer = [[MLiveCCRenderVTB alloc] initWithTextureCache:nil];
            _renderer.format = frame->format;
            NSLog(@"[MLiveCCPlayer] OK use NV12 renderer");
        } else if (frame->format == DECODE_FORMAT_I420) {
            _renderer = [[MLiveCCRenderYUV alloc] init];
            _renderer.format = frame->format;
            NSLog(@"[MLiveCCPlayer] OK use I420 renderer");
        }
    }
    return YES;
}

static CVPixelBufferPoolRef mlivecc_createPixelBufferPool( int32_t width, int32_t height, OSType pixelFormat, int32_t maxBufferCount)
{
    CVPixelBufferPoolRef outputPool = NULL;
    
    CFMutableDictionaryRef sourcePixelBufferOptions = CFDictionaryCreateMutable( kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks );
    CFNumberRef number = CFNumberCreate( kCFAllocatorDefault, kCFNumberSInt32Type, &pixelFormat );
    CFDictionaryAddValue( sourcePixelBufferOptions, kCVPixelBufferPixelFormatTypeKey, number );
    CFRelease( number );
    
    number = CFNumberCreate( kCFAllocatorDefault, kCFNumberSInt32Type, &width );
    CFDictionaryAddValue( sourcePixelBufferOptions, kCVPixelBufferWidthKey, number );
    CFRelease( number );
    
    number = CFNumberCreate( kCFAllocatorDefault, kCFNumberSInt32Type, &height );
    CFDictionaryAddValue( sourcePixelBufferOptions, kCVPixelBufferHeightKey, number );
    CFRelease( number );
    
    ((__bridge NSMutableDictionary *)sourcePixelBufferOptions)[(id)kCVPixelBufferIOSurfacePropertiesKey] = @{ @"IOSurfaceIsGlobal" : @YES };
    
    number = CFNumberCreate( kCFAllocatorDefault, kCFNumberSInt32Type, &maxBufferCount );
    CFDictionaryRef pixelBufferPoolOptions = CFDictionaryCreate( kCFAllocatorDefault, (const void **)&kCVPixelBufferPoolMinimumBufferCountKey, (const void **)&number, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks );
    CFRelease( number );
    
    CVPixelBufferPoolCreate( kCFAllocatorDefault, pixelBufferPoolOptions, sourcePixelBufferOptions, &outputPool );
    
    CFRelease( sourcePixelBufferOptions );
    CFRelease( pixelBufferPoolOptions );
    
    return outputPool;
}

static CFDictionaryRef mlivecc_createPixelBufferPoolAuxAttributes( int32_t maxBufferCount )
{
    // CVPixelBufferPoolCreatePixelBufferWithAuxAttributes() will return kCVReturnWouldExceedAllocationThreshold if we have already vended the max number of buffers
    NSDictionary *auxAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:[NSNumber numberWithInt:maxBufferCount], (id)kCVPixelBufferPoolAllocationThresholdKey, nil];
    return (__bridge_retained CFDictionaryRef)auxAttributes;
}

- (CVPixelBufferRef)dequeuePixelBufferForFrame:(MLiveCCVideoFrame *)frame
{
    CVPixelBufferRef dstPixelBuffer = NULL;
    
    if ( frame == NULL ) {
        @throw [NSException exceptionWithName:NSInvalidArgumentException reason:@"NULL frame" userInfo:nil];
        return NULL;
    }
    
    CVReturn err = CVPixelBufferPoolCreatePixelBufferWithAuxAttributes( kCFAllocatorDefault, _videoBufferPool, _videoBufferPoolAuxAttributes, &dstPixelBuffer );
    
    if ( err == kCVReturnWouldExceedAllocationThreshold ) {
        NSLog(@"[MLiveCCPlayer] Pool is out of buffers, dropping frame");
    }
    else if ( err != kCVReturnSuccess) {
        NSLog(@"[MLiveCCPlayer] Error at CVPixelBufferPoolCreatePixelBuffer %d", err);
    }
    
    return dstPixelBuffer;
}

- (void)preparePixelBufferPoolForFrame:(MLiveCCVideoFrame *)frame
{
    int width = frame->w;
    int height = frame->h;
    
    if(abs(frame->rotate) == 90 || abs(frame->rotate) == 270) {
        width = frame->h;
        height = frame->w;
    }
       
    int bufferWidth = _useAccelerate ? width : ((width % 16 == 0) ? width : (width / 16 + 1) * 16);
    int bufferHeight = height;
    
    BOOL createBuffer = NO;
    
    if(_videoWidth != width || _videoHeight != height) {
        createBuffer = YES;
    }
    
    if(createBuffer) {
        _videoWidth = width;
        _videoHeight = height;
        NSLog(@"MLiveCCPlayer create buffer pool %d %d", _videoWidth, _videoHeight);
        
        for (int i = 0; i < RENDER_BUFFER_LENGTH; i ++) {
            if(_videoPixelBuffers[i] != NULL) {
                CVPixelBufferRelease(_videoPixelBuffers[i]);
                _videoPixelBuffers[i] = NULL;
            }
            _pixelBufferRef = NULL;
            
            CVPixelBufferRef tmpPixelBufferRef;
            NSDictionary *pixelAttributes = [NSDictionary dictionaryWithObjectsAndKeys:[NSDictionary dictionary], (id)kCVPixelBufferIOSurfacePropertiesKey, nil];
            CVReturn ret = CVPixelBufferCreate(kCFAllocatorDefault, bufferWidth, bufferHeight, kCVPixelFormatType_32BGRA,  (__bridge CFDictionaryRef)pixelAttributes, &tmpPixelBufferRef);
                  NSParameterAssert(ret == kCVReturnSuccess && tmpPixelBufferRef != NULL);
            _videoPixelBuffers[i] = tmpPixelBufferRef;
        }

        NSLog(@"[MLiveCCPlayer] out pool pixelbuffer width %d height %d videoWidth %d videoHeight %d rotate %d accelerate %d", bufferWidth, bufferHeight, _videoWidth, _videoHeight, frame->rotate, _useAccelerate);
    }
}

- (void)preparePixelBufferForFrame:(MLiveCCVideoFrame *)frame
{
    int width = frame->w;
    int height = frame->h;
    
    if(abs(frame->rotate) == 90 || abs(frame->rotate) == 270) {
        width = frame->h;
        height = frame->w;
    }
    
    int bufferWidth = _useAccelerate ? width : ((width % 16 == 0) ? width : (width / 16 + 1) * 16);
    int bufferHeight = height;
    
    _videoWidth = width;
    _videoHeight = height;
    
    BOOL createBuffer = NO;
    if(_pixelBufferRef != NULL)
    {
        int frameWidth = (int)CVPixelBufferGetWidth(_pixelBufferRef);
        int frameHeight = (int)CVPixelBufferGetHeight(_pixelBufferRef);
        if(frameWidth != bufferWidth || frameHeight != bufferHeight) {
            createBuffer = YES;
        }
    } else {
        createBuffer = YES;
    }
    
    // Create CVPixelBuffer if needed.
    if (createBuffer)
    {
        // 分辨率改变重分配内存
        CVPixelBufferRelease(_pixelBufferRef);
        _pixelBufferRef = NULL;
        
        NSDictionary *pixelAttributes = [NSDictionary dictionaryWithObjectsAndKeys:
                                         [NSDictionary dictionary], (id)kCVPixelBufferIOSurfacePropertiesKey, nil];
        CVReturn ret = CVPixelBufferCreate(kCFAllocatorDefault, bufferWidth, bufferHeight, kCVPixelFormatType_32BGRA,  (__bridge CFDictionaryRef)pixelAttributes, &_pixelBufferRef);
        NSParameterAssert(ret == kCVReturnSuccess && _pixelBufferRef != NULL);
        
        NSLog(@"[MLiveCCPlayer] out pixelbuffer width %d height %d videoWidth %d videoHeight %d rotate %d accelerate %d", bufferWidth, bufferHeight, _videoWidth, _videoHeight, frame->rotate, _useAccelerate);
    }
}

- (void) playWithUrl2:(NSString *)videourl {
    
    if(videourl == nil || [videourl isEqualToString:@""]) {
        return;
    }
    
    if(self.player != nil) {
        if(_retainVideoTexture) {
            [self closeExcludeOutBuffer];
        } else {
            [self close];
        }
    }
    
    if(self.player == nil) {
        
        [self preparePlay];
        
        FrameProcessor *fp = [[FrameProcessor alloc] init];
        fp.onRenderAVFrame = ^(CVPixelBufferRef buf) {
            if(_isClosed) return;
            [self updateAVFrame:buf];
        };
        
        self.player = [[IJKAVMoviePlayerController alloc] initWithContentURLString: videourl withLoadingTimeout:8];
        
        [self.player setPauseInBackground:_pauseInBackground];
        
        [self.player muteAudio:_mute];
        
        [self.player setPlaybackVolume:_volume];
        
        [self.player setPlaybackRate:_playbackRate];
        
        [(IJKAVMoviePlayerController*)self.player setLoop:_loop];
        
        [self installMovieNotificationObservers];
        
        [self.player setFrameProcessor:fp];
        [self.player prepareToPlay];
        [self.player play];
    }
}


- (void) updateAVFrame:(CVPixelBufferRef)pixelbuffer
{
    if(_updatingTexure || _gameUsedTexture == 0) {
        //NSLog(@"[MLiveCCPlayer]: updateTextureInRenderQueue Busy\n");
        return;
    }
    _updatingTexure = YES;
    if(_gameUsedTexture == 1) //说明外部启用了纹理渲染通知接口
        _gameUsedTexture = 0;
    
    CFRetain(pixelbuffer);
    
    size_t width = CVPixelBufferGetWidth(pixelbuffer);
    size_t height = CVPixelBufferGetHeight(pixelbuffer);
    if(_videoWidth != width || _videoHeight != height) {
        _videoWidth = (int)width;
        _videoHeight = (int)height;
    }
    
    BOOL createBuffer = NO;
    if(_pixelBufferRef != NULL)
    {
        int frameWidth = (int)CVPixelBufferGetWidth(_pixelBufferRef);
        int frameHeight = (int)CVPixelBufferGetHeight(_pixelBufferRef);
        if(frameWidth != _videoWidth || frameHeight != _videoHeight) {
            createBuffer = YES;
        }
    } else {
        createBuffer = YES;
    }
    // Create CVPixelBuffer if needed.
    if (createBuffer)
    {
        // 分辨率改变重分配内存
        CVPixelBufferRelease(_pixelBufferRef);
        _pixelBufferRef = NULL;
        NSDictionary *pixelAttributes = [NSDictionary dictionaryWithObjectsAndKeys:
                                              [NSDictionary dictionary], (id)kCVPixelBufferIOSurfacePropertiesKey, nil];
        CVReturn ret = CVPixelBufferCreate(kCFAllocatorDefault, _videoWidth, _videoHeight, kCVPixelFormatType_32BGRA,  (__bridge CFDictionaryRef)pixelAttributes, &_pixelBufferRef);
        NSParameterAssert(ret == kCVReturnSuccess && _pixelBufferRef != NULL);
    }
    
    if(_playQueue != nil) {
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        if(dispatch_get_current_queue() == _playQueue)
#pragma clang diagnostic pop
        {
            //copy
            [self copyPixelBuffer:pixelbuffer];
            [self updateTexture];
            _updatingTexure = NO;
            CVPixelBufferRelease(pixelbuffer);
        } else {
            dispatch_async(_playQueue, ^{
                [self copyPixelBuffer:pixelbuffer];
                [self updateTexture];
                _updatingTexure = NO;
                CVPixelBufferRelease(pixelbuffer);
            });
        }
    }
}

- (void) copyPixelBuffer:(CVPixelBufferRef)pixelbuffer {
    
    if(kCVReturnSuccess == CVPixelBufferLockBaseAddress(pixelbuffer, 0)) {
        uint8_t *baseAddress = CVPixelBufferGetBaseAddress(pixelbuffer);
        int bufferHeight = (int)CVPixelBufferGetHeight(pixelbuffer);
        size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelbuffer);
        CVPixelBufferLockBaseAddress(_pixelBufferRef, 0);
        uint8_t *copyBaseAddress = CVPixelBufferGetBaseAddress(_pixelBufferRef);
        if(baseAddress != 0) {
            memcpy(copyBaseAddress, baseAddress, bufferHeight * bytesPerRow);
        }
        CVPixelBufferLockBaseAddress(_pixelBufferRef, 0);
        CVPixelBufferUnlockBaseAddress(pixelbuffer, 0);
    }
}

- (void) updateInRender {
    
    if(! [self isPlaying])
        return;
    
    _smartFpsEnable = YES;
    
    UInt64 curTime = [[NSDate date] timeIntervalSince1970]*1000;
    
    if(_lastUpdateInRenderTime == 0){
        
    }else{
        
        double diff = curTime - _lastUpdateInRenderTime;
        
        diff = fmin(diff, 1000);
        
        diff = fmax(diff, 10);
        
        _smartInterval  = _smartInterval * 0.7 + diff * 0.3;
        
    }
    
    _lastUpdateInRenderTime = curTime;
}

- (void) lockFameActive
{
    [self.frameLock lock];
}

- (void) unlockFrameActive
{
    [self.frameLock unlock];
}

- (void)toggleFramePaused:(BOOL)paused
{
    [self lockFameActive];
    self.framePaused = paused;
    [self unlockFrameActive];
}

- (void)applicationWillEnterForeground
{
    [self toggleFramePaused:NO];
}

- (void)applicationDidBecomeActive
{
    [self toggleFramePaused:NO];
}

- (void)applicationWillResignActive
{
    [self toggleFramePaused:YES];
}

- (void)applicationDidEnterBackground
{
    [self toggleFramePaused:YES];
}

- (void)applicationWillTerminate
{
    [self toggleFramePaused:YES];
}

- (pthread_t)getReadThreadId{
    IJKFFMoviePlayerController *player = self.player;
    return [player getReadThreadId];
}
@end
