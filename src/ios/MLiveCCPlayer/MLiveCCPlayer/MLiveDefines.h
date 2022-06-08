//
//  MLiveDefines.h
//  MLiveCCPlayer
//
//  Created by jlubobo on 2017/4/28.
//  Copyright © 2017年 cc. All rights reserved.
//

#ifndef MLiveDefines_h
#define MLiveDefines_h

#import <Foundation/Foundation.h>

#define WEAK_SELF_DECLARED              __typeof(&*self) __weak weakSelf = self;

#define STRONG_SELF_BEGIN               __typeof(&*weakSelf) strongSelf = weakSelf; \
if (strongSelf) {

#define STRONG_SELF_END                 }


#define MLIVECC_STRINGIZE(x) #x
#define MLIVECC_STRINGIZE2(x) MLIVECC_STRINGIZE(x)
#define MLIVECC_SHADER_STRING(text) @ MLIVECC_STRINGIZE2(text)

#define DECODE_FORMAT_I420 808596553
#define DECODE_FORMAT_VTB 1112823391

#define SDK_VERSION "505"

#endif /* MLiveDefines_h */
