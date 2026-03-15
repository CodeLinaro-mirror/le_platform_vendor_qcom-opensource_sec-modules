/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * ​​​​​Changes from Qualcomm Technologies, Inc. are provided under the following license:
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Required for correct in-place encryption on large (>2GB) block devices
 * when building 32-bit userspace with glibc.
 */
#define _FILE_OFFSET_BITS 64

#include <inttypes.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

#include <linux/fs.h>

#include <ext4_utils/ext4_kernel_headers.h>
#include <ext4_utils/ext4_utils.h>

#include "encrypt_inplace.h"
#include "fde_core.h"

// Aligned 32K writes tends to make flash happy.
#define IO_BUFFER_SIZE        32768

// Avoid spamming the logs. Print the "Encrypting blocks" log message once
// every 10000 blocks (which is usually every 40 MB or so), and once at the end.
static const int k_log_interval = 10000;

typedef enum {
    ENC_SUCCESS = 0,
    ENC_FAILURE,
    ENC_FS_NOT_FOUND,
} enc_inplace_err_t;

struct in_place_encrypter {
    const char *real_blkdev;
    const char *crypto_blkdev;

    uint64_t nr_sec;

    int realfd;
    int cryptofd;

    const char *fs_type;

    uint64_t blocks_done;
    uint64_t blocks_to_encrypt;
    uint32_t block_size;

    uint8_t *io_buffer;
    size_t io_buffer_size;

    uint64_t first_pending_block;
    size_t blocks_pending;
};

static uint64_t round_up(uint64_t val, uint64_t align)
{
    if (val % align)
        val += align - (val % align);

    return val;
}

static enc_inplace_err_t inplace_init_fs(struct in_place_encrypter *e,
                            uint64_t blocks_to_encrypt,
                            uint64_t total_blocks,
                            uint32_t block_size)
{
    e->blocks_done = 0;
    e->blocks_to_encrypt = blocks_to_encrypt;
    e->block_size = block_size;

    /* 32K buffer, round the block size up */
    e->io_buffer_size = round_up(IO_BUFFER_SIZE, (uint64_t)block_size);
    e->io_buffer = (uint8_t *)aligned_alloc(block_size, e->io_buffer_size);
    if (!e->io_buffer)
        e->io_buffer = (uint8_t *)malloc(e->io_buffer_size);

    if (!e->io_buffer) {
        LOGE("%s: Failed to alloc io buffer, errno=%d.", __func__, errno);
        return ENC_FAILURE;
    }

    e->first_pending_block = 0;
    e->blocks_pending = 0;

    uint64_t mb = (blocks_to_encrypt * (uint64_t)block_size) / 1000000ULL;
    LOGD("Encrypting ext4 filesystem on %s via %s\n",
         e->real_blkdev, e->crypto_blkdev);
    LOGD("%" PRIu64 " blocks (%" PRIu64 " MB) of %" PRIu64 " blocks "
        "are in-use (estimated).\n",
        blocks_to_encrypt, mb, total_blocks);

    return ENC_SUCCESS;
}

static void update_progress(struct in_place_encrypter *e,
                            uint64_t blocks,
                            int done)
{
    uint64_t blocks_next_msg = round_up(e->blocks_done + 1, k_log_interval);

    e->blocks_done += blocks;

    if (done && e->blocks_done % k_log_interval != 0)
        blocks_next_msg = e->blocks_done;

    if (e->blocks_done >= blocks_next_msg)
        LOGD("Encrypted %" PRIu64 " of %" PRIu64 " blocks.\n",
                blocks_next_msg, e->blocks_to_encrypt);
}

