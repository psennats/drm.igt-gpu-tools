/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 *
 * Authors:
 *    Francois Dugast <francois.dugast@intel.com>
 */

#ifndef INTEL_COMPUTE_H
#define INTEL_COMPUTE_H

#include <stdbool.h>

#include "xe_drm.h"

/*
 * OpenCL Kernels are generated using:
 *
 * GPU=tgllp &&                                                         \
 *      ocloc -file opencl/compute_square_kernel.cl -device $GPU &&     \
 *      xxd -i compute_square_kernel_Gen12LPlp.bin
 *
 * For each GPU model desired. A list of supported models can be obtained with: ocloc compile --help
 */

struct intel_compute_kernels {
	int ip_ver;
	unsigned int size;
	const unsigned char *kernel;
	unsigned int sip_kernel_size;
	const unsigned char *sip_kernel;
	unsigned int long_kernel_size;
	const unsigned char *long_kernel;
};

/**
 * struct user_execenv - Container of the user-provided execution environment
 */
struct user_execenv {
	/** @vm: use this VM if provided, otherwise create one */
	uint32_t vm;
	/**
	 * @kernel: use this custom kernel if provided, otherwise use a default square kernel
	 *
	 * Custom kernel execution in lib/intel_compute has strong limitations, it does not
	 * allow running any custom kernel. "count" is the size of the input and output arrays
	 * and the provided kernel must have the following prototype:
	 *
	 *    __kernel void square(__global float* input,
	 *                         __global float* output,
	 *                         const unsigned int count)
	 */
	const unsigned char *kernel;
	/** @kernel_size: size of the custom kernel, if provided */
	unsigned int kernel_size;
};

extern const struct intel_compute_kernels intel_compute_square_kernels[];

bool run_intel_compute_kernel(int fd, struct user_execenv *user);
bool xe_run_intel_compute_kernel_on_engine(int fd, struct drm_xe_engine_class_instance *eci,
					   struct user_execenv *user);
bool run_intel_compute_kernel_preempt(int fd, struct drm_xe_engine_class_instance *eci,
				      bool threadgroup_preemption);
#endif	/* INTEL_COMPUTE_H */
