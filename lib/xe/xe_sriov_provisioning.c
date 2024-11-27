// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2024 Intel Corporation. All rights reserved.
 */

#include <errno.h>

#include "igt_core.h"
#include "intel_chipset.h"
#include "linux_scaffold.h"
#include "xe/xe_mmio.h"
#include "xe/xe_sriov_debugfs.h"
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

#define PRE_1250_IP_VER_GGTT_PTE_VFID_MASK	GENMASK_ULL(4, 2)
#define GGTT_PTE_VFID_MASK			GENMASK_ULL(11, 2)
#define GGTT_PTE_VFID_SHIFT			2
#define GUC_GGTT_TOP				0xFEE00000
#define MAX_WOPCM_SIZE				SZ_8M
#define START_PTE_OFFSET			(MAX_WOPCM_SIZE / SZ_4K * sizeof(xe_ggtt_pte_t))
#define MAX_PTE_OFFSET				(GUC_GGTT_TOP / SZ_4K * sizeof(xe_ggtt_pte_t))

static uint64_t get_vfid_mask(int fd)
{
	uint16_t dev_id = intel_get_drm_devid(fd);

	return (intel_graphics_ver(dev_id) >= IP_VER(12, 50)) ?
		GGTT_PTE_VFID_MASK : PRE_1250_IP_VER_GGTT_PTE_VFID_MASK;
}

#define MAX_DEBUG_ENTRIES 70

static int append_range(struct xe_sriov_provisioned_range **ranges,
			unsigned int *nr_ranges, unsigned int vf_id,
			uint32_t start, uint32_t end)
{
	struct xe_sriov_provisioned_range *new_ranges;

	new_ranges = realloc(*ranges,
			     (*nr_ranges + 1) * sizeof(struct xe_sriov_provisioned_range));
	if (!new_ranges) {
		free(*ranges);
		*ranges = NULL;
		*nr_ranges = 0;
		return -ENOMEM;
	}

	*ranges = new_ranges;
	if (*nr_ranges < MAX_DEBUG_ENTRIES)
		igt_debug("Found VF%u GGTT range [%#x-%#x] num_ptes=%ld\n",
			  vf_id, start, end,
			  (end - start + sizeof(xe_ggtt_pte_t)) /
			  sizeof(xe_ggtt_pte_t));
	(*ranges)[*nr_ranges].vf_id = vf_id;
	(*ranges)[*nr_ranges].start = start;
	(*ranges)[*nr_ranges].end = end;
	(*nr_ranges)++;

	return 0;
}

/**
 * xe_sriov_find_ggtt_provisioned_pte_offsets - Find GGTT provisioned PTE offsets
 * @pf_fd: File descriptor for the Physical Function
 * @gt: GT identifier
 * @mmio: Pointer to the MMIO structure
 * @ranges: Pointer to the array of provisioned ranges
 * @nr_ranges: Pointer to the number of provisioned ranges
 *
 * Searches for GGTT provisioned PTE ranges for each VF and populates
 * the provided ranges array with the start and end offsets of each range.
 * The number of ranges found is stored in nr_ranges.
 *
 * Reads the GGTT PTEs and identifies the VF ID associated with each PTE.
 * It then groups contiguous PTEs with the same VF ID into ranges.
 * The ranges are dynamically allocated and must be freed by the caller.
 * The start and end offsets in each range are inclusive.
 *
 * Returns 0 on success, or a negative error code on failure.
 */
int xe_sriov_find_ggtt_provisioned_pte_offsets(int pf_fd, int gt, struct xe_mmio *mmio,
					       struct xe_sriov_provisioned_range **ranges,
					       unsigned int *nr_ranges)
{
	uint64_t vfid_mask = get_vfid_mask(pf_fd);
	unsigned int vf_id, current_vf_id = -1;
	uint32_t current_start = 0;
	uint32_t current_end = 0;
	xe_ggtt_pte_t pte;
	int ret;

	*ranges = NULL;
	*nr_ranges = 0;

	for (uint32_t offset = START_PTE_OFFSET; offset < MAX_PTE_OFFSET;
	     offset += sizeof(xe_ggtt_pte_t)) {
		pte = xe_mmio_ggtt_read(mmio, gt, offset);
		vf_id = (pte & vfid_mask) >> GGTT_PTE_VFID_SHIFT;

		if (vf_id != current_vf_id) {
			if (current_vf_id != -1) {
				/* End the current range and append it */
				ret = append_range(ranges, nr_ranges, current_vf_id,
						   current_start, current_end);
				if (ret < 0)
					return ret;
			}
			/* Start a new range */
			current_vf_id = vf_id;
			current_start = offset;
		}
		current_end = offset;
	}

	if (current_vf_id != -1) {
		/* Append the last range */
		ret = append_range(ranges, nr_ranges, current_vf_id,
				   current_start, current_end);
		if (ret < 0)
			return ret;
	}

	if (*nr_ranges > MAX_DEBUG_ENTRIES)
		igt_debug("Ranges output trimmed to first %u entries out of %u\n",
			  MAX_DEBUG_ENTRIES, *nr_ranges);

	return 0;
}

