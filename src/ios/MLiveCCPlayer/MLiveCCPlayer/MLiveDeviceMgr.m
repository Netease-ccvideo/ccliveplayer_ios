//
//  MLiveDeviceMgr.m
//  MLiveCCPlayer
//
//  Created by jlubobo on 2017/4/28.
//  Copyright © 2017年 cc. All rights reserved.
//

#import "MLiveDeviceMgr.h"
#import "MLiveCCKeyChain.h"
#import <sys/utsname.h>
#import <AdSupport/ASIdentifierManager.h>
#import <UIKit/UIKit.h>

static NSString *g_currentIp = @"0.0.0.0";
static NSString *g_sn = nil;
static NSString *g_mac = nil;

@implementation MLiveDeviceMgr

+(NSString *)generateRandomString:(NSInteger)length {
    NSString *string = [[NSString alloc] init];
    for (NSInteger i = 0; i < length; i++) {
        NSInteger number = arc4random() % 36;
        if (number < 10) {
            NSString *tempString = [NSString stringWithFormat:@"%zd", number];
            string = [string stringByAppendingString:tempString];
        }else {
            char character = number - 10 + 97;
            NSString *tempString = [NSString stringWithFormat:@"%c", character];
            string = [string stringByAppendingString:tempString];
        }
    }
    return string;
}

+ (NSString *)sn {
    @synchronized(self) {
        if (g_sn) {
            return g_sn;
        }
        
        g_sn = [MLiveCCKeyChain getData:k_KEYCHAIN_SN];
        
        if (!g_sn) {
            g_sn = [NSString stringWithFormat:@"4%@", [MLiveDeviceMgr generateRandomString:15]];
            [MLiveCCKeyChain setData:k_KEYCHAIN_SN data:g_sn];
        }
        return g_sn;
    }
}

#define IsGreaterThanOrEqualToIOS10     ([[[UIDevice currentDevice] systemVersion] intValue] >= 10)

+ (NSString*)unisdkUdid {
    // UniSDK那边使用idfa作为udid
    NSString *udid = [[[ASIdentifierManager sharedManager] advertisingIdentifier] UUIDString];
    BOOL isEnabled = [[ASIdentifierManager sharedManager] isAdvertisingTrackingEnabled];
    // iOS10系统，如果用户限制广告追踪，idfa返回00000000-0000-0000-0000-000000000000。UniSDK那边使用idfv作为idfa
    if (IsGreaterThanOrEqualToIOS10 && !isEnabled) {
        udid = [[[UIDevice currentDevice] identifierForVendor] UUIDString];
        // idfv在某些情况会为nil
        if (!udid) {
            udid = @"";
        }
    }
    return udid;
}

+ (NSString *)devModelName {

    struct utsname systemInfo;
    uname(&systemInfo);
    NSString *code = [NSString stringWithCString:systemInfo.machine encoding:NSUTF8StringEncoding];
    if ([code isEqualToString:@"x86_64"] || [code isEqualToString:@"i386"]) {
        code = @"Simulator";
    }
    
    return code;
}


@end
