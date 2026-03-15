/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef FDE_CORE_H_
#define FDE_CORE_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef OE
#include <syslog.h>
#define LOGI(...) syslog(LOG_NOTICE, "INFO:" __VA_ARGS__)
#define LOGV(...) syslog(LOG_NOTICE,"VERB:" __VA_ARGS__)
#define LOGD(...) syslog(LOG_DEBUG,"DBG:" __VA_ARGS__)
#define LOGE(...) syslog(LOG_ERR,"ERR:" __VA_ARGS__)
#define LOGW(...) syslog(LOG_WARNING,"WRN:" __VA_ARGS__)
#ifdef USE_GLIB
#include <glib.h>
#define strlcpy g_strlcpy
#endif
#endif


#define BLK_DEV_MAX_PATH_LEN        256
#define MAX_PARTITION_NAME          64

typedef enum {
	FDE_SUCCESS_NEEDS_FORMAT    = 1,
	FDE_SUCCESS                 = 0,
	FDE_GENERAL_ERROR           = -1,
	FDE_NOT_SUPPORTED           = -2,
	FDE_INVALID_INPUT           = -3,
	FDE_BACKUP_FILE_ERROR       = -4,
	FDE_PARTITION_NOT_FOUND     = -5,
    FDE_KEY_ERROR               = -6,
    FDE_DM_ERROR                = -7,
    FDE_ENCRYPTION_ERROR        = -8,
    FDE_MOUNT_ERROR             = -9,
    FDE_PARTITION_ERROR         = -10,
    FDE_FORMAT_ERROR            = -11,
} fde_err_t;

fde_err_t enable_fde(const char* mount_point,
                     const char* fde_partition,
                     bool inplace);

#endif //FDE_CORE_H_
