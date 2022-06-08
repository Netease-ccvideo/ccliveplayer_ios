#include "CCVMetalCacheTexture.h"
//#include "CCVMetalLogoShader.h"
#import <simd/simd.h>
#include <sys/time.h>
#include <UIKit/UIKit.h>
#include <CoreGraphics/CoreGraphics.h>


#ifdef MTL_ENABLE
const CFStringRef kCVPixelBufferOpenGLESCompatibilityKey = CFSTR("OpenGLESCompatibility");
char* imagedata = NULL;
int count = 0;

namespace CCVideo {
    
    static const char LogoShader[] = {
        "#include <metal_stdlib> \n"
        "using namespace metal; \n"
        
        "struct VertexInput { \n"
        "float4 position [[attribute(0)]]; \n"
        "float2 texCoords [[attribute(1)]]; \n"
        // "float4 color [[attribute(1)]]; \n"
        "}; \n"
        
        "struct VertexOutput { \n"
        "float4 position [[position]]; \n"
        "float2 texCoords; \n"
        //"float4 color; \n"
        "}; \n"
        
        "vertex VertexOutput basic_vertex(VertexInput in [[stage_in]]) {  \n"
        "VertexOutput out; \n"
        "out.position = in.position; \n"
        "out.texCoords = in.texCoords; \n"
        "return out; \n"
        "} \n"
        
        "fragment float4 basic_fragment(VertexOutput in [[stage_in]], texture2d<float> inTexture [[texture(0)]], sampler samplr [[sampler(0)]]) { \n"
        
        "float4 color = inTexture.sample(samplr, in.texCoords); \n"
        "return color;\n"
        "} \n"
    };
    
    static char TestShader[] {
        /*
         Copyright (C) 2015 Apple Inc. All Rights Reserved.
         See LICENSE.txt for this sample’s licensing information
         
         Abstract:
         Textured quad vertex and fragment shaders.
         */
        
        "#include <metal_stdlib>\n"
        
        "using namespace metal;\n"
        
        // Vertex input/output structure for passing results
        // from a vertex shader to a fragment shader
        "struct VertexIO\n"
        "{\n"
        "    float4 m_Position [[position]];\n"
        "    float2 m_TexCoord [[user(texturecoord)]];\n"
        "};\n"
        
        // Vertex shader for a textured quad
        "vertex VertexIO basic_vertex(device float4 *pPosition   [[ buffer(0) ]],device packed_float2  *pTexCoords  [[ buffer(1) ]], constant float4x4     &mvp  [[ buffer(2) ]], uint  vid [[ vertex_id ]])\n"
        "{\n"
        
        "    VertexIO outVertices;\n"
        "    outVertices.m_Position = mvp * pPosition[vid];\n"
        "    outVertices.m_TexCoord = pTexCoords[vid];\n"
        "    return outVertices;\n"
        " }\n"
        
        // Fragment shader for a textured quad
        "fragment half4 basic_fragment(VertexIO inFrag [[ stage_in ]], texture2d<half>  tex2D   [[ texture(0) ]], sampler quadSampler [[sampler(0)]])\n"
        "{\n"
        //"    constexpr sampler quadSampler;\n"
        "    half4 color = tex2D.sample(quadSampler, inFrag.m_TexCoord);\n"
        "    return color;\n"
        "}\n"
        
    };
    
    static char Yuv2RGBA [] = {
        "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        
        
        "kernel void YCbCrColorConversion(texture2d<float, access::read> yTexture [[texture(0)]],\n"
        "                                 texture2d<float, access::read> cbcrTexture [[texture(1)]],\n"
        "                                texture2d<float, access::write> outTexture [[texture(2)]],\n"
        "                                 uint2 gid [[thread_position_in_grid]])\n"
        "{\n"
        "    float3 colorOffset = float3(-(16.0/255.0), -0.5, -0.5);\n"
        "    float3x3 colorMatrix = float3x3(\n"
        "                                    float3(1.164,  1.164, 1.164),\n"
        "                                    float3(0.000, -0.392, 2.017),\n"
        "                                    float3(1.596, -0.813, 0.000)\n"
        "                                    );\n"
        
        "    uint2 cbcrCoordinates = uint2(gid.x / 2, gid.y / 2);\n"
        
        "    float y = yTexture.read(gid).r;\n"
        "    float2 cbcr = cbcrTexture.read(cbcrCoordinates).rg;\n"
        
        "    float3 ycbcr = float3(y, cbcr);\n"
        
        "    float3 rgb = colorMatrix * (ycbcr + colorOffset);\n"
        
        "    outTexture.write(float4(float3(rgb), 1.0), gid);\n"
        " }\n"
        
    };
}


