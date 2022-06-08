//
//  IJKMuteAudioManager.h
//  IJKMediaPlayer
//
//  Created by xc on 14/12/6.
//  Copyright (c) 2014年 bilibili. All rights reserved.
//

#import <Foundation/Foundation.h>

@interface IJKMuteAudioManager : NSObject

+ (instancetype)sharedInstance;

@property (nonatomic, assign) BOOL mute;

@end
