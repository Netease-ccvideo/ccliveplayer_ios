//
//  MLiveCCPlayer.h
//  MLiveCCPlayer
//  v:505
//  Created by cc on 16/9/8.
//  Copyright © 2016年 cc. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import "MLiveVideoFrame.h"

typedef void (^OnVideoUpdated_t)();
typedef void (^OnVideoInfo_t)(NSArray *vbrList, NSString *curVbr,NSInteger selFreeFlow);
typedef void (^OnVideoSubtitle_t)(NSString *subtitleStr);
typedef void (^OnVideoUpdated_o)(MLiveVideoFrame* frame);
typedef void (^MediaDataSourceReadBlock)(unsigned char *data, int dataSize, int fd);

typedef enum MLiveCCPlayerLogLevel {
    k_CC_LOG_UNKNOWN = 0,
    k_CC_LOG_DEFAULT = 1,
    k_CC_LOG_VERBOSE = 2,
    k_CC_LOG_DEBUG   = 3,
    k_CC_LOG_INFO    = 4,
    k_CC_LOG_WARN    = 5,
    k_CC_LOG_ERROR   = 6,
    k_CC_LOG_FATAL   = 7,
    k_CC_LOG_SILENT  = 8,
} MLiveCCPlayerLogLevel;

typedef enum MLiveCCPlayerOutType {
    k_OUT_OPENGL_TEXTURE = 0,
    k_OUT_PIXEL_BUFFREF = 1,
} MLiveCCPlayerOutType;

extern NSString *const MLivePlayerIsPreparedToPlayNotification; // 播放器加载完成
extern NSString *const MLivePlayerDidFinishNotification; // 播放结束:正常结束/异常结束
extern NSString *const MLivePlayerStateDidChangeNotification; // 播放状态变化:暂停h/播放中
extern NSString *const MLivePlayerRestoreVideoPlay;// 开始播放/从暂停状态下恢复播放
extern NSString *const MLivePlayerBufferingUpdateNotification;// 缓冲中
extern NSString *const MLivePlayerVideoDecoderOpenNotification; // 解码器选择通知
extern NSString *const MLivePlayerSeekCompletedNotification; // 拖动播放完成通知
extern NSString *const MLivePlayerVideoCacheNotification;//缓存完毕通知

@interface MLiveCCPlayer : NSObject

- (id) init;

//返回方式选择，默认为返回opengl texture
- (id) initWithOutType:(MLiveCCPlayerOutType)type;

- (void) playWithUrl:(NSString*)videourl;

- (void) playWithUrl:(NSString*)videoUrl savePath:(NSString *)savePath;

- (void) playWithVodUrl:(NSString*)vodurl withoffset:(NSInteger)offset length:(NSInteger)length;

// 使用系统播放器AVPlayer播放点播视频
- (void) playWithUrl2:(NSString *)videourl;

- (void) close;

- (void) setLogLevel:(MLiveCCPlayerLogLevel)level;

//播放控制
- (void) play;
- (void) pause;
- (void) stop;
- (BOOL) isPlaying;
- (BOOL) isUsingHardDecoder;
- (void) muteAudio:(BOOL)mute;
- (void) reActiveAudio;
- (void) displaySubtitle:(BOOL)enable;
- (BOOL) changeDirection:(int) direct;

//播放时间戳信息,单位为秒
- (NSTimeInterval) playDuration;
- (NSTimeInterval) curPlaybackTime;
- (void) setPlaybackTime:(NSTimeInterval)value;

/*
 texture和pixelBufferRef的图像宽度可能比视频实际宽度略大（右边可能有黑边），
 显示时以videoWidht为准，只显示[0,videoWidth]部分
 */

/* 用于sdk检测游戏帧率*/
- (void) updateInRender;

/* 用于视频图像数据pixelBufferRef/texture真正被render之后告知sdk*/
- (void) afterRenderVideoBuffer;

/* 设置所有播放参数*/
- (void) configPlayerSettings:(NSString*)settings;

/* 自定义协议数据读取回调 */
- (void)setMediaDataSourceReadBlock:(MediaDataSourceReadBlock)mediaDataReadBlock;

/* 获取读线程id，可以用id判定hook的回调数据是否属于当前播放器 */
- (pthread_t)getReadThreadId;

@property (nonatomic, readonly) GLint texture;
@property (nonatomic, readonly) CVPixelBufferRef pixelBufferRef;
@property (nonatomic, readonly) MLiveCCPlayerOutType outType;
@property (nonatomic, readonly) int videoWidth;
@property (nonatomic, readonly) int videoHeight;
@property (nonatomic, readonly) NSString* vbrname;
@property (nonatomic, readonly) NSArray *vbrList;
@property (nonatomic) float volume; // [0, 1.0f]
@property (nonatomic) float playbackRate;//[0.5 - 2.0f] defalut 1.0f recommend [0.5 0.75 1 1.25 1.5 ..]
@property (nonatomic, assign) BOOL pauseInBackground;

@property (nonatomic, assign) BOOL radicalRealTime;
@property (nonatomic, assign) BOOL useDefaultJitter;//NO
@property (nonatomic, assign) BOOL useCellJitter;//NO
@property (nonatomic, strong) OnVideoUpdated_t  OnVideoUpdate;
@property (nonatomic, strong) OnVideoInfo_t     OnVideoInfo;
@property (nonatomic, strong) OnVideoSubtitle_t OnVideoSubtitle;
@property (nonatomic, copy) OnVideoUpdated_o OnVideoUpdate_o;

@property (nonatomic, assign) BOOL useHardDecoder;
@property (nonatomic, assign) BOOL enableAutoIdelTimer;
@property (nonatomic, assign) NSUInteger loop;//播放次数控制,默认1:不循环 0:无限循环
@property (nonatomic, assign) BOOL liteMode;// 支持低性能设备播高比特率视频
@property (nonatomic, assign) BOOL autoPlay;// 播放器打开流后是否自动播放，默认YES
@property (nonatomic, assign) BOOL ignoreSilenceMode;// 忽视静音模式，默认为YES
@property (nonatomic, assign) BOOL logEnable; // 是否开启日志
@property (nonatomic, assign) BOOL useRenderBuffer; // 是否使用渲染缓存方式
@property (nonatomic, assign) BOOL useAccelerate;// ios8以上支持，默认NO
@property (nonatomic, assign) BOOL useAccurateSeek;// 是否开启精准拖动播放，默认NO
@property (nonatomic, assign) BOOL enableSmoothLoop;// 是否开启无缝循环播放，默认NO
@property (nonatomic, assign) BOOL retainVideoTexture;// 是否在未执行close func前都保持视频纹理, 默认NO
@property (nonatomic, copy) NSString *videoCache;  // 边下边播缓存地址
@property (nonatomic, assign) BOOL useMovieWriter;// 是否保存播放视频，默认为NO，调试用
@property (nonatomic, assign) BOOL useMaxProbesize;// 解析m3u8用，默认为YES
@property (nonatomic, assign) BOOL useSoundtouch; // 支持4倍速度，默认为NO
@end
