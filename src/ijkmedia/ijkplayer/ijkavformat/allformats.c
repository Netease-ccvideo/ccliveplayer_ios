//
//  allformats.c
//  IJKMediaPlayer
//
//  Created by cc on 06/04/2021.
//  Copyright © 2021 bilibili. All rights reserved.
//

#include <stdio.h>
#include "libavformat/avformat.h"
#include "libavformat/url.h"
#include "libavformat/version.h"



#define IJK_REGISTER_PROTOCOL(x)                                        \
    {                                                                   \
        extern URLProtocol ijkimp_ff_##x##_protocol;                        \
        int ijkav_register_##x##_protocol(URLProtocol *protocol, int protocol_size);\
        ijkav_register_##x##_protocol(&ijkimp_ff_##x##_protocol, sizeof(URLProtocol));  \
    }

void ijkav_register_all(void)
{
    static int initialized;

    if (initialized)
        return;
    initialized = 1;

    av_register_all();

    /* protocols */
    av_log(NULL, AV_LOG_INFO, "===== custom modules begin =====\n");
//    IJK_REGISTER_PROTOCOL(ccfilehook);
    IJK_REGISTER_PROTOCOL(ijklongurl);
    av_log(NULL, AV_LOG_INFO, "===== custom modules end =====\n");
}
