/* SPDX-License-Identifier: MIT
 * Copyright 2025 Advanced Micro Devices, Inc.
 */

#ifndef _AMD_USER_QUEUE_
#define _AMD_USER_QUEUE_

#include <amdgpu_drm.h>
#include <amdgpu.h>
#include <time.h>
#include "amd_ip_blocks.h"


#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#define USERMODE_QUEUE_SIZE		(PAGE_SIZE * 256)   //In bytes with total size as 1 Mbyte
#define ALIGNMENT			4096
#define DOORBELL_INDEX			4
#define USERMODE_QUEUE_SIZE_DW		(USERMODE_QUEUE_SIZE >> 2)
#define USERMODE_QUEUE_SIZE_DW_MASK	(USERMODE_QUEUE_SIZE_DW - 1)

#define amdgpu_pkt_begin() uint32_t __num_dw_written = 0; \
	uint32_t __ring_start = *ring_context->wptr_cpu & USERMODE_QUEUE_SIZE_DW_MASK;

#define amdgpu_pkt_add_dw(value) do { \
	*(ring_context->queue_cpu + \
	((__ring_start + __num_dw_written) & USERMODE_QUEUE_SIZE_DW_MASK)) \
	= value; \
	__num_dw_written++;\
} while (0)

#define amdgpu_pkt_end() \
	*ring_context->wptr_cpu += __num_dw_written

void amdgpu_alloc_doorbell(amdgpu_device_handle device_handle, struct amdgpu_userq_bo *doorbell_bo,
			   unsigned int size, unsigned int domain);

int amdgpu_bo_alloc_and_map_uq(amdgpu_device_handle device_handle, unsigned int size,
			       unsigned int alignment, unsigned int heap, uint64_t alloc_flags,
			       uint64_t mapping_flags, amdgpu_bo_handle *bo, void **cpu,
			       uint64_t *mc_address, amdgpu_va_handle *va_handle,
			       uint32_t timeline_syncobj_handle, uint64_t point);

void amdgpu_bo_unmap_and_free_uq(amdgpu_device_handle device_handle, amdgpu_bo_handle bo,
				 amdgpu_va_handle va_handle, uint64_t mc_addr, uint64_t size,
				 uint32_t timeline_syncobj_handle, uint64_t point,
				 uint64_t syncobj_handles_array, uint32_t num_syncobj_handles);

int amdgpu_timeline_syncobj_wait(amdgpu_device_handle device_handle,
				 uint32_t timeline_syncobj_handle, uint64_t point);

void amdgpu_user_queue_create(amdgpu_device_handle device_handle, struct amdgpu_ring_context *ctxt,
			      unsigned int ip_type);

void amdgpu_user_queue_destroy(amdgpu_device_handle device_handle, struct amdgpu_ring_context *ctxt,
			       unsigned int ip_type);

void amdgpu_user_queue_submit(amdgpu_device_handle device, struct amdgpu_ring_context *ring_context,
			      unsigned int ip_type, uint64_t mc_address);

#endif
