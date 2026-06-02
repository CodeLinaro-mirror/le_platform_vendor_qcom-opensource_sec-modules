/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "fde_km.h"
#include "fde_core.h"

static void print_help(void)
{
    printf("fde_utils options:\n");
    printf("-h for help \n");
    printf("-b for getting the backend of key operation \n");
    printf("-p <partition name> -m <mount_point> \n");
    printf("-p <partition name> -m <mount_point> inplace \n");
}

static int try_enable_fde(const char* partition, const char* mount, bool in_place)
{
    fde_km_init();
    if (get_fde_km_backend() == FDE_KM_BACKEND_STUB) {
        printf("DRY-RUN: would enable FDE on partition %s.\n", partition);
        printf("DRY-RUN: would generate key blob and store key info.\n");
        printf("DRY-RUN: would call driver ioctl to enable FDE.\n");

        return 0;
    }

    return enable_fde(mount, partition, in_place);
}

static void check_km_backend(void)
{
    fde_km_backend_t backend;

    fde_km_init();
    backend = get_fde_km_backend();
    switch (backend)
    {
    case FDE_KM_BACKEND_STUB:
        printf("The key backend of fde_utils is STUB.\n");
        break;
    case FDE_KM_BACKEND_IMPL:
        printf("The key backend of fde_utils is IMPL.\n");
        break;
    default:
        printf("The key backend of fde_utils is unknown: %d.\n", backend);
        break;
    }
}

int main(int argc,char *argv[])
{
    /* Partition which has to undergo normal/inplace FDE */
    char* fde_partition = NULL;
    /* Mount point where the partition will be mounted */
    char* mount_point = NULL;
    /* If inplace FDE is required */
    bool in_place = false;

    if (argc == 2) {
        if (!strcmp(argv[1], "-h")) {
            print_help();
            return 0;
        } else if (!strcmp(argv[1], "-b")) {
            check_km_backend();
            return 0;
        }

        goto invalid_err;
    } else if (argc >= 5) {
        if (!strcmp(argv[1], "-p") && !strcmp(argv[3], "-m")) {

            fde_partition = argv[2];
            mount_point = argv[4];
            if (argc == 6 && !strcmp(argv[5], "inplace")) {
                in_place = true;
            } else {
                goto invalid_err;
            }

            LOGI("FDE partition: %s, mount point: %s, inplace: %d.\n",
                                 fde_partition, mount_point, in_place);

            return try_enable_fde(fde_partition, mount_point, in_place);
        } else {
            goto invalid_err;
        }
    }

invalid_err:
    LOGE("Invalid argument.\n");
    print_help();
    return -1;
}
