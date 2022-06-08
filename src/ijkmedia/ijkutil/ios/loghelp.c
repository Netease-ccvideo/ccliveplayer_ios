//
//  loghelp.c
//  IJKMediaPlayer
//
//  Created by jlubobo on 2016/11/21.
//  Copyright © 2016年 bilibili. All rights reserved.
//

#include <stdio.h>
#include "loghelp.h"

#ifdef DEBUG
int ijk_logLevel = IJK_LOG_INFO;
#else
int ijk_logLevel = IJK_LOG_SILENT;
#endif
