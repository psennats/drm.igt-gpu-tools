/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#include <stdbool.h>
#include <string.h>

#include "drmtest.h"
#include "igt_panel.h"

/**
 * igt_is_panel_blocked - Checks if a given vendor name is present in a blocklist.
 *
 * @vendor_name: The name of the vendor to check for in the blocklist.
 * @blocklist: An array of strings representing the blocklist.
 * @blocklist_size: The number of entries in the blocklist array.
 *
 * Returns:
 * true if the vendor name is found in the blocklist, false otherwise.
 */
bool igt_is_panel_blocked(const char *vendor_name,
			    const char *const blocklist[],
			    size_t blocklist_size)
{
	int i;

	for (i = 0; i < blocklist_size; i++) {
		if (strstr(blocklist[i], vendor_name) != NULL)
			return true;
	}

	return false;
}

