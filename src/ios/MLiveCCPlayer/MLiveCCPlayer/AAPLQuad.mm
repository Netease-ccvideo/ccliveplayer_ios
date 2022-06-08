/*
 Copyright (C) 2015 Apple Inc. All Rights Reserved.
 See LICENSE.txt for this sample’s licensing information
 
 Abstract:
 Utility class for creating a quad.
 */

#import <simd/simd.h>

#import "AAPLQuad.h"

//#import "MetalImageTransforms.h"
#ifdef MTL_ENABLE
static const uint32_t kCntQuadTexCoords = 6;
static const uint32_t kSzQuadTexCoords  = kCntQuadTexCoords * sizeof(simd::float2);

static const uint32_t kCntQuadVertices = kCntQuadTexCoords;
static const uint32_t kSzQuadVertices  = kCntQuadVertices * sizeof(simd::float4);

static const simd::float4 kQuadVertices[kCntQuadVertices] =
{
//    { -1.0f,  -1.0f, 0.0f, 1.0f },  // A
//    {  1.0f,  -1.0f, 0.0f, 1.0f },  // D
//    { -1.0f,   1.0f, 0.0f, 1.0f },  // B
//    
//    {  1.0f,  -1.0f, 0.0f, 1.0f },  //D
//    { -1.0f,   1.0f, 0.0f, 1.0f },  //B
//    {  1.0f,   1.0f, 0.0f, 1.0f }   //C
    
    { -1.0f,   1.0f, 0.0f, 1.0f },  // B
    {  1.0f,   1.0f, 0.0f, 1.0f },   //C
    { -1.0f,  -1.0f, 0.0f, 1.0f },  // A
    
    {  1.0f,   1.0f, 0.0f, 1.0f },   //C
    { -1.0f,  -1.0f, 0.0f, 1.0f },  // A
    {  1.0f,  -1.0f, 0.0f, 1.0f }  // D
};

static const simd::float2 noRotationTextureCoordinates[kCntQuadTexCoords] = {
    {0.0f, 0.0f}, // 1
    {1.0f, 0.0f}, // 2
    {0.0f, 1.0f}, // 3
    
    {1.0f, 0.0f}, // 4
    {0.0f, 1.0f}, // 5
    {1.0f, 1.0f}  // 6
};

static const simd::float2 rotateLeftTextureCoordinates[kCntQuadTexCoords] = {
    {1.0f, 0.0f},
    {1.0f, 1.0f},
    {0.0f, 0.0f},
    
    {1.0f, 1.0f},
    {0.0f, 0.0f},
    {0.0f, 1.0f},
};

static const simd::float2 rotateRightTextureCoordinates[kCntQuadTexCoords] = {
    {0.0f, 1.0f},
    {0.0f, 0.0f},
    {1.0f, 1.0f},
    
    {0.0f, 0.0f},
    {1.0f, 1.0f},
    {1.0f, 0.0f},
};

static const simd::float2 verticalFlipTextureCoordinates[kCntQuadTexCoords] = {
    {0.0f, 1.0f},
    {1.0f, 1.0f},
    {0.0f, 0.0f},
    
    {1.0f,  1.0f},
    {0.0f,  0.0f},
    {1.0f,  0.0f},
};

static const simd::float2 horizontalFlipTextureCoordinates[kCntQuadTexCoords] = {
    {1.0f,  0.0f},
    {0.0f,  0.0f},
    {1.0f,  1.0f},
    
    {0.0f,  0.0f},
    {1.0f,  1.0f},
    {0.0f,  1.0f},
};

static const simd::float2 rotateRightVerticalFlipTextureCoordinates[kCntQuadTexCoords] = {
    {0.0f, 0.0f},
    {0.0f, 1.0f},
    {1.0f, 0.0f},
    
    {0.0f, 1.0f},
    {1.0f, 0.0f},
    {1.0f, 1.0f},
};

static const simd::float2 rotateRightHorizontalFlipTextureCoordinates[kCntQuadTexCoords] = {
    {1.0f, 1.0f},
    {1.0f, 0.0f},
    {0.0f, 1.0f},
    
    {1.0f, 0.0f},
    {0.0f, 1.0f},
    {0.0f, 0.0f},
};

static const simd::float2 rotate180TextureCoordinates[kCntQuadTexCoords] = {
    {1.0f, 1.0f},
    {0.0f, 1.0f},
    {1.0f, 0.0f},
    
    {0.0f, 1.0f},
    {1.0f, 0.0f},
    {0.0f, 0.0f},
};



@implementation AAPLQuad
{
@private
    // textured Quad
    id <MTLBuffer>      m_VertexBuffer;
    id <MTLBuffer>      m_TexCoordBuffer;
    id <MTLBuffer>      m_MvpBuffer;
    id <MTLDevice>      m_MtlDevice;
    BOOL                m_bNeedUpdateRotate;
    MetalOrientation    m_nMetalOrientation;
    // Dimensions
    CGSize  _size;
    CGRect  _bounds;
    float   _aspect;
    
    // Indicies
    NSUInteger  _vertexIndex;
    NSUInteger  _texCoordIndex;
    NSUInteger  _mvpIndex;
    NSUInteger  _samplerIndex;


}

