//
//  MLiveCCRenderVTB.m
//  MLiveCCPlayer
//
//  Created by jlubobo on 2017/5/26.
//  Copyright © 2017年 cc. All rights reserved.
//

#import "MLiveCCRenderVTB.h"
#import "MLiveDefines.h"
#import <OpenGLES/ES2/glext.h>
#import "MLiveVideoFrameConverter.h"

// BT.709, which is the standard for HDTV.
static const GLfloat kColorConversion709[] = {
    1.164,  1.164,  1.164,
    0.0,   -0.213,  2.112,
    1.793, -0.533,  0.0,
};

// BT.601, which is the standard for SDTV.
static const GLfloat kColorConversion601[] = {
    1.164,  1.164, 1.164,
    0.0,   -0.392, 2.017,
    1.596, -0.813,   0.0,
};


static NSString *const g_nv12FragmentShaderString = MLIVECC_SHADER_STRING
(
 varying highp vec2 v_texcoord;
 precision mediump float;
 uniform sampler2D SamplerY;
 uniform sampler2D SamplerUV;
 uniform mat3 colorConversionMatrix;
 
 void main()
 {
     mediump vec3 yuv;
     lowp vec3 rgb;
     
     // Subtract constants to map the video range start at 0
     yuv.x = (texture2D(SamplerY, v_texcoord).r - (16.0/255.0));
     yuv.yz = (texture2D(SamplerUV, v_texcoord).rg - vec2(0.5, 0.5));
     rgb = colorConversionMatrix * yuv;
     gl_FragColor = vec4(rgb,1);
 }
 );

@implementation MLiveCCRenderVTB {
    GLint _uniform[1];
    GLint _uniformSamplers[2];
    GLuint _textures[2];
    
    CVOpenGLESTextureCacheRef _textureCache;
    CVOpenGLESTextureRef      _cvTexturesRef[2];
    
    const GLfloat *_preferredConversion;
    int _format;
}
@synthesize frameConverter = _frameConverter;

-(id)initWithTextureCache:(CVOpenGLESTextureCacheRef) textureCache
{
    self = [super init];
    if (self) {
        _textureCache = textureCache;
    }
    return self;
}

- (BOOL) isValid
{
    return (_textures[0] != 0) && (_textures[1] != 0);
}

- (NSString *) fragmentShader
{
    return g_nv12FragmentShaderString;
}

- (void) resolveUniforms: (GLuint) program
{
    _uniformSamplers[0] = glGetUniformLocation(program, "SamplerY");
    _uniformSamplers[1] = glGetUniformLocation(program, "SamplerUV");
    _uniform[0] = glGetUniformLocation(program, "colorConversionMatrix");
}

