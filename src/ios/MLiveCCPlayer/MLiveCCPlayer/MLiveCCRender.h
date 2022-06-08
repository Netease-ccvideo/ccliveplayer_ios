//
//  MLiveCCRender.h
//  MLiveCCPlayer
//
//  Created by jlubobo on 2017/5/26.
//  Copyright © 2017年 cc. All rights reserved.
//

#ifndef MLiveCCRender_h
#define MLiveCCRender_h

#import "IJKMediaPlayer/IJKMediaPlayer.h"
#import <OpenGLES/es2/gl.h>
#import "MLiveVideoFrameConverter.h"

@protocol MLiveCCRender

- (BOOL) isValid;
- (NSString *) fragmentShader;
- (void) resolveUniforms: (GLuint) program;
- (void) genTexture:(MLiveCCVideoFrame*)frame;
- (BOOL) prepareDisplay;
- (void) clearBuffer;
- (void) genPixelBufferFrom:(MLiveCCVideoFrame *)frame To:(CVPixelBufferRef)pixelBufferRef Accelerate:(BOOL)accelerate;

@property (nonatomic, assign) int format;
@property (nonatomic, strong) MLiveVideoFrameConverter *frameConverter;

@end

#endif /* MLiveCCRender_h */
