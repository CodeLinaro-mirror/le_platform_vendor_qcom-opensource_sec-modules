/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * ​​​​​Changes from Qualcomm Technologies, Inc. are provided under the following license:
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef ENCRYPT_INPLACE_H
#define ENCRYPT_INPLACE_H

#include <stdint.h>
#include <stdbool.h>

#define SUPPORTED_INPLACE_FS_TYPE "ext4"

bool encrypt_inplace(const char *crypto_blkdev,
                     const char *real_blkdev,
                     const char *fs_type,
                     uint64_t nr_sec);

#endif //ENCRYPT_INPLACE_H