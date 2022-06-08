/*****************************************************************************
 * ijksdl_vout_dummy.c
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

#include "ijksdl_vout_dummy.h"

#include "ijkutil/ijkutil.h"
#include "../ijksdl_vout.h"
#include "../ijksdl_vout_internal.h"

typedef struct CCSDL_VoutSurface_Opaque {
    CCSDL_Vout *vout;
} CCSDL_VoutSurface_Opaque;

typedef struct CCSDL_Vout_Opaque {
    char dummy;
} CCSDL_Vout_Opaque;

static void vout_free_l(CCSDL_Vout *vout)
{
    if (!vout)
        return;

    CCSDL_Vout_Opaque *opaque = vout->opaque;
    if (opaque) {
    }

    CCSDL_Vout_FreeInternal(vout);
}

static int voud_display_overlay_l(CCSDL_Vout *vout, CCSDL_VoutOverlay *overlay)
{
    return 0;
}

static int voud_display_overlay(CCSDL_Vout *vout, CCSDL_VoutOverlay *overlay)
{
    CCSDL_LockMutex(vout->mutex);
    int retval = voud_display_overlay_l(vout, overlay);
    CCSDL_UnlockMutex(vout->mutex);
    return retval;
}

CCSDL_Vout *CCSDL_VoutDummy_Create()
{
    CCSDL_Vout *vout = CCSDL_Vout_CreateInternal(sizeof(CCSDL_Vout_Opaque));
    if (!vout)
        return NULL;

    // CCSDL_Vout_Opaque *opaque = vout->opaque;

    vout->free_l = vout_free_l;
    vout->display_overlay = voud_display_overlay;

    return vout;
}
