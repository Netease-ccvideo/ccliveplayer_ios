/*****************************************************************************
 * ijksdl_aout.h
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

#ifndef IJKCCSDL__IJKCCSDL_AOUT_H
#define IJKCCSDL__IJKCCSDL_AOUT_H

#include "ijksdl_audio.h"
#include "ijksdl_class.h"
#include "ijksdl_mutex.h"

typedef struct CCSDL_Aout_Opaque CCSDL_Aout_Opaque;
typedef struct CCSDL_Aout CCSDL_Aout;
typedef struct CCSDL_Aout {
    CCSDL_mutex *mutex;
    double     minimal_latency_seconds;

    CCSDL_Class       *opaque_class;
    CCSDL_Aout_Opaque *opaque;
    void (*free_l)(CCSDL_Aout *vout);
    int (*open_audio)(CCSDL_Aout *aout, CCSDL_AudioSpec *desired, CCSDL_AudioSpec *obtained);
    void (*pause_audio)(CCSDL_Aout *aout, int pause_on);
    void (*flush_audio)(CCSDL_Aout *aout);
    void (*set_volume)(CCSDL_Aout *aout, float left, float right);
    void (*close_audio)(CCSDL_Aout *aout);

    double (*func_get_latency_seconds)(CCSDL_Aout *aout);
    void   (*func_set_default_latency_seconds)(CCSDL_Aout *aout, double latency);
#ifdef __APPLE__
    int    (*func_get_audio_persecond_callbacks)(CCSDL_Aout *aout);
    void   (*func_set_playback_rate)(CCSDL_Aout *aout, float playbackRate);
    void   (*func_set_playback_volume)(CCSDL_Aout *aout, float playbackVolume);
#endif
} CCSDL_Aout;

int CCSDL_AoutOpenAudio(CCSDL_Aout *aout, const CCSDL_AudioSpec *desired, CCSDL_AudioSpec *obtained);
void CCSDL_AoutPauseAudio(CCSDL_Aout *aout, int pause_on);
void CCSDL_AoutFlushAudio(CCSDL_Aout *aout);
void CCSDL_AoutSetStereoVolume(CCSDL_Aout *aout, float left_volume, float right_volume);
void CCSDL_AoutCloseAudio(CCSDL_Aout *aout);
void CCSDL_AoutFree(CCSDL_Aout *aout);
void CCSDL_AoutFreeP(CCSDL_Aout **paout);

double CCSDL_AoutGetLatencySeconds(CCSDL_Aout *aout);
void   CCSDL_AoutSetDefaultLatencySeconds(CCSDL_Aout *aout, double latency);
#ifdef __APPLE__
// optional
int    SDL_AoutGetAudioPerSecondCallBacks(CCSDL_Aout *aout);
void   SDL_AoutSetPlaybackRate(CCSDL_Aout *aout, float playbackRate);
void   SDL_AoutSetPlaybackVolume(CCSDL_Aout *aout, float volume);
#endif
#endif
