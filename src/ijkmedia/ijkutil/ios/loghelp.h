/*****************************************************************************
 * loghelper.h
 *****************************************************************************
 *
 * copyright (c) 2013 Zhang Rui <bbcallen@gmail.com>
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

#ifndef IJKUTIL_IOS__LOGHELP_H
#define IJKUTIL_IOS__LOGHELP_H

#include <stdio.h>
#include <mach/mach_time.h>

#ifdef __cplusplus
extern "C" {
#endif
    
#define IJK_LOG_TAG "CCPlayer"
    
#define IJK_LOG_UNKNOWN     0
#define IJK_LOG_DEFAULT     1
#define LOG_ENABLE          1
#define IJK_LOG_VERBOSE     2
#define IJK_LOG_DEBUG       3
#define IJK_LOG_INFO        4
#define IJK_LOG_WARN        5
#define IJK_LOG_ERROR       6
#define IJK_LOG_FATAL       7
#define IJK_LOG_SILENT      8
#define IJK_LOG_FORCE       9
    
    extern int ijk_logLevel;
    
#define VLOG(level, TAG, ...)  \
do { \
if (LOG_ENABLE) { \
vprintf(__VA_ARGS__); \
} \
} while(0)
#define VLOGV(...)  VLOG(IJK_LOG_VERBOSE,   IJK_LOG_TAG, __VA_ARGS__)
#define VLOGD(...)  VLOG(IJK_LOG_DEBUG,     IJK_LOG_TAG, __VA_ARGS__)
#define VLOGI(...)  VLOG(IJK_LOG_INFO,      IJK_LOG_TAG, __VA_ARGS__)
#define VLOGW(...)  VLOG(IJK_LOG_WARN,      IJK_LOG_TAG, __VA_ARGS__)
#define VLOGE(...)  VLOG(IJK_LOG_ERROR,     IJK_LOG_TAG, __VA_ARGS__)
    
    static uint64_t getPlayerCurrentTimeTick() {
        mach_timebase_info_data_t info = {0};
        if (mach_timebase_info(&info) != KERN_SUCCESS)
        {
            return 0;
        }
        uint64_t sec =( mach_absolute_time() * info.numer / info.denom ) / 1000 / 1000;
        return sec;
    }
    
#define ALOG(level, tag, fmt, ...) \
do { \
if (LOG_ENABLE) { \
if (level >= ijk_logLevel) { \
printf("[%s] %d " fmt, tag, (int)getPlayerCurrentTimeTick(), ##__VA_ARGS__); \
} \
} \
} while(0)
    
#define ALOGV(fmt, ...)  ALOG(IJK_LOG_VERBOSE,   IJK_LOG_TAG, fmt, ##__VA_ARGS__)
#define ALOGD(fmt, ...)  ALOG(IJK_LOG_DEBUG,     IJK_LOG_TAG, fmt, ##__VA_ARGS__)
#define ALOGI(fmt, ...)  ALOG(IJK_LOG_INFO,      IJK_LOG_TAG, fmt, ##__VA_ARGS__)
#define ALOGW(fmt, ...)  ALOG(IJK_LOG_WARN,      IJK_LOG_TAG, fmt, ##__VA_ARGS__)
#define ALOGE(fmt, ...)  ALOG(IJK_LOG_ERROR,     IJK_LOG_TAG, fmt, ##__VA_ARGS__)
#define ALOGF(fmt, ...)  ALOG(IJK_LOG_FORCE,     IJK_LOG_TAG, fmt, ##__VA_ARGS__)
    
#define LOG_ALWAYS_FATAL(...)   do { ALOGE(__VA_ARGS__); exit(1); } while (0)
    
#ifdef __cplusplus
}
#endif

#endif
