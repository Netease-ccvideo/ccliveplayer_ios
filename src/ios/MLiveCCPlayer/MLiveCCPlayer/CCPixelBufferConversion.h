//
//  CCPixelBufferConversion.h
//  MLiveCCPlayer
//
//  Created by jlubobo on 2018/3/13.
//  Copyright © 2018年 cc. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

@interface CCPixelBufferConversion : NSObject

-(CVPixelBufferRef) convertBuffer:(CVPixelBufferRef)srcBuffer;

@property (nonatomic, assign) CGSize outputSize;

- (void) clear;

@end