void dump(id<MTLTexture> src, NSString* name){
    
    if(imagedata == NULL){
        imagedata = new char[src.width * src.height * 4];
    }
    if(count ++ % 20 != 0){
        return;
    }
    [src getBytes:imagedata bytesPerRow:src.width * 4 fromRegion:MTLRegionMake2D(0, 0, src.width, src.height) mipmapLevel:0];
    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL,
                                                              imagedata,
                                                              src.width * src.height * 4,
                                                              NULL);
    
    int bitsPerComponent = 8;
    int bitsPerPixel = 32;
    int bytesPerRow = 4*src.width;
    CGColorSpaceRef colorSpaceRef = CGColorSpaceCreateDeviceRGB();
    CGBitmapInfo bitmapInfo = kCGBitmapByteOrderDefault;
    CGColorRenderingIntent renderingIntent = kCGRenderingIntentDefault;
    CGImageRef imageRef = CGImageCreate(src.width,
                                        src.height,
                                        8,
                                        32,
                                        4*src.width,colorSpaceRef,
                                        bitmapInfo,
                                        provider,NULL,NO,renderingIntent);
    /*I get the current dimensions displayed here */
    
    UIImage *newImage = [UIImage imageWithCGImage:imageRef];
    
    NSString* path = [NSTemporaryDirectory() stringByAppendingPathComponent:name];
    [UIImagePNGRepresentation(newImage) writeToFile:path atomically:YES];
    
}

typedef struct {
    simd::float4 position;
    simd::float2 texCoords;
} Vertex;

@implementation CCVMetalCacheTexture
{
    id <MTLCommandQueue>        m_CCGMTLCommandQueue;
    CVPixelBufferRef            m_RenderTarget;
    CVMetalTextureRef           m_RenderTexture;
    CVMetalTextureCacheRef      m_RenderCache;
    MTLRenderPassDescriptor*    m_RenderPass;
    
    id<MTLDevice>               m_MTLDevice;
    AAPLQuad*                   m_APPSrcLQuad;
    
    id <MTLDepthStencilState>   m_MTLDepthState;
    
    id<MTLSamplerState>         m_MTLSampleState;
    id<MTLRenderPipelineState>  m_MTLPipeline;
    
    int                         m_nTextureWdith;
    int                         m_nTextureHeight;
    
    CVMetalTextureCacheRef      m_YRenderCache;
    CVMetalTextureCacheRef      m_CbCrRenderCache;
    
    
    id<MTLComputePipelineState>  m_YUV2RGBPipeline;
    CVPixelBufferRef            m_RenderBGRATarget;
    CVMetalTextureRef           m_RenderBGRATexture;
    CVMetalTextureCacheRef      m_RenderBGRACache;
}



-(void)  Clear {

    if(m_RenderBGRATarget != NULL)
        CVPixelBufferRelease(m_RenderBGRATarget);
    
    if(m_RenderBGRATexture != NULL)
        CVBufferRelease(m_RenderBGRATexture);
    
    if(m_RenderBGRACache != NULL)
        CFRelease(m_RenderBGRACache);
    
    if(m_YRenderCache != NULL) {
        CFRelease(m_YRenderCache);
    }
    
    if(m_CbCrRenderCache != NULL) {
        CFRelease(m_CbCrRenderCache);
    }
    
    if(m_RenderTarget != NULL)
        CVPixelBufferRelease(m_RenderTarget);
    
    if(m_RenderTexture != NULL)
        CFRelease(m_RenderTexture);
    
    if(m_RenderCache != NULL)
        CFRelease(m_RenderCache);
    
    m_RenderTarget = NULL;
    m_RenderTexture = NULL;
    m_RenderCache = NULL;

}

