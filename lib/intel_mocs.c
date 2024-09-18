// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "igt.h"
#include "intel_mocs.h"

struct drm_intel_mocs_index {
	uint8_t uc_index;
	uint8_t wb_index;
	uint8_t defer_to_pat_index;
};

static void get_mocs_index(int fd, struct drm_intel_mocs_index *mocs)
{
	uint16_t devid = intel_get_drm_devid(fd);

	/*
	 * Gen >= 12 onwards don't have a setting for PTE,
	 * so using I915_MOCS_PTE as mocs index may leads to
	 * some undefined MOCS behavior.
	 * This helper function is providing current UC as well
	 * as WB MOCS index based on platform.
	 */
	if (intel_graphics_ver(devid) >= IP_VER(20, 0)) {
		mocs->uc_index = 3;
		mocs->wb_index = 4;
		mocs->defer_to_pat_index = 0;
	} else if (IS_METEORLAKE(devid)) {
		mocs->uc_index = 5;
		mocs->wb_index = 10;
	} else if (IS_DG2(devid)) {
		mocs->uc_index = 1;
		mocs->wb_index = 3;
	} else if (IS_DG1(devid)) {
		mocs->uc_index = 1;
		mocs->wb_index = 5;
	} else if (IS_GEN12(devid)) {
		mocs->uc_index = 3;
		mocs->wb_index = 2;
	} else {
		mocs->uc_index = I915_MOCS_PTE;
		mocs->wb_index = I915_MOCS_CACHED;
	}
}

uint8_t intel_get_wb_mocs_index(int fd)
{
	struct drm_intel_mocs_index mocs;

	get_mocs_index(fd, &mocs);

	return mocs.wb_index;
}

uint8_t intel_get_uc_mocs_index(int fd)
{
	struct drm_intel_mocs_index mocs;

	get_mocs_index(fd, &mocs);

	return mocs.uc_index;
}

uint8_t intel_get_defer_to_pat_mocs_index(int fd)
{
	struct drm_intel_mocs_index mocs;
	uint16_t dev_id = intel_get_drm_devid(fd);

	igt_assert(intel_gen(dev_id) >= 20);

	get_mocs_index(fd, &mocs);

	return mocs.defer_to_pat_index;
}
