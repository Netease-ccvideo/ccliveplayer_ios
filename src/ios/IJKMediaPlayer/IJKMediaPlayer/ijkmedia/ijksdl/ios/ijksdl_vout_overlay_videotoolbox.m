/*****************************************************************************
 * ijksdl_vout_overlay_videotoolbox.m
 *****************************************************************************
 *
 * copyright (c) 2014 ZhouQuan <zhouqicy@gmail.com>
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

#include "ijksdl_vout_overlay_videotoolbox.h"

#include <assert.h>
#include "ijksdl_stdinc.h"
#include "ijksdl_mutex.h"
#include "ijksdl_vout_internal.h"
#include "ijksdl_video.h"


struct CCSDL_VoutOverlay_Opaque {
    CCSDL_mutex *mutex;
    CVPixelBufferRef pixel_buffer;
    Uint16 pitches[AV_NUM_DATA_POINTERS];
    Uint16 planeWidths[2];
    Uint16 planeHeights[2];
    Uint8 *pixels[AV_NUM_DATA_POINTERS];
};


static void func_free_l(CCSDL_VoutOverlay *overlay)
{
    if (!overlay)
        return;
    CCSDL_VoutOverlay_Opaque *opaque = overlay->opaque;
    if (!opaque)
        return;
    overlay->unref(overlay);
    if (opaque->mutex)
        CCSDL_DestroyMutex(opaque->mutex);

    CCSDL_VoutOverlay_FreeInternal(overlay);
}

static int func_lock(CCSDL_VoutOverlay *overlay)
{
    CCSDL_VoutOverlay_Opaque *opaque = overlay->opaque;
    return CCSDL_LockMutex(opaque->mutex);
}

static int func_unlock(CCSDL_VoutOverlay *overlay)
{
    CCSDL_VoutOverlay_Opaque *opaque = overlay->opaque;
    return CCSDL_UnlockMutex(opaque->mutex);
}

static void func_unref(CCSDL_VoutOverlay *overlay)
{
    if (!overlay) {
        return;
    }
    CCSDL_VoutOverlay_Opaque *opaque = overlay->opaque;
    if (!opaque) {
        return;
    }

    CVBufferRelease(opaque->pixel_buffer);

    opaque->pixel_buffer = NULL;
    overlay->pixel_buffer = NULL;
    overlay->pixels[0] = NULL;
    overlay->pixels[1] = NULL;

    return;
}

static int func_fill_frame(CCSDL_VoutOverlay *overlay, const AVFrame *frame)
{
    assert(frame->format == CCSDL_FCC__VTB);

    CVBufferRef pixel_buffer = CVBufferRetain(frame->opaque);
    CCSDL_VoutOverlay_Opaque *opaque = overlay->opaque;
    if (opaque->pixel_buffer != NULL) {
        CVBufferRelease(opaque->pixel_buffer);
        overlay->pixel_buffer = NULL;
    }
    opaque->pixel_buffer = pixel_buffer;
    overlay->pixel_buffer = pixel_buffer;
    overlay->format = CCSDL_FCC__VTB;
    overlay->planes = 2;

#if 1
    if (CVPixelBufferLockBaseAddress(pixel_buffer, 0) != kCVReturnSuccess) {
        overlay->pixels[0]  = NULL;
        overlay->pixels[1]  = NULL;
        overlay->pitches[0] = 0;
        overlay->pitches[1] = 0;
        overlay->w = 0;
        overlay->h = 0;
        overlay->pixel_buffer = NULL;
        CVBufferRelease(pixel_buffer);
        opaque->pixel_buffer = NULL;
        return -1;
    }
    overlay->pixels[0]  = CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, 0);
    overlay->pixels[1]  = CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, 1);
    
    overlay->pitches[0] = CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 0);
    overlay->pitches[1] = CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 1);
    
    overlay->planeWidths[0] = CVPixelBufferGetWidthOfPlane(pixel_buffer, 0);
    overlay->planeWidths[1] = CVPixelBufferGetWidthOfPlane(pixel_buffer, 1);
    
    overlay->planeHeights[0] = CVPixelBufferGetHeightOfPlane(pixel_buffer, 0);
    overlay->planeHeights[1] = CVPixelBufferGetHeightOfPlane(pixel_buffer, 1);
    
    CVPixelBufferUnlockBaseAddress(pixel_buffer, 0);
#else
    overlay->pixels[0]  = NULL;
    overlay->pixels[1]  = NULL;
    overlay->pitches[0] = 0;
    overlay->pitches[1] = 0;
    
#endif
    overlay->is_private = 1;
    overlay->w = (int)frame->width;
    overlay->h = (int)frame->height;
    return 0;
}

static CCSDL_Class g_vout_overlay_videotoolbox_class = {
    .name = "VideoToolboxVoutOverlay",
};

static bool check_object(CCSDL_VoutOverlay* object, const char *func_name)
{
    if (!object || !object->opaque || !object->opaque_class) {
        ALOGE("%s: invalid pipeline\n", func_name);
        return false;
    }

    if (object->opaque_class != &g_vout_overlay_videotoolbox_class) {
        ALOGE("%s.%s: unsupported method\n", object->opaque_class->name, func_name);
        return false;
    }

    return true;
}

CVPixelBufferRef CCSDL_VoutOverlayVideoToolBox_GetCVPixelBufferRef(CCSDL_VoutOverlay *overlay)
{
    if (!check_object(overlay, __func__))
        return NULL;

    CCSDL_VoutOverlay_Opaque *opaque = overlay->opaque;
    return opaque->pixel_buffer;
}

CCSDL_VoutOverlay *CCSDL_VoutVideoToolBox_CreateOverlay(int width, int height, CCSDL_Vout *display, int crop, int rotate)
{
    SDLTRACE("CCSDL_VoutVideoToolBox_CreateOverlay(w=%d, h=%d, fmt=_VTB, dp=%p)\n",
             width, height, display);
    CCSDL_VoutOverlay *overlay = CCSDL_VoutOverlay_CreateInternal(sizeof(CCSDL_VoutOverlay_Opaque));
    if (!overlay) {
        ALOGE("overlay allocation failed");
        return NULL;
    }
    CCSDL_VoutOverlay_Opaque *opaque = overlay->opaque;
    overlay->opaque_class = &g_vout_overlay_videotoolbox_class;
    overlay->format     = CCSDL_FCC__VTB;
    overlay->w          = width;
    overlay->h          = height;
    overlay->pitches    = opaque->pitches;
    overlay->pixels     = opaque->pixels;
    
    overlay->planeWidths = opaque->planeWidths;
    overlay->planeHeights = opaque->planeHeights;
    
    overlay->is_private = 1;
    
    overlay->free_l             = func_free_l;
    overlay->lock               = func_lock;
    overlay->unlock             = func_unlock;
    overlay->unref              = func_unref;
    overlay->func_fill_frame    = func_fill_frame;
    overlay->crop = crop;
    overlay->rotate = rotate;
    
    opaque->mutex = CCSDL_CreateMutex();
    return overlay;
}