-(void) initCache:(id <MTLDevice>) metalDevice withSize:(CGSize) size{
    
    m_nTextureWdith = size.width;
    m_nTextureHeight = size.height;
    m_MTLDevice = metalDevice;
    
    
    if(m_APPSrcLQuad == nil)
        m_APPSrcLQuad = [[AAPLQuad alloc] initWithDevice:m_MTLDevice];
    
    const void *keys[] = {
        kCVPixelBufferOpenGLESCompatibilityKey,
        kCVPixelBufferMetalCompatibilityKey
    };
    const void *values[] = {
        (__bridge const void *)([NSNumber numberWithBool:YES]),
        (__bridge const void *)([NSNumber numberWithBool:YES])
    };
    
    CFDictionaryRef optionsDictionary = CFDictionaryCreate(NULL, keys, values, 2, NULL, NULL);
    
    CVReturn err= CVPixelBufferCreate(kCFAllocatorDefault,
                        m_nTextureWdith,
                        m_nTextureHeight,
                        kCVPixelFormatType_32BGRA,
                        optionsDictionary,
                        &m_RenderTarget);

    CFRelease(optionsDictionary);
    
    CVMetalTextureCacheCreate(kCFAllocatorDefault, 0, metalDevice, 0, &m_RenderCache);
    
    CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                              m_RenderCache,
                                              m_RenderTarget,
                                              0,
                                              MTLPixelFormatBGRA8Unorm, m_nTextureWdith, m_nTextureHeight,0, &m_RenderTexture);
    
  
    m_RenderPass = [MTLRenderPassDescriptor renderPassDescriptor];
    
    CVMetalTextureRef metlTextureRef = [self getCVMetalTexture] ;
    m_RenderPass.colorAttachments[0].texture =  CVMetalTextureGetTexture(metlTextureRef);
    m_RenderPass.colorAttachments[0].loadAction = MTLLoadActionClear;
    m_RenderPass.colorAttachments[0].storeAction = MTLStoreActionStore;
    
    [self InitPipeLine:metalDevice];
    
    [self InitSampleState];
    
    [self InitYUVPipLine:metalDevice];

}

-(void) setRotation:(int) rotation{
    //m_APPSrcLQuad.rotation = METAL_ORIENTATION(rotation);
}

-(void) InitYUVPipLine:(id <MTLDevice>) metalDevice{
    NSError *error = nil;
    NSString * libSource            = [NSString stringWithFormat:@"%s", CCVideo::Yuv2RGBA];
    id<MTLLibrary>  library         = [m_MTLDevice newLibraryWithSource:libSource options:nil error:&error];
    
    id<MTLFunction> computeProgram   = [library newFunctionWithName:@"YCbCrColorConversion"];
    
    m_YUV2RGBPipeline = [m_MTLDevice newComputePipelineStateWithFunction:computeProgram error:&error];
}
    
-(void) InitPipeLine:(id <MTLDevice>) metalDevice{
    
    NSError *error = nil;
    NSString *      libSource       = [NSString stringWithFormat:@"%s", CCVideo::LogoShader];
    libSource       = [NSString stringWithFormat:@"%s", CCVideo::TestShader];
    id<MTLLibrary>  library         = [m_MTLDevice newLibraryWithSource:libSource options:nil error:&error];

    id<MTLFunction> vertexProgram   = [library newFunctionWithName:@"basic_vertex"];
    id<MTLFunction> fragmentProgram = [library newFunctionWithName:@"basic_fragment"];

    MTLVertexDescriptor* vertexDesc = [[MTLVertexDescriptor alloc] init];
    vertexDesc.attributes[0].format = MTLVertexFormatFloat4;
    vertexDesc.attributes[0].bufferIndex = 0;
    vertexDesc.attributes[0].offset = 0;

    vertexDesc.attributes[1].format = MTLVertexFormatFloat2;
    vertexDesc.attributes[1].bufferIndex = 0;
    vertexDesc.attributes[1].offset = 4 * sizeof(float);  // 8 bytes
    
    vertexDesc.layouts[0].stride = sizeof(Vertex);
    vertexDesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    
    // create a pipeline description
    MTLRenderPipelineDescriptor *desc       = [MTLRenderPipelineDescriptor new];
    
    desc.vertexDescriptor                   = vertexDesc;
    desc.vertexFunction   					= vertexProgram;
    desc.fragmentFunction 					= fragmentProgram;

    desc.colorAttachments[0].pixelFormat 	= MTLPixelFormatBGRA8Unorm;// framebuffer pixel format must match with metal layer
    desc.sampleCount      					= 1;
   
    desc.colorAttachments[0].blendingEnabled = YES;
    m_MTLPipeline = [m_MTLDevice newRenderPipelineStateWithDescriptor:desc error:&error];

}

