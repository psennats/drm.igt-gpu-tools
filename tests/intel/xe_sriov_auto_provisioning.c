// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2023 Intel Corporation. All rights reserved.
 */

#include <stdbool.h>

#include "drmtest.h"
#include "igt_core.h"
#include "igt_sriov_device.h"
#include "igt_sysfs.h"
#include "xe/xe_sriov_debugfs.h"
#include "xe/xe_sriov_provisioning.h"
#include "xe/xe_query.h"

/**
 * TEST: xe_sriov_auto_provisioning
 * Category: Core
 * Mega feature: SR-IOV
 * Sub-category: provisioning
 * Functionality: auto-provisioning
 * Run type: FULL
 * Description: Examine behavior of SR-IOV auto-provisioning
 *
 * SUBTEST: fair-allocation
 * Description:
 *   Verify that auto-provisioned resources are allocated by PF driver in fairly manner
 *
 * SUBTEST: resources-released-on-vfs-disabling
 * Description:
 *   Verify that auto-provisioned resources are released once VFs are disabled
 *
 * SUBTEST: exclusive-ranges
 * Description:
 *   Verify that ranges of auto-provisioned resources are exclusive
 */

IGT_TEST_DESCRIPTION("Xe tests for SR-IOV auto-provisioning");

/* Expects ranges sorted by VF IDs */
static int ranges_fair_allocation(enum xe_sriov_shared_res res,
				  struct xe_sriov_provisioned_range *ranges,
				  unsigned int nr_ranges)
{
	uint64_t expected_allocation = ranges[0].end - ranges[0].start + 1;

	for (unsigned int i = 1; i < nr_ranges; i++) {
		uint64_t current_allocation = ranges[i].end - ranges[i].start + 1;

		if (igt_debug_on_f(current_allocation != expected_allocation,
				   "%s: Allocation mismatch, expected=%lu VF%u=%lu\n",
				   xe_sriov_debugfs_provisioned_attr_name(res),
				   expected_allocation, ranges[i].vf_id,
				   current_allocation)) {
			return -1;
		}
	}

	return 0;
}

static int check_fair_allocation(int pf_fd, unsigned int num_vfs, unsigned int gt_id,
				 enum xe_sriov_shared_res res)
{
	struct xe_sriov_provisioned_range *ranges;
	int ret;

	ret = xe_sriov_pf_debugfs_read_check_ranges(pf_fd, res, gt_id, &ranges, num_vfs);
	if (igt_debug_on_f(ret, "%s: Failed ranges check on GT%u (%d)\n",
			   xe_sriov_debugfs_provisioned_attr_name(res), gt_id, ret))
		return ret;

	ret = ranges_fair_allocation(res, ranges, num_vfs);
	if (ret) {
		free(ranges);
		return ret;
	}

	free(ranges);

	return 0;
}

static void fair_allocation(int pf_fd, unsigned int num_vfs)
{
	enum xe_sriov_shared_res res;
	unsigned int gt;
	int fails = 0;

	igt_sriov_disable_driver_autoprobe(pf_fd);
	igt_sriov_enable_vfs(pf_fd, num_vfs);

	xe_for_each_gt(pf_fd, gt) {
		xe_sriov_for_each_provisionable_shared_res(res, pf_fd, gt) {
			if (igt_debug_on_f(check_fair_allocation(pf_fd, num_vfs, gt, res),
					   "%s fair allocation failed on gt%u\n",
					   xe_sriov_shared_res_to_string(res), gt))
				fails++;
		}
	}

	igt_sriov_disable_vfs(pf_fd);

	igt_fail_on_f(fails, "fair allocation failed\n");
}

static void resources_released_on_vfs_disabling(int pf_fd, unsigned int num_vfs)
{
	struct xe_sriov_provisioned_range *ranges;
	enum xe_sriov_shared_res res;
	unsigned int gt;
	int fails = 0;

	igt_sriov_disable_driver_autoprobe(pf_fd);
	igt_sriov_enable_vfs(pf_fd, num_vfs);

	xe_for_each_gt(pf_fd, gt) {
		xe_sriov_for_each_provisionable_shared_res(res, pf_fd, gt) {
			if (igt_warn_on_f(xe_sriov_pf_debugfs_read_check_ranges(pf_fd, res,
										gt,
										&ranges,
										num_vfs),
					  "%s: Failed ranges check on gt%u\n",
					  xe_sriov_debugfs_provisioned_attr_name(res), gt))
				continue;

			free(ranges);
		}
	}

	igt_sriov_disable_vfs(pf_fd);

	xe_for_each_gt(pf_fd, gt) {
		xe_sriov_for_each_provisionable_shared_res(res, pf_fd, gt) {
			if (igt_debug_on_f(xe_sriov_pf_debugfs_read_check_ranges(pf_fd, res,
										 gt,
										 &ranges,
										 0),
					   "%s: Failed ranges check on gt%u\n",
					   xe_sriov_debugfs_provisioned_attr_name(res), gt))
				fails++;
		}
	}

	igt_fail_on_f(fails, "shared resource release check failed\n");
}

static int compare_ranges_by_start(const void *a, const void *b)
{
	const struct xe_sriov_provisioned_range *range_a = a;
	const struct xe_sriov_provisioned_range *range_b = b;

	if (range_a->start < range_b->start)
		return -1;
	if (range_a->start > range_b->start)
		return 1;
	return 0;
}

