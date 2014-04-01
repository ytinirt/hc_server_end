#include "common.h"
#include "sc_header.h"
#include "sc_resource.h"

sc_res_list_t *sc_res_info_list = NULL;
int sc_res_share_mem_shmid = -1;

sc_res_video_t sc_res_video_type_obtain(char *str)
{
    char *p;
    int len;

    if (str == NULL) {
        return SC_RES_VIDEO_MAX;
    }

    len = strlen(str);
    if (len < 3) {
        return SC_RES_VIDEO_MAX;
    }

    len = len - 3;
    p = str + len;

    if (strncmp(p, SC_VIDEO_FLV_SUFFIX, SC_VIDEO_FLV_SUFFIX_LEN) == 0) {
        return SC_RES_VIDEO_FLV;
    }
    if (strncmp(p, SC_VIDEO_MP4_SUFFIX, SC_VIDEO_MP4_SUFFIX_LEN) == 0) {
        return SC_RES_VIDEO_MP4;
    }

    return SC_RES_VIDEO_MAX;
}

/*
 * zhaoyao XXX: not copy "http://", omitting parameter in o_url or not, is depending on with_para.
 */
void sc_res_copy_url(char *url, char *o_url, unsigned int len, char with_para)
{
    char *start = o_url, *p, *q;

    if (url == NULL || o_url == NULL) {
        return;
    }

    if (strncmp(start, HTTP_URL_PREFIX, HTTP_URL_PRE_LEN) == 0) {
        start = start + HTTP_URL_PRE_LEN;
    }

    if (with_para) {
        for (p = url, q = start; *q != '\0'; p++, q++) {
            *p = *q;
        }
    } else {
        for (p = url, q = start; *q != '?' && *q != '\0'; p++, q++) {
            *p = *q;
        }
    }

    if (url + len < p) {
        fprintf(stderr, "%s ERROR overflow, buffer len %u, but copied %u\n", __func__, len, (unsigned int)p - (unsigned int)url);
    }
}

static int sc_res_list_alloc_share_mem(sc_res_list_t **prl)
{
    int mem_size;
    void *shmptr;
    int shmid;

    if (prl == NULL) {
        return -1;
    }

    mem_size = SC_RES_SHARE_MEM_SIZE;
    if ((shmid = shmget(SC_RES_SHARE_MEM_ID, mem_size, SC_RES_SHARE_MEM_MODE | IPC_CREAT)) < 0) {
        fprintf(stderr, "%s shmget failed, memory size %d: %s", __func__, mem_size, strerror(errno));
        return -1;
    }

    if ((shmptr = shmat(shmid, 0, 0)) == (void *)-1) {
        fprintf(stderr, "%s shmat failed: %s", __func__, strerror(errno));
        sc_res_list_destroy_and_uninit(shmid);
        return -1;
    }
    memset(shmptr, 0, mem_size);

    *prl = shmptr;

    return shmid;
}

int sc_res_list_alloc_and_init(sc_res_list_t **prl)
{
    sc_res_list_t *rl;
    int ret, i;

    if (prl == NULL) {
        return -1;
    }

    ret = sc_res_list_alloc_share_mem(&rl);
    if (ret < 0) {
        fprintf(stderr, "%s allocate failed\n", __func__);
        return -1;
    }

    rl->origin[0].common.id = (unsigned long)INVALID_PTR;
    for (i = 1; i < SC_RES_INFO_NUM_MAX_ORGIIN; i++) {
        rl->origin[i].common.id = (unsigned long)(&(rl->origin[i - 1]));
    }
    rl->origin_free = (&(rl->origin[i - 1]));

    rl->active[0].common.id = (unsigned long)INVALID_PTR;
    for (i = 1; i < SC_RES_INFO_NUM_MAX_ACTIVE; i++) {
        rl->active[i].common.id = (unsigned long)(&(rl->active[i - 1]));
    }
    rl->active_free = (&(rl->active[i - 1]));

    *prl = rl;

    return ret;
}

int sc_res_list_destroy_and_uninit(int shmid)
{
    if (shmctl(shmid, IPC_RMID, NULL) < 0) {
        fprintf(stderr, "%s shmctl remove %d failed: %s", __func__, shmid, strerror(errno));
        return -1;
    }

    sc_res_info_list = NULL;

    return 0;
}

