// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

/**
 * TEST: Check compute-related preemption functionality
 * Category: Core
 * Mega feature: WMTP
 * Sub-category: Compute tests
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

igt_main
{
	int xe;
	struct drm_xe_engine_class_instance *hwe;

	igt_fixture
		xe = drm_open_driver(DRIVER_XE);

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
				uint16_t dev_id = intel_get_drm_devid(xe);
				int child_count;

				if (IS_PANTHERLAKE(dev_id))
					child_count = 50;
				else
					child_count = 100;

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
