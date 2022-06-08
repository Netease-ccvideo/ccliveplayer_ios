//
//  ccfilehook.c
//  IJKMediaPlayer
//
//  Created by cc on 01/04/2021.
//  Copyright © 2021 bilibili. All rights reserved.
//

#include <stdio.h>
#include "libavformat/avformat.h"
#include "libavformat/url.h"
#include "libavutil/avstring.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
//#include "libavutil/libm.h"
#include "libavutil/application.h"
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct Context {
    const AVClass *class;
    int fd;
    int trunc;
    int blocksize;
    int follow;
    int sliceoffset;
    int slicelength;
    int seekpos;
#if HAVE_DIRENT_H
    DIR *dir;
#endif
} Context;

static int file_cc_parse(URLContext *h, const char *filename)
{
    Context *c = h->priv_data;
    const char *offsetChStart = NULL;
    const char *offsetChEnd = NULL;
    char tmpBuf[512];
    int len = 0;
    
    offsetChStart = strstr(filename, "ccsliceoffset");
    if (offsetChStart == NULL) {
        return -1;
    }
    offsetChStart += strlen("ccsliceoffset");
    if (*offsetChStart != '=') {
        return -2;
    }
    offsetChStart++;
    offsetChEnd = strchr(offsetChStart, '&');
    if (offsetChEnd)
        len = offsetChEnd - offsetChStart;
    else
        len = strlen(offsetChStart);
    
    memcpy(tmpBuf, offsetChStart, len);
    tmpBuf[len] = 0;
    int64_t sliceoffset = atol(tmpBuf);
    
    offsetChStart = NULL;
    offsetChEnd = NULL;
    memset(tmpBuf, 0, 512);
    int len2 = 0;
    offsetChStart = strstr(filename, "ccslicelength");
    if (offsetChStart == NULL) {
        return -3;
    }
    offsetChStart += strlen("ccslicelength");
    if (*offsetChStart != '=') {
        return -4;
    }
    offsetChStart++;
    offsetChEnd = strchr(offsetChStart, '&');
    if (offsetChEnd) {
        len2 = offsetChEnd - offsetChStart;
    }
    else {
        len2 = strlen(offsetChStart);
    }
    memcpy(tmpBuf, offsetChStart, len2);
    tmpBuf[len2] = 0;
    
    int64_t slicelength= atol(tmpBuf);
    av_log(NULL, AV_LOG_ERROR, "[cc] file_cc_parse tmpBuf %s len2 %d sliceoffset %ld  slicelength %ld\n", tmpBuf, len2, sliceoffset, slicelength);
    int cclen = strlen("ccsliceoffset") + strlen("ccslicelength") + len + len2 + 3;
    if(sliceoffset <= 0 || slicelength <= 0) {
        return -5;
    }
    
    c->sliceoffset = sliceoffset;
    c->slicelength = slicelength;
    
    return cclen;
}

int avpriv_open(const char *filename, int flags, ...);

static int ccfilehook_open(URLContext *h, const char *filename, int flags){
    Context *c = h->priv_data;
    int access;
    int fd;
    struct stat st;
    av_strstart(filename, "ijkmediadatasource:", &filename);
    char filepath[4096];
    memset(filepath, 0 ,sizeof(filepath));
    int ret = file_cc_parse(h, filename);
    if(ret > 0) {
        int len = strlen(filename) - ret;
        av_strlcpy(filepath, filename, len);
        av_log(NULL, AV_LOG_ERROR, "[cc] file_open %s\n", filepath);
    } else {
        av_strlcpy(filepath, filename, sizeof(filepath));
    }
    if (flags & AVIO_FLAG_WRITE && flags & AVIO_FLAG_READ) {
        access = O_CREAT | O_RDWR;
        if (c->trunc)
            access |= O_TRUNC;
    } else if (flags & AVIO_FLAG_WRITE) {
        access = O_CREAT | O_WRONLY;
        if (c->trunc)
            access |= O_TRUNC;
    } else {
        access = O_RDONLY;
    }
#ifdef O_BINARY
    access |= O_BINARY;
#endif
    fd = avpriv_open(filepath, access, 0666);
    if (fd == -1)
        return AVERROR(errno);
    c->fd = fd;
    
    h->is_streamed = !fstat(fd, &st) && S_ISFIFO(st.st_mode);
    
    return 0;
}

