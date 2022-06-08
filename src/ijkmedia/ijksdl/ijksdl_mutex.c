/*****************************************************************************
 * ijksdl_mutex.c
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

#include "ijksdl_mutex.h"
#include <errno.h>
#include <assert.h>
#include <sys/time.h>
#include "ijksdl_inc_internal.h"

CCSDL_mutex *CCSDL_CreateMutex(void)
{
    CCSDL_mutex *mutex;
    mutex = (CCSDL_mutex *) mallocz(sizeof(CCSDL_mutex));
    if (!mutex)
        return NULL;

    if (pthread_mutex_init(&mutex->id, NULL) != 0) {
        free(mutex);
        return NULL;
    }

    return mutex;
}

void CCSDL_DestroyMutex(CCSDL_mutex *mutex)
{
    if (mutex) {
        pthread_mutex_destroy(&mutex->id);
        free(mutex);
    }
}

void CCSDL_DestroyMutexP(CCSDL_mutex **mutex)
{
    if (mutex) {
        CCSDL_DestroyMutex(*mutex);
        *mutex = NULL;
    }
}

int CCSDL_LockMutex(CCSDL_mutex *mutex)
{
    assert(mutex);
    if (!mutex)
        return -1;

    return pthread_mutex_lock(&mutex->id);
}

int CCSDL_UnlockMutex(CCSDL_mutex *mutex)
{
    assert(mutex);
    if (!mutex)
        return -1;

    return pthread_mutex_unlock(&mutex->id);
}

CCSDL_cond *CCSDL_CreateCond(void)
{
    CCSDL_cond *cond;
    cond = (CCSDL_cond *) mallocz(sizeof(CCSDL_cond));
    if (!cond)
        return NULL;

    if (pthread_cond_init(&cond->id, NULL) != 0) {
        free(cond);
        return NULL;
    }

    return cond;
}

void CCSDL_DestroyCond(CCSDL_cond *cond)
{
    if (cond) {
        pthread_cond_destroy(&cond->id);
        free(cond);
    }
}

void CCSDL_DestroyCondP(CCSDL_cond **cond)
{

    if (cond) {
        CCSDL_DestroyCond(*cond);
        *cond = NULL;
    }
}

int CCSDL_CondSignal(CCSDL_cond *cond)
{
    assert(cond);
    if (!cond)
        return -1;

    return pthread_cond_signal(&cond->id);
}

int CCSDL_CondBroadcast(CCSDL_cond *cond)
{
    assert(cond);
    if (!cond)
        return -1;

    return pthread_cond_broadcast(&cond->id);
}

int CCSDL_CondWaitTimeout(CCSDL_cond *cond, CCSDL_mutex *mutex, uint32_t ms)
{
    int retval;
    struct timeval delta;
    struct timespec abstime;

    assert(cond);
    assert(mutex);
    if (!cond || !mutex) {
        return -1;
    }

    gettimeofday(&delta, NULL);

    abstime.tv_sec = delta.tv_sec + (ms / 1000);
    abstime.tv_nsec = (delta.tv_usec + (ms % 1000) * 1000) * 1000;
    if (abstime.tv_nsec > 1000000000) {
        abstime.tv_sec += 1;
        abstime.tv_nsec -= 1000000000;
    }

    while (1) {
        retval = pthread_cond_timedwait(&cond->id, &mutex->id, &abstime);
        if (retval == 0)
            return 0;
        else if (retval == EINTR)
            continue;
        else if (retval == ETIMEDOUT)
            return CCSDL_MUTEX_TIMEDOUT;
        else
            break;
    }

    return -1;
}

int CCSDL_CondWait(CCSDL_cond *cond, CCSDL_mutex *mutex)
{
    assert(cond);
    assert(mutex);
    if (!cond || !mutex)
        return -1;

    return pthread_cond_wait(&cond->id, &mutex->id);
}