static int check_no_overlap(int pf_fd, unsigned int num_vfs, unsigned int gt_id,
			    enum xe_sriov_shared_res res)
{
	struct xe_sriov_provisioned_range *ranges;
	int ret;

	ret = xe_sriov_pf_debugfs_read_check_ranges(pf_fd, res, gt_id, &ranges, num_vfs);
	if (ret)
		return ret;

	igt_assert(ranges);
	qsort(ranges, num_vfs, sizeof(ranges[0]), compare_ranges_by_start);

	for (unsigned int i = 0; i < num_vfs - 1; i++)
		if (ranges[i].end >= ranges[i + 1].start) {
			igt_debug((res == XE_SRIOV_SHARED_RES_GGTT) ?
				  "Overlapping ranges: VF%u [%lx-%lx] and VF%u [%lx-%lx]\n" :
				  "Overlapping ranges: VF%u [%lu-%lu] and VF%u [%lu-%lu]\n",
				  ranges[i].vf_id, ranges[i].start, ranges[i].end,
				  ranges[i + 1].vf_id, ranges[i + 1].start, ranges[i + 1].end);
			free(ranges);
			return -1;
		}

	free(ranges);

	return 0;
}

static void exclusive_ranges(int pf_fd, unsigned int num_vfs)
{
	enum xe_sriov_shared_res res;
	unsigned int gt;
	int fails = 0;

	igt_sriov_disable_driver_autoprobe(pf_fd);
	igt_sriov_enable_vfs(pf_fd, num_vfs);

	xe_for_each_gt(pf_fd, gt) {
		xe_sriov_for_each_provisionable_shared_res(res, pf_fd, gt) {
			if (res == XE_SRIOV_SHARED_RES_LMEM)
				/*
				 * lmem_provisioned is not applicable for this test,
				 * as it does not expose ranges
				 */
				continue;

			if (igt_debug_on_f(check_no_overlap(pf_fd, num_vfs, gt, res),
					   "%s overlap check failed on gt%u\n",
					   xe_sriov_shared_res_to_string(res), gt))
				fails++;
		}
	}

	igt_sriov_disable_vfs(pf_fd);

	igt_fail_on_f(fails, "exclusive ranges check failed\n");
}

igt_main
{
	enum xe_sriov_shared_res res;
	unsigned int gt;
	bool autoprobe;
	int pf_fd;

	igt_fixture {
		struct xe_sriov_provisioned_range *ranges;
		int ret;

		pf_fd = drm_open_driver(DRIVER_XE);
		igt_require(igt_sriov_is_pf(pf_fd));
		igt_require(igt_sriov_get_enabled_vfs(pf_fd) == 0);

		xe_for_each_gt(pf_fd, gt) {
			xe_sriov_for_each_provisionable_shared_res(res, pf_fd, gt) {
				ret = xe_sriov_pf_debugfs_read_check_ranges(pf_fd, res, gt,
									    &ranges, 0);
				igt_skip_on_f(ret, "%s: Failed ranges check on gt%u (%d)\n",
					      xe_sriov_debugfs_provisioned_attr_name(res),
					      gt, ret);
			}
		}
		autoprobe = igt_sriov_is_driver_autoprobe_enabled(pf_fd);
	}

	igt_describe("Verify that auto-provisioned resources are allocated by PF driver in fairly manner");
	igt_subtest_with_dynamic("fair-allocation") {
		for_random_sriov_num_vfs(pf_fd, num_vfs) {
			igt_dynamic_f("numvfs-random") {
				igt_debug("numvfs=%u\n", num_vfs);
				fair_allocation(pf_fd, num_vfs);
			}
		}
	}

	igt_describe("Verify that auto-provisioned resources are released once VFs are disabled");
	igt_subtest_with_dynamic("resources-released-on-vfs-disabling") {
		for_random_sriov_num_vfs(pf_fd, num_vfs) {
			igt_dynamic_f("numvfs-random") {
				igt_debug("numvfs=%u\n", num_vfs);
				resources_released_on_vfs_disabling(pf_fd, num_vfs);
			}
		}
	}

	igt_describe("Verify that ranges of auto-provisioned resources are exclusive");
	igt_subtest_with_dynamic_f("exclusive-ranges") {
		unsigned int total_vfs = igt_sriov_get_total_vfs(pf_fd);

		igt_skip_on(total_vfs < 2);

		for_random_sriov_vf_in_range(pf_fd, 2, total_vfs, num_vfs) {
			igt_dynamic_f("numvfs-random") {
				igt_debug("numvfs=%u\n", num_vfs);
				exclusive_ranges(pf_fd, num_vfs);
			}
		}
	}

	igt_fixture {
		igt_sriov_disable_vfs(pf_fd);
		/* abort to avoid execution of next tests with enabled VFs */
		igt_abort_on_f(igt_sriov_get_enabled_vfs(pf_fd) > 0, "Failed to disable VF(s)");
		autoprobe ? igt_sriov_enable_driver_autoprobe(pf_fd) :
			    igt_sriov_disable_driver_autoprobe(pf_fd);
		igt_abort_on_f(autoprobe != igt_sriov_is_driver_autoprobe_enabled(pf_fd),
			       "Failed to restore sriov_drivers_autoprobe value\n");
		drm_close_driver(pf_fd);
	}
}
