/*
 * ijksdl_ffinc.h
 *      ffmpeg headers
 *
 * Copyright (c) 2013 Zhang Rui <bbcallen@gmail.com>
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

#ifndef IJKCCSDL__FFMPEG__IJKCCSDL_IMAGE_CONVERT_H
#define IJKCCSDL__FFMPEG__IJKCCSDL_IMAGE_CONVERT_H

#include <stdint.h>
#include "ijksdl_inc_ffmpeg.h"

int ijk_image_convert(int width, int height,
    enum AVPixelFormat dst_format, uint8_t **dst_data, int *dst_linesize,
    enum AVPixelFormat src_format, const uint8_t **src_data, int *src_linesize);

int ijk_argb_scale(const uint8_t* src_argb, int src_stride_argb,
    int src_width, int src_height,
    uint8_t* dst_argb, int dst_stride_argb,
    int dst_width, int dst_height);

#endif