static sc_res_info_origin_t *sc_res_info_get_origin(sc_res_list_t *rl)
{
    sc_res_info_origin_t *ri;

    if (rl == NULL) {
        return NULL;
    }

    if (rl->origin_free == INVALID_PTR) {
        fprintf(stderr, "%s ERROR: resource info totally ran out, already used %d\n",
                            __func__, rl->origin_cnt);
        return NULL;
    }

    ri = rl->origin_free;
    rl->origin_free = (sc_res_info_origin_t *)(ri->common.id);
    rl->origin_cnt++;

    memset(ri, 0, sizeof(sc_res_info_origin_t));
    ri->common.id = ((unsigned long)ri - (unsigned long)(rl->origin)) / sizeof(sc_res_info_origin_t);

    sc_res_set_origin(&ri->common);

    return ri;
}

static sc_res_info_active_t *sc_res_info_get_active(sc_res_list_t *rl)
{
    sc_res_info_active_t *ri;

    if (rl == NULL) {
        return NULL;
    }

    if (rl->active_free == INVALID_PTR) {
        fprintf(stderr, "%s ERROR: resource info totally ran out, already used %d\n",
                            __func__, rl->active_cnt);
        return NULL;
    }

    ri = rl->active_free;
    rl->active_free = (sc_res_info_active_t *)(ri->common.id);
    rl->active_cnt++;

    memset(ri, 0, sizeof(sc_res_info_active_t));
    ri->common.id = ((unsigned long)ri - (unsigned long)(rl->active)) / sizeof(sc_res_info_active_t);

    return ri;
}

static void sc_res_info_put(sc_res_list_t *rl, sc_res_info_t *ri)
{
    sc_res_info_origin_t *origin;
    sc_res_info_active_t *active;

    if (rl == NULL || ri == NULL) {
        return;
    }

    if (sc_res_is_origin(ri)) {
        if (rl->origin_cnt == 0) {
            fprintf(stderr, "%s ERROR: rl->origin_cnt = 0, can not put\n", __func__);
            return;
        }
        origin = (sc_res_info_origin_t *)ri;
        memset(origin, 0, sizeof(sc_res_info_origin_t));
        origin->common.id = (unsigned long)(rl->origin_free);
        rl->origin_free = origin;
        rl->origin_cnt--;
        return;
    } else if (sc_res_is_normal(ri) || sc_res_is_parsed(ri)) {
        if (rl->active_cnt == 0) {
            fprintf(stderr, "%s ERROR: rl->active_cnt = 0, can not put\n", __func__);
            return;
        }
        active = (sc_res_info_active_t *)ri;
        memset(active, 0, sizeof(sc_res_info_active_t));
        active->common.id = (unsigned long)(rl->active_free);
        rl->active_free = active;
        rl->active_cnt--;
        return;
    } else {
        fprintf(stderr, "%s ERROR: unknown type 0x%lx\n", __func__, ri->flag);
        return;
    }
}

/*
 * zhaoyao TODO
 */
static int sc_res_info_permit_adding(char *url)
{
    if (sc_url_is_yk(url)) {
        return 1;
    }

    return 0;
}

int sc_res_info_add_normal(sc_res_list_t *rl, char *url, sc_res_info_active_t **normal)
{
    int len;
    sc_res_info_active_t *active;

    if (rl == NULL || url == NULL) {
        fprintf(stderr, "%s ERROR: invalid input\n", __func__);
        return -1;
    }

    if (!sc_res_info_permit_adding(url)) {
        fprintf(stderr, "%s: unknown url %s\n", __func__, url);
        return -1;
    }

    len = strlen(url);
    if (len >= SC_RES_URL_MAX_LEN) {
        fprintf(stderr, "%s ERROR: url is longer than MAX_LEN %d\n", __func__, SC_RES_URL_MAX_LEN);
        return -1;
    }

    active = sc_res_info_get_active(rl);
    if (active == NULL) {
        fprintf(stderr, "%s ERROR: get free res_info failed\n", __func__);
        return -1;
    }

    sc_res_set_normal(&active->common);
    sc_res_copy_url(active->common.url, url, SC_RES_URL_MAX_LEN, 1);
#if DEBUG
    fprintf(stdout, "%s: copied url with parameter:%s\n", __func__, active->common.url);
#endif

    if (normal != NULL) {
        *normal = active;
    }

    return 0;
}

