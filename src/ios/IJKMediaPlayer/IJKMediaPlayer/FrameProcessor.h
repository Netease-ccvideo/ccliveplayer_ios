//
//  FrameProcessor.h
//  IJKMediaPlayer
//
//  Created by luobiao on 16/4/14.
//  Copyright © 2016年 bilibili. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MLiveCCVideoFrame.h"

typedef int (^OnBindFramebuffer)();
typedef int (^OnPreRenderFrame)();
typedef int (^OnPostRenderFrame)();
typedef void (^OnRenderFrame)(MLiveCCVideoFrame* frame);
typedef void (^OnRenderAVFrame)(CVPixelBufferRef buf);

@interface FrameProcessor : NSObject

@property (readwrite, copy) OnBindFramebuffer onBindFrameBuffer;
@property (readwrite, copy) OnPreRenderFrame onPreRenderFrame;
@property (readwrite, copy) OnPostRenderFrame onPostRenderFrame;
@property (readwrite, copy) OnRenderFrame onRenderFrame;
@property (readwrite, copy) OnRenderAVFrame onRenderAVFrame;

@end
