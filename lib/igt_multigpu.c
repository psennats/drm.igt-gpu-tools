// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "drmtest.h"
#include "i915/gem.h"
#include "igt_core.h"
#include "igt_device_scan.h"
#include "igt_multigpu.h"

/**
 * igt_multigpu_count_class:
 * @class: chipset, e.g. DRIVER_XE or DRIVER_INTEL
 *
 * Function counts number of GPU cards with the help of opening all of them.
 *
 * Returns: number of GPUs cards found
 */
int igt_multigpu_count_class(int class)
{
	int count = 0;

	igt_foreach_gpu(fd, class)
		count++;

	return count;
}

static int print_gpus(int count, int gpu_num)
{
	struct igt_devices_print_format fmt = {
		.type = IGT_PRINT_SIMPLE,
		.option = IGT_PRINT_PCI,
	};
	int devices;

	igt_info("PCI devices available in the system:\n");

	igt_devices_scan();
	devices = igt_device_filter_pci();
	igt_devices_print(&fmt);

	return devices;
}

/**
 * igt_require_filtered_multigpu:
 * @count: minimum number of GPUs required found with filters
 *
 * Function checks number of filtered GPU cards.
 * On error prints available GPUs found on PCI bus and skips.
 */
int igt_require_filtered_multigpu(int gpus_wanted)
{
	int gpu_count = igt_device_filter_count();
	int num;

	if (gpu_count >= gpus_wanted)
		return gpu_count;

	num = print_gpus(gpus_wanted, gpu_count);
	igt_skip_on_f(gpu_count < gpus_wanted, "Test requires at least %d GPUs, got %d, available: %d\n", gpus_wanted, gpu_count, num);

	return 0; /* unreachable */
}

/**
 * igt_require_multigpu:
 * @count: minimum number of GPUs required
 * @chipset: for example DRIVER_XE or DRIVER_INTEL
 *
 * Function checks number of GPU cards with __drm_open_driver_another()
 * On error prints available GPUs found on PCI bus and skips.
 */
int igt_require_multigpu(int gpus_wanted, unsigned int chipset)
{
	int gpu_filters = igt_multigpu_count_class(chipset);
	int num;

	if (gpu_filters >= gpus_wanted)
		return gpu_filters;

	num = print_gpus(gpus_wanted, gpu_filters);
	igt_skip_on_f(gpu_filters < gpus_wanted, "Test requires at least %d GPUs, got %d, available: %d\n", gpus_wanted, gpu_filters, num);

	return 0; /* unreachable */
}
