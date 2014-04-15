#include "common.h"
#include "sc_header.h"
#include "net_util.h"
#include "sohu_lib.h"

static char sc_sohu_origin_url_pattern1[] = "hot.vrs.sohu.com/ipad%s";
static char sc_sohu_origin_url_pattern2[] = "my.tv.sohu.com/ipad/%s";

/* zhaoyao XXX: sc's private simple check */
int sc_url_is_sohu(char *url)
{
    if (strstr(url, "sohu") != NULL) {
        return 1;
    } else {
        return 0;
    }
}

int sc_url_is_sohu_file_url(char *url)
{
    if (strstr(url, "?file=/") != NULL) {
        return 1;
    } else {
        return 0;
    }
}

int sc_sohu_is_local_path(char *local_path)
{
    if (strstr(local_path, "_file=_") != NULL) {
        return 1;
    } else {
        return 0;
    }
}

int sc_sohu_file_url_to_local_path(char *file_url, char *local_path, int len)
{
    char *p, *q;
    int first_slash = 1;

    if (file_url == NULL || local_path == NULL) {
        return -1;
    }

    if (!sc_url_is_sohu_file_url(file_url)) {
        return -1;
    }

    if (len <= strlen(file_url)) {
        return -1;
    }

    for (p = file_url, q = local_path; *p != '\0'; p++, q++) {
        if (first_slash && *p == '.') {
            *q = '_';
            continue;
        }
        if (*p == '/') {
            if (first_slash) {
                *q = *p;
                first_slash = 0;
            } else {
                *q = '_';
            }
            continue;
        }
        if (*p == '?') {
            *q = '_';
            continue;
        }
        *q = *p;
    }
    *q = '\0';

    return 0;
}

int sc_sohu_gen_origin_url(char *req_url, char *origin_url)
{
    char buf[HTTP_URL_MAX_LEN];
    char tag1[] = "hot.vrs.sohu.com/ipad";
    char tag2[] = "my.tv.sohu.com/ipad/";
    char suffix[] = ".m3u8";
    char *start = NULL;
    int len = 0;
    int tag;

    if (req_url == NULL || origin_url == NULL) {
        return -1;
    }

    if ((start = strstr(req_url, tag1)) != NULL) {
        tag = 1;
    } else if ((start = strstr(req_url, tag2)) != NULL) {
        tag = 2;
    } else {
        return -1;
    }

    if (strstr(start, suffix) == NULL) {
        return -1;
    }

    if (tag == 1) {
        start = start + strlen(tag1);
    } else {
        start = start + strlen(tag2);
    }

    for (len = 0; start[len] != '?' && start[len] != '\0'; len++) {
        ;
    }

    bzero(buf, sizeof(buf));
    strncpy(buf, start, len);
    if (tag == 1) {
        sprintf(origin_url, sc_sohu_origin_url_pattern1, buf);
    } else {
        sprintf(origin_url, sc_sohu_origin_url_pattern2, buf);
    }

    return 0;
}

int sc_sohu_download(sc_res_info_active_t *parsed)
{
    char response[BUFFER_LEN];
    char real_url[HTTP_URL_MAX_LEN];
    int status, ret;

    if (parsed == NULL) {
        return -1;
    }

    if (sohu_http_session(parsed->common.url, response, BUFFER_LEN) < 0) {
        fprintf(stderr, "%s ERROR: sohu_http_session faild, URL: %s\n", __func__, parsed->common.url);
        return -1;
    }

    if (http_parse_status_line(response, strlen(response), &status) < 0) {
        fprintf(stderr, "%s ERROR: parse status line failed:\n%s", __func__, response);
        return -1;
    }

    if (status == 301) {
//            fprintf(stdout, "%s get response success:\n%s", __func__, response);
    } else {
        fprintf(stderr, "%s ERROR: file_url response status code %d:\n%s", __func__, status, response);
        return -1;
    }

    bzero(real_url, HTTP_URL_MAX_LEN);
    ret = sohu_parse_file_url_response(response, real_url);
    if (ret != 0) {
        fprintf(stderr, "%s ERROR: parse real_url failed, response is\n%s", __func__, response);
        return -1;
    }

#if DEBUG
    fprintf(stdout, "%s file_url: %s\n", __func__, parsed->common.url);
    fprintf(stdout, "%s local_path: %s\n", __func__, parsed->localpath);
    fprintf(stdout, "%s real_url: %s\n", __func__, real_url);
#endif

    ret = sc_ngx_download(real_url, parsed->localpath);
    if (ret < 0) {
        fprintf(stderr, "%s ERROR: url %s inform Nginx failed\n", __func__, real_url);
        sc_res_set_i_fail(&parsed->common);
    }

    return ret;
}