/**
 * xe_sriov_shared_res_attr_name - Retrieve the attribute name for a shared resource
 * @res: Shared resource type (see enum xe_sriov_shared_res)
 * @vf_num: VF number (1-based) or 0 for PF
 *
 * Returns the attribute name corresponding to the specified
 * shared resource type and VF number. For VF (vf_num > 0), the "quota"
 * attribute name is returned (e.g., "contexts_quota"). For PF (vf_num == 0),
 * the "spare" attribute name is returned (e.g., "contexts_spare").
 *
 * Return:
 * The attribute name as a string if the resource type is valid.
 * NULL if the resource type is invalid.
 */
const char *xe_sriov_shared_res_attr_name(enum xe_sriov_shared_res res,
					  unsigned int vf_num)
{
	switch (res) {
	case XE_SRIOV_SHARED_RES_CONTEXTS:
		return vf_num ? "contexts_quota" : "contexts_spare";
	case XE_SRIOV_SHARED_RES_DOORBELLS:
		return vf_num ? "doorbells_quota" : "doorbells_spare";
	case XE_SRIOV_SHARED_RES_GGTT:
		return vf_num ? "ggtt_quota" : "ggtt_spare";
	case XE_SRIOV_SHARED_RES_LMEM:
		return vf_num ? "lmem_quota" : "lmem_spare";
	}

	return NULL;
}

/**
 * __xe_sriov_pf_get_shared_res_attr - Read shared resource attribute
 * @pf: PF device file descriptor
 * @res: Shared resource type (see enum xe_sriov_shared_res)
 * @vf_num: VF number (1-based) or 0 for PF
 * @gt_num: GT number
 * @value: Pointer to store the read attribute value
 *
 * Reads the specified shared resource attribute for the given PF device @pf,
 * VF number @vf_num, and GT @gt_num. The attribute depends on @vf_num:
 * - For VF (vf_num > 0), reads the "quota" attribute.
 * - For PF (vf_num == 0), reads the "spare" attribute.
 *
 *
 * Return: 0 on success, negative error code on failure.
 */
int __xe_sriov_pf_get_shared_res_attr(int pf, enum xe_sriov_shared_res res,
				      unsigned int vf_num, unsigned int gt_num,
				      uint64_t *value)
{
	return __xe_sriov_pf_debugfs_get_u64(pf, vf_num, gt_num,
					     xe_sriov_shared_res_attr_name(res, vf_num),
					     value);
}

/**
 * xe_sriov_pf_get_shared_res_attr - Read shared resource attribute
 * @pf: PF device file descriptor
 * @res: Shared resource type (see enum xe_sriov_shared_res)
 * @vf_num: VF number (1-based) or 0 for PF
 * @gt_num: GT number
 *
 * A throwing version of __xe_sriov_pf_get_shared_res_attr().
 * Instead of returning an error code, it returns the quota value and asserts
 * in case of an error.
 *
 * Return: The value for the given shared resource attribute.
 *         Asserts in case of failure.
 */
uint64_t xe_sriov_pf_get_shared_res_attr(int pf, enum xe_sriov_shared_res res,
					 unsigned int vf_num,
					 unsigned int gt_num)
{
	uint64_t value;

	igt_fail_on(__xe_sriov_pf_get_shared_res_attr(pf, res, vf_num, gt_num, &value));

	return value;
}

/**
 * __xe_sriov_pf_set_shared_res_attr - Set a shared resource attribute
 * @pf: PF device file descriptor
 * @res: Shared resource type (see enum xe_sriov_shared_res)
 * @vf_num: VF number (1-based) or 0 for PF
 * @gt_num: GT number
 * @value: Value to set for the shared resource attribute
 *
 * Sets the specified shared resource attribute for the given PF device @pf,
 * VF number @vf_num, and GT @gt_num. The attribute depends on @vf_num:
 * - For VF (vf_num > 0), reads the "quota" attribute.
 * - For PF (vf_num == 0), reads the "spare" attribute.
 *
 * Return: 0 on success, negative error code on failure.
 */
int __xe_sriov_pf_set_shared_res_attr(int pf, enum xe_sriov_shared_res res,
				      unsigned int vf_num, unsigned int gt_num,
				      uint64_t value)
{
	return __xe_sriov_pf_debugfs_set_u64(pf, vf_num, gt_num,
					     xe_sriov_shared_res_attr_name(res, vf_num),
					     value);
}

/**
 * xe_sriov_pf_set_shared_res_attr - Set the shared resource attribute value
 * @pf: PF device file descriptor
 * @res: Shared resource type (see enum xe_sriov_shared_res)
 * @vf_num: VF number (1-based) or 0 for PF
 * @gt_num: GT number
 * @value: Value to set
 *
 * A throwing version of __xe_sriov_pf_set_shared_res_attr().
 * Instead of returning an error code, it asserts in case of an error.
 */
void xe_sriov_pf_set_shared_res_attr(int pf, enum xe_sriov_shared_res res,
				     unsigned int vf_num, unsigned int gt_num,
				     uint64_t value)
{
	igt_fail_on(__xe_sriov_pf_set_shared_res_attr(pf, res, vf_num, gt_num, value));
}
