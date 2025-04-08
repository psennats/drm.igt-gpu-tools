// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

/**
 * TEST: Check compute-related preemption functionality
 * Category: Core
 * Mega feature: WMTP
 * Sub-category: wmtp tests
 * Functionality: OpenCL kernel
 * Test category: functionality test
 */

#include <string.h>

#include "igt.h"
#include "intel_compute.h"
#include "xe/xe_query.h"

/**
 * SUBTEST: compute-preempt
 * GPU requirement: LNL, PTL
 * Description:
 *      Exercise compute walker mid thread preemption scenario
 *
 * SUBTEST: compute-preempt-many
 * GPU requirement: LNL, PTL
 * Description:
 *      Exercise multiple walker mid thread preemption scenario
 *
 * SUBTEST: compute-threadgroup-preempt
 * GPU requirement: LNL, PTL
 * Description:
 *      Exercise compute walker threadgroup preemption scenario
 */
static void
test_compute_preempt(int fd, struct drm_xe_engine_class_instance *hwe, bool threadgroup_preemption)
{
	igt_require_f(run_intel_compute_kernel_preempt(fd, hwe, threadgroup_preemption), "GPU not supported\n");
}

#define CONTEXT_MB 100

igt_main
{
	int xe;
	struct drm_xe_engine_class_instance *hwe;
	uint64_t ram_mb;

	igt_fixture {
		xe = drm_open_driver(DRIVER_XE);
		ram_mb = igt_get_avail_ram_mb();
	}

	igt_subtest_with_dynamic("compute-preempt") {
		xe_for_each_engine(xe, hwe) {
			if (hwe->engine_class != DRM_XE_ENGINE_CLASS_COMPUTE)
				continue;

			igt_dynamic_f("engine-%s", xe_engine_class_string(hwe->engine_class))
				test_compute_preempt(xe, hwe, false);
		}
	}

	igt_subtest_with_dynamic("compute-preempt-many") {
		xe_for_each_engine(xe, hwe) {
			if (hwe->engine_class != DRM_XE_ENGINE_CLASS_COMPUTE)
				continue;

			igt_dynamic_f("engine-%s", xe_engine_class_string(hwe->engine_class)) {
				int child_count;

				/*
				 * Get half of ram / 2, then divide by
				 * CONTEXT_MB * 2 (long and short) job
				 */
				child_count = ram_mb / 2 / CONTEXT_MB / 2;

				igt_debug("RAM: %zd, child count: %d\n",
					  ram_mb, child_count);

				test_compute_preempt(xe, hwe, false);
				igt_fork(child, child_count)
					test_compute_preempt(xe, hwe, false);
				igt_waitchildren();
			}
		}
	}

	igt_subtest_with_dynamic("compute-threadgroup-preempt") {
		xe_for_each_engine(xe, hwe) {
			if (hwe->engine_class != DRM_XE_ENGINE_CLASS_COMPUTE)
				continue;

			igt_dynamic_f("engine-%s", xe_engine_class_string(hwe->engine_class))
			test_compute_preempt(xe, hwe, true);
		}
	}

	igt_fixture
		drm_close_driver(xe);

}