-(void) InitSampleState {
    MTLSamplerDescriptor *desc = [[MTLSamplerDescriptor alloc] init];
    desc.minFilter = MTLSamplerMinMagFilterLinear;
    
    desc.magFilter = MTLSamplerMinMagFilterLinear;
    desc.sAddressMode = MTLSamplerAddressModeRepeat;
    desc.tAddressMode = MTLSamplerAddressModeRepeat;
    //  all properties below have default values
    desc.mipFilter        = MTLSamplerMipFilterNotMipmapped;
    desc.maxAnisotropy    = 1U;
    desc.normalizedCoordinates = YES;
    desc.lodMinClamp      = 0.0f;
    desc.lodMaxClamp      = FLT_MAX;
   
    // create MTLSamplerState
    m_MTLSampleState = [m_MTLDevice newSamplerStateWithDescriptor:desc];
    MTLDepthStencilDescriptor *pDepthStateDesc = [MTLDepthStencilDescriptor new];
    pDepthStateDesc.depthCompareFunction = MTLCompareFunctionAlways;
    pDepthStateDesc.depthWriteEnabled    = YES;
    m_MTLDepthState = [m_MTLDevice newDepthStencilStateWithDescriptor:pDepthStateDesc];
    
}



-(void) encodeToCommandBuffer:(id<MTLCommandBuffer>) commandBuffer sourceTexture:(id<MTLTexture>) src{
   
    @autoreleasepool {
    id<MTLRenderCommandEncoder> commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:m_RenderPass];
    [commandEncoder setFragmentTexture:src atIndex:0];
    [commandEncoder setFragmentSamplerState:m_MTLSampleState atIndex:0];
    [commandEncoder setRenderPipelineState:m_MTLPipeline];
    
    // Encode quad vertex and texture coordinate buffers
    m_APPSrcLQuad.inputSize = CGSizeMake(src.width, src.height);
    m_APPSrcLQuad.outputSize = CGSizeMake(m_nTextureWdith, m_nTextureHeight);
    [m_APPSrcLQuad encode:commandEncoder];
    
    // tell the render context we want to draw our primitives
    [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                       vertexStart:0
                       vertexCount:6
                     instanceCount:1];
    
    [commandEncoder endEncoding];
    };
    
}

-(void) updateInputBGRA:(size_t) width withHeight:(size_t) height{
    
    id<MTLTexture> bgraTexture = nil;
    if(m_RenderBGRATexture)
        bgraTexture = CVMetalTextureGetTexture(m_RenderBGRATexture);
    
    if(bgraTexture == nil || bgraTexture.width != width || bgraTexture.height != height){
        
        if(m_RenderBGRATarget != NULL)
            CVPixelBufferRelease(m_RenderBGRATarget);
        
        if(m_RenderBGRATexture != NULL)
            CVBufferRelease(m_RenderBGRATexture);
        
        if(m_RenderBGRACache != NULL)
            CFRelease(m_RenderBGRACache);
        
        
        CVMetalTextureCacheCreate(kCFAllocatorDefault, NULL, m_MTLDevice, NULL, &m_RenderBGRACache);
        const void *keys[] = {
            kCVPixelBufferOpenGLESCompatibilityKey,
            kCVPixelBufferMetalCompatibilityKey
        };
        const void *values[] = {
            (__bridge const void *)([NSNumber numberWithBool:YES]),
            (__bridge const void *)([NSNumber numberWithBool:YES])
        };
        
        CFDictionaryRef optionsDictionary = CFDictionaryCreate(NULL, keys, values, 2, NULL, NULL);
        
        CVPixelBufferCreate(kCFAllocatorDefault,
                                          width,
                                          height,
                                          kCVPixelFormatType_32BGRA,
                                          optionsDictionary,
                                          &m_RenderBGRATarget);
        

        
        CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                  m_RenderBGRACache,
                                                  m_RenderBGRATarget,
                                                  0,
                                                  MTLPixelFormatBGRA8Unorm, width, height,0, &m_RenderBGRATexture);
        
        CFRelease(optionsDictionary);
    }
    
    bgraTexture = nil;

}

