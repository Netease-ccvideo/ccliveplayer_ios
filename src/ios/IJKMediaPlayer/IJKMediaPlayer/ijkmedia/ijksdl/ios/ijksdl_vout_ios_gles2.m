/*
 * ijksdl_vout_ios_gles2.c
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

#import "ijksdl_vout_ios_gles2.h"

#include <assert.h>
#include "ijkutil/ijkutil.h"
#include "ijksdl/ijksdl_vout.h"
#include "ijksdl/ijksdl_vout_internal.h"
#include "ijksdl/ffmpeg/ijksdl_vout_overlay_ffmpeg.h"
#include "ijksdl_vout_overlay_videotoolbox.h"
#import "IJKSDLGLView.h"
#import "FrameProcessor.h"

typedef struct CCSDL_VoutSurface_Opaque {
    CCSDL_Vout *vout;
} CCSDL_VoutSurface_Opaque;

typedef struct CCSDL_Vout_Opaque {
    IJKSDLGLView *gl_view;
    FrameProcessor *fp;
    MLiveCCVideoFrame *frame;
    
} CCSDL_Vout_Opaque;

static CCSDL_VoutOverlay *vout_create_overlay_l(int width, int height, Uint32 format, CCSDL_Vout *vout,  int crop, int rotate)
{
    return CCSDL_VoutFFmpeg_CreateOverlay(width, height, format, vout, crop, 0, 0, rotate);
}

static CCSDL_VoutOverlay *vout_create_overlay(int width, int height, Uint32 format, CCSDL_Vout *vout, int crop, int surface_width, int surface_height, int rotate)
{
    switch (format) {
        case CCSDL_FCC__VTB:
            return CCSDL_VoutVideoToolBox_CreateOverlay(width, height, vout, crop, rotate);
        default:
            return CCSDL_VoutFFmpeg_CreateOverlay(width, height, format, vout, crop, 0, 0, rotate);
    }
}

static void vout_free_l(CCSDL_Vout *vout)
{
    if (!vout)
        return;

    CCSDL_Vout_Opaque *opaque = vout->opaque;
    if (opaque) {
        if (opaque->gl_view) {
            // TODO: post to MainThread?
            [opaque->gl_view release];
            opaque->gl_view = nil;
        }
        if (opaque->fp) {
            [opaque->fp release];
            opaque->fp = nil;
        }
        if(opaque->frame) {
            free(opaque->frame);
        }
        
    }

    CCSDL_Vout_FreeInternal(vout);
}

static void vout_clear_buffer(CCSDL_Vout *vout)
{
    if (!vout) {
        return;
    }
    CCSDL_Vout_Opaque *opaque = vout->opaque;
    if (opaque) {
        if (opaque->gl_view) {
            [opaque->gl_view clearbuffer];
        }
    }
}

static void voud_display_init_l(CCSDL_Vout *vout)
{
    CCSDL_Vout_Opaque *opaque = vout->opaque;
    IJKSDLGLView *gl_view = opaque->gl_view;
    
    if (!gl_view) {
        ALOGE("voud_display_overlay_l: NULL gl_view\n");
        return ;
    }
    
   // [gl_view displayInit];
}

static void voud_display_init(CCSDL_Vout *vout)
{
    @autoreleasepool {
        CCSDL_LockMutex(vout->mutex);
        voud_display_init_l(vout);
        CCSDL_UnlockMutex(vout->mutex);
    }
}

//static CVPixelBufferRef generatePixelBufferFromYUV(CCSDL_VoutOverlay *yuvFrame)
//{
//    NSUInteger uIndex,vIndex;
//    NSUInteger uvDataIndex;
//    CVPixelBufferRef pixelBuffer = nil;
//    CVReturn err;
//    int w = ((yuvFrame->w % 16 == 0) ? yuvFrame->w : (yuvFrame->w / 16 + 1) * 16);
//    int h = yuvFrame->h;
//    int lumaLen = yuvFrame->w * yuvFrame->h;
//    int chromaLen = (yuvFrame->w * yuvFrame->h) / 4;
//    
//    if (pixelBuffer == nil)
//    {
////        NSDictionary *pixelAttributes =  @{
////                                           (id) kCVPixelBufferOpenGLESCompatibilityKey : @(YES),
////                                           (id) kCVPixelBufferIOSurfacePropertiesKey : @{},
////                                           (id) kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)
////                                           };
//        NSDictionary *pixelAttributes = [NSDictionary dictionaryWithObjectsAndKeys:
//                                         [NSDictionary dictionary], (id)kCVPixelBufferIOSurfacePropertiesKey, nil];
//        err = CVPixelBufferCreate(kCFAllocatorDefault, w, h, kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,  (__bridge CFDictionaryRef)(pixelAttributes), &pixelBuffer);
//        if (err != 0) {
//            ALOGI(" Error at CVPixelBufferCreate %d \n", err);
//            return nil;
//        }
//    }
//    
//    if (pixelBuffer != nil)
//    {
//        CVPixelBufferLockBaseAddress(pixelBuffer, 0);
//        UInt8* yBaseAddress = CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0);
//        if (yBaseAddress != nil)
//        {
//            UInt8 *yDataPtr = yuvFrame->pixels[0];
//            // Y-plane data
//            memcpy(yBaseAddress, yDataPtr, lumaLen);
//        }
//        UInt8* uvBaseAddress = CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 1);
//        if (uvBaseAddress != nil)
//        {
//            UInt8* pUPointer = (UInt8*)(yuvFrame->pixels[1]);
//            UInt8* pVPointer = (UInt8*)(yuvFrame->pixels[2]);
//            // For the uv data, we need to interleave them as uvuvuvuv....
//            int iuvRow = (chromaLen * 2 / w);
//            int iHalfWidth = w / 2;
//            for(int i = 0;i < iuvRow; i++)
//            {
//                for(int j = 0; j<iHalfWidth; j++)
//                {
//                    // UV data for original frame.  Just interleave them.
//                    uvDataIndex = i * iHalfWidth + j;
//                    uIndex = (i * w) + (j * 2);
//                    vIndex = uIndex + 1;
//                    uvBaseAddress[uIndex] = pUPointer[uvDataIndex];
//                    uvBaseAddress[vIndex] = pVPointer[uvDataIndex];
//                }
//            }
//        }
//        CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
//    }
//    
//    return pixelBuffer;
//}

//    if(opaque->fp.onRenderFramePixelBufferRef) {
//        int w = ((overlay->w % 16 == 0) ? overlay->w : (overlay->w / 16 + 1) * 16);
//        int h = overlay->h;
//
//        if(opaque->pixelData != nil) {
//            CVPixelBufferRelease(opaque->pixelData);
//            opaque->pixelData = nil;
//        }
//
//        NSDictionary *pixelAttributes = [NSDictionary dictionaryWithObjectsAndKeys:
//                                         [NSDictionary dictionary], (id)kCVPixelBufferIOSurfacePropertiesKey, nil];
//        CVPixelBufferCreate(kCFAllocatorDefault, w, h, kCVPixelFormatType_32BGRA,  (__bridge CFDictionaryRef)pixelAttributes, &opaque->pixelData);
//        CVPixelBufferLockBaseAddress(opaque->pixelData, 0);
//        uint8_t *pixelData = (uint8_t*)CVPixelBufferGetBaseAddress(opaque->pixelData);
//        int rgbStride = w *4;
//        I420ToARGB(overlay->pixels[0], overlay->pitches[0],
//                   overlay->pixels[1], overlay->pitches[2],
//                   overlay->pixels[2], overlay->pitches[1],
//                   pixelData,
//                   rgbStride,
//                   w, h);
//
//        CVPixelBufferUnlockBaseAddress(opaque->pixelData, 0);
//        opaque->fp.onRenderFramePixelBufferRef(opaque->pixelData);
//    }

static int voud_display_overlay_l(CCSDL_Vout *vout, CCSDL_VoutOverlay *overlay)
{
    
    CCSDL_Vout_Opaque *opaque = vout->opaque;
    if (!overlay || !overlay->opaque || !overlay->opaque_class) {
        NSLog(@"invalid pipeline\n");
        return 0;
    }
    
    if(opaque->fp.onRenderFrame) {
        if(opaque->frame == NULL)
            opaque->frame = malloc(sizeof(MLiveCCVideoFrame));
        
        opaque->frame->pixels = overlay->pixels;
        opaque->frame->pitches = overlay->pitches;
        opaque->frame->rotate = overlay->rotate;
        opaque->frame->planeWidths = overlay->planeWidths;
        opaque->frame->planeHeights = overlay->planeHeights;
        opaque->frame->format = overlay->format;
        opaque->frame->w = overlay->w;
        opaque->frame->h = overlay->h;
        opaque->frame->planes = overlay->planes;
        opaque->frame->is_private = overlay->is_private;
        opaque->frame->pixel_buffer = overlay->pixel_buffer;
        opaque->frame->opaque_name = overlay->opaque_class->name;
        if(opaque->fp.onRenderFrame) {
            opaque->fp.onRenderFrame(opaque->frame);
        }
        
        return 0;
    }
    else
    {
        IJKSDLGLView *gl_view = opaque->gl_view;
        
        if (!gl_view) {
            ALOGE("voud_display_overlay_l: NULL gl_view\n");
            return -1;
        }
        
        if (!overlay) {
            ALOGE("voud_display_overlay_l: NULL overlay\n");
            return -1;
        }

        if (overlay->w <= 0 || overlay->h <= 0) {
            ALOGE("voud_display_overlay_l: invalid overlay dimensions(%d, %d)\n", overlay->w, overlay->h);
            return -1;
        }
        
        [gl_view displayVout:vout overlay:overlay];
    }
    return 0;
}



static int voud_display_overlay(CCSDL_Vout *vout, CCSDL_VoutOverlay *overlay)
{
    @autoreleasepool {
        CCSDL_LockMutex(vout->mutex);
        int retval = voud_display_overlay_l(vout, overlay);
        CCSDL_UnlockMutex(vout->mutex);
        return retval;
    }
}

static int vout_on_pre_render_frame(CCSDL_Vout *vout, CCSDL_VoutOverlay *overlay)
{
    if (vout->opaque && vout->opaque->fp) {
        FrameProcessor *fp = vout->opaque->fp;
        if (fp.onPreRenderFrame) {
            return fp.onPreRenderFrame();
        }
    }
    return 0;
}

static int vout_on_post_render_frame(CCSDL_Vout *vout, CCSDL_VoutOverlay *overlay)
{
    if (vout->opaque && vout->opaque->fp) {
        FrameProcessor *fp = vout->opaque->fp;
        if (fp.onPostRenderFrame) {
            return fp.onPostRenderFrame();
        }
    }
    return 0;
}

static int vout_on_bind_frame_buffer(CCSDL_Vout *vout, CCSDL_VoutOverlay *overlay)
{
    if (vout->opaque && vout->opaque->fp) {
        FrameProcessor *fp = vout->opaque->fp;
        if (fp.onBindFrameBuffer) {
            return fp.onBindFrameBuffer();
        }
    }
    return 0;
}

CCSDL_Vout *CCSDL_VoutIos_CreateForGLES2()
{
    CCSDL_Vout *vout = CCSDL_Vout_CreateInternal(sizeof(CCSDL_Vout_Opaque));
    if (!vout)
        return NULL;

    CCSDL_Vout_Opaque *opaque = vout->opaque;
    opaque->gl_view = nil;

    vout->create_overlay = vout_create_overlay;
    vout->free_l = vout_free_l;
    vout->clear_buffer = vout_clear_buffer;
    vout->display_init = voud_display_init;
    vout->display_overlay = voud_display_overlay;
    vout->on_bind_frame_buffer = vout_on_bind_frame_buffer;
    vout->on_pre_render_frame = vout_on_pre_render_frame;
    vout->on_post_render_frame = vout_on_post_render_frame;
    return vout;
}

static void CCSDL_VoutIos_SetGLView_l(CCSDL_Vout *vout, IJKSDLGLView *view)
{
    CCSDL_Vout_Opaque *opaque = vout->opaque;

    if (opaque->gl_view == view)
        return;

    if (opaque->gl_view) {
        [opaque->gl_view release];
        opaque->gl_view = nil;
    }

    if (view)
        opaque->gl_view = [view retain];
}

void CCSDL_VoutIos_SetGLView(CCSDL_Vout *vout, IJKSDLGLView *view)
{
    CCSDL_LockMutex(vout->mutex);
    CCSDL_VoutIos_SetGLView_l(vout, view);
    CCSDL_UnlockMutex(vout->mutex);
}

static void CCSDL_VoutIos_SetFrameProcessor_l(CCSDL_Vout *vout, FrameProcessor *fp)
{
    CCSDL_Vout_Opaque *opaque = vout->opaque;
    if (opaque) {
        if (opaque->fp == fp) {
            return;
        }
        if (opaque->fp) {
            [opaque->fp release];
            opaque->fp = nil;
        }
        if (fp) {
            opaque->fp = [fp retain];
        }
        opaque->fp = fp;
    }
}

void CCSDL_VoutIos_SetFrameProcessor(CCSDL_Vout *vout, FrameProcessor *fp)
{
    CCSDL_LockMutex(vout->mutex);
    CCSDL_VoutIos_SetFrameProcessor_l(vout, fp);
    CCSDL_UnlockMutex(vout->mutex);
}
