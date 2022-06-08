//
//  CCKeyChain.m
//  CC-iPhone
//
//  Created by luogui on 16/8/1.
//  Copyright © 2016年 netease. All rights reserved.
//

#import "MLiveCCKeyChain.h"

static MLiveCCKeyChainErrorBlock mMLiveCCKeyChainErrorBlock;

@implementation MLiveCCKeyChain


+ (NSMutableDictionary*)getKeychQuery:(NSString*)service {
    
    return [NSMutableDictionary dictionaryWithObjectsAndKeys:
            (__bridge_transfer id)kSecClassGenericPassword,
            (__bridge_transfer id)kSecClass,
            service, (__bridge_transfer id)kSecAttrService,
            service, (__bridge_transfer id)kSecAttrAccount,
            //(__bridge_transfer id)kSecAttrAccessibleAlways, (__bridge_transfer id)kSecAttrAccessible, //这个属性请不要添加，添加后会有读取失败的情况。
            nil];
}

+ (void)setErrorBlock:(MLiveCCKeyChainErrorBlock)block
{
    mMLiveCCKeyChainErrorBlock = block;
}

+ (BOOL)setData:(NSString*)service data:(id)data {
    NSMutableDictionary *keychainQuery = [self getKeychQuery:service];
    
    OSStatus st = SecItemDelete((__bridge_retained CFDictionaryRef)keychainQuery);
    
    [keychainQuery setObject: [NSKeyedArchiver archivedDataWithRootObject:data] forKey:(__bridge_transfer id)kSecValueData];
    
    st = SecItemAdd((__bridge_retained CFDictionaryRef)keychainQuery, NULL);
    
    if (st != errSecSuccess && mMLiveCCKeyChainErrorBlock) {
        mMLiveCCKeyChainErrorBlock(service, st);
    }
    
    return (st == errSecSuccess);
}

+ (id)getData:(NSString*)service {
    id ret = nil;
    NSMutableDictionary *keychainQuery = [self getKeychQuery:service];
    [keychainQuery setObject:(__bridge_transfer id)kCFBooleanTrue forKey:(__bridge_transfer id)kSecReturnData];
    [keychainQuery setObject:(__bridge_transfer id)kSecMatchLimitOne forKey:(__bridge_transfer id)kSecMatchLimit];
    
    CFDataRef keyData = NULL;
    OSStatus st = SecItemCopyMatching((__bridge_retained CFDictionaryRef)keychainQuery, (CFTypeRef*)&keyData);
    if (st == errSecSuccess) {
        @try {
            ret = [NSKeyedUnarchiver unarchiveObjectWithData:(__bridge NSData*)keyData];
        }
        @catch (NSException *e) {
            
        }
        @finally {
        };
    } else {
        
    }
    
    if (keyData) {
        CFRelease(keyData);
    }
    
    return ret;
}

@end
