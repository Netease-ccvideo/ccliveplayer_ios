#import "CCVMetalDef.h"
#import "AAPLQuad.h"
#ifdef MTL_ENABLE
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CVPixelBuffer.h>
#import <CoreVideo/CVMetalTextureCache.h>
#import <CoreVideo/CVMetalTexture.h>
#import <Metal/Metal.h>

@interface CCVMetalCacheTexture : NSObject
-(void) initCache:(id <MTLDevice>) metalDevice withSize:(CGSize) size;

-(void) encodeToCommandBuffer:(id<MTLCommandBuffer>) commandBuffer sourceTexture:(id<MTLTexture>) src;
-(void) encodeToCommandBuffer:(id<MTLCommandBuffer>) commandBuffer sourcePixelBuffer:(CVPixelBufferRef) src;
-(CVPixelBufferRef) getPixelBuffer;

-(void) setRotation:(int) rotation;

-(void)  Clear;

@end
#endif
