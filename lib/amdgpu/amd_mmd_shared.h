/* SPDX-License-Identifier: MIT
 * Copyright 2023 Advanced Micro Devices, Inc.
 * Copyright 2014 Advanced Micro Devices, Inc.
 */

#include <amdgpu.h>
#include "amdgpu_drm.h"

#include "igt.h"
#include "amd_ip_blocks.h"
#include "amd_mmd_decode_messages.h"
#include "amd_mmd_util_math.h"
#include "amd_memory.h"
#include "amd_mmd_frame.h"
#include "amd_mmd_uve_ib.h"


#define UVD_4_0_GPCOM_VCPU_CMD   0x3BC3
#define UVD_4_0_GPCOM_VCPU_DATA0 0x3BC4
#define UVD_4_0_GPCOM_VCPU_DATA1 0x3BC5
#define UVD_4_0__ENGINE_CNTL	 0x3BC6

#define VEGA_20_GPCOM_VCPU_CMD   0x81C3
#define VEGA_20_GPCOM_VCPU_DATA0 0x81C4
#define VEGA_20_GPCOM_VCPU_DATA1 0x81C5
#define VEGA_20_UVD_ENGINE_CNTL 0x81C6

#define IB_SIZE		8192
#define MAX_RESOURCES	16

enum decoder_error_type {
	INVALID_DECODER_IB_TYPE = 0,
	INVALID_DECODER_IB_SIZE,
	INVALID_DECODER_DPB_BUFFER,
	INVALID_DECODER_CODEC_PARAM,
	INVALID_DECODER_TARGET_BUFFER,
	INVALID_DECODER_BITSTREAM,
	INVALID_DECODER_BITSTREAM_BUFFER,
	INVALID_DECODER_NONE,
};

struct mmd_shared_context {
	uint32_t family_id;
	uint32_t chip_id;
	uint32_t chip_rev;
	uint32_t asic_id;

	/* vce */
	uint32_t vce_harvest_config;

	/* vcn */
	uint32_t vcn_ip_version_major;
	uint32_t vcn_ip_version_minor;
	bool vcn_dec_sw_ring;
	bool vcn_unified_ring;
	uint8_t vcn_reg_index;
	bool dec_ring;
	bool enc_ring;
	/* jpeg */
	bool jpeg_direct_reg;

	/*vpe*/
	uint32_t vpe_ip_version_major;
	uint32_t vpe_ip_version_minor;
	bool vpe_ring;
	enum amd_ip_block_type ip_type;
};

struct mmd_context {
	amdgpu_context_handle context_handle;
	amdgpu_bo_handle ib_handle;
	amdgpu_va_handle ib_va_handle;
	uint64_t ib_mc_address;
	uint32_t *ib_cpu;

	amdgpu_bo_handle resources[MAX_RESOURCES];
	unsigned int num_resources;
};

struct amdgpu_mmd_bo {
	amdgpu_bo_handle handle;
	amdgpu_va_handle va_handle;
	uint64_t addr;
	uint64_t size;
	uint8_t *ptr;
};

struct amdgpu_uvd_enc {
	unsigned int width;
	unsigned int height;
	struct amdgpu_mmd_bo session;
	struct amdgpu_mmd_bo vbuf;
	struct amdgpu_mmd_bo bs;
	struct amdgpu_mmd_bo fb;
	struct amdgpu_mmd_bo cpb;
};

struct uvd_enc_context {
	struct mmd_context uvd;
	struct amdgpu_uvd_enc enc;
};

bool
is_gfx_pipe_removed(uint32_t family_id, uint32_t chip_id, uint32_t chip_rev);

bool
is_uvd_tests_enable(uint32_t family_id, uint32_t chip_id, uint32_t chip_rev);

bool
amdgpu_is_vega_or_polaris(uint32_t family_id, uint32_t chip_id, uint32_t chip_rev);

int
mmd_context_init(amdgpu_device_handle device_handle, struct mmd_context *context);

void
mmd_context_clean(amdgpu_device_handle device_handle,
		struct mmd_context *context);

int
mmd_shared_context_init(amdgpu_device_handle device_handle, struct mmd_shared_context *context);

int
submit(amdgpu_device_handle device_handle, struct mmd_context *context,
		unsigned int ndw, unsigned int ip);

void
alloc_resource(amdgpu_device_handle device_handle,
		struct amdgpu_mmd_bo *mmd_bo, unsigned int size,
		unsigned int domain);

void
free_resource(struct amdgpu_mmd_bo *mmd_bo);

typedef int (*mm_test_callback) (amdgpu_device_handle device_handle, struct mmd_shared_context *context,
		int err);
int
mm_queue_test_helper(amdgpu_device_handle device_handle, struct mmd_shared_context *context,
		mm_test_callback test, int err_type, const struct pci_addr *pci);