/* XXX: use llseek */
static int64_t ccfilehook_seek(URLContext *h, int64_t pos, int whence)
{
    printf("ccfilehook_seek  pos:%lld\n", pos);
    Context *c = h->priv_data;
    int64_t ret;
    if (whence == AVSEEK_SIZE) {
        struct stat st;
        ret = fstat(c->fd, &st);
        int size = ret < 0 ? AVERROR(errno) : (S_ISFIFO(st.st_mode) ? 0 : st.st_size);
        if(c->slicelength != 0) {
            size = FFMIN(size, c->slicelength);
            //            av_log(NULL, AV_LOG_ERROR, "[cc] file_seek size %d \n", size);
        }
        return size;
    }
    
    int fixpos = pos + c->sliceoffset;
    ret = lseek(c->fd, fixpos, whence);
    c->seekpos = fixpos;
    //    av_log(NULL, AV_LOG_ERROR, "[cc] file_seek pos %d whence %d seekpos %d \n", pos, whence, c->seekpos);
    return ret < 0 ? AVERROR(errno) : ret;
}

void ijkmp_ijkmediadatasource_read_data(unsigned char *data, int size, int fd);
static int ccfilehook_read(URLContext *h, unsigned char *buf, int size){
    Context *c = h->priv_data;
    int ret;
//    size = FFMIN(size, c->blocksize);
    size = 32768;
    
    int slicelength = c->slicelength;
    int sliceoffset = c->sliceoffset;
    int seekpos = c->seekpos;
    if(sliceoffset != 0 && seekpos == 0) {
        if (sliceoffset == AVSEEK_SIZE) {
            struct stat st;
            int ret = fstat(c->fd, &st);
            int size = ret < 0 ? AVERROR(errno) : (S_ISFIFO(st.st_mode) ? 0 : st.st_size);
            if(c->slicelength != 0) {
                size = FFMIN(size, c->slicelength);
                //                av_log(NULL, AV_LOG_ERROR, "[cc] file_seek size %d \n", size);
            }
            return size;
        }
        int ret = lseek(c->fd, sliceoffset, SEEK_SET);
        seekpos = sliceoffset;
    }
    
    ret = read(c->fd, buf, size);
    ijkmp_ijkmediadatasource_read_data(buf, size, c->fd);
    
    if (ret == 0 && c->follow)
        return AVERROR(EAGAIN);
    return (ret == -1) ? AVERROR(errno) : ret;
}

static int64_t ccfilehook_write(URLContext *h, const unsigned char *buf, int size){
    Context *c = h->priv_data;
    int ret;
    size = FFMIN(size, c->blocksize);
    ret = write(c->fd, buf, size);
    return (ret == -1) ? AVERROR(errno) : ret;
}

static int ccfilehook_close(URLContext *h){
    Context *c = h->priv_data;
    return close(c->fd);
}

static const AVOption file_options[] = {
//    { "truncate", "truncate existing files on write", offsetof(Context, trunc), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, AV_OPT_FLAG_ENCODING_PARAM },
    { "blocksize", "set I/O operation maximum block size", offsetof(Context, blocksize), AV_OPT_TYPE_INT, { .i64 = INT_MAX }, 1, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { "follow", "Follow a file as it is being written", offsetof(Context, follow), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, AV_OPT_FLAG_DECODING_PARAM },
    { NULL }
};

static const AVClass ccfilehook_context_class = {
    .class_name = "IJKMediaDataSource",
    .item_name  = av_default_item_name,
    .option     = file_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

//这里借助ijkplayer-ffmepg 中预定义好的 ijklongurl（自定义URLProtocol，在ijk后续版本中有实现，但当前版本没有，只在ffmpeg中有定义）
URLProtocol ijkimp_ff_ijklongurl_protocol = {
    .name                = "ijkmediadatasource",
    .url_open            = ccfilehook_open,
    .url_read            = ccfilehook_read,
    .url_write           = ccfilehook_write,
    .url_seek            = ccfilehook_seek,
    .url_close           = ccfilehook_close,
    
    .priv_data_size      = sizeof(Context),
    .priv_data_class     = &ccfilehook_context_class,
    .default_whitelist   = "file,crypto,ijkmediadatasource"
};