- (instancetype) initWithDevice:(id <MTLDevice>)device
{
    self = [super init];
    
    if(self)
    {
        
        m_MtlDevice = device;
        
        m_bNeedUpdateRotate = YES;
        m_nMetalOrientation = MetalOrientationUnknown;
        
        m_VertexBuffer = [device newBufferWithBytes:kQuadVertices
                                             length:kSzQuadVertices
                                            options:MTLResourceOptionCPUCacheModeDefault];
        
        if(!m_VertexBuffer)
        {
            NSLog(@">> ERROR: Failed creating a vertex buffer for a quad!");
            
            return nil;
        } // if
        
        m_VertexBuffer.label = @"ccsdk quad vertices";


    
        m_MvpBuffer = [device newBufferWithLength:16 * sizeof(float)
                                              options:MTLResourceOptionCPUCacheModeDefault];
        
        m_MvpBuffer.label = @"ccsdk mvp texcoords";
        
        _vertexIndex   = 0;
        _texCoordIndex = 1;
        _mvpIndex      = 2;
        _samplerIndex  = 0;
        
        _size   = CGSizeMake(0.0, 0.0);
        _bounds = CGRectMake(0.0, 0.0, 0.0, 0.0);
        
        _aspect = 1.0f;
        
        _rotation = MetalOrientationUnknown;

    } // if
    
    return self;
} // _setupWithTexture

-(void)loadViewBufferwithOrientation:(MetalOrientation)orient
{

    if(m_nMetalOrientation == orient && m_TexCoordBuffer != nil) {
        return;
    }
    m_nMetalOrientation = orient;

    if(m_TexCoordBuffer)
    {
//        [m_TexCoordBuffer setPurgeableState:MTLPurgeableStateEmpty];
        m_TexCoordBuffer = nil;
    }

    switch (orient)
    {
        case MetalOrientationUnknown:
        {
            m_TexCoordBuffer = [m_MtlDevice newBufferWithBytes:noRotationTextureCoordinates
                                                        length:kSzQuadTexCoords
                                                       options:MTLResourceOptionCPUCacheModeDefault];
        }
            break;
        case MetalOrientationPortrait:
        {
            m_TexCoordBuffer = [m_MtlDevice newBufferWithBytes:rotateLeftTextureCoordinates
                                                        length:kSzQuadTexCoords
                                                       options:MTLResourceOptionCPUCacheModeDefault];
        }
            break;
        case MetalOrientationLandscapeLeft:
        {
            m_TexCoordBuffer = [m_MtlDevice newBufferWithBytes:rotateRightVerticalFlipTextureCoordinates
                                                        length:kSzQuadTexCoords
                                                       options:MTLResourceOptionCPUCacheModeDefault];
        }
            break;
        case MetalOrientationLandscapeRight:
        {
            m_TexCoordBuffer = [m_MtlDevice newBufferWithBytes:rotateRightHorizontalFlipTextureCoordinates
                                                        length:kSzQuadTexCoords
                                                       options:MTLResourceOptionCPUCacheModeDefault];
        }
            break;
        case  MetalOrientationPortraitUpsideDown:
            // Device oriented vertically, home button on the top
            m_TexCoordBuffer = [m_MtlDevice newBufferWithBytes: rotateRightTextureCoordinates
                                                        length:kSzQuadTexCoords
                                                       options:MTLResourceOptionCPUCacheModeDefault];
            break;
        case  MetalOrientationFaceUp:             // Device oriented flat, face up
            m_TexCoordBuffer = [m_MtlDevice newBufferWithBytes: verticalFlipTextureCoordinates
                                                        length:kSzQuadTexCoords
                                                       options:MTLResourceOptionCPUCacheModeDefault];
            
            break;
        case MetalOrientationFaceDown:
            m_TexCoordBuffer = [m_MtlDevice newBufferWithBytes: horizontalFlipTextureCoordinates
                                                        length:kSzQuadTexCoords
                                                       options:MTLResourceOptionCPUCacheModeDefault];
            break;
        case MetalOrientation180:
            m_TexCoordBuffer = [m_MtlDevice newBufferWithBytes: rotate180TextureCoordinates
                                                        length:kSzQuadTexCoords
                                                       options:MTLResourceOptionCPUCacheModeDefault];
            break;
        default:
        {
            m_TexCoordBuffer = [m_MtlDevice newBufferWithBytes: rotate180TextureCoordinates
                                                        length:kSzQuadTexCoords
                                                       options:MTLResourceOptionCPUCacheModeDefault];
        }
            break;
    }
    
    
    m_TexCoordBuffer.label = @"ccsdk quad texcoords";
}

simd::float4x4 scale(const float& x,
                                   const float& y,
                                   const float& z)
{
    simd::float4 v = {x, y, z, 1.0f};
    
    return simd::float4x4(v);
} // scale

- (void) update{

    [self loadViewBufferwithOrientation:_rotation];
    
    float inputW = _inputSize.width;
    float inputH = _inputSize.height;
    
    if(_rotation == MetalOrientationLandscapeLeft ||
       _rotation == MetalOrientationLandscapeRight ||
       _rotation == MetalOrientationPortrait ||
       _rotation == MetalOrientationPortraitUpsideDown){
        float tmp = inputW;
        inputW = inputH;
        inputH = tmp;
    }

    float input_aspect = inputW / inputH;
    float out_aspect = _outputSize.width / _outputSize.height;
    
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    
    if(out_aspect != input_aspect){
        if(input_aspect > out_aspect){
            scaleY = out_aspect / input_aspect;
        }else{
            scaleX = input_aspect / out_aspect;
        }
    }
    
    simd::float4x4 mvp = scale(scaleX, scaleY, 0);
    
    memcpy([m_MvpBuffer contents], mvp.columns, 16 * sizeof(float));
}


- (void) encode:(id <MTLRenderCommandEncoder>)renderEncoder
{
    [self update];
    [renderEncoder setVertexBuffer:m_VertexBuffer
                            offset:0
                           atIndex:_vertexIndex ];
    
    [renderEncoder setVertexBuffer:m_TexCoordBuffer
                            offset:0
                           atIndex:_texCoordIndex ];
    
    [renderEncoder setVertexBuffer:m_MvpBuffer
                            offset:0
                           atIndex:_mvpIndex ];
    
}
@end
#endif

