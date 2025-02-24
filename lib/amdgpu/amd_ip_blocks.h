/* SPDX-License-Identifier: MIT
 * Copyright 2014 Advanced Micro Devices, Inc.
 * Copyright 2022 Advanced Micro Devices, Inc.
 * Copyright 2023 Advanced Micro Devices, Inc.
 */

#ifndef AMD_IP_BLOCKS_H
#define AMD_IP_BLOCKS_H

#include <amdgpu_drm.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysmacros.h>

#include "amd_registers.h"
#include "amd_family.h"

#define MAX_CARDS_SUPPORTED 4

/* reset mask */
#define AMDGPU_RESET_TYPE_FULL (1 << 0) /* full adapter reset, mode1/mode2/BACO/etc. */
#define AMDGPU_RESET_TYPE_SOFT_RESET (1 << 1) /* IP level soft reset */
#define AMDGPU_RESET_TYPE_PER_QUEUE (1 << 2) /* per queue */
#define AMDGPU_RESET_TYPE_PER_PIPE (1 << 3) /* per pipe */

enum amd_ip_block_type {
	AMD_IP_GFX = 0,
	AMD_IP_COMPUTE,
	AMD_IP_DMA,
	AMD_IP_UVD,
	AMD_IP_VCE,
	AMD_IP_UVD_ENC,
	AMD_IP_VCN_DEC,
	AMD_IP_VCN_ENC,
	AMD_IP_VCN_UNIFIED = AMD_IP_VCN_ENC,
	AMD_IP_VCN_JPEG,
	AMD_IP_VPE,
	AMD_IP_MAX,
};

enum  cmd_error_type {
	CMD_STREAM_EXEC_SUCCESS,
	CMD_STREAM_EXEC_INVALID_OPCODE,
	CMD_STREAM_EXEC_INVALID_PACKET_LENGTH,
	CMD_STREAM_EXEC_INVALID_PACKET_EOP_QUEUE,
	CMD_STREAM_TRANS_BAD_REG_ADDRESS,
	CMD_STREAM_TRANS_BAD_MEM_ADDRESS,
	CMD_STREAM_TRANS_BAD_MEM_ADDRESS_BY_SYNC,

	BACKEND_SE_GC_SHADER_EXEC_SUCCESS,
	BACKEND_SE_GC_SHADER_INVALID_SHADER,
	BACKEND_SE_GC_SHADER_INVALID_PROGRAM_ADDR,    /* COMPUTE_PGM */
	BACKEND_SE_GC_SHADER_INVALID_PROGRAM_SETTING, /* COMPUTE_PGM_RSRC */
	BACKEND_SE_GC_SHADER_INVALID_USER_DATA, /* COMPUTE_USER_DATA */

	DMA_CORRUPTED_HEADER_HANG,
	DMA_SLOW_LINEARCOPY_HANG
};

#define _MAX_NUM_ASIC_ID_EXCLUDE_FILTER 3

struct asic_id_filter
{
	int family_id;
	int chip_id_begin;
	int chip_id_end;
};

/* expected reset result for differant ip's */
struct reset_err_result {
	int compute_reset_result;
	int gfx_reset_result;
	int sdma_reset_result;
};

struct dynamic_test{
	enum cmd_error_type test;
	const char *name;
	const char *describe;
	struct asic_id_filter exclude_filter[_MAX_NUM_ASIC_ID_EXCLUDE_FILTER];
	struct reset_err_result result;
	bool support_sdma;
};

#define for_each_test(t, T) for(typeof(*T) *t = T; t->name; t++)

/* set during execution */
struct amdgpu_cs_err_codes {
	int err_code_cs_submit;
	int err_code_wait_for_fence;
};

/* aux struct to hold misc parameters for convenience to maintain */
struct amdgpu_ring_context {

	int ring_id; /* ring_id from amdgpu_query_hw_ip_info */
	int res_cnt; /* num of bo in amdgpu_bo_handle resources[2] */

	uint32_t write_length;  /* length of data */
	uint32_t write_length2; /* length of data for second packet */
	uint32_t *pm4;		/* data of the packet */
	uint32_t pm4_size;	/* max allocated packet size */
	bool secure;		/* secure or not */

	uint64_t bo_mc;		/* GPU address of first buffer */
	uint64_t bo_mc2;	/* GPU address for p4 packet */
	uint64_t bo_mc3;	/* GPU address of second buffer */
	uint64_t bo_mc4;	/* GPU address of second p4 packet */

	uint32_t pm4_dw;	/* actual size of pm4 */
	uint32_t pm4_dw2;	/* actual size of second pm4 */

	volatile uint32_t *bo_cpu;	/* cpu adddress of mapped GPU buf */
	volatile uint32_t *bo2_cpu;	/* cpu adddress of mapped pm4 */
	volatile uint32_t *bo3_cpu;	/* cpu adddress of mapped GPU second buf */
	volatile uint32_t *bo4_cpu;	/* cpu adddress of mapped second pm4 */

	uint32_t bo_cpu_origin;

	amdgpu_bo_handle bo;
	amdgpu_bo_handle bo2;
	amdgpu_bo_handle bo3;
	amdgpu_bo_handle bo4;

	amdgpu_bo_handle boa_vram[2];
	amdgpu_bo_handle boa_gtt[2];

