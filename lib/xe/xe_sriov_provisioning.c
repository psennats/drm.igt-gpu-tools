// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2024 Intel Corporation. All rights reserved.
 */

#include <stdlib.h>

#include "xe/xe_sriov_provisioning.h"

/**
 * xe_sriov_shared_res_to_string:
 * @key: The shared resource of type enum xe_sriov_shared_res
 *
 * Converts a shared resource enum to its corresponding string
 * representation. It is useful for logging and debugging purposes.
 *
 * Return: A string representing the shared resource key.
 */
const char *xe_sriov_shared_res_to_string(enum xe_sriov_shared_res res)
{
	switch (res) {
	case XE_SRIOV_SHARED_RES_CONTEXTS:
		return "contexts";
	case XE_SRIOV_SHARED_RES_DOORBELLS:
		return "doorbells";
	case XE_SRIOV_SHARED_RES_GGTT:
		return "ggtt";
	case XE_SRIOV_SHARED_RES_LMEM:
		return "lmem";
	}

	return NULL;
}
