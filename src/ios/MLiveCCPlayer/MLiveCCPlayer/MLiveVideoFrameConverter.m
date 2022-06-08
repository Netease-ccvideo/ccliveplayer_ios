//
//  MLiveVideoFrameConverter.m
//  MLiveCCPlayer
//
//  Created by jlubobo on 2019/8/7.
//  Copyright © 2019 cc. All rights reserved.
//

#import "MLiveVideoFrameConverter.h"
#import "libyuv.h"
#import "MLiveDefines.h"

@interface MLiveVideoFrameConverter()
{
    vImage_Buffer argbBuff;
}

@property (nonatomic, assign) vImage_YpCbCrToARGB *conversionInfo;
@property (nonatomic, assign) BOOL supportsAccelerate;

@end

@implementation MLiveVideoFrameConverter

- (instancetype) initWithAccelerate:(BOOL)accelerate {
    self = [super init];
    if (self) {
        _supportsAccelerate = accelerate;
        [self prepareForAccelerateConversion];

    }
    return self;
}

- (vImage_Error)prepareForAccelerateConversion
{
    // Setup the YpCbCr to ARGB conversion.
    
    if (_conversionInfo != NULL) {
        return kvImageNoError;
    }
//    vImage_YpCbCrPixelRange pixelRange = { 0, 128, 255, 255, 255, 1, 255, 0 };
    vImage_YpCbCrPixelRange pixelRange = { 16, 128, 235, 240, 235, 16, 240, 16 };
    vImage_YpCbCrToARGB *outInfo = malloc(sizeof(vImage_YpCbCrToARGB));
    vImageYpCbCrType inType = kvImage420Yp8_Cb8_Cr8;
    vImageARGBType outType = kvImageARGB8888;
    
    vImage_Error error = vImageConvert_YpCbCrToARGB_GenerateConversion(kvImage_YpCbCrToARGBMatrix_ITU_R_601_4, &pixelRange, outInfo, inType, outType, kvImagePrintDiagnosticsToConsole);
    _conversionInfo = outInfo;
    
    return error;
}

- (void)unprepareForAccelerateConversion
{
    if (_conversionInfo != NULL) {
        free(_conversionInfo);
        _conversionInfo = NULL;
    }
}

- (void)dealloc
{
    [self unprepareForAccelerateConversion];
}


- (void)convertFrame:(MLiveCCVideoFrame *)frame toBuffer:(CVPixelBufferRef)pixelBufferRef {
    vImage_Error ret = kvImageNoError;
    if(frame->format == DECODE_FORMAT_I420)
         ret = [self convertFrameVImageYUV:frame toBuffer:pixelBufferRef];
    else if(frame->format == DECODE_FORMAT_VTB)
        ret = [self convertFrameVImageNV12:frame toBuffer:pixelBufferRef];
    if(ret != kvImageNoError) {
        NSLog(@"[MLiveCCPlayer] convertFrame err %zd", ret);
    }
}

// premutemap nil --> kCGImageAlphaPremultipliedLast | kCGImageByteOrder32Little abgr ok
//permuteMap[4] = {3, 2, 1, 0} --> kCGImageAlphaPremultipliedLast  rgba ok

// AlphaFirst – the alpha channel is next to the red channel, argb and bgra are both alpha first formats.
// AlphaLast – the alpha channel is next to the blue channel, rgba and abgr are both alpha last formats.
// LittleEndian – blue comes before red, bgra and abgr are little endian formats.
// Little endian ordered pixels are BGR (BGRX, XBGR, BGRA, ABGR, BGR).
// BigEndian – red comes before blue, argb and rgba are big endian formats.
// Big endian ordered pixels are RGB (XRGB, RGBX, ARGB, RGBA, RGB).
//    if alphaFirst && endianLittle {
//        return .bgr a 0
//    } else if alphaFirst {
//        return .a rgb 0
//    } else if alphaLast && endianLittle {
//        return .a bgr 1
//    } else if alphaLast {
//        return .rgb a 1
//    }