	amdgpu_context_handle context_handle;
	struct drm_amdgpu_info_hw_ip hw_ip_info;  /* result of amdgpu_query_hw_ip_info */

	amdgpu_bo_handle resources[4]; /* amdgpu_bo_alloc_and_map */
	amdgpu_va_handle va_handle;    /* amdgpu_bo_alloc_and_map */
	amdgpu_va_handle va_handle2;   /* amdgpu_bo_alloc_and_map */
	amdgpu_va_handle va_handle3;   /* amdgpu_bo_alloc_and_map */
	amdgpu_va_handle va_handle4;   /* amdgpu_bo_alloc_and_map */

	struct amdgpu_cs_ib_info ib_info;     /* amdgpu_bo_list_create */
	struct amdgpu_cs_request ibs_request; /* amdgpu_cs_query_fence_status */
	struct amdgpu_cs_err_codes err_codes;
};


struct amdgpu_ip_funcs {
	uint32_t	family_id;
	uint32_t	chip_external_rev;
	uint32_t	chip_rev;
	uint32_t	align_mask;
	uint32_t	nop;
	uint32_t	deadbeaf;
	uint32_t	pattern;
	/* functions */
	int (*write_linear)(const struct amdgpu_ip_funcs *func, const struct amdgpu_ring_context *context, uint32_t *pm4_dw);
	int (*bad_write_linear)(const struct amdgpu_ip_funcs *func, const struct amdgpu_ring_context *context,
				uint32_t *pm4_dw, unsigned int cmd_error);
	int (*write_linear_atomic)(const struct amdgpu_ip_funcs *func, const struct amdgpu_ring_context *context, uint32_t *pm4_dw);
	int (*const_fill)(const struct amdgpu_ip_funcs *func, const struct amdgpu_ring_context *context, uint32_t *pm4_dw);
	int (*copy_linear)(const struct amdgpu_ip_funcs *func, const struct amdgpu_ring_context *context, uint32_t *pm4_dw);
	int (*compare)(const struct amdgpu_ip_funcs *func, const struct amdgpu_ring_context *context, int div);
	int (*compare_pattern)(const struct amdgpu_ip_funcs *func, const struct amdgpu_ring_context *context, int div);
	int (*get_reg_offset)(enum general_reg reg);
	int (*wait_reg_mem)(const struct amdgpu_ip_funcs *func, const struct amdgpu_ring_context *context, uint32_t *pm4_dw);

};

extern const struct amdgpu_ip_block_version gfx_v6_0_ip_block;

struct amdgpu_ip_block_version {
	const enum amd_ip_block_type type;
	const int major;
	const int minor;
	const int rev;
	struct amdgpu_ip_funcs *funcs;
};

/* global holder for the array of in use ip blocks */

struct amdgpu_ip_blocks_device {
	struct amdgpu_ip_block_version *ip_blocks[AMD_IP_MAX];
	int			num_ip_blocks;
};

struct chip_info {
	const char *name;
	enum radeon_family family;
	enum chip_class chip_class;
	amdgpu_device_handle dev;
};

struct pci_addr {
    unsigned int domain;
    unsigned int bus;
    unsigned int device;
    unsigned int function;
};

extern  struct amdgpu_ip_blocks_device amdgpu_ips;
extern const struct chip_info  *g_pChip;
int
setup_amdgpu_ip_blocks(uint32_t major, uint32_t minor, struct amdgpu_gpu_info *amdinfo,
		       amdgpu_device_handle device);

const struct amdgpu_ip_block_version *
get_ip_block(amdgpu_device_handle device, enum amd_ip_block_type type);

struct amdgpu_cmd_base {
	uint32_t cdw;  /* Number of used dwords. */
	uint32_t max_dw; /* Maximum number of dwords. */
	uint32_t *buf; /* The base pointer of the chunk. */
	bool is_assigned_buf;

	/* functions */
	int (*allocate_buf)(struct amdgpu_cmd_base  *base, uint32_t size);
	int (*attach_buf)(struct amdgpu_cmd_base  *base, void *ptr, uint32_t size_bytes);
	void (*emit)(struct amdgpu_cmd_base  *base, uint32_t value);
	void (*emit_aligned)(struct amdgpu_cmd_base  *base, uint32_t mask, uint32_t value);
	void (*emit_repeat)(struct amdgpu_cmd_base  *base, uint32_t value, uint32_t number_of_times);
	void (*emit_at_offset)(struct amdgpu_cmd_base  *base, uint32_t value, uint32_t offset_dwords);
	void (*emit_buf)(struct amdgpu_cmd_base  *base, const void *ptr, uint32_t offset_bytes, uint32_t size_bytes);
};

struct amdgpu_cmd_base *get_cmd_base(void);

void free_cmd_base(struct amdgpu_cmd_base *base);

int
amdgpu_open_devices(bool open_render_node, int max_cards_supported, int drm_amdgpu_fds[]);

void
asic_rings_readness(amdgpu_device_handle device_handle, uint32_t mask, bool arr[AMD_IP_MAX]);

bool
is_reset_enable(enum amd_ip_block_type ip_type, uint32_t reset_type, const struct pci_addr *pci);

int
get_pci_addr_from_fd(int fd, struct pci_addr *pci);

bool
is_support_page_queue(enum amd_ip_block_type ip_type, const struct pci_addr *pci);
#endif
