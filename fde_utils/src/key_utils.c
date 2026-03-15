/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>

#include <sys/stat.h>

#include "fde_core.h"
#include "fde_km.h"
#include "key_utils.h"
#include "utils.h"

// Ensure this is larger than the size of g_key_path.
#define KEY_PATH_SIZE 30
static const char *g_key_path = "/persist/fde/key_id_wkb_info";

/**
 * Retrieve a key from the storage file based on key id.
 * @param p_key_blob: provides key id and its key blob need be filled.
 * @param key_file: implies where the key is stroed.
 * @return 0 on success, negtive value on error.
 */
static fde_err_t retrieve_key(partition_key_blob_t *p_key_blob, const char *key_file)
{
    FILE *fp = NULL;
    partition_key_blob_t read_iter;
    int index = 0;
    fde_err_t ret = FDE_KEY_ERROR;

    if (!p_key_blob || !key_file)
        return FDE_INVALID_INPUT;

    fp = fopen(key_file, "rb");
    if (!fp) {
        LOGE("Failed to open the key file, fp = %p, errno = %d.\n", fp, errno);
        return FDE_GENERAL_ERROR;
    }

    rewind(fp);
    while (fread(&read_iter, sizeof(partition_key_blob_t), 1, fp) == 1)
    {
        if ((read_iter.p_name.size == p_key_blob->p_name.size) &&
                    !memcmp(read_iter.p_name.name,
                            p_key_blob->p_name.name,
                            p_key_blob->p_name.size)) {
            LOGI("Found key, index: %d, id: %s.\n", index, p_key_blob->p_name.name);
            p_key_blob->p_key.size = read_iter.p_key.size;
            size_t copy_size = memscpy(p_key_blob->p_key.key, KEY_BLOB_MAX_SIZE,
                    read_iter.p_key.key, KEY_BLOB_MAX_SIZE);
            if (copy_size != KEY_BLOB_MAX_SIZE) {
                LOGE("The key is corrupted for the partition (%s), copy_size: %zu.\n",
                                    p_key_blob->p_name.name, copy_size);
                ret = FDE_KEY_ERROR;
            } else {
                ret = FDE_SUCCESS;
            }
            break;
        }
        index++;
    }

#ifdef DEBUG_FDE
    char key_print[2 * KEY_BLOB_MAX_SIZE + 1] = {'\0'};
    bytes_to_hex(p_key_blob->p_key.key, p_key_blob->p_key.size, key_print, sizeof(key_print));
    LOGD("%s: key_size: %zu, key: %s.\n", __func__, p_key_blob->p_key.size, key_print);
#endif
    if (fp)
        fclose(fp);
    return ret;
}

/**
 * Appends a key entry to the end of storage file in form of
 * (id, keyblob).
 * @param p_key_blob: includes key id and its key blob.
 * @param key_file: implies where the key is stroed.
 * @return 0 on success, negtive value on error.
 */
static fde_err_t store_key(const partition_key_blob_t *p_key_blob, const char *key_file,
                           bool is_new_file)
{
    FILE *fp = NULL;
    fde_err_t ret = FDE_KEY_ERROR;

    if (!p_key_blob || !key_file)
        return FDE_INVALID_INPUT;

    fp = fopen(key_file, "ab+");
    if (!fp) {
        LOGE("Failed to open the key file, fp = %p, errno = %d.\n", fp, errno);
        return FDE_GENERAL_ERROR;
    }
    if (is_new_file && chmod(key_file, 0600) != 0) {
        LOGE("Failed to set permissions on key file, errno = %d.\n", errno);
        ret = FDE_GENERAL_ERROR;
        goto out;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        LOGE("Failed to seek to end of key file, errno = %d.\n", errno);
        ret = FDE_GENERAL_ERROR;
        goto out;
    }
    if (fwrite(p_key_blob, sizeof(partition_key_blob_t), 1, fp) == 1) {
        ret = FDE_SUCCESS;
    } else {
        LOGE("Failed to save key, id: %s, errno = %d.\n",
             (char *)p_key_blob->p_name.name, errno);
    }

out:
    if (fp)
        fclose(fp);

    return ret;
}

fde_err_t retrieve_or_generate_key(partition_key_blob_t *p_key_blob, bool *is_new_key)
{
    fde_err_t ret = FDE_GENERAL_ERROR;
    char key_path[KEY_PATH_SIZE] = {'\0'};
    char *key_dir = NULL;
    bool create_file = false;

    if (!p_key_blob || !is_new_key)
        return FDE_INVALID_INPUT;

    *is_new_key = false;

    // Check if the key directory exist or not.
    if (strlcpy(key_path, g_key_path, sizeof(key_path)) > KEY_PATH_SIZE) {
        LOGD("The key path is truncated.\n");
        goto out;
    }
    key_dir = dirname(key_path);
    if (!key_dir)
        goto out;

    if (pathExists(key_dir)) {
        LOGD("The key path already exists, retrieve key.\n");
        ret = retrieve_key(p_key_blob, g_key_path);
        if (ret) {
            LOGW("Didn't find the key for %s in the key file, need add it.\n",
                  p_key_blob->p_name.name);
        } else {
            ret = FDE_SUCCESS;
            goto out;
        }
    } else {
        LOGD("The key path doesn't exist, need to create it.\n");
        if (mkdir(key_dir, 0600) != 0 && errno != EEXIST) {
            LOGE("Failed to create the key directory %s.\n", key_dir);
            ret = FDE_GENERAL_ERROR;
            goto out;
        }
        create_file = true;
    }

    fde_km_status_t err = fde_generate_wrapped_key(p_key_blob->p_key.key, &p_key_blob->p_key.size);
    if (err != FDE_KM_OK) {
        LOGE("Failed to generate key, err = %d.\n", err);
        ret = FDE_KEY_ERROR;
        goto out;
    }

    ret = store_key(p_key_blob, g_key_path, create_file);
    *is_new_key = true;

#ifdef DEBUG_FDE
    char key_print[2 * KEY_BLOB_MAX_SIZE + 1] = {'\0'};
    bytes_to_hex(p_key_blob->p_key.key, p_key_blob->p_key.size, key_print, sizeof(key_print));
    LOGD("%s: key_size: %zu, key: %s.\n", __func__, p_key_blob->p_key.size, key_print);
#endif
out:
    memset(key_path, 0, KEY_PATH_SIZE);
    key_dir = NULL;

    return ret;
}

fde_err_t export_ephemeral_wrapped_key(const struct par_key *p_key,
                                      uint8_t *eph_key, size_t *eph_key_size)
{
    fde_err_t ret = FDE_SUCCESS;
    fde_km_status_t err = FDE_KM_OK;

    if (!p_key || !p_key->key || !p_key->size
               || !eph_key || !eph_key_size)
        return FDE_INVALID_INPUT;

    err = fde_export_ephemeral_key(p_key->key, p_key->size, eph_key, eph_key_size);
    if (err != FDE_KM_OK) {
        ret = FDE_KEY_ERROR;
        LOGE("Failed to export ephemeral key, err = %d.\n", err);
    }

#ifdef DEBUG_FDE
    char key_print[2 * EPH_WRAPPED_KEY_MAX_SIZE + 1] = {'\0'};
    bytes_to_hex(eph_key, *eph_key_size, key_print, sizeof(key_print));
    LOGD("%s: key_size: %zu, key: %s.\n", __func__, *eph_key_size, key_print);
#endif

    return ret;
}