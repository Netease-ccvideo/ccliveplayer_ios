//
//  MLiveVideoFrameConverter.h
//  MLiveCCPlayer
//
//  Created by jlubobo on 2019/8/7.
//  Copyright © 2019 cc. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "IJKMediaPlayer/IJKMediaPlayer.h"
#import <Accelerate/Accelerate.h>

NS_ASSUME_NONNULL_BEGIN

@interface MLiveVideoFrameConverter : NSObject

- (instancetype)initWithAccelerate:(BOOL)accelerate;

- (void)convertFrame:(MLiveCCVideoFrame *)frame toBuffer:(CVPixelBufferRef)pixelBufferRef;

@end

NS_ASSUME_NONNULL_END
