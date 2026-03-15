/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef FDE_KM_H
#define FDE_KM_H


#include <stdint.h>
#include <stddef.h>

#include "km_contract.h"

typedef enum {
    FDE_KM_BACKEND_STUB = 0,
    FDE_KM_BACKEND_IMPL  = 1,
} fde_km_backend_t;


typedef enum {
    FDE_KM_OK = 0,
    FDE_KM_ERR    = -1,
    FDE_KM_ERR_POLICY      = -2,
    FDE_KM_ERR_UNSUPPORTED = -3,
} fde_km_status_t;


void fde_km_init(void);
fde_km_backend_t get_fde_km_backend(void);

fde_km_status_t fde_generate_wrapped_key(uint8_t *key, size_t *key_size);
fde_km_status_t fde_export_ephemeral_key(const uint8_t *key, const size_t key_size,
                                uint8_t *eph_key, size_t *eph_key_size);

#endif //FDE_KM_H