/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2024 Intel Corporation. All rights reserved.
 */

#ifndef __XE_SRIOV_PROVISIONING_H__
#define __XE_SRIOV_PROVISIONING_H__

#include <stdint.h>

/**
 * enum xe_sriov_shared_res - Shared resource types
 * @XE_SRIOV_SHARED_RES_CONTEXTS: Contexts
 * @XE_SRIOV_SHARED_RES_DOORBELLS: Doorbells
 * @XE_SRIOV_SHARED_RES_GGTT: GGTT (Global Graphics Translation Table)
 * @XE_SRIOV_SHARED_RES_LMEM: Local memory
 *
 * This enumeration defines the types of shared resources
 * that can be provisioned to Virtual Functions (VFs).
 */
enum xe_sriov_shared_res {
	XE_SRIOV_SHARED_RES_CONTEXTS,
	XE_SRIOV_SHARED_RES_DOORBELLS,
	XE_SRIOV_SHARED_RES_GGTT,
	XE_SRIOV_SHARED_RES_LMEM,
};

/**
 * struct xe_sriov_provisioned_range - Provisioned range for a Virtual Function (VF)
 * @vf_id: The ID of the VF
 * @start: The inclusive start of the provisioned range
 * @end: The inclusive end of the provisioned range
 *
 * This structure represents a range of resources that have been provisioned
 * for a specific VF, with both start and end values included in the range.
 */
struct xe_sriov_provisioned_range {
	unsigned int vf_id;
	uint64_t start;
	uint64_t end;
};

const char *xe_sriov_shared_res_to_string(enum xe_sriov_shared_res res);

#endif /* __XE_SRIOV_PROVISIONING_H__ */