- (vImage_Error)convertFrameVImageYUV:(MLiveCCVideoFrame *)frame toBuffer:(CVPixelBufferRef)pixelBufferRef
{
    vImage_Error convertError = kvImageNoError;
    
    int width = frame->w;
    int height = frame->h;
    
    if(_supportsAccelerate) {
        
        vImagePixelCount subsampledWidth = width ;
        vImagePixelCount subsampledHeight = frame->h ;
        
        const uint8_t *yPlane = frame->pixels[0];
        const uint8_t *uPlane = frame->pixels[1];
        const uint8_t *vPlane = frame->pixels[2];
        
        size_t yStride = (size_t)frame->pitches[0];
        size_t uStride = (size_t)frame->pitches[1];
        size_t vStride = (size_t)frame->pitches[2];
        
        // Create vImage buffers to represent each of the Y, U, and V planes
        vImage_Buffer yPlaneBuffer = {.data = (void *)yPlane, .height = height, .width = width, .rowBytes = yStride};
        vImage_Buffer uPlaneBuffer = {.data = (void *)uPlane, .height = subsampledHeight, .width = subsampledWidth, .rowBytes = uStride};
        vImage_Buffer vPlaneBuffer = {.data = (void *)vPlane, .height = subsampledHeight, .width = subsampledWidth, .rowBytes = vStride};
        
        // Create a vImage buffer for the destination pixel buffer.
        void *pixelBufferData = CVPixelBufferGetBaseAddress(pixelBufferRef);
        size_t rowBytes = CVPixelBufferGetBytesPerRow(pixelBufferRef);
        
        int rotationConstant = 0; //只处理90/270
        //        *    rotationConstant:
        //            kRotate0DegreesClockwise = 0,
        //            kRotate90DegreesClockwise = 3,
        //            kRotate180DegreesClockwise = 2,
        //            kRotate270DegreesClockwise = 1,
        //
        //            kRotate0DegreesCounterClockwise = 0,
        //            kRotate90DegreesCounterClockwise = 1,
        //            kRotate180DegreesCounterClockwise = 2,
        //            kRotate270DegreesCounterClockwise = 3
        
        //            vImageVerticalReflect_ARGB8888(&src, &dest, kvImageBackgroundColorFill);
        //            vImageHorizontalReflect_ARGB8888(&src, &dest, kvImageBackgroundColorFill);
        switch (frame->rotate) {
            case 90:
                rotationConstant = 3;
                break;
            case 270:
                rotationConstant = 1;
                break;
            default:
                break;
        }
        
        uint8_t permuteMap[4] = { 3, 2, 1, 0}; // BGRA
        if(rotationConstant == 0) {
            vImage_Buffer destinationImageBuffer = {.data = pixelBufferData, .height = height, .width = width, .rowBytes = rowBytes};
            // Do the conversion.
            convertError = vImageConvert_420Yp8_Cb8_Cr8ToARGB8888(&yPlaneBuffer, &uPlaneBuffer, &vPlaneBuffer,  &destinationImageBuffer, self.conversionInfo, permuteMap, 255, kvImageNoFlags);
          
        } else {
            // rotate if need
            // convert
            if(argbBuff.height != height || argbBuff.width != width) {
                if (argbBuff.data != NULL) free(argbBuff.data);
                argbBuff.height = height;
                argbBuff.width = width;
                argbBuff.rowBytes = width * 4;
                vImage_Error init = vImageBuffer_Init(&argbBuff, height, width, 32, kvImageNoFlags);
                if (init != 0) {
                    NSLog(@"[MLiveCCPlayer] vImageBuffer_Init failed");
                    return kvImageMemoryAllocationError;
                }
            }
            convertError = vImageConvert_420Yp8_Cb8_Cr8ToARGB8888(&yPlaneBuffer, &uPlaneBuffer, &vPlaneBuffer,  &argbBuff, self.conversionInfo, permuteMap, 255, kvImageNoFlags);
            //rotate
            double a2 = CFAbsoluteTimeGetCurrent();
            uint8_t bgColor[4]                  = {0, 0, 0, 0};
            vImage_Buffer destinationImageBuffer = {.data = pixelBufferData, .height = width, .width = height, .rowBytes = rowBytes};
            convertError = vImageRotate90_ARGB8888(&argbBuff, &destinationImageBuffer, rotationConstant, bgColor, 0);
            double b = CFAbsoluteTimeGetCurrent();
//            NSLog(@"[MLiveCCPlayer] scale take %f ms", (b - a2) * 1000);
            
            //        int bytes_per_pix = 4;
            //        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
            //        CGContextRef Context = CGBitmapContextCreate(destinationImageBuffer.data,width, height, 8, width * bytes_per_pix,colorSpace, kCGImageByteOrder32Little | kCGImageAlphaPremultipliedFirst);
            //        CGImageRef frame2 = CGBitmapContextCreateImage(Context);
            //        UIImage *image = [UIImage imageWithCGImage:frame2];
            //        CGImageRelease(frame2);
            //        CGContextRelease(Context);
            //        CGColorSpaceRelease(colorSpace);
        }

    } else {
        int bufferWidth = ((width % 16 == 0) ? width : (width / 16 + 1) * 16);
        uint8_t *baseAddress = CVPixelBufferGetBaseAddress(pixelBufferRef);
        if(baseAddress != NULL) {
            I420ToARGB(frame->pixels[0], frame->pitches[0], frame->pixels[1], frame->pitches[1], frame->pixels[2], frame->pitches[2], baseAddress, bufferWidth * 4, width, height);
        }
    }
    
    return convertError;
}

