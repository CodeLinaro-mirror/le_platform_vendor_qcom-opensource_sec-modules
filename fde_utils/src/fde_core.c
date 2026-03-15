/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <dlfcn.h>
#include <sys/wait.h>

#include "fde_core.h"
#include "key_utils.h"
#include "utils.h"
#include "km_contract.h"
#include "dmctl_inlinecrypt.h"
#include "encrypt_inplace.h"

static const char *blkdev_dir = "/dev/disk/by-partlabel";

// This is equal to "aes-256-xts" usually seen in usersapce.
static const char *cipher_in_kernel = "aes-xts-plain64";

static fde_err_t get_blk_device_path(char blk_dev[BLK_DEV_MAX_PATH_LEN],
                                const char *blk_dir,
                                const char *basename)
{
    if (snprintf(blk_dev, BLK_DEV_MAX_PATH_LEN, "%s/%s",
                 blk_dir, basename) >= BLK_DEV_MAX_PATH_LEN) {
        LOGE("Its device node path (%s) is truncated.\n", basename);
        return FDE_GENERAL_ERROR;
    }
    return FDE_SUCCESS;
}

static fde_err_t create_crypto_block_device(const char *dm_name,
                        const char *blk_device, const struct par_key *key_buffer,
                        char *crypto_blkdev, size_t crypto_dev_len, uint64_t *blk_sec)
{

    fde_err_t ret = FDE_SUCCESS;
    struct par_key eph_key_buffer;
    char hex_key[2 * EPH_WRAPPED_KEY_MAX_SIZE + 1] = {'\0'};
    // dm_inlinecrypt_t dm_inline;
    char *dm_table = NULL;

    // Get number of sectors and make it 4KB aligned.
    int err = get_block_device_sectors_512(blk_device, blk_sec);
    if (err) {
        LOGE("Get block 512-byte sector, err: %d.\n", err);
        ret = FDE_GENERAL_ERROR;
        goto out;
    }
    *blk_sec &= ~7;

    // Get an enphemeral key as the key is hw_wrapped key
    eph_key_buffer.size = sizeof(eph_key_buffer.key);
    ret = export_ephemeral_wrapped_key(key_buffer,
                                       eph_key_buffer.key,
                                       &eph_key_buffer.size);
    if (ret) goto out;
    if (bytes_to_hex(eph_key_buffer.key, eph_key_buffer.size,
                                hex_key, sizeof(hex_key))) {
        ret = FDE_KEY_ERROR;
        goto out;
    }

    dm_table = get_table_params(
                        cipher_in_kernel, hex_key, 0, blk_device, 0);
    ret = create_dm_device(dm_name, *blk_sec, dm_table,
                           crypto_blkdev, crypto_dev_len);

    if (dm_table)
        free(dm_table);
out:
    memset(&eph_key_buffer, 0, sizeof(eph_key_buffer));
    return ret;
}


fde_err_t enable_fde(const char* mount_point,
                     const char* fde_partition,
                     bool inplace)
{
    fde_err_t ret = FDE_SUCCESS;

    partition_key_blob_t p_key_blob;
    char blk_device[BLK_DEV_MAX_PATH_LEN] = {'\0'};
    char crypto_blkdev[BLK_DEV_MAX_PATH_LEN] = {'\0'};
    uint64_t sectors = 0;

    bool need_format =  false;

    // Validate mount point and fde partition name
    if (!mount_point || !*mount_point || *mount_point != '/') {
        ret = FDE_INVALID_INPUT;
        return ret;
    }
    if (!fde_partition || !*fde_partition
                       || *fde_partition == '/'
                       || strlen(fde_partition) >= MAX_PARTITION_NAME) {
        ret = FDE_INVALID_INPUT;
        return ret;
    }

    ret = get_blk_device_path(blk_device, blkdev_dir, fde_partition);
    if(ret != FDE_SUCCESS) {
        return ret;
    }
    if(!is_real_block_device(blk_device)) {
        LOGE("Checking block device error: %s.\n", strerror(errno));
        ret = FDE_PARTITION_NOT_FOUND;
        return ret;
    }

    LOGD("blk_device: %s.\n", blk_device);

    if (is_blockdev_mounted(blk_device) != UMOUNTED) {
        ret = FDE_MOUNT_ERROR;
        goto out;
    }

    memset(&p_key_blob, 0, sizeof(p_key_blob));
    p_key_blob.p_key.size = sizeof(p_key_blob.p_key.key);
    p_key_blob.p_name.size = strlen(fde_partition);
    strlcpy(p_key_blob.p_name.name,
            fde_partition,
            sizeof(p_key_blob.p_name.name));

    LOGI("Initiale key for the partition, p_name.size = %zu, p_name.name = %s.\n",
                        p_key_blob.p_name.size, p_key_blob.p_name.name);

    // Get key
    ret = retrieve_or_generate_key(&p_key_blob, &need_format);
    if (ret)
        goto out;

    // Create crypto device with dm-inlinecrypt target specified
    ret = create_crypto_block_device(fde_partition,
                                     blk_device,
                                     &p_key_blob.p_key,
                                     crypto_blkdev,
                                     sizeof(crypto_blkdev),
                                     &sectors);
    if (ret) {
        LOGE("Failed to create a crypto block device for %s.\n", blk_device);
        goto out;
    }

    LOGD("Succeed creating crypto device (%s).\n", crypto_blkdev);

    if (!inplace && need_format) {
        if (format_blkdev(crypto_blkdev, SUPPORTED_INPLACE_FS_TYPE)) {
            LOGE("Format %s, error: %s.\n", crypto_blkdev, strerror(errno));
            ret = FDE_FORMAT_ERROR;
            goto out;
        }
    }

    if (inplace) {
        // Try to encrypt the content
        if (!encrypt_inplace(crypto_blkdev, blk_device,
                             SUPPORTED_INPLACE_FS_TYPE, sectors)) {
            LOGE("Failed to encrypt the device (%s)!\n", blk_device);
            ret = FDE_ENCRYPTION_ERROR;
            goto out;
        }
    }

    LOGI("Mounting %s to %s.\n", crypto_blkdev, mount_point);

    if (mount_blkdev(mount_point, crypto_blkdev, SUPPORTED_INPLACE_FS_TYPE)) {
        ret = FDE_MOUNT_ERROR;
        goto out;
    }

out:
    memset(&p_key_blob, 0, sizeof(p_key_blob));

    return ret;
}