static bool encrypt_pending_data(struct in_place_encrypter *e)
{
    if (e->blocks_pending == 0)
        return true;

    size_t bytes = e->blocks_pending * e->block_size;
    uint64_t offset = e->first_pending_block * e->block_size;

    if (pread(e->realfd, e->io_buffer, bytes, (off_t)offset) != (ssize_t)bytes) {
        LOGE("Error of pread real_blkdev (%s).\n", e->real_blkdev);
        return false;
    }

    if (pwrite(e->cryptofd, e->io_buffer, bytes, (off_t)offset) != (ssize_t)bytes) {
        LOGE("Error of pwrite crypto_blkdev (%s).\n", e->crypto_blkdev);
        return false;
    }

    update_progress(e, e->blocks_pending, false);

    e->blocks_pending = 0;
    return true;
}

static bool process_used_block(struct in_place_encrypter *e, uint64_t block_num)
{
    if (e->blocks_pending * e->block_size == e->io_buffer_size ||
        block_num != e->first_pending_block + e->blocks_pending ||
        (block_num * e->block_size) % e->io_buffer_size == 0) {

        if (!encrypt_pending_data(e))
            return false;

        e->first_pending_block = block_num;
    }

    e->blocks_pending++;
    return true;
}

static uint64_t first_block_in_group(uint32_t group) {
    return (uint64_t)aux_info.first_data_block +
           (uint64_t)group * (uint64_t)info.blocks_per_group;
}

static uint32_t num_blocks_in_group(uint32_t group) {
    uint64_t first = first_block_in_group(group);
    uint64_t remaining = aux_info.len_blocks - first;
    return (remaining < (uint64_t)info.blocks_per_group) ?
            (uint32_t)remaining : (uint32_t)info.blocks_per_group;
}

/*
 * In block groups with an uninitialized block bitmap, original code encrypts
 * only the backup superblock and the block group descriptors (if present).
 * num = 1 + aux_info.bg_desc_blocks if group has superblock; else 0
 */
static uint32_t num_base_meta_blocks_in_group(uint32_t group) {
    if (!ext4_bg_has_super_block((int)group))
        return 0;
    return 1u + (uint32_t)aux_info.bg_desc_blocks;
}

static bool read_ext4_block_bitmap(struct in_place_encrypter *e,
                                   uint32_t group, uint8_t *buf)
{
    uint64_t bb = (uint64_t)aux_info.bg_desc[group].bg_block_bitmap;
    uint64_t offset = bb * (uint64_t)info.block_size;

    ssize_t r = pread(e->realfd, buf, info.block_size, (off_t)offset);
    if (r != (ssize_t)info.block_size) {
        /* log if you want; keep behavior: return false */
        return false;
    }
    return true;
}

enc_inplace_err_t encrypt_in_place_ext4(struct in_place_encrypter *e)
{
    enc_inplace_err_t ret = ENC_SUCCESS;

    /* If setjmp triggers, treat as FS not found */
    if (setjmp(setjmp_env)) { // NOLINT
        ret = ENC_FS_NOT_FOUND;
        goto out;
    }

    if (read_ext(e->realfd, 0) != 0)
        return ENC_FS_NOT_FOUND;

    /* Compute blocks_to_encrypt */
    uint64_t blocks_to_encrypt = 0;
    for (uint32_t group = 0; group < (uint32_t)aux_info.groups; group++) {
        if (aux_info.bg_desc[group].bg_flags & EXT4_BG_BLOCK_UNINIT) {
            blocks_to_encrypt += (uint64_t)num_base_meta_blocks_in_group(group);
        } else {
            uint32_t blocks_in_group = num_blocks_in_group(group);
            uint32_t free_blocks = (uint32_t)aux_info.bg_desc[group].bg_free_blocks_count;
            blocks_to_encrypt += (uint64_t)(blocks_in_group - free_blocks);
        }
    }

    ret = inplace_init_fs(e, blocks_to_encrypt, aux_info.len_blocks, info.block_size);
    if (ret != ENC_SUCCESS)
        return ret;

    /* Encrypt each block group */
    uint8_t *block_bitmap = (uint8_t *)malloc(info.block_size);
    if (!block_bitmap)
        return ENC_FAILURE;

    for (uint32_t group = 0; group < (uint32_t)aux_info.groups; group++) {

        if (!read_ext4_block_bitmap(e, group, block_bitmap)) {
            free(block_bitmap);
            return ENC_FAILURE;
        }

        uint64_t first_blk = first_block_in_group(group);
        bool uninit = (aux_info.bg_desc[group].bg_flags & EXT4_BG_BLOCK_UNINIT) != 0;

        uint32_t block_count = uninit ?
            num_base_meta_blocks_in_group(group) :
            num_blocks_in_group(group);

        /* Encrypt each used block in the block group */
        for (uint32_t i = 0; i < block_count; i++) {
            if (uninit || bitmap_get_bit(block_bitmap, i)) {
                if (!process_used_block(e, first_blk + (uint64_t)i)) {
                    free(block_bitmap);
                    return ENC_FAILURE;
                }
            }
        }
    }

out:
    if (block_bitmap)
        free(block_bitmap);
    return ret;
}


