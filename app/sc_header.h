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

#define SC_KF_FLV_READ_MAX              (32*1024)
#define SC_KF_FLV_MAX_NUM               256

typedef struct sc_kf_flv_info_s {
	unsigned int file_pos;	/* ƫ��������λ�ֽ� */
	unsigned int time;		/* ʱ�䣬��λ�� */
} sc_kf_flv_info_t;

#include "sc_resource.h"

int sc_kf_flv_create_info(sc_res_info_t *ri);

/* �ڹؼ�֡�б��У�����ʱ����Ҷ�Ӧ��ƫ����
 *@keyframe�����򣬰�ʱ���С����
 * return��Ӧ��ƫ����
 */
static inline u32 sc_kf_flv_seek_offset(u32 targetTime, sc_kf_flv_info_t *keyframe, u32 key_num)
{
    u32 left = 0, right = 0;
    u32 midIndex, mid;
    u32 midValue, last_judge;

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
}

int sc_ngx_download(char *ngx_ip, char *url);

void sc_snooping_serve(int sockfd);
int sc_snooping_do_add(sc_res_info_t *ri);

int sc_get_yk_video(char *url, sc_res_info_t *origin);
int sc_url_is_yk(char *url);

#endif /* __SC_HEADER_H__ */
