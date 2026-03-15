/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <errno.h>

#include <stdalign.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <linux/dm-ioctl.h>

#include "fde_core.h"
#include "dmctl_inlinecrypt.h"

#define DM_VERSION0 (4)
#define DM_VERSION1 (0)
#define DM_VERSION2 (0)

#define DM_BUF_SIZE (4096)

#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

static int append_token(char **buf, size_t *len, size_t *cap, const char *tok) {
    if (!tok) tok = "";

    size_t tok_len = strlen(tok);
    size_t need = *len + ((*len > 0) ? 1 : 0) + tok_len + 1; // +space? +NUL

    if (need > *cap) {
        size_t new_cap = (*cap == 0) ? 128 : *cap;
        while (new_cap < need) new_cap *= 2;

        char *new_buf = (char *)realloc(*buf, new_cap);
        if (!new_buf) {
            LOGE("Failed to realloc memory, new_cap: %zu.\n", new_cap);
            return -1;
        }
        *buf = new_buf;
        *cap = new_cap;
    }

    if (*len > 0) {
        (*buf)[(*len)++] = ' ';
    }

    memcpy(*buf + *len, tok, tok_len);
    *len += tok_len;
    (*buf)[*len] = '\0';
    return 0;
}

char* get_table_params(const char *cipher,
                       const char *key,
                       const uint64_t iv_offset,
                       const char *blk_dev,
                       const uint64_t start_sector)
{
    char *params_out = NULL;
    size_t len = 0, cap = 0;
    // Need 2*sizeof(unsigned long long) + 1 bytes to put the string of uint64_t type
    // and its NULL terminal.
    char tmp_buf[128];

    // argv: cipher key iv_offset blk_dev start_sector
    if (append_token(&params_out, &len, &cap, cipher) != 0) goto oom;
    if (append_token(&params_out, &len, &cap, key) != 0) goto oom;
    memset(tmp_buf, 0, sizeof(tmp_buf));
    snprintf(tmp_buf, sizeof(tmp_buf), "%" PRIu64, iv_offset);
    if (append_token(&params_out, &len, &cap, tmp_buf) != 0) goto oom;
    if (append_token(&params_out, &len, &cap, blk_dev) != 0) goto oom;

    memset(tmp_buf, 0, sizeof(tmp_buf));
    snprintf(tmp_buf, sizeof(tmp_buf), "%" PRIu64, start_sector);
    if (append_token(&params_out, &len, &cap, tmp_buf) != 0) goto oom;

    //argv += [count] + extras
    const char *extra[3];
    int extra_cnt = sizeof(extra)/sizeof(extra[0]);
    extra[0] = "allow_discards";
    extra[1] = "sector_size:4096";
    extra[2] = "iv_large_sectors";

    char cnt_str[16];
    snprintf(cnt_str, sizeof(cnt_str), "%d", extra_cnt);
    if (append_token(&params_out, &len, &cap, cnt_str) != 0) goto oom;

    for (int i = 0; i < extra_cnt; ++i) {
        if (append_token(&params_out, &len, &cap, extra[i]) != 0) goto oom;
    }

    return params_out;

oom:
    free(params_out);
    return NULL;
}



static int dm_init_ioctl(struct dm_ioctl* io, const char* name)
{
    memset(io, 0, DM_BUF_SIZE);
    io->data_size = DM_BUF_SIZE;
    io->data_start = sizeof(struct dm_ioctl);
    io->version[0] = DM_VERSION0;
    io->version[1] = DM_VERSION1;
    io->version[2] = DM_VERSION2;

    if (name && name[0]) {
        int len = strlcpy(io->name, name, sizeof(io->name));
        if (len >= sizeof(io->name)) {
            LOGE("dm name (%s) is truncated.\n", name);
            return -1;
        }
    }
    return 0;
}

