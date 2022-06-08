/*****************************************************************************
 * ijksdl_fourcc.h
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

#ifndef IJKCCSDL__IJKCCSDL_FOURCC_H
#define IJKCCSDL__IJKCCSDL_FOURCC_H

#include "ijksdl_stdinc.h"
#include "ijksdl_endian.h"

#if CCSDL_BYTEORDER == CCSDL_LIL_ENDIAN
#   define CCSDL_FOURCC(a, b, c, d) \
        (((uint32_t)a) | (((uint32_t)b) << 8) | (((uint32_t)c) << 16) | (((uint32_t)d) << 24))
#   define CCSDL_TWOCC(a, b) \
        ((uint16_t)(a) | ((uint16_t)(b) << 8))
#else
#   define CCSDL_FOURCC(a, b, c, d) \
        (((uint32_t)d) | (((uint32_t)c) << 8) | (((uint32_t)b) << 16) | (((uint32_t)a) << 24))
#   define CCSDL_TWOCC( a, b ) \
        ((uint16_t)(b) | ((uint16_t)(a) << 8))
#endif

/*-
 *  http://www.webartz.com/fourcc/indexyuv.htm
 *  http://www.neuro.sfc.keio.ac.jp/~aly/polygon/info/color-space-faq.html
 *  http://www.fourcc.org/yuv.php
 */

// YUV formats
#define CCSDL_FCC_YV12    CCSDL_FOURCC('Y', 'V', '1', '2')  /**< bpp=12, Planar mode: Y + V + U  (3 planes) */
#define CCSDL_FCC_IYUV    CCSDL_FOURCC('I', 'Y', 'U', 'V')  /**< bpp=12, Planar mode: Y + U + V  (3 planes) */
#define CCSDL_FCC_I420    CCSDL_FOURCC('I', '4', '2', '0')  /**< bpp=12, Planar mode: Y + U + V  (3 planes) */
#define CCSDL_FCC_I444P10LE   CCSDL_FOURCC('I', '4', 'A', 'L')

#define CCSDL_FCC_YUV2    CCSDL_FOURCC('Y', 'U', 'V', '2')  /**< bpp=16, Packed mode: Y0+U0+Y1+V0 (1 plane) */
#define CCSDL_FCC_UYVY    CCSDL_FOURCC('U', 'Y', 'V', 'Y')  /**< bpp=16, Packed mode: U0+Y0+V0+Y1 (1 plane) */
#define CCSDL_FCC_YVYU    CCSDL_FOURCC('Y', 'V', 'Y', 'U')  /**< bpp=16, Packed mode: Y0+V0+Y1+U0 (1 plane) */

#define CCSDL_FCC_NV12    CCSDL_FOURCC('N', 'V', '1', '2')

// RGB formats
#define CCSDL_FCC_RV16    CCSDL_FOURCC('R', 'V', '1', '6')    /**< bpp=16, RGB565 */
#define CCSDL_FCC_RV24    CCSDL_FOURCC('R', 'V', '2', '4')    /**< bpp=24, RGBX8888 */
#define CCSDL_FCC_RV32    CCSDL_FOURCC('R', 'V', '3', '2')    /**< bpp=24, RGBX8888 */

// opaque formats
#define CCSDL_FCC__AMC    CCSDL_FOURCC('_', 'A', 'M', 'C')    /**< Android MediaCodec */
#define CCSDL_FCC__VTB    CCSDL_FOURCC('_', 'V', 'T', 'B')    /**< iOS VideoToolbox */
#define CCSDL_FCC__GLES2  CCSDL_FOURCC('_', 'E', 'S', '2')    /**< let Vout choose format */

// undefine
#define CCSDL_FCC_UNDF    CCSDL_FOURCC('U', 'N', 'D', 'F')    /**< undefined */

enum {
    IJK_AV_PIX_FMT__START = 10000,
    IJK_AV_PIX_FMT__ANDROID_MEDIACODEC,
    IJK_AV_PIX_FMT__VIDEO_TOOLBOX,
};

#endif
