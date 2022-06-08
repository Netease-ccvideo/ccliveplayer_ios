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

#ifndef IJKCCSDL__IJKCCSDL_TIMER_H
#define IJKCCSDL__IJKCCSDL_TIMER_H

#include "ijksdl_stdinc.h"

void CCSDL_Delay(Uint32 ms);

Uint64 CCSDL_GetTickHR(void);


typedef struct CCSDL_Profiler
{
    int64_t total_elapsed;
    int     total_counter;
    
    int64_t sample_elapsed;
    int     sample_counter;
    float   sample_per_seconds;
    int64_t average_elapsed;
    
    int64_t begin_time;
    
    int     max_sample;
} CCSDL_Profiler;

void    CCSDL_ProfilerReset(CCSDL_Profiler* profiler, int max_sample);
void    CCSDL_ProfilerBegin(CCSDL_Profiler* profiler);
int64_t CCSDL_ProfilerEnd(CCSDL_Profiler* profiler);

typedef struct CCSDL_SpeedSampler
{
    Uint64  samples[10];
    
    int     capacity;
    int     count;
    int     first_index;
    int     next_index;
    
    Uint64  last_log_time;
} CCSDL_SpeedSampler;

void  CCSDL_SpeedSamplerReset(CCSDL_SpeedSampler *sampler);
// return samples per seconds
float CCSDL_SpeedSamplerAdd(CCSDL_SpeedSampler *sampler, int enable_log, const char *log_tag);

#endif
