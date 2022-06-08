/*****************************************************************************
 * ijksdl_vout.h
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

#ifndef IJKCCSDL__IJKCCSDL_VOUT_H
#define IJKCCSDL__IJKCCSDL_VOUT_H

#include "ijksdl_stdinc.h"
#include "ijksdl_class.h"
#include "ijksdl_mutex.h"
#include "ijksdl_video.h"
#include "ffmpeg/ijksdl_inc_ffmpeg.h"

typedef struct CCSDL_VoutOverlay_Opaque CCSDL_VoutOverlay_Opaque;
typedef struct CCSDL_VoutOverlay CCSDL_VoutOverlay;
typedef struct CCSDL_VoutOverlay {
    bool crop;
    bool reset_padding;
    int wanted_display_width;
    int wanted_display_height;
    int crop_padding_horizontal;
    int crop_padding_vertical;
    int buff_w;
    int buff_h;
    int w; /**< Read-only */
    int h; /**< Read-only */
    Uint32 format; /**< Read-only */
    int planes; /**< Read-only */
    Uint16 *pitches; /**< in bytes, Read-only */
    Uint8 **pixels; /**< Read-write */
    
    Uint16 *planeWidths;
    Uint16 *planeHeights;
    
    int rotate;
    
    int is_private;
    
    int sar_num;
    int sar_den;

    CCSDL_Class               *opaque_class;
    CCSDL_VoutOverlay_Opaque  *opaque;
    void                    (*free_l)(CCSDL_VoutOverlay *overlay);
    int                     (*lock)(CCSDL_VoutOverlay *overlay);
    int                     (*unlock)(CCSDL_VoutOverlay *overlay);
    void                    (*unref)(CCSDL_VoutOverlay *overlay);
    
    void                    *pixel_buffer;
    
    int     (*func_fill_frame)(CCSDL_VoutOverlay *overlay, const AVFrame *frame);
    
} CCSDL_VoutOverlay;

typedef struct CCSDL_Vout_Opaque CCSDL_Vout_Opaque;
typedef struct CCSDL_Vout CCSDL_Vout;
typedef struct CCSDL_Vout {
    CCSDL_mutex *mutex;
	CCSDL_cond  *con;

    CCSDL_Class       *opaque_class;
    CCSDL_Vout_Opaque *opaque;
    CCSDL_VoutOverlay *(*create_overlay)(int width, int height, Uint32 format, CCSDL_Vout *vout, int crop, int surface_width, int surface_height, int roate);
    void (*free_l)(CCSDL_Vout *vout);
    void (*clear_buffer)(CCSDL_Vout *vout);//for ios player sdk only
    void (*display_init)(CCSDL_Vout *vout);
    int (*display_overlay)(CCSDL_Vout *vout, CCSDL_VoutOverlay *overlay);
    int (*on_bind_frame_buffer)(CCSDL_Vout *vout, CCSDL_VoutOverlay *overlay);
    int (*on_pre_render_frame)(CCSDL_Vout *vout, CCSDL_VoutOverlay *overlay);
    int (*on_post_render_frame)(CCSDL_Vout *vout, CCSDL_VoutOverlay *overlay);
    Uint32 overlay_format;
    int panorama;
} CCSDL_Vout;

void CCSDL_VoutFree(CCSDL_Vout *vout);
void CCSDL_VoutFreeP(CCSDL_Vout **pvout);
void CCSDL_VoutDisplayInit(CCSDL_Vout *vout);
int CCSDL_VoutDisplayYUVOverlay(CCSDL_Vout *vout, CCSDL_VoutOverlay *overlay);
int  CCSDL_VoutSetOverlayFormat(CCSDL_Vout *vout, Uint32 overlay_format);

CCSDL_VoutOverlay *CCSDL_Vout_CreateOverlay(int width, int height, Uint32 format, CCSDL_Vout *vout, int crop, int surface_width, int surface_height, int rotate);
int CCSDL_VoutLockYUVOverlay(CCSDL_VoutOverlay *overlay);
int CCSDL_VoutUnlockYUVOverlay(CCSDL_VoutOverlay *overlay);
void CCSDL_VoutFreeYUVOverlay(CCSDL_VoutOverlay *overlay);
void CCSDL_VoutUnrefYUVOverlay(CCSDL_VoutOverlay *overlay);
int  CCSDL_VoutFillFrameYUVOverlay(CCSDL_VoutOverlay *overlay, const AVFrame *frame);

#endif
