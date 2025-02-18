/* SPDX-License-Identifier: MIT
 * Copyright 2025 Advanced Micro Devices, Inc.
 */
#ifndef AMD_MEM_LEAK_H
#define AMD_MEM_LEAK_H

#include <stdio.h>
#include <amdgpu.h>
#include "amd_ip_blocks.h"

/* return true if kmemleak is enabled and then clear earlier leak records */
bool clear_memleak(bool is_more_than_one);

/* return true if kmemleak did not pick up any memory leaks */
bool is_no_memleak(void);

#endif
