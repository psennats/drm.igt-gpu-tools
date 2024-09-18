// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include "drmtest.h"
#include "intel_chipset.h"
#include "intel_common.h"
#include "i915/intel_memory_region.h"
#include "xe/xe_query.h"

/**
 * is_intel_dgfx:
 * @fd: drm fd
 *
 * Check if Intel device opened at @fd is discrete regardless driver (i915/xe).
 *
 * Returns: True if device is Intel discrete, false otherwise
 */
bool is_intel_dgfx(int fd)
{
	return get_intel_driver(fd) == INTEL_DRIVER_XE ? xe_has_vram(fd) :
							 gem_has_lmem(fd);
}

/**
 * is_intel_system_region:
 * @fd: drm fd
 * @region: region id
 *
 * Check if @region is system region on @fd opened Intel device.
 *
 * Returns: True if @region is system memory, false otherwise
 */
bool is_intel_system_region(int fd, uint64_t region)
{
	enum intel_driver driver = get_intel_driver(fd);
	bool is_system;

	if (driver == INTEL_DRIVER_I915) {
		is_system = IS_SYSTEM_MEMORY_REGION(region);
	} else {
		igt_assert_neq(region, 0);
		is_system = (region == system_memory(fd));
	}

	return is_system;
}

/**
 * is_intel_vram_region:
 * @fd: drm fd
 * @region: region id
 *
 * Check if @region is vram (device memory) region on @fd opened Intel device.
 *
 * Returns: True if @region is vram, false otherwise
 */
bool is_intel_vram_region(int fd, uint64_t region)
{
	enum intel_driver driver = get_intel_driver(fd);
	bool is_vram;

	if (driver == INTEL_DRIVER_I915) {
		is_vram = IS_DEVICE_MEMORY_REGION(region);
	} else {
		igt_assert_neq(region, 0);
		is_vram = (region & (all_memory_regions(fd) & (~system_memory(fd))));
	}

	return is_vram;
}

/**
 * is_intel_region_compressible:
 * @fd: drm fd
 * @region: region id
 *
 * Check if @region is compressible on @fd opened Intel device.
 *
 * Returns: True if @region is compressible, false otherwise
 */
bool is_intel_region_compressible(int fd, uint64_t region)
{
	uint32_t devid = intel_get_drm_devid(fd);
	bool is_dgfx = is_intel_dgfx(fd);
	bool has_flatccs = HAS_FLATCCS(devid);

	/* Integrated or DG1 with aux-ccs */
	if (IS_GEN12(devid) && !has_flatccs)
		return true;

	/* Integrated Xe2+ supports compression on system memory */
	if (intel_gen(devid) >= 20 && !is_dgfx && is_intel_system_region(fd, region))
		return true;

	/* Discrete supports compression on vram */
	if (is_dgfx && is_intel_vram_region(fd, region))
		return true;

	return false;
}
