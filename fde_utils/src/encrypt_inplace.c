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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <inttypes.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>

#include <linux/fs.h>

#include <ext2fs/ext2_fs.h>
#include <ext2fs/ext2fs.h>
#include <com_err.h>

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

static int block_iter_cb(
        ext2_filsys fs,
        blk64_t     *blocknr,
        e2_blkcnt_t blkcnt,
        blk64_t     ref_blk,
        int         ref_offset,
        void        *priv)
{
    struct in_place_encrypter *e = priv;

    /* skip metadata blocks */
    if (*blocknr == 0)
        return 0;

    if (!process_used_block(e, (uint64_t)*blocknr))
        return BLOCK_ABORT;

    return 0;
}

enc_inplace_err_t encrypt_inplace_ext4_ext2fs(struct in_place_encrypter *e)
{
    enc_inplace_err_t ret = ENC_SUCCESS;
    ext2_filsys fs;
    errcode_t err;

    /* open filesystem (read-only, no journal replay) */
    err = ext2fs_open(
        e->real_blkdev,
        EXT2_FLAG_SOFTSUPP_FEATURES |
        EXT2_FLAG_64BITS |
        EXT2_FLAG_IGNORE_CSUM_ERRORS,
        0, 0, unix_io_manager, &fs);
    if (err) {
        LOGE("ext2fs_open failed: %s.\n", error_message(err));
        return ENC_FS_NOT_FOUND;
    }

    /* Read block bitmap so we can walk allocated (in-use) blocks */
    err = ext2fs_read_block_bitmap(fs);
    if (err) {
            LOGE("ext2fs_read_block_bitmap failed: %s.\n", error_message(err));
            ret = ENC_FAILURE;
            goto fail;
    }

    blk64_t total_blocks = ext2fs_blocks_count(fs->super);

    /*
     * Count allocated blocks to make progress reporting meaningful.
     * (One extra pass over the bitmap; acceptable and keeps logs sane.)
     */
    uint64_t used_blocks = 0;
    for (blk64_t blk = 0; blk < total_blocks; blk++) {
        if (ext2fs_test_block_bitmap2(fs->block_map, blk))
                used_blocks++;
    }

    ret = inplace_init_fs(e, used_blocks, (uint64_t)total_blocks, fs->blocksize);
    if (ret != ENC_SUCCESS) {
        ret = ENC_FAILURE;
        goto fail;
    }

    /* Encrypt each allocated block (bitmap bit == 1) */
    for (blk64_t blk = 0; blk < total_blocks; blk++) {
        if (!ext2fs_test_block_bitmap2(fs->block_map, blk))
                continue;
        if (!process_used_block(e, (uint64_t)blk)) {
                ret = ENC_FAILURE;
                goto fail;
        }
    }

fail:
    ext2fs_close(fs);
    return ret;
}

bool do_encrypt_inplace(struct in_place_encrypter *e)
{
    enc_inplace_err_t rc;

    rc = encrypt_inplace_ext4_ext2fs(e);
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

    bool success = do_encrypt_inplace(&encrypter);

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