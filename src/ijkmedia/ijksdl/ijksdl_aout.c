/*****************************************************************************
 * ijksdl_aout.c
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

#include "ijksdl_aout.h"
#include <stdlib.h>
#include "ijkplayer/ff_ffplay_def.h"

int CCSDL_AoutOpenAudio(CCSDL_Aout *aout, const CCSDL_AudioSpec *desired, CCSDL_AudioSpec *obtained)
{
    if (aout && desired && aout->open_audio)
        return aout->open_audio(aout, (CCSDL_AudioSpec*)desired, obtained);

    return -1;
}

void CCSDL_AoutPauseAudio(CCSDL_Aout *aout, int pause_on)
{
    if (aout && aout->pause_audio)
        aout->pause_audio(aout, pause_on);
}

void CCSDL_AoutFlushAudio(CCSDL_Aout *aout)
{
    if (aout && aout->flush_audio)
        aout->flush_audio(aout);
}

void CCSDL_AoutSetStereoVolume(CCSDL_Aout *aout, float left_volume, float right_volume)
{
    if (aout && aout->set_volume)
        aout->set_volume(aout, left_volume, right_volume);
}

void CCSDL_AoutCloseAudio(CCSDL_Aout *aout)
{
    if (aout && aout->close_audio)
        return aout->close_audio(aout);
}

void CCSDL_AoutFree(CCSDL_Aout *aout)
{
    if (!aout)
        return;

    if (aout->free_l)
        aout->free_l(aout);
    else
        free(aout);
}

void CCSDL_AoutFreeP(CCSDL_Aout **paout)
{
    if (!paout)
        return;

    CCSDL_AoutFree(*paout);
    *paout = NULL;
}

double CCSDL_AoutGetLatencySeconds(CCSDL_Aout *aout)
{
    if (!aout)
        return 0;

    if (aout->func_get_latency_seconds)
        return aout->func_get_latency_seconds(aout);

    return aout->minimal_latency_seconds;
}

void CCSDL_AoutSetDefaultLatencySeconds(CCSDL_Aout *aout, double latency)
{
    if (aout) {
        if (aout->func_set_default_latency_seconds)
            aout->func_set_default_latency_seconds(aout, latency);
        aout->minimal_latency_seconds = latency;
    }
}

#ifdef __APPLE__
void SDL_AoutSetPlaybackRate(CCSDL_Aout *aout, float playbackRate)
{
    if (aout) {
        if (aout->func_set_playback_rate)
            aout->func_set_playback_rate(aout, playbackRate);
    }
}

void SDL_AoutSetPlaybackVolume(CCSDL_Aout *aout, float volume)
{
    if (aout) {
        if (aout->func_set_playback_volume)
            aout->func_set_playback_volume(aout, volume);
    }
}

int SDL_AoutGetAudioPerSecondCallBacks(CCSDL_Aout *aout)
{
    if (aout) {
        if (aout->func_get_audio_persecond_callbacks) {
            return aout->func_get_audio_persecond_callbacks(aout);
        }
    }
    return CCSDL_AUDIO_MAX_CALLBACKS_PER_SEC;
}

#endif
