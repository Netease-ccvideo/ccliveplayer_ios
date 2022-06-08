//
//  CCPixelBufferConversion.m
//  MLiveCCPlayer
//
//  Created by jlubobo on 2018/3/13.
//  Copyright © 2018年 cc. All rights reserved.
//

#import "CCPixelBufferConversion.h"
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>
#import "CCVMetalCacheTexture.h"

@interface CCPixelBufferConversion()
{
    id<MTLDevice> mtlDevice;
    id<MTLCommandQueue> mtlQueue;
    CCVMetalCacheTexture* mtlScaleCache;
}
@end

@implementation CCPixelBufferConversion

-(CVPixelBufferRef) convertBuffer:(CVPixelBufferRef)srcBuffer
{
    if(!srcBuffer)
        return nil;
    
    if(!mtlDevice){
        mtlDevice = MTLCreateSystemDefaultDevice();
        mtlQueue = [mtlDevice newCommandQueue];
        
    }
    
    if(!mtlScaleCache) {
        mtlScaleCache = [CCVMetalCacheTexture alloc];
        [mtlScaleCache initCache:mtlDevice withSize:self.outputSize];
    }
    
    id<MTLCommandBuffer> buffer = [mtlQueue commandBuffer];
    //    id<MTLCommandBuffer> buffer = [mtlQueue commandBuffer];
    //[mtlScaleCache setRotation:self.rotate];
    [mtlScaleCache encodeToCommandBuffer:buffer sourcePixelBuffer:srcBuffer];
    [buffer commit];
    
    [buffer waitUntilCompleted];
    buffer = nil;
    
    return [mtlScaleCache getPixelBuffer];
}

- (void) clear {
    
    if(mtlScaleCache) {
        
        [mtlScaleCache Clear];
    }
    
    if(mtlDevice)
        mtlDevice = nil;
}

@end
