/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2024 Intel Corporation. All rights reserved.
 */

#ifndef __XE_SRIOV_PROVISIONING_H__
#define __XE_SRIOV_PROVISIONING_H__

#include <stdint.h>

struct xe_mmio;

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
 * XE_SRIOV_SHARED_RES_NUM - Number of shared resource types
 */
#define XE_SRIOV_SHARED_RES_NUM (XE_SRIOV_SHARED_RES_LMEM + 1)

/**
 * xe_sriov_for_each_shared_res - Iterate over all shared resource types
 * @res: Loop counter variable of type `enum xe_sriov_shared_res`
 *
 * Iterates over each shared resource type defined in the `enum xe_sriov_shared_res`.
 */
#define xe_sriov_for_each_shared_res(res) \
	for ((res) = 0; (res) < XE_SRIOV_SHARED_RES_NUM; (res)++)

/**
 * xe_sriov_for_each_provisionable_shared_res - Iterate over provisionable shared
 * resource types
 * @res: Loop counter variable of type `enum xe_sriov_shared_res`
 * @pf: PF device file descriptor of type int
 * @gt: GT number of type unsigned int
 *
 * Iterates over each provisionable shared resource type for the given PF device
 * and GT number.
 */
#define xe_sriov_for_each_provisionable_shared_res(res, pf, gt) \
	for ((res) = 0; (res) < XE_SRIOV_SHARED_RES_NUM; (res)++) \
		for_if(xe_sriov_is_shared_res_provisionable((pf), (res), (gt)))

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
bool xe_sriov_is_shared_res_provisionable(int pf, enum xe_sriov_shared_res res, unsigned int gt);
int xe_sriov_find_ggtt_provisioned_pte_offsets(int pf_fd, int gt, struct xe_mmio *mmio,
					       struct xe_sriov_provisioned_range **ranges,
					       unsigned int *nr_ranges);
const char *xe_sriov_shared_res_attr_name(enum xe_sriov_shared_res res,
					  unsigned int vf_num);
int __xe_sriov_pf_get_shared_res_attr(int pf, enum xe_sriov_shared_res res,
				      unsigned int vf_num, unsigned int gt_num,
				      uint64_t *value);
uint64_t xe_sriov_pf_get_shared_res_attr(int pf, enum xe_sriov_shared_res res,
					 unsigned int vf_num,
					 unsigned int gt_num);
int __xe_sriov_pf_set_shared_res_attr(int pf, enum xe_sriov_shared_res res,
				      unsigned int vf_num, unsigned int gt_num,
				      uint64_t value);
void xe_sriov_pf_set_shared_res_attr(int pf, enum xe_sriov_shared_res res,
				     unsigned int vf_num, unsigned int gt_num,
				     uint64_t value);

#endif /* __XE_SRIOV_PROVISIONING_H__ */