- (void) genTexture:(MLiveCCVideoFrame *)frame
{
    assert(frame->planes);
//    assert(overlay->format == SDL_FCC__VTB);
    assert(frame->planes == 2);
    
    if (!frame->is_private)
        return;
    
    if (!_textureCache) {
        NSLog(@"[MLiveCCPlayer] nil textureCache\n");
        return;
    }
    
    CVPixelBufferRef pixelBuffer = frame->pixel_buffer;
    if (!pixelBuffer) {
        NSLog(@"[MLiveCCPlayer] nil pixelBuffer in overlay\n");
        return;
    }
    
    

    
    
    CFTypeRef colorAttachments = CVBufferGetAttachment(pixelBuffer, kCVImageBufferYCbCrMatrixKey, NULL);
    if (colorAttachments == kCVImageBufferYCbCrMatrix_ITU_R_601_4) {
        _preferredConversion = kColorConversion601;
    } else if (colorAttachments == kCVImageBufferYCbCrMatrix_ITU_R_709_2){
        _preferredConversion = kColorConversion709;
    } else {
        _preferredConversion = kColorConversion709;
    }
    
    for (int i = 0; i < 2; ++i) {
        if (_cvTexturesRef[i]) {
            CFRelease(_cvTexturesRef[i]);
            _cvTexturesRef[i] = 0;
            _textures[i] = 0;
        }
    }
    
    // Periodic texture cache flush every frame
    if (_textureCache)
        CVOpenGLESTextureCacheFlush(_textureCache, 0);
    
    if (_textures[0])
        glDeleteTextures(2, _textures);
    
    size_t frameWidth  = CVPixelBufferGetWidth(pixelBuffer);
    size_t frameHeight = CVPixelBufferGetHeight(pixelBuffer);
    
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    
    CVOpenGLESTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                 _textureCache,
                                                 pixelBuffer,
                                                 NULL,
                                                 GL_TEXTURE_2D,
                                                 GL_RED_EXT,
                                                 (GLsizei)frameWidth,
                                                 (GLsizei)frameHeight,
                                                 GL_RED_EXT,
                                                 GL_UNSIGNED_BYTE,
                                                 0,
                                                 &_cvTexturesRef[0]);
    _textures[0] = CVOpenGLESTextureGetName(_cvTexturesRef[0]);
    glBindTexture(CVOpenGLESTextureGetTarget(_cvTexturesRef[0]), _textures[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    
    CVOpenGLESTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                 _textureCache,
                                                 pixelBuffer,
                                                 NULL,
                                                 GL_TEXTURE_2D,
                                                 GL_RG_EXT,
                                                 (GLsizei)frameWidth / 2,
                                                 (GLsizei)frameHeight / 2,
                                                 GL_RG_EXT,
                                                 GL_UNSIGNED_BYTE,
                                                 1,
                                                 &_cvTexturesRef[1]);
    _textures[1] = CVOpenGLESTextureGetName(_cvTexturesRef[1]);
    glBindTexture(CVOpenGLESTextureGetTarget(_cvTexturesRef[1]), _textures[1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

- (BOOL) prepareDisplay
{
    if (_textures[0] == 0)
        return NO;
    
    for (int i = 0; i < 2; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, _textures[i]);
        glUniform1i(_uniformSamplers[i], i);
    }
    
    glUniformMatrix3fv(_uniform[0], 1, GL_FALSE, _preferredConversion);
    return YES;
}

- (void) dealloc
{

}

- (void) clearBuffer
{
    for (int i = 0; i < 2; ++i) {
        if (_cvTexturesRef[i]) {
            CFRelease(_cvTexturesRef[i]);
            _cvTexturesRef[i] = 0;
            _textures[i] = 0;
        }
    }
    
    if (_textures[0])
        glDeleteTextures(2, _textures);
    
}

- (void)genPixelBufferFrom:(MLiveCCVideoFrame *)frame To:(CVPixelBufferRef)pixelBufferRef Accelerate:(BOOL)accelerate {
    
    assert(frame->planes);
    assert(frame->format == DECODE_FORMAT_VTB);
    assert(frame->planes == 2);
    
//    int width = frame->w;
//    int height = frame->h;
//
//    int bufferWidth = accelerate ? width : ((width % 16 == 0) ? width : (width / 16 + 1) * 16);
//    int bufferHeight = height;
    
//    CVPixelBufferRef pixelBuffer = frame->pixel_buffer;
//
    double a = CFAbsoluteTimeGetCurrent();
#if 0
    if(!_pixelBufferConversion) {
        _pixelBufferConversion = [[CCPixelBufferConversion alloc] init];
        _pixelBufferConversion.outputSize = CGSizeMake(width, height);
    }
    CVPixelBufferRef destPixelBuffer = (CVPixelBufferRef)([_pixelBufferConversion convertBuffer:pixelBuffer]);
    double b = CFAbsoluteTimeGetCurrent();
//    NSLog(@"convert yuv 2 rgb take %f", b - a);
#else
    if(pixelBufferRef != NULL)
    {
        if(kCVReturnSuccess == CVPixelBufferLockBaseAddress(pixelBufferRef, 0))
        {
            if(!_frameConverter) {
                _frameConverter = [[MLiveVideoFrameConverter alloc] initWithAccelerate:accelerate];
            }
            [_frameConverter convertFrame:frame toBuffer:pixelBufferRef];
            
            CVPixelBufferUnlockBaseAddress(pixelBufferRef, 0);
        }
    }
    double b = CFAbsoluteTimeGetCurrent();
//    NSLog(@"convert nv12 --> rgb take %f ms", (b - a) * 1000);
#endif
}



- (void)setFormat:(int)format {
    _format = format;
}

- (int)format {
    
    return _format;
}
@end
