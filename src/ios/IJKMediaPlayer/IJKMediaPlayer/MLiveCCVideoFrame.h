//
//  MLiveCCVideoFrame.h
//  IJKMediaPlayer
//
//  Created by jlubobo on 2017/5/26.
//  Copyright © 2017年 bilibili. All rights reserved.
//

#ifndef MLiveCCVideoFrame_h
#define MLiveCCVideoFrame_h

typedef struct MLiveCCVideoFrame {
    int w; /**< Read-only */
    int h; /**< Read-only */
    uint32_t format; /**< Read-only */
    int planes; /**< Read-only */
    uint16_t *pitches; /**< in bytes, Read-only */
    uint8_t **pixels; /**< Read-write */
    int is_private;
    void *pixel_buffer;
    uint16_t *planeWidths;
    uint16_t *planeHeights;
    const char *opaque_name;
    int rotate;
    
} MLiveCCVideoFrame;


#endif /* MLiveCCVideoFrame_h */
