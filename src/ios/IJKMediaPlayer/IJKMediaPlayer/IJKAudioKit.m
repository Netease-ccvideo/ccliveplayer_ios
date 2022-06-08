/*
 * IJKAudioKit.m
 *
 * Copyright (c) 2013-2014 Zhang Rui <bbcallen@gmail.com>
 *
 * based on https://github.com/kolyvan/kxmovie
 *
 * This file is part of ijkPlayer.
 *
 * ijkPlayer is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * ijkPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with ijkPlayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#import "IJKAudioKit.h"
#import "IJKMediaPlayback.h"
#import "loghelp.h"

@implementation IJKAudioKit {

    BOOL _audioSessionInitialized;
    
    int _mediaPlay;
}

+ (IJKAudioKit *)sharedInstance
{
    static IJKAudioKit *sAudioKit = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sAudioKit = [[IJKAudioKit alloc] init];
        sAudioKit->_mediaPlay = -1;
    });
    return sAudioKit;
}

- (void)setupAudioSessionWithMediaPlay:(int)mediaPlay
{
    if(_mediaPlay != mediaPlay) {
        _mediaPlay = mediaPlay;
        [self initializeAudioSession];
    }
    /* Set audio session to mediaplayback */
    //    UInt32 sessionCategory = kAudioSessionCategory_MediaPlayback;
    //    status = AudioSessionSetProperty(kAudioSessionProperty_AudioCategory, sizeof(sessionCategory), &sessionCategory);
    //    UInt32 allowMixing = true;
    //    status = AudioSessionSetProperty(kAudioSessionProperty_OverrideCategoryMixWithOthers, sizeof(allowMixing), &allowMixing);
    //    if (status != noErr) {
    //        ALOGI(" IJKAudioKit: AudioSessionSetProperty(kAudioSessionProperty_AudioCategory) failed (%d) \n", (int)status);
    //        return;
    //    }
    
    //    status = AudioSessionSetActive(true);
    //    if (status != noErr) {
    //        ALOGI(" IJKAudioKit: AudioSessionSetActive(true) failed (%d) \n", (int)status);
    //        return;
    //    }
    
    return ;
}

- (BOOL) listenCategory {
    //    kAudioSessionCategory_AmbientSound               = 'ambi',
    //    kAudioSessionCategory_SoloAmbientSound           = 'solo',
    //    kAudioSessionCategory_MediaPlayback              = 'medi',
    //    kAudioSessionCategory_RecordAudio                = 'reca',
    //    kAudioSessionCategory_PlayAndRecord              = 'plar',
    UInt32 category = 0;
    UInt32 categorySize = sizeof(category);
    if(AudioSessionGetProperty(kAudioSessionProperty_AudioCategory, &categorySize, &category) == 0) {
        NSString *categoryName = @"kAudioSessionCategory_Unknow";
        switch (category) {
            case kAudioSessionCategory_PlayAndRecord:
                categoryName = @"kAudioSessionCategory_PlayAndRecord";
                break;
            case kAudioSessionCategory_SoloAmbientSound:
                categoryName = @"kAudioSessionCategory_SoloAmbientSound";
                break;
            case kAudioSessionCategory_AmbientSound:
                categoryName = @"kAudioSessionCategory_AmbientSound";
                break;
            case kAudioSessionCategory_MediaPlayback:
                categoryName = @"kAudioSessionCategory_MediaPlayback";
                break;
            case kAudioSessionCategory_RecordAudio:
                categoryName = @"kAudioSessionCategory_RecordAudio";
                break;
            default:
                break;
        }
         ALOGF("IJKAudioKit: traceCategory category is %s \n", [categoryName UTF8String]);
        CFStringRef route;
        UInt32 propertySize = sizeof(CFStringRef);
        if(AudioSessionGetProperty(kAudioSessionProperty_AudioRoute, &propertySize, &route) == 0) {
            NSString *routeString = (__bridge_transfer NSString *)route;
            ALOGF("IJKAudioKit: 1 routeString %s \n",[routeString UTF8String]);
        }
    }
    
    //    BOOL actived = [self setActive:YES error:nil];
    //    if (!actived) {
    //        ALOGF("IJKAudioKit: AudioSessionSetActive(true) failed (%d) setCategoryWith \n");
    //        return NO;
    //    }
    return YES;
    
}

