/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef INTEL_FBC_H
#define INTEL_FBC_H

#include "igt.h"

#define intel_fbc_enable(device) igt_set_module_param_int(device, "enable_fbc", 1)
#define intel_fbc_disable(device) igt_set_module_param_int(device, "enable_fbc", 0)

enum psr_mode;

bool intel_fbc_supported_on_chipset(int device, enum pipe pipe);
bool intel_fbc_wait_until_enabled(int device, enum pipe pipe);
bool intel_fbc_is_enabled(int device, enum pipe pipe, int log_level);
void intel_fbc_max_plane_size(int fd, uint32_t *width, uint32_t *height);
bool intel_fbc_plane_size_supported(int device, uint32_t width, uint32_t height);
bool intel_fbc_supported_for_psr_mode(int disp_ver, enum psr_mode mode);

#endif