int sc_res_info_add_origin(sc_res_list_t *rl, char *url, sc_res_info_origin_t **origin)
{
    int len;
    sc_res_info_origin_t *ri;

    if (rl == NULL || url == NULL) {
        return -1;
    }

    if (!sc_res_info_permit_adding(url)) {
        fprintf(stderr, "%s: unknown url %s\n", __func__, url);
        return -1;
    }

    len = strlen(url);
    if (len >= SC_RES_URL_MAX_LEN) {
        fprintf(stderr, "%s ERROR: url is longer than MAX_LEN %d\n", __func__, SC_RES_URL_MAX_LEN);
        return -1;
    }

    ri = sc_res_info_get_origin(rl);
    if (ri == NULL) {
        fprintf(stderr, "%s ERROR: get free res_info failed\n", __func__);
        return -1;
    }

    sc_res_copy_url(ri->common.url, url, SC_RES_URL_MAX_LEN, 0);
#if DEBUG
    fprintf(stdout, "%s: copied url without parameter:%s\n", __func__, ri->common.url);
#endif

    if (origin != NULL) {
        *origin = ri;
    }

    return 0;
}

int sc_res_info_add_parsed(sc_res_list_t *rl,
                           sc_res_info_origin_t *origin,
                           sc_res_video_t vtype,
                           char *url,
                           sc_res_info_active_t **parsed)
{
    int len;
    sc_res_info_active_t *active;

    if (rl == NULL || origin == NULL || url == NULL) {
        return -1;
    }

    if (!sc_res_video_type_is_valid(vtype)) {
        fprintf(stderr, "%s: video type is not supported\n", __func__);
        return -1;
    }

    if (!sc_res_info_permit_adding(url)) {
        fprintf(stderr, "%s: unknown url %s\n", __func__, url);
        return -1;
    }

    len = strlen(url);
    if (len >= SC_RES_URL_MAX_LEN) {
        fprintf(stderr, "%s ERROR: url is longer than MAX_LEN %d\n", __func__, SC_RES_URL_MAX_LEN);
        return -1;
    }

    active = sc_res_info_get_active(rl);
    if (active == NULL) {
        fprintf(stderr, "%s ERROR: get free res_info failed\n", __func__);
        return -1;
    }

    sc_res_set_parsed(&active->common);
    sc_res_copy_url(active->common.url, url, SC_RES_URL_MAX_LEN, 1);
#if DEBUG
    fprintf(stdout, "%s: copied url without parameter:%s\n", __func__, active->common.url);
#endif

    active->vtype = vtype;
    if (origin->child_cnt[vtype] == 0) {  /* First derivative is come */
        origin->child[vtype] = active;
        active->parent = origin;
    } else {
        active->siblings = origin->child[vtype];
        origin->child[vtype] = active;
        active->parent = origin;
    }
    origin->child_cnt[vtype]++;

    if (parsed != NULL) {
        *parsed = active;
    }

    return 0;
}

void sc_res_info_del(sc_res_list_t *rl, sc_res_info_t *ri)
{
    if (rl == NULL || ri == NULL) {
        return;
    }

    if (sc_res_is_origin(ri)) {
        /* zhaoyao XXX TODO FIXME: huge parsed URL stuff need to be done before put it */
        sc_res_info_put(rl, ri);
        return;
    }

    if (sc_res_is_stored(ri)) {
        fprintf(stderr, "%s WARNING: \n%s\n\tstored local file is not deleted\n", __func__, ri->url);
    }

    if (sc_res_is_notify(ri)) {
        fprintf(stderr, "%s WARNING: \n%s\n\thas notified snooping module\n", __func__, ri->url);
    }

    if (sc_res_is_normal(ri)) {
        sc_res_info_put(rl, ri);
        return;
    }

    if (sc_res_is_parsed(ri)) {
        /*
         * zhaoyao XXX TODO FIXME: huge origin URL stuff need to be done before put it,
         *                         should tell its parent.
         */
        sc_res_info_put(rl, ri);
        return;
    }

    fprintf(stderr, "%s ERROR: unknown type 0x%lx\n", __func__, ri->flag);
}

sc_res_info_active_t *sc_res_info_find_active(sc_res_list_t *rl, const char *url)
{
    sc_res_info_active_t *curr;
    int i;

    if (rl == NULL || url == NULL) {
        return NULL;
    }

    for (i = 0; i < SC_RES_INFO_NUM_MAX_ACTIVE; i++) {
        curr = &rl->active[i];
        if (strcmp(url, curr->common.url) == 0) {
            return curr;
        }
    }

    return NULL;
}

sc_res_info_origin_t *sc_res_info_find_origin(sc_res_list_t *rl, const char *url)
{
    sc_res_info_origin_t *curr;
    int i;

    if (rl == NULL || url == NULL) {
        return NULL;
    }

    for (i = 0; i < SC_RES_INFO_NUM_MAX_ORGIIN; i++) {
        curr = &rl->origin[i];
        if (strcmp(url, curr->common.url) == 0) {
            return curr;
        }
    }

    return NULL;
}