- (void)initializeAudioSession
{
#if 1
    NSError *error = nil;
    if(_mediaPlay) {
        NSLog(@"[MLiveCCPlayer] mediaPlay %d category %@ --> AVAudioSessionCategoryPlayback",_mediaPlay, [AVAudioSession sharedInstance].category);
        if (NO == [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayback error:&error]) {
            NSLog(@"IJKAudioKit: AVAudioSession.setCategory(AVAudioSessionCategoryPlayback) failed: %@\n", error ? [error localizedDescription] : @"nil");
            return;
        }
    } else {
        if([AVAudioSession sharedInstance].category == AVAudioSessionCategoryAmbient || [AVAudioSession sharedInstance].category == AVAudioSessionCategorySoloAmbient) {
                NSLog(@"[MLiveCCPlayer] mediaPlay %d category = %@ no need update",_mediaPlay, [AVAudioSession sharedInstance].category);
        } else {
            NSLog(@"[MLiveCCPlayer] mediaPlay %d category %@ --> AVAudioSessionCategoryAmbient",_mediaPlay, [AVAudioSession sharedInstance].category);
            if (NO == [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryAmbient error:&error]) {
                NSLog(@"IJKAudioKit: AVAudioSession.setCategory(AVAudioSessionCategoryAmbient) failed: %@\n", error ? [error localizedDescription] : @"nil");
                return;
            }
        }
    }
    error = nil;
    if (NO == [[AVAudioSession sharedInstance] setActive:YES error:&error]) {
        NSLog(@"IJKAudioKit: AVAudioSession.setActive(YES) failed: %@\n", error ? [error localizedDescription] : @"nil");
        return;
    }
#else
    OSStatus status = AudioSessionInitialize(NULL,
                                                kCFRunLoopCommonModes,
                                                IjkAudioSessionInterruptionListener,
                                                (__bridge void *)self);
       if (status != noErr) {
           ALOGI("IJKAudioKit: AudioSessionInitialize failed (%d) \n", (int)status);
           return;
       }
       if(_mediaPlay) {
           ALOGI("IJKAudioKit: AudioSessionSetProperty to play mode \n");
           /* Set audio session to mediaplayback */
           UInt32 sessionCategory = kAudioSessionCategory_MediaPlayback;
           status = AudioSessionSetProperty(kAudioSessionProperty_AudioCategory, sizeof(sessionCategory), &sessionCategory);
           if (status != noErr) {
               NSLog(@"IJKAudioKit: AudioSessionSetProperty(kAudioSessionProperty_AudioCategory) failed (%d)", (int)status);
               return;
           }
       }
       ALOGI("IJKAudioKit: AudioSessionInitialize success \n");
       
       BOOL actived = [self setActive:YES error:nil];
       if (!actived) {
           NSLog(@"IJKAudioKit: AudioSessionSetActive(true) failed (%d)", (int)status);
           return;
       }
#endif
}

- (BOOL)setActive:(BOOL)active error:(NSError *__autoreleasing *)outError
{
#if 1
    if (active != NO) {
           [[AVAudioSession sharedInstance] setActive:YES error:nil];
       } else {
           @try {
               [[AVAudioSession sharedInstance] setActive:NO error:nil];
           } @catch (NSException *exception) {
               NSLog(@"failed to inactive AVAudioSession\n");
           }
       }
    return YES;
    
#else
    OSStatus status = AudioSessionSetActiveWithFlags(active,kAudioSessionSetActiveFlag_NotifyOthersOnDeactivation);
    if(status == 0x21696E74) {//AVAudioSessionErrorCodeCannotInterruptOthers
        UInt32 allowMixing = true;
        status = AudioSessionSetProperty(kAudioSessionProperty_OverrideCategoryMixWithOthers, sizeof(allowMixing), &allowMixing);
        if (status != noErr) {
            ALOGI("retry IJKAudioKit: AudioSessionSetProperty(kAudioSessionProperty_OverrideCategoryMixWithOthers) failed (%d) \n", (int)status);
            return NO;
        }
        status = AudioSessionSetActiveWithFlags(active, kAudioSessionSetActiveFlag_NotifyOthersOnDeactivation);
        if (status != noErr) {
            ALOGI("retry IJKAudioKit: AudioSessionSetActive(true) failed (%d) \n", (int)status);
            return NO;
        }
        UInt32 allowMixing2 = false;
        status = AudioSessionSetProperty(kAudioSessionProperty_OverrideCategoryMixWithOthers, sizeof(allowMixing2), &allowMixing2);
        if (status != noErr) {
            ALOGI("retry IJKAudioKit: AudioSessionSetProperty(kAudioSessionProperty_OverrideCategoryMixWithOthers) failed (%d) \n", (int)status);
            return NO;
        }
        status = noErr;
    } else if(status == kAudioSessionNotInitialized) {
        ALOGI("retry IJKAudioKit: setActive kAudioSessionNotInitialized \n");
        [self initializeAudioSession];
        status = AudioSessionSetActiveWithFlags(active, kAudioSessionSetActiveFlag_NotifyOthersOnDeactivation);
    }
    ALOGI("retry IJKAudioKit: setActive ret (%d) \n", (int)status);
    return status == noErr;
    
#endif
    
}


static void IjkAudioSessionInterruptionListener(void *inClientData, UInt32 inInterruptionState)
{
//    id<IJKAudioSessionDelegate> delegate = [IJKAudioKit sharedInstance]->_delegate;
//    if (delegate == nil)
//        return;
//    switch (inInterruptionState) {
//        case kAudioSessionBeginInterruption: {
//            NSLog(@"kAudioSessionBeginInterruption\n");
//            dispatch_async(dispatch_get_main_queue(), ^{
//                // AudioSessionSetActive(false);
//                ALOGI("ijkAudioBeginInterruption \n");
//                [delegate ijkAudioBeginInterruption];
//            });
//            break;
//        }
//        case kAudioSessionEndInterruption: {
//            NSLog(@"kAudioSessionEndInterruption\n");
//            dispatch_async(dispatch_get_main_queue(), ^{
//                //AudioSessionSetActive(true);
//                ALOGI("ijkAudioEndInterruption \n");
//                [delegate ijkAudioEndInterruption];
//            });
//            break;
//        }
//    }
}

@end
