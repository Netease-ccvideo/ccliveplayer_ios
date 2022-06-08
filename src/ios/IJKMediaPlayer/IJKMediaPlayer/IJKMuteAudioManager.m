//
//  IJKMuteAudioManager.m
//  IJKMediaPlayer
//
//  Created by xc on 14/12/6.
//  Copyright (c) 2014年 bilibili. All rights reserved.
//

#import "IJKMuteAudioManager.h"

@implementation IJKMuteAudioManager

+ (instancetype)sharedInstance
{
	static IJKMuteAudioManager *_sharedClient = nil;
	
	static dispatch_once_t oncePredicate;
	dispatch_once(&oncePredicate, ^{
		_sharedClient = [[self alloc] init];
	});
	
	return _sharedClient;
}




@end
