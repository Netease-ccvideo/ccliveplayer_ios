//
//  MediaMovieWriter.m
//  Peertalk Example
//
//  Created by cc on 2020/9/23.
//

#import "CCMLiveMovieWriter.h"
#import <AVFoundation/AVFoundation.h>
//#import <AssetsLibrary/AssetsLibrary.h>

@interface CCMLiveMovieWriter()
{
    int m_nImageHeight;
    int m_nImageWidth;
    CFTimeInterval  m_cfFirstTimeStamp;
    CMTime          m_cmPreviousFrameTime;
}
@property (nonatomic, strong) AVAssetWriter *pAssetWriter;;
@property (nonatomic, strong) AVAssetWriterInput *pAssetWriterVideoInput;
@property (nonatomic, strong) AVAssetWriterInputPixelBufferAdaptor *pAssetWriterPixelBufferInput;
@property (nonatomic, strong) NSString *videoURL;
@end

size_t fixedLineValue(size_t sourceValue){
    size_t divide = sourceValue%4;
    size_t finalValue = sourceValue;
    if (divide) {
        finalValue=(sourceValue/4+1)*4;
    }
    return finalValue;
}

@implementation CCMLiveMovieWriter

-(id)initWithURL:(NSURL *)outputURL quality:(int) quality size:(CGSize)imageSize
{
    if (![super init]) {
        return nil;
    }
    _movieQuality = quality;
    m_nImageWidth = imageSize.width;
    m_nImageHeight = imageSize.height;
    [self InitVideoWriter:outputURL];
    return self;
}

-(int)InitVideoWriter:(NSURL *)outputURL
{
    NSFileManager* fileManager = [NSFileManager defaultManager];
    NSError *error = nil;
    if(outputURL != nil) {
        if ([fileManager fileExistsAtPath:[outputURL path]]) {
            [fileManager removeItemAtPath:[outputURL path] error:&error];
            NSLog(@"remove file %@", [outputURL path]);
        }
    }
    self.pAssetWriter = [[AVAssetWriter alloc] initWithURL:outputURL fileType:AVFileTypeQuickTimeMovie error:&error];
    self.pAssetWriter.movieFragmentInterval = CMTimeMakeWithSeconds(1.0, 1000);
    NSLog(@"[params] InitVideoWriter pixelbufferattr w %d h %d",m_nImageWidth, m_nImageHeight);
    _pAssetWriterVideoInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:[self GetEncodeParam]];
    _pAssetWriterVideoInput.expectsMediaDataInRealTime = TRUE;
    
    NSDictionary *sourcePixelBufferAttributesDictionary = [NSDictionary dictionaryWithObjectsAndKeys: [NSNumber numberWithInt:kCVPixelFormatType_32BGRA], kCVPixelBufferPixelFormatTypeKey,
                                                           [NSNumber numberWithInt:m_nImageWidth], kCVPixelBufferWidthKey,
                                                           [NSNumber numberWithInt:m_nImageHeight], kCVPixelBufferHeightKey,
                                                           nil];
    
    _pAssetWriterPixelBufferInput = [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:_pAssetWriterVideoInput sourcePixelBufferAttributes:sourcePixelBufferAttributesDictionary];
    
    if([_pAssetWriter canAddInput:_pAssetWriterVideoInput])
        [_pAssetWriter addInput:_pAssetWriterVideoInput];
    else
        NSLog(@"Counld Not Add Video Input");
    
    return 0;
    
}

-(void)StartWriter{
    
    m_cfFirstTimeStamp = 0;
    [_pAssetWriter startWriting];
    [_pAssetWriter startSessionAtSourceTime:CMTimeMakeWithSeconds(0, 1000)];
    NSLog(@"[Writer-Start] Start done");
}

-(void)StopWriter{
    
    if (_pAssetWriter.status == AVAssetWriterStatusCompleted || _pAssetWriter.status == AVAssetWriterStatusCancelled || _pAssetWriter.status == AVAssetWriterStatusUnknown)
    {
        
        return ;
    }
    [_pAssetWriter finishWriting];
    return ;
}