/*
 * zhaoyao XXX: using file_url to create parsed ri, and local_path to store file.
 * m3u8_url:   hot.vrs.sohu.com/ipad1683703_4507722770245_4894024.m3u8?plat=0
 *                              ipad1683703_4507722770245_4894024.m3u8
 * file_url:   220.181.61.212/ipad?file=/109/193/XKUNcCADy8eM9ypkrIfhU4.mp4
 * local_path: 220.181.61.212/ipad_file=_109_193_XKUNcCADy8eM9ypkrIfhU4.mp4
 * real_url:   118.123.211.11/sohu/ts/zC1LzEwlslvesEAgoE.........
 */

static int sc_get_sohu_video_m3u8(sc_res_info_origin_t *origin)
{
    int err = 0, status, ret;
    char m3u8_url[HTTP_URL_MAX_LEN];
    char file_url[HTTP_URL_MAX_LEN];
    sc_res_info_active_t *parsed;
    char *response = NULL, *curr;

    /* zhaoyao: do not care para in m3u8 way */
    sc_res_copy_url(m3u8_url, origin->common.url, HTTP_URL_MAX_LEN, 0);

    response = malloc(RESP_BUF_LEN);
    if (response == NULL) {
        fprintf(stderr, "%s ERROR: allocate response buffer failed: %s\n", __func__, strerror(errno));
        err = -1;
        goto out;
    }

    if (sohu_http_session(m3u8_url, response, RESP_BUF_LEN) < 0) {
        fprintf(stderr, "sohu_http_session faild, URL: %s\n", m3u8_url);
        err = -1;
        goto out;
    }

    if (http_parse_status_line(response, strlen(response), &status) < 0) {
        fprintf(stderr, "%s ERROR: parse status line failed:\n%s", __func__, response);
        err = -1;
        goto out;
    }

    if (status == 200) {
//        fprintf(stdout, "%s get response success:\n***************\n%s\n***************\n", __func__, response);
    } else {
        fprintf(stderr, "%s ERROR: m3u8 response status code %d:\n%s", __func__, status, response);
        err = -1;
        goto out;
    }

    for (curr = response; curr != NULL; ) {
        bzero(file_url, HTTP_URL_MAX_LEN);
        curr = sohu_parse_m3u8_response(curr, file_url);

        /* zhaoyao XXX: for Sohu, file_url is not the final url to download data */
        ret = sc_res_info_add_parsed(sc_res_info_list, origin, file_url, &parsed);
        if (ret != 0) {
            fprintf(stderr, "%s ERROR: add file_url\n\t%s\nto resource list failed, give up downloading...\n",
                                __func__, file_url);
            /*
             * zhaoyao XXX: we can not using Nginx to download, without ri we can not track
             *              downloaded resources.
             */
            continue;
        }

        ret = sc_sohu_download(parsed);
        if (ret != 0) {
            fprintf(stderr, "%s ERROR: sc_sohu_download %s failed\n", __func__, parsed->common.url);
            continue;
        }
    }

out:
    free(response);

    return err;
}

int sc_get_sohu_video(sc_res_info_origin_t *origin)
{
    int ret = 0;

    if (origin == NULL) {
        fprintf(stderr, "%s need origin URL to parse real URL\n", __func__);
        return -1;
    }

    if (sohu_is_m3u8_url(origin->common.url)) {
        ret = sc_get_sohu_video_m3u8(origin);
    } else {
        fprintf(stderr, "%s ERROR: no support: %s\n", __func__, origin->common.url);
        ret = -1;
    }

    return ret;
}

