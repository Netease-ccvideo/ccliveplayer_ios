//
//  MLiveDeviceMgr.h
//  MLiveCCPlayer
//
//  Created by jlubobo on 2017/4/28.
//  Copyright © 2017年 cc. All rights reserved.
//

#import <Foundation/Foundation.h>

@interface MLiveDeviceMgr : NSObject

+ (NSString *)sn;

+ (NSString *)unisdkUdid;

+ (NSString *)generateRandomString:(NSInteger)length;

+ (NSString *)devModelName;


@end