-(NSDictionary*) GetEncodeParam{

    if(m_nImageHeight <= 0)
        return nil;

    int mWidth,mHeight,bitrate;

    switch (_movieQuality) {
        case 0:
            mWidth  = 640;
            mHeight = 640;
            bitrate = 800 * 1024;
            break;
        case 1:
            mWidth  = 720;
            mHeight = 720;
            bitrate = 1200 * 1024;
            break;
        case 2:
            mWidth  = 960;
            mHeight = 960;
            bitrate = 1500 * 1024;
            break;
        case 3:
            mWidth  = 1280;
            mHeight = 1280;
            bitrate = 2000 * 1024;
            break;
        default:
            mWidth  = 720;
            mHeight = 720;
            bitrate = 1200 * 1024;
            break;
    }
    // fix to screen size
    if(m_nImageWidth > m_nImageHeight){
        if(mWidth >= m_nImageWidth){
            mWidth = m_nImageWidth;
            mHeight = m_nImageHeight;
        }else{
            mHeight = mWidth * m_nImageHeight / m_nImageWidth;
            if(mHeight > m_nImageHeight)
                mHeight = m_nImageHeight;
        }
    }else{
        if(mWidth >= m_nImageWidth){
            mWidth = m_nImageWidth;
            mHeight = m_nImageHeight;
        }else{
            mWidth = mHeight * m_nImageWidth / m_nImageHeight;
            if(mWidth > m_nImageWidth)
                mWidth = m_nImageWidth;
        }
    }

    NSDictionary* videoCompression = @{AVVideoAverageBitRateKey : [NSNumber numberWithInteger:bitrate],
                                       AVVideoProfileLevelKey : AVVideoProfileLevelH264MainAutoLevel,
                                        AVVideoMaxKeyFrameIntervalDurationKey:[NSNumber numberWithInteger:2]
                                       };
    NSDictionary* outputSettings = @{AVVideoCodecKey: AVVideoCodecTypeH264,
                                     AVVideoWidthKey: [NSNumber numberWithInt:fixedLineValue(mWidth)],
                                     AVVideoHeightKey: [NSNumber numberWithInt:fixedLineValue(mHeight)],
                                     AVVideoCompressionPropertiesKey: videoCompression};

    NSLog(@"movie w:%d h:%d, bitrate:%d",mWidth,mHeight,bitrate);

    return outputSettings;
}

-(int) EncodeVideoFrame:(CVPixelBufferRef) cvpixel
{
    if(m_cfFirstTimeStamp == 0)
        m_cfFirstTimeStamp = CFAbsoluteTimeGetCurrent();
    
    CMTime frameTime = CMTimeMakeWithSeconds(CFAbsoluteTimeGetCurrent() - m_cfFirstTimeStamp, 1000);
    
    if ( (CMTIME_IS_INVALID(frameTime)) || (CMTIME_COMPARE_INLINE(frameTime, ==, m_cmPreviousFrameTime)) || (CMTIME_IS_INDEFINITE(frameTime)) ) {
        NSLog(@"EncodeVideoFrame frame time invalid 0");
        return -1;
    }
    
    if (!_pAssetWriterVideoInput.readyForMoreMediaData)
    {
        NSLog(@"1: Had to drop a video frame: %@", CFBridgingRelease(CMTimeCopyDescription(kCFAllocatorDefault, frameTime)));
        return -2;
    }
    int code = 0;
    if(_pAssetWriter.status == AVAssetWriterStatusWriting){
        @try {
            if (![_pAssetWriterPixelBufferInput appendPixelBuffer:cvpixel withPresentationTime:frameTime]){
                NSLog(@"Problem appending pixel buffer at time: %@, %@", CFBridgingRelease(CMTimeCopyDescription(kCFAllocatorDefault, frameTime)),[_pAssetWriter.error localizedDescription]);
            }else{
//                NSLog(@"EncodeVideoFrame Video time: %@", CFBridgingRelease(CMTimeCopyDescription(kCFAllocatorDefault, frameTime)));
            }
        }
        @catch (NSException *exception) {
            NSLog(@"appendPixelBuffer error %@", exception);
        }
    }
    else{
        NSString *description = _pAssetWriter.error.localizedDescription;
        NSString *reason = _pAssetWriter.error.localizedFailureReason;
        NSString *errorMessage = [NSString stringWithFormat:@"%@ %@", description, reason];
        
        NSLog(@"status != AVAssetWriterStatusWriting Drop Video %ld %@", (long)_pAssetWriter.status,errorMessage);
        code = -3;
    }
//    NSLog(@"EncodeVideoFrame frame success");
    m_cmPreviousFrameTime = frameTime;
    return code;
}

@end
