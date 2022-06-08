//
//  hevc_parser.h
//  IJKMediaPlayer
//
//  Created by cc on 2019/7/29.
//  Copyright © 2019 bilibili. All rights reserved.
//
#ifndef hevc_parser_h
#define hevc_parser_h

#include <stdio.h>
#include "hevc_stream.h"

const int MAX_NAL_SIZE = 1 * 1024 * 1024;
const int OUTPUT_SIZE = 512 * 1024;


typedef struct
{
    unsigned int num;               // 序号
    unsigned int len;               // 含起始码的总的长度
    unsigned int offset;       // nal包在文件中的偏移
    int sliceType;               // 帧类型
    int nalType;            // NAL类型
    int startcodeLen;             // start code长度
    char startcodeBuffer[16];         // 起始码，字符串形式
} NALU_t;

typedef struct
{
    int profile_idc;
    int level_idc;
    int width;
    int height;
    int crop_left;
    int crop_right;
    int crop_top;
    int crop_bottom;
    float max_framerate;  // 由SPS计算得到的帧率，为0表示SPS中没有相应的字段计算
    int chroma_format_idc;  // YUV颜色空间 0: monochrome 1:420 2:422 3:444
}SPSInfo_t;

typedef struct
{
    int encoding_type;  // 为1表示CABAC 0表示CAVLC

}PPSInfo_t;


h265_stream_t* m_hH265;
uint8_t* m_naluData;
char m_tmpStore[1024];
char m_outputInfo[OUTPUT_SIZE];

int init()
{
    memset(m_tmpStore, '\0', 1024);
    memset(m_outputInfo, '\0', OUTPUT_SIZE);
    m_hH265 = h265_new();
    return 0;
}

void parse_NALU(uint8_t* buffer, NALU_t * nalu)
{
    h265_read_nal_unit(m_hH265, buffer, nalu->len);
    nalu->nalType = m_hH265->nal->nal_unit_type;
}


bool hevc_parse(uint8_t* buffer, uint32_t extrasize, int32_t *max_ref_frames)
{
    
    init();
    NALU_t n;
    memset(&n, '\0', sizeof(NALU_t));
    int offset = 28;
    
    if(extrasize < offset){
        return false;
    }
    unsigned int vps_length = (buffer[offset-2] * 256) + buffer[offset-1];
    n.len = vps_length;
    //parse_NALU(&buffer[offset], &n);
    
    if(extrasize < offset)
    {
        return false;
    }
    
    offset += vps_length + 5;
    unsigned int sps_length = (buffer[offset-2] * 256) + buffer[offset-1];
    n.len = sps_length;
    parse_NALU(&buffer[offset], &n);
    *max_ref_frames = m_hH265->sps->sps_max_dec_pic_buffering_minus1[0];
    ALOGI("%s - hevc sps parse complete--->get max_ref(%d)\n", __FUNCTION__, *max_ref_frames);
    /*
    if(extrasize < offset){
        return false;
    }
    offset += sps_length + 5;
    unsigned int pps_length = (buffer[offset-2] * 256) + buffer[offset-1];
    n.len = pps_length;
    parse_NALU(&buffer[offset], &n);
    */
    h265_free(m_hH265);
    return true;
}
#endif


