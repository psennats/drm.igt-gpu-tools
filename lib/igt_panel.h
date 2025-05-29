/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef IGT_PANEL_H
#define IGT_PANEL_H

#include <stdbool.h>
#include <string.h>

bool igt_is_panel_blocked(const char *vendor_name,
				 const char *const blocklist[],
				 size_t blocklist_size);

#endif

