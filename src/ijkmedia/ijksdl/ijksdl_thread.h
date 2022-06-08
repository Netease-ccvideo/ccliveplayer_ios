/*****************************************************************************
 * ijksdl_thread.h
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

#ifndef IJKCCSDL__IJKCCSDL_THREAD_H
#define IJKCCSDL__IJKCCSDL_THREAD_H

#include <stdint.h>
#include <pthread.h>

typedef enum {
    CCSDL_THREAD_PRIORITY_LOW,
    CCSDL_THREAD_PRIORITY_NORMAL,
    CCSDL_THREAD_PRIORITY_HIGH
} CCSDL_ThreadPriority;

typedef struct CCSDL_Thread
{
    pthread_t id;
    int (*func)(void *);
    void *data;
    char name[32];
    int retval;
} CCSDL_Thread;

CCSDL_Thread *CCSDL_CreateThreadEx(CCSDL_Thread *thread, int (*fn)(void *), void *data, const char *name);
int         CCSDL_SetThreadPriority(CCSDL_ThreadPriority priority);
void        CCSDL_WaitThread(CCSDL_Thread *thread, int *status);

#endif
