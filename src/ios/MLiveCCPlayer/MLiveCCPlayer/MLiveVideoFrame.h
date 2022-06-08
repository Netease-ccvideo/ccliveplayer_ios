//
//  MLiveCCVideoFrame.h
//  IJKMediaPlayer
//
//  Created by jlubobo on 2017/5/26.
//  Copyright © 2017年 bilibili. All rights reserved.
//

#ifndef MLiveVideoFrame_h
#define MLiveVideoFrame_h

typedef struct MLiveVideoFrame {
    int w; /**< Read-only */
    int h; /**< Read-only */
    uint32_t format; /**< Read-only */
    int planes; /**< Read-only */
    uint16_t pitches[8]; /**< in bytes, Read-only */
    uint8_t *pixels[8];
    
} MLiveVideoFrame;


#endif /* MLiveVideoFrame_h */
