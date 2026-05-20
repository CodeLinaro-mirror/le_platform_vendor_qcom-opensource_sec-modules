/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef KM_UTILS_H_
#define KM_UTILS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fde_core.h"

#define KEY_BLOB_MAX_SIZE 256
/*
 * Wrapped key sizes may be different for different versions of
 * HW. Ensure this max size is same to BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE
 * in linux/block-crypto.h.
 */
#define EPH_WRAPPED_KEY_MAX_SIZE 128


struct par_name {
    char name[MAX_PARTITION_NAME];
    size_t size;
};

struct par_key {
    uint8_t key[KEY_BLOB_MAX_SIZE];
    size_t size;
};

// struct partition_key_blob_t for each (name, key) pair
typedef struct {
    struct par_name p_name;
    struct par_key p_key;
} partition_key_blob_t;


/**
 * Find the crypto key of a specific partition from the key file.
 * If it doesn't exit, generate one for this partition.
 * @param p_key_blob: the crypto key which protects this partition.
 * @param is_new_key: if the key is generated in the first time, that means the
 *                    corresponding partition need to be formated.
 * @return 0 on success, negtive value on error.
 */
fde_err_t retrieve_or_generate_key(partition_key_blob_t *p_key_blob, bool *is_new_key);


fde_err_t export_ephemeral_wrapped_key(const struct par_key *p_key,
                                      uint8_t *eph_key, size_t *eph_key_size);

#endif //KM_UTILS_H_