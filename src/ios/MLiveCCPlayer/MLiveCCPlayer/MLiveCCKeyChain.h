//
//  CCKeyChain.h
//  CC-iPhone
//
//  Created by luogui on 16/8/1.
//  Copyright © 2016年 netease. All rights reserved.
//

#import <Foundation/Foundation.h>

#define k_KEYCHAIN_SN       @"com.163.cc.keychain.sn"
#define k_KEYCHAIN_MAC      @"com.163.cc.keychain.mac"

typedef void(^MLiveCCKeyChainErrorBlock)(NSString *service, OSStatus status);

/*
 * MLiveCCKeyChain
 * 用来从keychain中读取或者保存App变量
 */
@interface MLiveCCKeyChain : NSObject

+ (void)setErrorBlock:(MLiveCCKeyChainErrorBlock)block;

+ (BOOL)setData:(NSString*)service data:(id)data;

+ (id)getData:(NSString*)service;

@end
