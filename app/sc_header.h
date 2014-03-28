/*
 * Copyright(C) 2014 Ruijie Network. All rights reserved.
 */
/*
 * header.h
 * Original Author: zhaoyao@ruijie.com.cn, 2014-03-11
 *
 * Hot cache application's common header file.
 *
 * ATTENTION:
 *     1. xxx
 *
 * History
 */

#ifndef __SC_HEADER_H__
#define __SC_HEADER_H__

#define SC_NGX_ROOT_PATH                "/data/usb0/"
#define SC_NGX_ROOT_PATH_LEN            11
#define SC_NGX_DEFAULT_IP_ADDR          "127.0.0.1"
#define SC_NGX_DEFAULT_PORT             8089

#define SC_CLIENT_DEFAULT_IP_ADDR       "0.0.0.0"
#define SC_SNOOP_MOD_DEFAULT_IP_ADDR    "20.0.0.1"
#define SC_SNOOPING_SND_RCV_BUF_LEN     1024

#define SC_KF_FLV_READ_MAX              (32*1024)
#define SC_KF_FLV_MAX_NUM               256

typedef struct sc_kf_flv_info_s {
	unsigned int file_pos;	/* 偏移量，单位字节 */
	unsigned int time;		/* 时间，单位秒 */
} sc_kf_flv_info_t;

#include "sc_resource.h"

int sc_kf_flv_create_info(sc_res_info_t *ri);

/* 在关键帧列表中，根据时间查找对应的偏移量
 *@keyframe已有序，按时间从小到大
 * return对应的偏移量
 */
static inline unsigned int sc_kf_flv_seek_offset(unsigned int targetTime,
                                                 sc_kf_flv_info_t *keyframe,
                                                 unsigned int key_num)
{
    /* 顺序查找，返回比targetTime小的最后一个值 */
    unsigned int index;
    unsigned int i;

    /* zhaoyao TODO: performance should be optimized */
    for (i = 1 ; i < key_num; i++) {
        if (keyframe[i].time > targetTime) {
            break;
        }
    }

    index = i - 1;
    return keyframe[index].file_pos;

#if 0
    /* 二分查找，返回相隔最近的值 */
    unsigned int left = 0, right = 0;
    unsigned int midIndex, mid;
    unsigned int midValue, last_judge;

    for (right = key_num - 1; left != right;) {
        midIndex = (right + left) / 2;
        mid = (right - left);
        midValue = keyframe[midIndex].time ;
        if (midValue == targetTime) {
            return keyframe[midIndex].file_pos;
        }

        if(targetTime > midValue){
            left = midIndex;
        } else {
            right = midIndex;
        }
       
        if (mid <= 2) {
           break;
        }        
    }

    last_judge = (keyframe[right].time - keyframe[left].time) / 2;
    return (last_judge > targetTime ? keyframe[right].file_pos : keyframe[left].file_pos);
#endif
}

int sc_ngx_download(char *ngx_ip, char *url);

void sc_snooping_serve(int sockfd);
int sc_snooping_do_add(sc_res_info_t *ri);

int sc_get_yk_video(char *url, sc_res_info_t *origin);
int sc_url_is_yk(char *url);

#endif /* __SC_HEADER_H__ */

