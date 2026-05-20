/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <dlfcn.h>

#include "fde_core.h"
#include "fde_km.h"
#include "km_contract.h"

struct key_impl_library {
    char *name;
    char *get_wrapped_key;
    char *get_ephemeral_key;
};

static struct key_impl_library g_impl_lib = {
#ifdef OE
    .name = "libkm_contract_impl.so.1",
#else
    .name = "km_contract_impl.so",
#endif
    .get_wrapped_key = "generate_wrapped_key",
    .get_ephemeral_key = "export_ephemeral_key",
};

typedef struct {
    km_contract_status_t (*generate_key)(uint8_t *, size_t *);
    km_contract_status_t (*export_key)
                         (const uint8_t *, const size_t, uint8_t *, size_t *);
} km_contract_vtbl_t;

static km_contract_vtbl_t g_km = {0};
static void *g_km_handle = NULL;
static fde_km_backend_t g_backend = FDE_KM_BACKEND_STUB;

static bool fde_km_initialized = false;

// Point to whatever is currently linked (stub weak symbols)
static void set_default_key_impl(void)
{
    g_km.generate_key= generate_wrapped_key;
    g_km.export_key = export_ephemeral_key;
}

// Try to load strong implementation library
static void load_specific_key_impl(void)
{
    const char *key_so = getenv("KM_CONTRACT_IMPL_LIB");
    if (!key_so || !*key_so) {
        LOGE("Don't find the lib %s.\n",
                        g_impl_lib.name);
        return;
    }

    g_km_handle = dlopen(key_so, RTLD_NOW);
    if (g_km_handle) {
        LOGD("dlopen(%s) succeeds.\n", g_impl_lib.name);

        km_contract_vtbl_t key_imp;
        key_imp.generate_key =
                    dlsym(g_km_handle, g_impl_lib.get_wrapped_key);
        if (!key_imp.generate_key) {
            LOGE("dlsym(%s) fails, err: %s.\n",
                    g_impl_lib.get_wrapped_key, dlerror());

            dlclose(g_km_handle);
            g_km_handle = NULL;
            return;
        }
        key_imp.export_key =
                    dlsym(g_km_handle, g_impl_lib.get_ephemeral_key);
        if (!key_imp.export_key) {
            LOGE("dlsym(%s) fails, err: %s.\n",
                    g_impl_lib.get_ephemeral_key, dlerror());

            dlclose(g_km_handle);
            g_km_handle = NULL;
            return;
        }

        g_km.generate_key = key_imp.generate_key;
        g_km.export_key = key_imp.export_key;

        g_backend = FDE_KM_BACKEND_IMPL;
    } else {
        LOGE("Failed to open the lib %s.\n",
                g_impl_lib.name);
    }

    return;
}

__attribute__((destructor))
static void km_contract_loader_dtor(void)
{
    if (g_km_handle) {
        dlclose(g_km_handle);
        g_km_handle = NULL;
    }
}

void fde_km_init(void)
{
    if (!fde_km_initialized) {
        load_specific_key_impl();
        if (g_backend == FDE_KM_BACKEND_STUB) {
            LOGW("Fallback to the stub.\n");
            set_default_key_impl();
        }

        fde_km_initialized = true;
    }
}

fde_km_backend_t get_fde_km_backend(void)
{
    return g_backend;
}

fde_km_status_t fde_generate_wrapped_key(uint8_t *key, size_t *key_size)
{
    if (g_backend == FDE_KM_BACKEND_STUB)
        return FDE_KM_ERR_POLICY;

    if (!g_km.generate_key)
        return FDE_KM_ERR;

    km_contract_status_t rc = g_km.generate_key(key, key_size);
    return (rc == KM_CONTRACT_OK) ? FDE_KM_OK : FDE_KM_ERR;
}

fde_km_status_t fde_export_ephemeral_key(const uint8_t *key, const size_t key_size,
                                uint8_t *eph_key, size_t *eph_key_size)
{
    if (g_backend == FDE_KM_BACKEND_STUB)
        return FDE_KM_ERR_POLICY;

    if (!g_km.export_key)
        return FDE_KM_ERR;

    km_contract_status_t rc = g_km.export_key(key, key_size, eph_key, eph_key_size);
    return (rc == KM_CONTRACT_OK) ? FDE_KM_OK : FDE_KM_ERR;
}