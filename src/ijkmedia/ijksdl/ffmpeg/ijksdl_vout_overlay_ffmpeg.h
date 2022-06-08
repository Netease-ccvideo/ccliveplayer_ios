/*****************************************************************************
 * ijksdl_vout_overlay_ffmpeg.h
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

#ifndef IJKCCSDL__FFMPEG__IJKCCSDL_VOUT_OVERLAY_FFMPEG_H
#define IJKCCSDL__FFMPEG__IJKCCSDL_VOUT_OVERLAY_FFMPEG_H

#include "../ijksdl_stdinc.h"
#include "../ijksdl_vout.h"
#include "ijksdl_inc_ffmpeg.h"

// TODO: 9 alignment to speed up memcpy when display
CCSDL_VoutOverlay *CCSDL_VoutFFmpeg_CreateOverlay(int width, int height, Uint32 format, CCSDL_Vout *vout, int crop, int surface_width, int surface_height, int rotate);

int CCSDL_VoutFFmpeg_ConvertFrame(
    CCSDL_VoutOverlay *overlay, AVFrame *frame,
    struct SwsContext **p_sws_ctx, int sws_flags);

int CCSDL_VoutFFmpeg_ConverI420ToARGB(CCSDL_VoutOverlay *overlay, struct SwsContext **p_sws_ctx, int sws_flags, uint8_t **data, int *pitch);

#endif