- (vImage_Error)convertFrameVImageNV12:(MLiveCCVideoFrame *)frame toBuffer:(CVPixelBufferRef)pixelBufferRef
{
    vImage_Error convertError = kvImageNoError;
    int width = frame->w;
    int height = frame->h;

    if(_supportsAccelerate) {

        void* lumaBaseAddress = frame->pixels[0];
        vImagePixelCount lumaWidth = frame->planeWidths[0];
        vImagePixelCount lumaHeight = frame->planeHeights[0];
        vImagePixelCount lumaRowBytes = frame->pitches[0];
        vImage_Buffer sourceLumaBuffer = {.data = lumaBaseAddress,.height = lumaHeight, .width = lumaWidth, .rowBytes = lumaRowBytes};
        
        void* chromaBaseAddress = frame->pixels[1];
        vImagePixelCount chromaWidth = frame->planeWidths[1];
        vImagePixelCount chromaHeight =frame->planeHeights[1];
        vImagePixelCount chromaRowBytes = frame->pitches[1];
        
        vImage_Buffer sourceChromaBuffer = {.data = chromaBaseAddress, .height = chromaHeight, .width = chromaWidth, .rowBytes = chromaRowBytes};
//
        void *pixelBufferData = CVPixelBufferGetBaseAddress(pixelBufferRef);
        size_t rowBytes = CVPixelBufferGetBytesPerRow(pixelBufferRef);
  
        uint8_t permuteMap[4] = {3, 2, 1, 0}; // BGRA
        
        int rotationConstant = 0;
        switch (frame->rotate) {
            case 90:
                rotationConstant = 3;
                break;
            case 270:
                rotationConstant = 1;
                break;
            default:
                break;
        }
        if(rotationConstant == 0) {
            vImage_Buffer destinationImageBuffer = {.data = pixelBufferData, .height = height, .width = width, .rowBytes = rowBytes};
            convertError = vImageConvert_420Yp8_CbCr8ToARGB8888(&sourceLumaBuffer, &sourceChromaBuffer, &destinationImageBuffer, self.conversionInfo, permuteMap, 255, kvImageNoFlags);
        } else {
            // convert
            if(argbBuff.height != height || argbBuff.width != width) {
                if (argbBuff.data != NULL) free(argbBuff.data);
                argbBuff.height = height;
                argbBuff.width = width;
                argbBuff.rowBytes = width * 4;
                vImage_Error init = vImageBuffer_Init(&argbBuff, height, width, 32, kvImageNoFlags);
                if (init != 0) {
                    NSLog(@"[MLiveCCPlayer] vImageBuffer_Init failed");
                    return kvImageMemoryAllocationError;
                }
            }
            convertError = vImageConvert_420Yp8_CbCr8ToARGB8888(&sourceLumaBuffer, &sourceChromaBuffer, &argbBuff, self.conversionInfo, permuteMap, 255, kvImageNoFlags);
            //rotate
            double a2 = CFAbsoluteTimeGetCurrent();
            uint8_t bgColor[4]                  = {0, 0, 0, 0};
            vImage_Buffer destinationImageBuffer = {.data = pixelBufferData, .height = width, .width = height, .rowBytes = rowBytes};
            convertError = vImageRotate90_ARGB8888(&argbBuff, &destinationImageBuffer, rotationConstant, bgColor, 0);
            double b = CFAbsoluteTimeGetCurrent();
//            NSLog(@"[MLiveCCPlayer] scale take %f ms", (b - a2) * 1000);
        }
        
    } else {
        uint8_t *baseAddress = CVPixelBufferGetBaseAddress(pixelBufferRef);
        int bufferWidth = ((width % 16 == 0) ? width : (width / 16 + 1) * 16);
        if(baseAddress != NULL) {
            //NV12ToARGB
            NV12ToARGB(frame->pixels[0],frame->pitches[0],frame->pixels[1], frame->pitches[1],baseAddress,bufferWidth * 4,width,height);
        }
    }
    
    return convertError;
}

@end
