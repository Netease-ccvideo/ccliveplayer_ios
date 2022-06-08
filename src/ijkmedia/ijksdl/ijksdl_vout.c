/*****************************************************************************
 * ijksdl_vout.c
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

#include "ijksdl_vout.h"
#include <stdlib.h>

#include <assert.h>
#if defined(__ANDROID__)
#include <android/native_window_jni.h>
#endif

void CCSDL_VoutFree(CCSDL_Vout *vout)
{
    if (!vout)
        return;

    if (vout->free_l) {
        vout->free_l(vout);
    } else {
        free(vout);
    }
}

void CCSDL_VoutFreeP(CCSDL_Vout **pvout)
{
    if (!pvout)
        return;

    CCSDL_VoutFree(*pvout);
    *pvout = NULL;
}

void CCSDL_VoutDisplayInit(CCSDL_Vout *vout)
{
    if (vout && vout->display_init){
        vout->display_init(vout);
    }
}

int CCSDL_VoutDisplayYUVOverlay(CCSDL_Vout *vout, CCSDL_VoutOverlay *overlay)
{
    if (vout && overlay && vout->display_overlay) {
        return vout->display_overlay(vout, overlay);
    }

    return -1;
}

int CCSDL_VoutSetOverlayFormat(CCSDL_Vout *vout, Uint32 overlay_format)
{
    if (!vout)
        return -1;
    
    vout->overlay_format = overlay_format;
    return 0;
}

CCSDL_VoutOverlay *CCSDL_Vout_CreateOverlay(int width, int height, Uint32 format, CCSDL_Vout *vout, int crop, int surface_width, int surface_height, int rotate)
{
    if (vout && vout->create_overlay)
        return vout->create_overlay(width, height, format, vout, crop, surface_width, surface_height, rotate);
    
    return NULL;
}

int CCSDL_VoutLockYUVOverlay(CCSDL_VoutOverlay *overlay)
{
    if (overlay && overlay->lock)
        return overlay->lock(overlay);

    return -1;
}

int CCSDL_VoutUnlockYUVOverlay(CCSDL_VoutOverlay *overlay)
{
    if (overlay && overlay->unlock)
        return overlay->unlock(overlay);

    return -1;
}

void CCSDL_VoutFreeYUVOverlay(CCSDL_VoutOverlay *overlay)
{
    if (!overlay)
        return;

    if (overlay->free_l) {
        overlay->free_l(overlay);
    } else {
        free(overlay);
    }
}

void CCSDL_VoutUnrefYUVOverlay(CCSDL_VoutOverlay *overlay)
{
    if (overlay && overlay->unref)
        overlay->unref(overlay);
}

int CCSDL_VoutFillFrameYUVOverlay(CCSDL_VoutOverlay *overlay, const AVFrame *frame)
{
    if (!overlay || !overlay->func_fill_frame)
        return -1;
    
    return overlay->func_fill_frame(overlay, frame);
}
