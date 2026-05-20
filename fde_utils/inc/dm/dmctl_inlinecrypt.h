/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef DM_TARGET_INLINECRYPT_H_
#define DM_TARGET_INLINECRYPT_H_

#include <stdint.h>
#include <stddef.h>

#include "fde_core.h"

// copied from /include/uapi/linux/dm-ioctl.h
#define DM_MAX_TYPE_NAME 16
#define DM_INLINE_TYPE_NAME "inlinecrypt"

char* get_table_params(const char *cipher,
                       const char *key,
                       const uint64_t iv_offset,
                       const char *blk_dev,
                       const uint64_t start_sector);
fde_err_t create_dm_device(const char *name, const uint64_t nr_sec,
                     const char *table, char *dm_path, size_t dm_path_len);


#endif //DM_TARGET_INLINECRYPT_H_