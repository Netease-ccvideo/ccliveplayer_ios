//
//  MLiveCCRenderYUV.m
//  MLiveCCPlayer
//
//  Created by jlubobo on 2017/5/26.
//  Copyright © 2017年 cc. All rights reserved.
//

#import "MLiveCCRenderYUV.h"
#import "MLiveDefines.h"


static NSString *const yuvFragmentShaderString = MLIVECC_SHADER_STRING
(
 varying highp vec2 v_texcoord;
 uniform sampler2D s_texture_y;
 uniform sampler2D s_texture_u;
 uniform sampler2D s_texture_v;
 
 void main()
 {
     highp float y = texture2D(s_texture_y, v_texcoord).r;
     highp float u = texture2D(s_texture_u, v_texcoord).r - 0.5;
     highp float v = texture2D(s_texture_v, v_texcoord).r - 0.5;
     
     highp float r = y +               1.40200 * v;
     highp float g = y - 0.34414 * u - 0.71414 * v;
     highp float b = y + 1.77200 * u;
     
     gl_FragColor = vec4(r,g,b,1.0);
 }
 );

@interface MLiveCCRenderYUV()

//@property (nonatomic, strong) MLiveVideoFrameConverter *frameConverter;

@end

@implementation MLiveCCRenderYUV {
    GLint _uniformSamplers[3];
    GLuint _textures[3];
    int _format;
}
@synthesize frameConverter = _frameConverter;

- (BOOL) isValid
{
    return (_textures[0] != 0);
}

- (NSString *) fragmentShader
{
    return yuvFragmentShaderString;
}

- (void) resolveUniforms: (GLuint) program
{
    _uniformSamplers[0] = glGetUniformLocation(program, "s_texture_y");
    _uniformSamplers[1] = glGetUniformLocation(program, "s_texture_u");
    _uniformSamplers[2] = glGetUniformLocation(program, "s_texture_v");
}

- (void) genTexture:(MLiveCCVideoFrame *)frame
{
    assert(frame->planes);
//    assert(frame->format == SDL_FCC_I420);
    assert(frame->planes == 3);
    assert(frame->planes == 3);
    
    const NSUInteger frameHeight = frame->h;
    
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    
    if (0 == _textures[0])
        glGenTextures(3, _textures);
    
    const UInt8 *pixels[3] = { frame->pixels[0], frame->pixels[1], frame->pixels[2] };
    const NSUInteger widths[3]  = { frame->pitches[0], frame->pitches[1], frame->pitches[2] };
    const NSUInteger heights[3] = { frameHeight, frameHeight / 2, frameHeight / 2 };
    
    for (int i = 0; i < 3; ++i) {
        
        glBindTexture(GL_TEXTURE_2D, _textures[i]);
        
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_LUMINANCE,
                     (int)widths[i],
                     (int)heights[i],
                     0,
                     GL_LUMINANCE,
                     GL_UNSIGNED_BYTE,
                     pixels[i]);
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}

- (BOOL) prepareDisplay
{
    if (_textures[0] == 0)
        return NO;
    
    for (int i = 0; i < 3; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, _textures[i]);
        glUniform1i(_uniformSamplers[i], i);
    }
    
    return YES;
}

- (void) dealloc
{
    
}

- (void)clearBuffer
{
    if (_textures[0])
        glDeleteTextures(3, _textures);
    memset(_textures, 0, sizeof(_textures));
}

- (void)genPixelBufferFrom:(MLiveCCVideoFrame *)frame To:(CVPixelBufferRef)pixelBufferRef Accelerate:(BOOL)accelerate {
 
    assert(frame->planes);
    assert(frame->format == DECODE_FORMAT_I420);
    assert(frame->planes == 3);
    assert(frame->planes == 3);
    
    double a = CFAbsoluteTimeGetCurrent();
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
//    NSLog(@"convert yuv --> rgb take %f ms", (b - a) * 1000);

}

- (void)setFormat:(int)format {
    _format = format;
}

- (int)format {
    
    return _format;
}

@end
