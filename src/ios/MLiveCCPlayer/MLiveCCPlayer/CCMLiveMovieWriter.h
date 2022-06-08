//
//  MediaMovieWriter.h
//  Peertalk Example
//
//  Created by cc on 2020/9/23.
//

#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>
NS_ASSUME_NONNULL_BEGIN

@interface CCMLiveMovieWriter : NSObject

@property (nonatomic, assign) int movieQuality;

-(id)initWithURL:(NSURL *)outputURL quality:(int) quality size:(CGSize)imageSize;
-(void)StartWriter;
-(int) EncodeVideoFrame:(CVPixelBufferRef) cvpixel;
-(void)StopWriter;
@end

NS_ASSUME_NONNULL_END