/*
 * zhaoyao: exact matching, finding in all res_info, TODO XXX add fuzzy matching or pattern matching.
 */
sc_res_info_t *sc_res_info_find(sc_res_list_t *rl, const char *url)
{
    sc_res_info_active_t *active;
    sc_res_info_origin_t *origin;

    active = sc_res_info_find_active(rl, url);
    if (active != NULL) {
        return (sc_res_info_t *)active;
    }

    origin = sc_res_info_find_origin(rl, url);
    if (origin != NULL) {
        return (sc_res_info_t *)origin;
    }

    return NULL;
}

static int sc_res_retry_download(sc_res_info_t *ri)
{
    int ret;

    if (ri == NULL) {
        return -1;
    }

    ret = sc_ngx_download(NULL, ri->url);
    if (ret != 0) {
        fprintf(stderr, "%s ERROR: %s failed\n", __func__, ri->url);
    }

    return ret;
}

static int sc_res_list_process_origin(sc_res_list_t *rl)
{
    /*
     * zhaoyao TODO: origin process
     */
     
    if (rl == NULL) {
        return -1;
    }

    return 0;
}

static int sc_res_list_process_active(sc_res_list_t *rl)
{
    sc_res_info_active_t *curr;
    sc_res_info_t *ri;
    int i, err = 0, ret;

    if (rl == NULL) {
        return -1;
    }

    for (i = 0; i < SC_RES_INFO_NUM_MAX_ACTIVE; i++) {
        curr = &rl->active[i];
        ri = &curr->common;

        if (ri->url[0] == '\0') {
            continue;
        }

        if (!sc_res_is_normal(ri) && !sc_res_is_parsed(ri)) {
            fprintf(stderr, "%s ERROR: active type check wrong, type: 0x%lx\n", __func__, ri->flag);
            continue;
        }

        if (!sc_res_is_stored(ri)) {
            /* zhaoyao XXX: timeout, and re-download */
            if (sc_res_is_d_fail(ri)) {   /* Nginx tell us to re-download it */
                fprintf(stderr, "%s use sc_res_retry_download %s\n", __func__, ri->url);
                ret = sc_res_retry_download(ri);
                if (ret != 0) {
                    fprintf(stderr, "%s inform Nginx re-download %s failed\n", __func__, ri->url);
                    err++;
                } else {
                    fprintf(stdout, "%s inform Nginx re-download %s success\n", __func__, ri->url);
                    sc_res_unset_d_fail(ri);
                }
            }
            continue;
        }

        if (!sc_res_is_notify(ri)) {
            fprintf(stderr, "%s use sc_snooping_do_add %s\n", __func__, ri->url);
            ret = sc_snooping_do_add(ri);
            if (ret != 0) {
                fprintf(stderr, "%s inform Snooping Module add URL failed\n", __func__);
                err++;
            }
        }

        if ((!sc_res_is_kf_crt(ri)) && (curr->vtype == SC_RES_VIDEO_FLV)) {
            fprintf(stderr, "%s use sc_kf_flv_create_info %s\n", __func__, ri->url);
            ret = sc_kf_flv_create_info(curr);
            if (ret != 0) {
                fprintf(stderr, "%s create FLV key frame information failed\n", __func__);
                err++;
            }
        }
    }

    return err;
}

int sc_res_list_process_func(sc_res_list_t *rl)
{
    int err = 0, ret;

    if (rl == NULL) {
        return -1;
    }

    ret = sc_res_list_process_origin(rl);
    if (ret < 0) {
        fprintf(stderr, "%s ERROR: sc_res_list_process_origin, return %d\n", __func__, ret);
        return ret;
    }

    err = err + ret;
    ret = sc_res_list_process_active(rl);
    if (ret < 0) {
        fprintf(stderr, "%s ERROR: sc_res_list_process_active, return %d\n", __func__, ret);
        return ret;
    }

    err = err + ret;

    return err;
}

void *sc_res_list_process_thread(void *arg)
{
    int ret;

    while (1) {
        ret = sc_res_list_process_func(sc_res_info_list);
        if (ret < 0) {
            fprintf(stderr, "%s exit...\n", __func__);
            break;
        } else if (ret > 0) {
            fprintf(stderr, "%s problem occured %d time(s)...\n", __func__, ret);
        }

        sleep(3);
    }

    return ((void *)0);
}
