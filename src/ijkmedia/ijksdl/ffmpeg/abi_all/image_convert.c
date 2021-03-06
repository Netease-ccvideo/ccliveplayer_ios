/*****************************************************************************
 * yuv_rgb.c : ARM NEONv1 YUV to RGB32 chroma conversion for VLC
 *****************************************************************************
 * Copyright (C) 2011 Sébastien Toque
 *                    Rémi Denis-Courmont
 * Copyright (C) 2013 Zhang Rui <bbcallen@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include "../ijksdl_image_convert.h"
#if defined(__ANDROID__)
#include "libyuv.h"
#endif

int ijk_image_convert(int width, int height,
    enum AVPixelFormat dst_format, uint8_t **dst_data, int *dst_linesize,
    enum AVPixelFormat src_format, const uint8_t **src_data, int *src_linesize)
{
#if defined(__ANDROID__)
    switch (src_format) {
        case AV_PIX_FMT_YUV420P:
        case AV_PIX_FMT_YUVJ420P: // FIXME: 9 not equal to AV_PIX_FMT_YUV420P, but a workaround
            switch (dst_format) {
            case AV_PIX_FMT_RGB565:
                return I420ToRGB565(
                    src_data[0], src_linesize[0],
                    src_data[1], src_linesize[1],
                    src_data[2], src_linesize[2],
                    dst_data[0], dst_linesize[0],
                    width, height);
            case AV_PIX_FMT_0BGR32:
                return I420ToABGR(
                    src_data[0], src_linesize[0],
                    src_data[1], src_linesize[1],
                    src_data[2], src_linesize[2],
                    dst_data[0], dst_linesize[0],
                    width, height);
            default:
                break;
            }
            break;
        default:
            break;
    }
#endif
    return -1;
}

int ijk_argb_scale(const uint8_t* src_argb, int src_stride_argb,
    int src_width, int src_height,
    uint8_t* dst_argb, int dst_stride_argb,
    int dst_width, int dst_height)
{
#if defined(__ANDROID__)
   return ARGBScale(src_argb, src_stride_argb, src_width, src_height, dst_argb, dst_stride_argb, dst_width, dst_height, kFilterNone);
#endif
    return -1;
}
