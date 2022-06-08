//
//  PlayerConfig.h
//  IJKMediaPlayer
//
//  Created by luobiao on 16/4/29.
//  Copyright © 2016年 bilibili. All rights reserved.
//

#ifndef PlayerConfig_h
#define PlayerConfig_h

#ifdef __cplusplus
extern "C" {
#endif
    
#define MAX_IDENTITY_LENGTH 32
#define MAX_VIDEO_URL_LENGTH 256
#define MAX_SID_LENGTH 128
#define MAX_VERSION_LENGTH 64
#define MAX_SERVER_IP_LENGTH 32
#define MAX_STREAM_ID_LENGTH 64
#define MAX_CDN_LENGTH 32
#define MAX_SRC_LENGTH 16
#define MAX_MOBILE_IPV4_LENGTH 32
#define MAX_URS_LENGTH 128
#define MAX_GAMESERVER_LENGTH 128
#define MAX_UDID_LENGTH 512
#define MAX_DESC_LENGTH 4096
    
    typedef struct PlayerConfig {
        int eid;//观众eid
        int64_t uid;//观众uid
//        int game_uid;//游戏uid
        int ccid;//观众ccid
        int anchorCCid;//主播ccid
        long anchorUid;
        int templateType;//模版类型
        int roomId;//房间号
        int subId;//子频道号
        int context;//保留，可以填0
        char identity[MAX_IDENTITY_LENGTH];//填player
        char videoUrl[MAX_VIDEO_URL_LENGTH];//视频地址
        char sid[MAX_SID_LENGTH];//设备唯一标识号
        char clientNo[MAX_SID_LENGTH];//设备唯一标识号2
        char version[MAX_VERSION_LENGTH];//sdk版本号
        char cdn[MAX_CDN_LENGTH];//cdn类型
        char src[MAX_SRC_LENGTH];//填ccios
        char urs[MAX_URS_LENGTH];//观众urs
        int panorama; //标记是否是全景模式
        int gametype; //游戏类型
//        char gameServer[MAX_GAMESERVER_LENGTH];
//        char udid[MAX_UDID_LENGTH];
        char desc[MAX_DESC_LENGTH];
        
    } PlayerConfig;
    
    
#ifdef __cplusplus
}
#endif


#endif /* PlayerConfig_h */
