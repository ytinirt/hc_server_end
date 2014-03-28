#include "common.h"
#include "sc_header.h"
#include "sc_resource.h"

sc_res_list_t *sc_res_info_list = NULL;
int sc_res_share_mem_shmid = -1;

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

    rl->total = 0x1 << SC_RES_NUM_MAX_SHIFT;
    rl->res[0].id = (unsigned long)INVALID_PTR;
    for (i = 1; i < rl->total; i++) {
        rl->res[i].id = (unsigned long)(&(rl->res[i - 1]));
    }
    rl->free = (&(rl->res[i - 1]));

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

static sc_res_info_t *sc_res_info_get(sc_res_list_t *rl)
{
    sc_res_info_t *ri;

    if (rl == NULL) {
        return NULL;
    }

    if (rl->free == INVALID_PTR) {
        fprintf(stderr, "%s ERROR: %d resource info totally ran out\n", __func__, rl->total);
        return NULL;
    }

    ri = rl->free;
    rl->free = (sc_res_info_t *)(ri->id);

    memset(ri, 0, sizeof(sc_res_info_t));
    ri->id = ((unsigned long)ri - (unsigned long)(rl->res)) / sizeof(sc_res_info_t);

    return ri;
}

static void sc_res_info_put(sc_res_list_t *rl, sc_res_info_t *ri)
{
    if (rl == NULL || ri == NULL) {
        return;
    }

    memset(ri, 0, sizeof(sc_res_info_t));
    ri->id = (unsigned long)(rl->free);
    rl->free = ri;
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

int sc_res_info_add_normal(sc_res_list_t *rl, char *url, sc_res_info_t **normal)
{
    int len;
    sc_res_info_t *ri;

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

    ri = sc_res_info_get(rl);
    if (ri == NULL) {
        fprintf(stderr, "%s ERROR: get free res_info failed\n", __func__);
        return -1;
    }

    sc_res_set_normal(ri);
    sc_res_copy_url(ri->url, url, SC_RES_URL_MAX_LEN, 1);
#if DEBUG
    fprintf(stdout, "%s: copied url with parameter:%s\n", __func__, ri->url);
#endif

    if (normal != NULL) {
        *normal = ri;
    }

    return 0;
}

void sc_res_info_del_normal(sc_res_list_t *rl, sc_res_info_t *ri)
{
    if (rl == NULL || ri == NULL) {
        return;
    }

    if (!sc_res_is_normal(ri)) {
        fprintf(stderr, "%s ERROR: can not delete 0x%lx flag res_info\n", __func__, ri->flag);
        return;
    }

    if (sc_res_is_stored(ri)) {
        fprintf(stderr, "%s WARNING: \n%s\n\tstored local file is not deleted\n", __func__, ri->url);
    }

    sc_res_info_put(rl, ri);
}

int sc_res_info_add_origin(sc_res_list_t *rl, char *url, sc_res_info_t **origin)
{
    int len;
    sc_res_info_t *ri;

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

    ri = sc_res_info_get(rl);
    if (ri == NULL) {
        fprintf(stderr, "%s ERROR: get free res_info failed\n", __func__);
        return -1;
    }

    sc_res_set_origin(ri);
    sc_res_copy_url(ri->url, url, SC_RES_URL_MAX_LEN, 0);
#if DEBUG
    fprintf(stdout, "%s: copied url without parameter:%s\n", __func__, ri->url);
#endif

    if (origin != NULL) {
        *origin = ri;
    }

    return 0;
}

void sc_res_info_del_origin(sc_res_list_t *rl, sc_res_info_t *ri)
{
    if (rl == NULL || ri == NULL) {
        return;
    }

    if (!sc_res_is_origin(ri)) {
        fprintf(stderr, "%s ERROR: can not delete 0x%lx flag resource_info\n", __func__, ri->flag);
        return;
    }

    if (sc_res_is_stored(ri)) {
        fprintf(stderr, "%s WARNING: \n%s\n\tstored local file is not deleted\n", __func__, ri->url);
    }

    /* zhaoyao XXX TODO FIXME: huge parsed URL stuff need to be done before put it */

    sc_res_info_put(rl, ri);
}

int sc_res_info_add_parsed(sc_res_list_t *rl,
                           sc_res_info_t *origin_ri,
                           char *url,
                           sc_res_info_t **parsed)
{
    int len;
    sc_res_info_t *ri;

    if (rl == NULL || origin_ri == NULL || url == NULL) {
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

    ri = sc_res_info_get(rl);
    if (ri == NULL) {
        fprintf(stderr, "%s ERROR: get free res_info failed\n", __func__);
        return -1;
    }

    sc_res_set_parsed(ri);
    sc_res_copy_url(ri->url, url, SC_RES_URL_MAX_LEN, 1);
#if DEBUG
    fprintf(stdout, "%s: copied url without parameter:%s\n", __func__, ri->url);
#endif

    if (origin_ri->cnt == 0) {  /* First derivative is come */
        origin_ri->parsed = ri;
        ri->parent = origin_ri;
    } else {
        ri->siblings = origin_ri->parsed;
        origin_ri->parsed = ri;
        ri->parent = origin_ri;
    }
    origin_ri->cnt++;

    if (parsed != NULL) {
        *parsed = ri;
    }

    return 0;
}

void sc_res_info_del_parsed(sc_res_list_t *rl,
                            sc_res_info_t *origin_ri,
                            sc_res_info_t *ri)
{
    if (rl == NULL || origin_ri == NULL || ri == NULL) {
        return;
    }

    if (!sc_res_is_parsed(ri)) {
        fprintf(stderr, "%s ERROR: can not delete 0x%lx flag res_info\n", __func__, ri->flag);
        return;
    }

    if (sc_res_is_stored(ri)) {
        fprintf(stderr, "%s WARNING: \n%s\n\tstored local file is not deleted\n", __func__, ri->url);
    }

    /* zhaoyao XXX TODO FIXME: huge origin URL stuff need to be done before put it */

    sc_res_info_put(rl, ri);
}

/*
 * zhaoyao: exact matching, TODO XXX add fuzzy matching or pattern matching.
 */
sc_res_info_t *sc_res_info_find(sc_res_list_t *rl, const char *url)
{
    sc_res_info_t *curr;
    int i;

    if (rl == NULL || url == NULL) {
        return NULL;
    }

    for (i = 0; i < rl->total; i++) {
        curr = &rl->res[i];
        if (strcmp(url, curr->url) == 0) {
            return curr;
        }
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


int sc_res_list_process_func(sc_res_list_t *rl)
{
    sc_res_info_t *curr;
    int i, err = 0, ret;

    if (rl == NULL) {
        return -1;
    }

    for (i = 0; i < rl->total; i++) {
        curr = &rl->res[i];

        if (curr->url[0] == '\0') {
            continue;
        }

        if (sc_res_is_origin(curr)) {
            continue;
        }

        if (!sc_res_is_stored(curr)) {
            /* zhaoyao XXX: timeout, and re-download */
            if (sc_res_is_d_fail(curr)) {   /* Nginx tell us to re-download it */
                fprintf(stderr, "%s use sc_res_retry_download %s\n", __func__, curr->url);
                ret = sc_res_retry_download(curr);
                if (ret != 0) {
                    fprintf(stderr, "%s inform Nginx re-download %s failed\n", __func__, curr->url);
                    err++;
                } else {
                    fprintf(stdout, "%s inform Nginx re-download %s success\n", __func__, curr->url);
                    sc_res_unset_d_fail(curr);
                }
            }
            continue;
        }

        if (!sc_res_is_notify(curr)) {
            fprintf(stderr, "%s use sc_snooping_do_add %s\n", __func__, curr->url);
            ret = sc_snooping_do_add(curr);
            if (ret != 0) {
                fprintf(stderr, "%s inform Snooping Module add URL failed\n", __func__);
                err++;
            }
        }

        if (!sc_res_is_kf_crt(curr)) {
            fprintf(stderr, "%s use sc_kf_flv_create_info %s\n", __func__, curr->url);
            ret = sc_kf_flv_create_info(curr);
            if (ret != 0) {
                fprintf(stderr, "%s create FLV key frame information failed\n", __func__);
                err++;
            }
        }
    }

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
            fprintf(stderr, "%s problem occured...\n", __func__);
        }

        sleep(3);
    }

    return ((void *)0);
}
