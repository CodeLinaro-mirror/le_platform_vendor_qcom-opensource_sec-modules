/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef KM_CONTRACT_H_
#define KM_CONTRACT_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KM_CONTRACT_OK = 0,
    KM_CONTRACT_ERR = -1,
    KM_CONTRACT_ERR_INVALID_PARAM = -2,
    KM_CONTRACT_ERR_UNSUPPORTED   = -3,
    KM_CONTRACT_ERR_KEY = -4,
} km_contract_status_t;

km_contract_status_t generate_wrapped_key(uint8_t *key, size_t *key_size);
km_contract_status_t export_ephemeral_key(const uint8_t *key, const size_t key_size,
                                uint8_t *eph_key, size_t *eph_key_size);

#ifdef __cplusplus
}
#endif

#endif //KM_CONTRACT_H_