-(void) renderYCbCr2BGRA:(id<MTLCommandBuffer>) commandBuffer sourcePixelBuffer:(CVPixelBufferRef) pixelBuffer{

//    @autoreleasepool {
    CVMetalTextureRef yTextureRef;
    CVMetalTextureRef cbcrTextureRef;
    
    size_t yWidth = CVPixelBufferGetWidthOfPlane(pixelBuffer, 0);
    size_t yHeight = CVPixelBufferGetHeightOfPlane(pixelBuffer, 0);
    
    [self updateInputBGRA:yWidth withHeight:yHeight];

//    if(m_YRenderCache != nil){
//        CFRelease(m_YRenderCache);
//        CFRelease(m_CbCrRenderCache);
//
//        m_YRenderCache = nil;
//        m_CbCrRenderCache = nil;
//    }
    if(m_YRenderCache == nil){
        CVMetalTextureCacheCreate(kCFAllocatorDefault, 0, m_MTLDevice, 0, &m_YRenderCache);
        CVMetalTextureCacheCreate(kCFAllocatorDefault, 0, m_MTLDevice, 0, &m_CbCrRenderCache);
    }
    
    CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, m_YRenderCache, pixelBuffer, NULL, MTLPixelFormatR8Unorm, yWidth, yHeight, 0, &yTextureRef);
    
    size_t cbcrWidth = CVPixelBufferGetWidthOfPlane(pixelBuffer, 1);
    size_t cbcrHeight = CVPixelBufferGetHeightOfPlane(pixelBuffer, 1);
    
    CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, m_CbCrRenderCache, pixelBuffer, NULL, MTLPixelFormatRG8Unorm, cbcrWidth, cbcrHeight, 1, &cbcrTextureRef);
    
    id<MTLTexture> yTexture = CVMetalTextureGetTexture(yTextureRef);
    id<MTLTexture> cbcrTexture = CVMetalTextureGetTexture(cbcrTextureRef);
    id<MTLTexture> bgraTexture = CVMetalTextureGetTexture(m_RenderBGRATexture);
        
    id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
    [encoder setComputePipelineState:m_YUV2RGBPipeline];
    
    [encoder setTexture:yTexture atIndex:0];
    [encoder setTexture:cbcrTexture atIndex:1];
    [encoder setTexture:bgraTexture atIndex:2];
    
    size_t gridX = 16;
    size_t gridY = 16;
    size_t gridZ = 1;
    size_t groupX = (yWidth + gridX - 1) / gridX;
    size_t groupY = (yHeight + gridY - 1) / gridY;

    [encoder dispatchThreadgroups:MTLSizeMake(groupX, groupY, 1) threadsPerThreadgroup:MTLSizeMake(gridX, gridY, gridZ)];
    [encoder endEncoding];
    
    yTexture = nil;
    cbcrTexture = nil;
    
    CVBufferRelease(yTextureRef);
    CVBufferRelease(cbcrTextureRef);
//    };
}

-(void) encodeToCommandBuffer:(id<MTLCommandBuffer>) commandBuffer sourcePixelBuffer:(CVPixelBufferRef) pixelBuffer{
//    @autoreleasepool {
        OSType inType = CVPixelBufferGetPixelFormatType(pixelBuffer);
        
        CVMetalTextureRef textureRef = nil;
        
        id<MTLTexture> src = nil;
        
        if(inType == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange){
            [self renderYCbCr2BGRA:commandBuffer sourcePixelBuffer:pixelBuffer];
            src = CVMetalTextureGetTexture(m_RenderBGRATexture);
        }else{
            
            if(m_RenderBGRACache == nil){
                CVMetalTextureCacheCreate(kCFAllocatorDefault, NULL, m_MTLDevice, NULL, &m_RenderBGRACache);
            }
            
            size_t width = CVPixelBufferGetWidth(pixelBuffer);
            size_t height = CVPixelBufferGetHeight(pixelBuffer);
            CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, m_RenderBGRACache, pixelBuffer, NULL, MTLPixelFormatBGRA8Unorm, width, height, 0, &textureRef);
            
            src = CVMetalTextureGetTexture(textureRef);
        }
        
        if(src) {
            [self encodeToCommandBuffer:commandBuffer sourceTexture:src];
        }
        
        src = nil;
        
        if(textureRef)
            CVBufferRelease(textureRef);
//    }

    
}

-(CVPixelBufferRef) getPixelBuffer{
    return m_RenderTarget;
}

-(CVMetalTextureRef) getCVMetalTexture{
    return m_RenderTexture;
}

@end
#endif