bool do_encrypt_inplcae(struct in_place_encrypter *e)
{
    enc_inplace_err_t rc;

    rc = encrypt_in_place_ext4(e);
    if (rc != ENC_FS_NOT_FOUND)
        return rc == ENC_SUCCESS;

    // Fallback to 521-byte sector full disk encryption
    inplace_init_fs(e, e->nr_sec, e->nr_sec, 512);
    for (uint64_t i = 0; i < e->nr_sec; i++) {
        if (!process_used_block(e, i))
            return false;
    }

    return true;
}

bool encrypt_inplace(const char *crypto_blkdev,
                     const char *real_blkdev,
                     const char *fs_type,
                     uint64_t nr_sec)
{
    bool ret = false;
    struct in_place_encrypter encrypter;

    LOGD("encrypt_inplace, crypto_blkdev: %s, real_blkdev: %s, "
        "nr_sec: %" PRIu64 ".\n",
        crypto_blkdev, real_blkdev, nr_sec);

    memset(&encrypter, 0, sizeof(encrypter));

    encrypter.real_blkdev = real_blkdev;
    encrypter.crypto_blkdev = crypto_blkdev;
    encrypter.nr_sec = nr_sec;

    encrypter.realfd = open(real_blkdev, O_RDONLY | O_CLOEXEC);
    if (encrypter.realfd < 0) {
        LOGE("Open real_blkdev(%s), error(%d): %s.\n",
                real_blkdev, errno, strerror(errno));
        return ret;
    }

    encrypter.cryptofd = open(crypto_blkdev, O_WRONLY | O_CLOEXEC);
    if (encrypter.cryptofd < 0) {
        LOGE("Open crypto_blkdev(%s), error(%d): %s.\n",
                crypto_blkdev, errno, strerror(errno));
        close(encrypter.realfd);
        return ret;
    }

    encrypter.fs_type = fs_type;

    bool success = do_encrypt_inplcae(&encrypter);

    if (success)
        success &= encrypt_pending_data(&encrypter);

    if (success && fsync(encrypter.cryptofd) != 0) {
        LOGE("Fsync %s, error: %s.\n", encrypter.crypto_blkdev, strerror(errno));
        success = false;
    }

    if (!success) {
        LOGE("In-place encryption on %s failed.\n", encrypter.crypto_blkdev);
        goto fail;
    }
    if (encrypter.blocks_done != encrypter.blocks_to_encrypt) {
        LOGE("blocks_to_encrypt (%" PRIu64 ") was incorrect; we actually encrypted "
             "%" PRIu64 "blocks. Encryption progress was inaccurate.\n",
             encrypter.blocks_to_encrypt, encrypter.blocks_done);
    }

    update_progress(&encrypter, 0, true);
    LOGE("Successfully encrypted %s.\n", encrypter.crypto_blkdev);

    ret = true;

fail:
    free(encrypter.io_buffer);
    close(encrypter.realfd);
    close(encrypter.cryptofd);
    return ret;
}