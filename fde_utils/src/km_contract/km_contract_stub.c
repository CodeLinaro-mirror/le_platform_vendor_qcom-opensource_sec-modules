/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "km_contract.h"

__attribute__((weak))
km_contract_status_t generate_wrapped_key(uint8_t *key, size_t *key_size)
{
    return KM_CONTRACT_ERR;
}

__attribute__((weak))
km_contract_status_t export_ephemeral_key(const uint8_t *key, const size_t key_size,
                                uint8_t *eph_key, size_t *eph_key_size)
{
    return KM_CONTRACT_ERR;
}