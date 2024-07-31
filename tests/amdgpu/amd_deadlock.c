// SPDX-License-Identifier: MIT
/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 * Copyright 2022 Advanced Micro Devices, Inc.
 * Copyright 2023 Advanced Micro Devices, Inc.
 */

#include "lib/amdgpu/amd_memory.h"
#include "lib/amdgpu/amd_command_submission.h"
#include "lib/amdgpu/amd_deadlock_helpers.h"
#include "lib/amdgpu/amdgpu_asic_addr.h"

#define AMDGPU_FAMILY_SI                        110 /* Hainan, Oland, Verde, Pitcairn, Tahiti */
#define AMDGPU_FAMILY_CI                        120 /* Bonaire, Hawaii */
#define AMDGPU_FAMILY_CZ                        135 /* Carrizo, Stoney */
#define AMDGPU_FAMILY_RV                        142 /* Raven */

static bool
is_deadlock_tests_enable(const struct amdgpu_gpu_info *gpu_info)
{
	bool enable = true;
	/*
	 * skip for the ASICs that don't support GPU reset.
	 */
	if (gpu_info->family_id == AMDGPU_FAMILY_SI ||
	    gpu_info->family_id == AMDGPU_FAMILY_KV ||
	    gpu_info->family_id == AMDGPU_FAMILY_CZ ||
	    ((gpu_info->family_id == AMDGPU_FAMILY_RV) &&
	     (!ASICREV_IS_RENOIR(gpu_info->chip_external_rev)))) {
		igt_info("\n\nGPU reset is not enabled for the ASIC, deadlock test skip\n");
		enable = false;
	}
	return enable;
}

igt_main
{
	amdgpu_device_handle device;
	struct amdgpu_gpu_info gpu_info = {0};
	int fd = -1;
	int r;
	bool arr_cap[AMD_IP_MAX] = {0};

	igt_fixture {
		uint32_t major, minor;
		int err;

		fd = drm_open_driver(DRIVER_AMDGPU);

		err = amdgpu_device_initialize(fd, &major, &minor, &device);
		igt_require(err == 0);

		igt_info("Initialized amdgpu, driver version %d.%d\n",
			 major, minor);

		r = amdgpu_query_gpu_info(device, &gpu_info);
		igt_assert_eq(r, 0);
		r = setup_amdgpu_ip_blocks(major, minor, &gpu_info, device);
		igt_assert_eq(r, 0);
		asic_rings_readness(device, 1, arr_cap);
		igt_skip_on(!is_deadlock_tests_enable(&gpu_info));

	}
	igt_describe("Test-GPU-reset-by-flooding-sdma-ring-with-jobs");
	igt_subtest_with_dynamic("amdgpu-deadlock-sdma") {
		if (arr_cap[AMD_IP_DMA]) {
			igt_dynamic_f("amdgpu-deadlock-sdma")
			amdgpu_wait_memory_helper(device, AMDGPU_HW_IP_DMA);
		}
	}

	igt_describe("Test-GPU-reset-by-access-gfx-illegal-reg");
	igt_subtest_with_dynamic("amdgpu-gfx-illegal-reg-access") {
		if (arr_cap[AMD_IP_GFX]) {
			igt_dynamic_f("amdgpu-illegal-reg-access")
			bad_access_helper(device, CMD_STREAM_TRANS_BAD_REG_ADDRESS,
					AMDGPU_HW_IP_GFX);
		}
	}

	igt_describe("Test-GPU-reset-by-access-gfx-illegal-mem-addr");
	igt_subtest_with_dynamic("amdgpu-gfx-illegal-mem-access") {
		if (arr_cap[AMD_IP_GFX]) {
			igt_dynamic_f("amdgpu-illegal-mem-access")
			bad_access_helper(device, CMD_STREAM_TRANS_BAD_MEM_ADDRESS,
					AMDGPU_HW_IP_GFX);
		}
	}


	igt_describe("Test-GPU-reset-by-flooding-gfx-ring-with-jobs");
	igt_subtest_with_dynamic("amdgpu-deadlock-gfx") {
		if (arr_cap[AMD_IP_GFX]) {
			igt_dynamic_f("amdgpu-deadlock-gfx")
			amdgpu_wait_memory_helper(device, AMDGPU_HW_IP_GFX);
		}
	}

	igt_describe("Test-GPU-reset-by-access-compute-illegal-mem-addr");
	igt_subtest("amdgpu-compute-illegal-mem-access") {
		igt_skip_on_f(!arr_cap[AMD_IP_COMPUTE], "SKIP, compute ring don't support\n");
		bad_access_helper(device, CMD_STREAM_TRANS_BAD_MEM_ADDRESS,
				AMDGPU_HW_IP_COMPUTE);
	}

	igt_describe("Test-GPU-reset-by-flooding-compute-ring-with-jobs");
	igt_subtest_with_dynamic("amdgpu-deadlock-compute") {
		if (arr_cap[AMD_IP_COMPUTE]) {
			igt_dynamic_f("amdgpu-deadlock-compute")
			amdgpu_wait_memory_helper(device, AMDGPU_HW_IP_COMPUTE);
		}
	}

	igt_describe("Test-GPU-reset-by-sdma-corrupted-header-with-jobs");
	igt_subtest_with_dynamic("amdgpu-deadlock-sdma-corrupted-header-test") {
		if (arr_cap[AMD_IP_DMA]) {
			igt_dynamic_f("amdgpu-deadlock-sdma-corrupted-header-test")
			amdgpu_hang_sdma_helper(device, DMA_CORRUPTED_HEADER_HANG);
		}
	}

	igt_describe("Test-GPU-reset-by-sdma-slow-linear-copy-with-jobs");
	igt_subtest_with_dynamic("amdgpu-deadlock-sdma-slow-linear-copy") {
		if (arr_cap[AMD_IP_DMA]) {
			igt_dynamic_f("amdgpu-deadlock-sdma-slow-linear-copy")
			amdgpu_hang_sdma_helper(device, DMA_SLOW_LINEARCOPY_HANG);
		}
	}

	igt_fixture {
		amdgpu_device_deinitialize(device);
		drm_close_driver(fd);
	}
}
