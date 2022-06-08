/*****************************************************************************
 * ijksdl_audio.h
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

#ifndef IJKCCSDL__IJKCCSDL_AUDIO_H
#define IJKCCSDL__IJKCCSDL_AUDIO_H

#include "ijksdl_stdinc.h"
#include "ijksdl_endian.h"

typedef uint16_t CCSDL_AudioFormat;

#define CCSDL_AUDIO_MASK_BITSIZE       (0xFF)
#define CCSDL_AUDIO_MASK_DATATYPE      (1<<8)
#define CCSDL_AUDIO_MASK_ENDIAN        (1<<12)
#define CCSDL_AUDIO_MASK_SIGNED        (1<<15)
#define CCSDL_AUDIO_BITSIZE(x)         (x & CCSDL_AUDIO_MASK_BITSIZE)
#define CCSDL_AUDIO_ISFLOAT(x)         (x & CCSDL_AUDIO_MASK_DATATYPE)
#define CCSDL_AUDIO_ISBIGENDIAN(x)     (x & CCSDL_AUDIO_MASK_ENDIAN)
#define CCSDL_AUDIO_ISSIGNED(x)        (x & CCSDL_AUDIO_MASK_SIGNED)
#define CCSDL_AUDIO_ISINT(x)           (!CCSDL_AUDIO_ISFLOAT(x))
#define CCSDL_AUDIO_ISLITTLEENDIAN(x)  (!CCSDL_AUDIO_ISBIGENDIAN(x))
#define CCSDL_AUDIO_ISUNSIGNED(x)      (!CCSDL_AUDIO_ISSIGNED(x))

#define AUDIO_INVALID   0x0000
#define AUDIO_U8        0x0008  /**< Unsigned 8-bit samples */
#define AUDIO_S8        0x8008  /**< Signed 8-bit samples */
#define AUDIO_U16LSB    0x0010  /**< Unsigned 16-bit samples */
#define AUDIO_S16LSB    0x8010  /**< Signed 16-bit samples */
#define AUDIO_U16MSB    0x1010  /**< As above, but big-endian byte order */
#define AUDIO_S16MSB    0x9010  /**< As above, but big-endian byte order */
#define AUDIO_U16       AUDIO_U16LSB
#define AUDIO_S16       AUDIO_S16LSB

#define AUDIO_S32LSB    0x8020  /**< 32-bit integer samples */
#define AUDIO_S32MSB    0x9020  /**< As above, but big-endian byte order */
#define AUDIO_S32       AUDIO_S32LSB

#define AUDIO_F32LSB    0x8120  /**< 32-bit floating point samples */
#define AUDIO_F32MSB    0x9120  /**< As above, but big-endian byte order */
#define AUDIO_F32       AUDIO_F32LSB

#if CCSDL_BYTEORDER == CCSDL_LIL_ENDIAN
#define AUDIO_U16SYS    AUDIO_U16LSB
#define AUDIO_S16SYS    AUDIO_S16LSB
#define AUDIO_S32SYS    AUDIO_S32LSB
#define AUDIO_F32SYS    AUDIO_F32LSB
#else
#define AUDIO_U16SYS    AUDIO_U16MSB
#define AUDIO_S16SYS    AUDIO_S16MSB
#define AUDIO_S32SYS    AUDIO_S32MSB
#define AUDIO_F32SYS    AUDIO_F32MSB
#endif

typedef void (*CCSDL_AudioCallback) (void *userdata, Uint8 * stream,
                                   int len);

typedef struct CCSDL_AudioSpec
{
    int freq;                   /**< DSP frequency -- samples per second */
    CCSDL_AudioFormat format;     /**< Audio data format */
    Uint8 channels;             /**< Number of channels: 1 mono, 2 stereo */
    Uint8 silence;              /**< Audio buffer silence value (calculated) */
    Uint16 samples;             /**< Audio buffer size in samples (power of 2) */
    Uint16 padding;             /**< NOT USED. Necessary for some compile environments */
    Uint32 size;                /**< Audio buffer size in bytes (calculated) */
    CCSDL_AudioCallback callback;
    void *userdata;
} CCSDL_AudioSpec;

void CCSDL_CalculateAudioSpec(CCSDL_AudioSpec * spec);

#endif
