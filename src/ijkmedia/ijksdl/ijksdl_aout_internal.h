/*****************************************************************************
 * ijksdl_aout_internal.h
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

#ifndef IJKCCSDL__IJKCCSDL_AOUT_INTERNAL_H
#define IJKCCSDL__IJKCCSDL_AOUT_INTERNAL_H

#include "ijksdl_mutex.h"
#include "ijksdl_aout.h"

inline static CCSDL_Aout *CCSDL_Aout_CreateInternal(size_t opaque_size)
{
    CCSDL_Aout *aout = (CCSDL_Aout*) mallocz(sizeof(CCSDL_Aout));
    if (!aout)
        return NULL;

    aout->opaque = mallocz(opaque_size);
    if (!aout->opaque) {
        free(aout);
        return NULL;
    }

    aout->mutex = CCSDL_CreateMutex();
    if (aout->mutex == NULL) {
        free(aout->opaque);
        free(aout);
        return NULL;
    }

    return aout;
}

inline static void CCSDL_Aout_FreeInternal(CCSDL_Aout *aout)
{
    if (!aout)
        return;

    if (aout->mutex) {
        CCSDL_DestroyMutex(aout->mutex);
    }

    free(aout->opaque);
    memset(aout, 0, sizeof(CCSDL_Aout));
    free(aout);
}

#endif