static int create_inline_crypt_device(struct dm_ioctl* io,
                                      const char* name,
                                      int fd)
{
    if(dm_init_ioctl(io, name))
        return -1;

    if (ioctl(fd, DM_DEV_CREATE, io)) {
        LOGE("Creating mapped device error: %s.\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int load_inline_crypt_table(struct dm_ioctl* io,
                                   const char *name,
                                   uint64_t device_size,
                                   int fd,
                                   const char *table)
{
    char *inline_crypt_params;
    char *buffer = (char*) io;
    size_t buf_size;
    int len;


    if(dm_init_ioctl(io, name))
        return -1;

    io->target_count = 1;
    struct dm_target_spec *tgt = (struct dm_target_spec *) &buffer[sizeof(struct dm_ioctl)];

    // Init arguments for tgt.
    tgt->status = 0;
    tgt->sector_start = 0;
    tgt->length = device_size;
    len = strlcpy(tgt->target_type, DM_INLINE_TYPE_NAME, sizeof(tgt->target_type));
    if (len >= sizeof(tgt->target_type)) {
        LOGE("dm target type (%s) is truncated.\n", DM_INLINE_TYPE_NAME);
        return -1;
    }

    // Add dm table params at the end of dm_target_spec.
    inline_crypt_params = buffer + sizeof(struct dm_ioctl) + sizeof(struct dm_target_spec);
    buf_size = DM_BUF_SIZE - (inline_crypt_params - buffer);
    len = strlcpy(inline_crypt_params, table, buf_size);
    if (len >= buf_size) {
        LOGE("dm table params (%s) are truncated.\n", table);
        return -1;
    }

    /**
     * Set the location of the next dm_target_spec. 
     * The kernel expects each target to be 8-byte aligned.
     */
    inline_crypt_params += strlen(inline_crypt_params) + 1;
    inline_crypt_params = (char *)ALIGN_UP((uintptr_t)inline_crypt_params, 8);
    tgt->next = (uint32_t)(inline_crypt_params - (char*)tgt);

    if (ioctl(fd, DM_TABLE_LOAD, io)) {
        LOGE("Loading inline crypt table error: %s.\n", strerror(errno));
        return -1;
    }

    return 0;
}

static int resume_inline_crypt_table(struct dm_ioctl* io,
                                     const char *name,
                                     int fd)
{
    if(dm_init_ioctl(io, name))
        return -1;

    if (ioctl(fd, DM_DEV_SUSPEND, io)) {
        LOGE("Activating mapped device error: %s.\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int get_inline_crypt_device_path(struct dm_ioctl* io,
                                        const char *name,
                                        int fd,
                                        char *dm_path,
                                        size_t dm_path_len)
{
    if(dm_init_ioctl(io, name))
        return -1;

    if (ioctl(fd, DM_DEV_STATUS, io)) {
        LOGE("Geting inlinecrypt device number error: %s", strerror(errno));
        return -1;
    }

    int dev_num = (io->dev & 0xff) | ((io->dev >> 12) & 0xfff00);
    int len = snprintf(dm_path, dm_path_len,
                       "%s%d", "/dev/dm-", dev_num);
    if (len >= dm_path_len) {
        LOGE("dm path of dm-%d is truncated.\n", dev_num);
        return -1;
    }

    return 0;
}

fde_err_t create_dm_device(const char *name, const uint64_t nr_sec,
                     const char *table, char *dm_path, size_t dm_path_len)
{
    fde_err_t val = FDE_GENERAL_ERROR;
    int ret, fd = -1;

    if (!name || !nr_sec || !table)
        return val;

    if ((fd = open("/dev/mapper/control", O_RDWR)) < 0) {
        LOGE("Opening device mapper error: %s", strerror(errno));
        return val;
    }

    alignas(struct dm_ioctl) char buffer[DM_BUF_SIZE];
    struct dm_ioctl *io = (struct dm_ioctl *)buffer;

    ret = create_inline_crypt_device(io, name, fd);
    if (ret < 0) {
        LOGE("Couldn't create inline crypt device!");
        goto out;
    }

    ret = load_inline_crypt_table(io, name, nr_sec, fd, table);
    if (ret < 0) {
        goto out;
    }

    ret = resume_inline_crypt_table(io, name, fd);
    if (ret < 0)
        goto out;

    ret = get_inline_crypt_device_path(io, name, fd,
                                dm_path, dm_path_len);
    if (ret < 0)
        goto out;

    val = FDE_SUCCESS;
out:
    if (fd)
        close(fd);

    return val;
}
