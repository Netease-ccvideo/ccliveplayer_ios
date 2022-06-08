/*****************************************************************************
 * ijksdl_mutex.h
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

#ifndef IJKCCSDL__IJKCCSDL_MUTEX_H
#define IJKCCSDL__IJKCCSDL_MUTEX_H

#include <stdint.h>
#include <pthread.h>

#define CCSDL_MUTEX_TIMEDOUT  1
#define CCSDL_MUTEX_MAXWAIT   (~(uint32_t)0)

typedef struct CCSDL_mutex {
    pthread_mutex_t id;
} CCSDL_mutex;

CCSDL_mutex  *CCSDL_CreateMutex(void);
void        CCSDL_DestroyMutex(CCSDL_mutex *mutex);
void        CCSDL_DestroyMutexP(CCSDL_mutex **mutex);
int         CCSDL_LockMutex(CCSDL_mutex *mutex);
int         CCSDL_UnlockMutex(CCSDL_mutex *mutex);

typedef struct CCSDL_cond {
    pthread_cond_t id;
} CCSDL_cond;

CCSDL_cond   *CCSDL_CreateCond(void);
void        CCSDL_DestroyCond(CCSDL_cond *cond);
void        CCSDL_DestroyCondP(CCSDL_cond **mutex);
int         CCSDL_CondSignal(CCSDL_cond *cond);
int         CCSDL_CondBroadcast(CCSDL_cond *cond);
int         CCSDL_CondWaitTimeout(CCSDL_cond *cond, CCSDL_mutex *mutex, uint32_t ms);
int         CCSDL_CondWait(CCSDL_cond *cond, CCSDL_mutex *mutex);

#endif

