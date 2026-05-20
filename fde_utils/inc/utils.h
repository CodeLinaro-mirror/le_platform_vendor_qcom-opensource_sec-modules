/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef UTILS_H_
#define UTILS_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef enum {
  INVALID_MOUNT_STATUS = -1,
  UMOUNTED               = 0,
  MOUNTED              = 1,
} mount_status_t;

static inline size_t memscpy(void *dst, size_t dst_size,
                             const void  *src, size_t src_size)
{
  if (!dst || !dst_size || !src || !src_size)
    return 0;

  size_t  copy_size = (dst_size <= src_size)? dst_size : src_size;
  memcpy(dst, src, copy_size);
  return copy_size;
}

int bytes_to_hex(const uint8_t *bytes, size_t num_bytes,
                 char *hex, size_t hex_size);

bool pathExists(const char* path);

bool is_real_block_device(const char *blk_dev);
int get_block_device_sectors_512(const char *path, uint64_t *nr_sec);

mount_status_t is_blockdev_mounted(const char *blkdev);
int mount_blkdev(const char *mount_point, const char *blkdev,
                 const char *fs_type);
int format_blkdev(const char *blkdev, const char *fs_type);

#endif //UTILS_H_