// SPDX-License-Identifier: MIT
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 */

#include "amd_user_queue.h"
#include "amd_memory.h"
#include "amd_PM4.h"
#include "ioctl_wrappers.h"

#ifdef AMDGPU_USERQ_ENABLED
static void amdgpu_alloc_doorbell(amdgpu_device_handle device_handle,
				  struct amdgpu_userq_bo *doorbell_bo,
				  unsigned int size, unsigned int domain)
{
	struct amdgpu_bo_alloc_request req = {0};
	amdgpu_bo_handle buf_handle;
	int r;

	req.alloc_size = ALIGN(size, PAGE_SIZE);
	req.preferred_heap = domain;
	r = amdgpu_bo_alloc(device_handle, &req, &buf_handle);
	igt_assert_eq(r, 0);

	doorbell_bo->handle = buf_handle;
	doorbell_bo->size = req.alloc_size;

	r = amdgpu_bo_cpu_map(doorbell_bo->handle,
			      (void **)&doorbell_bo->ptr);
	igt_assert_eq(r, 0);
}

int
amdgpu_bo_alloc_and_map_uq(amdgpu_device_handle device_handle, unsigned int size,
			   unsigned int alignment, unsigned int heap, uint64_t alloc_flags,
			   uint64_t mapping_flags, amdgpu_bo_handle *bo, void **cpu,
			   uint64_t *mc_address, amdgpu_va_handle *va_handle,
			   uint32_t timeline_syncobj_handle, uint64_t point)
{
	struct amdgpu_bo_alloc_request request = {};
	amdgpu_bo_handle buf_handle;
	uint64_t vmc_addr;
	int r;

	request.alloc_size = size;
	request.phys_alignment = alignment;
	request.preferred_heap = heap;
	request.flags = alloc_flags;

	r = amdgpu_bo_alloc(device_handle, &request, &buf_handle);
	if (r)
		return r;

	r = amdgpu_va_range_alloc(device_handle,
				  amdgpu_gpu_va_range_general,
				  size, alignment, 0, &vmc_addr,
				  va_handle, 0);
	if (r)
		goto error_va_alloc;

	r = amdgpu_bo_va_op_raw2(device_handle, buf_handle, 0,
				 ALIGN(size, getpagesize()), vmc_addr,
				 AMDGPU_VM_PAGE_READABLE |
				 AMDGPU_VM_PAGE_WRITEABLE |
				 AMDGPU_VM_PAGE_EXECUTABLE |
				 mapping_flags,
				 AMDGPU_VA_OP_MAP,
				 timeline_syncobj_handle,
				 point, 0, 0);
	if (r)
		goto error_va_map;

	if (cpu) {
		r = amdgpu_bo_cpu_map(buf_handle, cpu);
		if (r)
			goto error_cpu_map;
	}

	*bo = buf_handle;
	*mc_address = vmc_addr;

	return 0;

error_cpu_map:
	amdgpu_bo_va_op(buf_handle, 0, size, vmc_addr, 0, AMDGPU_VA_OP_UNMAP);
error_va_map:
	amdgpu_va_range_free(*va_handle);
error_va_alloc:
	amdgpu_bo_free(buf_handle);
	return r;
}

static void amdgpu_bo_unmap_and_free_uq(amdgpu_device_handle device_handle,
					amdgpu_bo_handle bo, amdgpu_va_handle va_handle,
					uint64_t mc_addr, uint64_t size,
					uint32_t timeline_syncobj_handle,
					uint64_t point, uint64_t syncobj_handles_array,
					uint32_t num_syncobj_handles)
{
	amdgpu_bo_cpu_unmap(bo);
	amdgpu_bo_va_op_raw2(device_handle, bo, 0, size, mc_addr, 0, AMDGPU_VA_OP_UNMAP,
				  timeline_syncobj_handle, point,
				  syncobj_handles_array, num_syncobj_handles);
	amdgpu_va_range_free(va_handle);
	amdgpu_bo_free(bo);
}

int amdgpu_timeline_syncobj_wait(amdgpu_device_handle device_handle,
				 uint32_t timeline_syncobj_handle, uint64_t point)
{
	uint32_t flags = DRM_SYNCOBJ_QUERY_FLAGS_LAST_SUBMITTED;
	int r;

	r = amdgpu_cs_syncobj_query2(device_handle, &timeline_syncobj_handle,
				     &point, 1, flags);
	if (r)
		return r;

	r = amdgpu_cs_syncobj_timeline_wait(device_handle, &timeline_syncobj_handle,
					    &point, 1, INT64_MAX,
					    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL |
					    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
					    NULL);
	if (r)
		igt_warn("Timeline timed out\n");
	return r;
}

void amdgpu_user_queue_submit(amdgpu_device_handle device, struct amdgpu_ring_context *ring_context,
			      unsigned int ip_type, uint64_t mc_address)
{
	int r;
	uint32_t control = ring_context->pm4_dw;
	uint32_t syncarray[1];
	struct drm_amdgpu_userq_signal signal_data;


	amdgpu_pkt_begin();
	/* Prepare the Indirect IB to submit the IB to user queue */
	amdgpu_pkt_add_dw(PACKET3(PACKET3_INDIRECT_BUFFER, 2));
	amdgpu_pkt_add_dw(lower_32_bits(mc_address));
	amdgpu_pkt_add_dw(upper_32_bits(mc_address));

	if (ip_type == AMD_IP_GFX)
		amdgpu_pkt_add_dw(control | S_3F3_INHERIT_VMID_MQD_GFX(1));
	else
		amdgpu_pkt_add_dw(control | S_3F3_VALID_COMPUTE(1)
					       | S_3F3_INHERIT_VMID_MQD_COMPUTE(1));

	amdgpu_pkt_add_dw(PACKET3(PACKET3_PROTECTED_FENCE_SIGNAL, 0));

	/* empty dword is needed for fence signal pm4 */
	amdgpu_pkt_add_dw(0);

#if DETECT_CC_GCC && (DETECT_ARCH_X86 || DETECT_ARCH_X86_64)
	asm volatile ("mfence" : : : "memory");
#endif

	/* Below call update the wptr address so will wait till all writes are completed */
	amdgpu_pkt_end();

#if DETECT_CC_GCC && (DETECT_ARCH_X86 || DETECT_ARCH_X86_64)
	asm volatile ("mfence" : : : "memory");
#endif

	/* Update the door bell */
	ring_context->doorbell_cpu[DOORBELL_INDEX] = *ring_context->wptr_cpu;

	/* Add a fence packet for signal */
	syncarray[0] = ring_context->timeline_syncobj_handle;
	signal_data.queue_id = ring_context->queue_id;
	signal_data.syncobj_handles = (uintptr_t)syncarray;
	signal_data.num_syncobj_handles = 1;
	signal_data.bo_read_handles = 0;
	signal_data.bo_write_handles = 0;
	signal_data.num_bo_read_handles = 0;
	signal_data.num_bo_write_handles = 0;

	r = amdgpu_userq_signal(device, &signal_data);
	igt_assert_eq(r, 0);

	r = amdgpu_cs_syncobj_wait(device, &ring_context->timeline_syncobj_handle, 1, INT64_MAX,
				   DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL, NULL);
	igt_assert_eq(r, 0);
}

void amdgpu_user_queue_destroy(amdgpu_device_handle device_handle, struct amdgpu_ring_context *ctxt,
			       unsigned int type)
{
	int r;

	if (type > AMD_IP_DMA) {
		igt_info("Invalid IP not supported for UMQ Submission\n");
		return;
	}

	/* Free the Usermode Queue */
	r = amdgpu_free_userqueue(device_handle, ctxt->queue_id);
	igt_assert_eq(r, 0);

	switch (type) {
	case AMD_IP_GFX:
		amdgpu_bo_unmap_and_free_uq(device_handle, ctxt->csa.handle,
					    ctxt->csa.va_handle,
					    ctxt->csa.mc_addr, ctxt->info.gfx.csa_size,
					    ctxt->timeline_syncobj_handle, ++ctxt->point,
					    0, 0);

		amdgpu_bo_unmap_and_free_uq(device_handle, ctxt->shadow.handle,
					    ctxt->shadow.va_handle,
					    ctxt->shadow.mc_addr, ctxt->info.gfx.shadow_size,
					    ctxt->timeline_syncobj_handle, ++ctxt->point,
					    0, 0);

		r = amdgpu_timeline_syncobj_wait(device_handle, ctxt->timeline_syncobj_handle,
						 ctxt->point);
		igt_assert_eq(r, 0);
		break;

	case AMD_IP_COMPUTE:
		amdgpu_bo_unmap_and_free_uq(device_handle, ctxt->eop.handle,
					    ctxt->eop.va_handle,
					    ctxt->eop.mc_addr, 256,
					    ctxt->timeline_syncobj_handle, ++ctxt->point,
					    0, 0);

		r = amdgpu_timeline_syncobj_wait(device_handle, ctxt->timeline_syncobj_handle,
						 ctxt->point);
		igt_assert_eq(r, 0);
		break;

	case AMD_IP_DMA:
		amdgpu_bo_unmap_and_free_uq(device_handle, ctxt->csa.handle,
					    ctxt->csa.va_handle,
					    ctxt->csa.mc_addr, ctxt->info.gfx.csa_size,
					    ctxt->timeline_syncobj_handle, ++ctxt->point,
					    0, 0);

		r = amdgpu_timeline_syncobj_wait(device_handle, ctxt->timeline_syncobj_handle,
						 ctxt->point);
		igt_assert_eq(r, 0);
		break;

	default:
		igt_info("IP invalid for cleanup\n");
	}

	r = amdgpu_cs_destroy_syncobj(device_handle, ctxt->timeline_syncobj_handle);
	igt_assert_eq(r, 0);

	/* Clean up doorbell*/
	r = amdgpu_bo_cpu_unmap(ctxt->doorbell.handle);
	igt_assert_eq(r, 0);

	r = amdgpu_bo_free(ctxt->doorbell.handle);
	igt_assert_eq(r, 0);

	/* Clean up rptr wptr queue */
	amdgpu_bo_unmap_and_free(ctxt->rptr.handle, ctxt->rptr.va_handle,
				 ctxt->rptr.mc_addr, 8);

	amdgpu_bo_unmap_and_free(ctxt->wptr.handle, ctxt->wptr.va_handle,
				 ctxt->wptr.mc_addr, 8);

	amdgpu_bo_unmap_and_free(ctxt->queue.handle, ctxt->queue.va_handle,
				 ctxt->queue.mc_addr, USERMODE_QUEUE_SIZE);
}

void amdgpu_user_queue_create(amdgpu_device_handle device_handle, struct amdgpu_ring_context *ctxt,
			      unsigned int type)
{
	int r;
	uint64_t gtt_flags = 0;
	struct drm_amdgpu_userq_mqd_gfx11 gfx_mqd;
	struct drm_amdgpu_userq_mqd_sdma_gfx11 sdma_mqd;
	struct drm_amdgpu_userq_mqd_compute_gfx11 compute_mqd;
	void *mqd;

	if (type > AMD_IP_DMA) {
		igt_info("Invalid IP not supported for UMQ Submission\n");
		return;
	}

	r = amdgpu_query_uq_fw_area_info(device_handle, AMD_IP_GFX, 0, &ctxt->info);
	igt_assert_eq(r, 0);

	r = amdgpu_cs_create_syncobj2(device_handle, 0, &ctxt->timeline_syncobj_handle);
	igt_assert_eq(r, 0);

	r = amdgpu_bo_alloc_and_map_uq(device_handle, USERMODE_QUEUE_SIZE,
				       ALIGNMENT,
				       AMDGPU_GEM_DOMAIN_GTT,
				       gtt_flags,
				       AMDGPU_VM_MTYPE_UC,
				       &ctxt->queue.handle, &ctxt->queue.ptr,
				       &ctxt->queue.mc_addr, &ctxt->queue.va_handle,
				       ctxt->timeline_syncobj_handle, ++ctxt->point);
	igt_assert_eq(r, 0);

	r = amdgpu_bo_alloc_and_map_uq(device_handle, 8,
				       ALIGNMENT,
				       AMDGPU_GEM_DOMAIN_GTT,
				       gtt_flags,
				       AMDGPU_VM_MTYPE_UC,
				       &ctxt->wptr.handle, &ctxt->wptr.ptr,
				       &ctxt->wptr.mc_addr, &ctxt->wptr.va_handle,
				       ctxt->timeline_syncobj_handle, ++ctxt->point);
	igt_assert_eq(r, 0);

	r = amdgpu_bo_alloc_and_map_uq(device_handle, 8,
				       ALIGNMENT,
				       AMDGPU_GEM_DOMAIN_GTT,
				       gtt_flags,
				       AMDGPU_VM_MTYPE_UC,
				       &ctxt->rptr.handle, &ctxt->rptr.ptr,
				       &ctxt->rptr.mc_addr, &ctxt->rptr.va_handle,
				       ctxt->timeline_syncobj_handle, ++ctxt->point);
	igt_assert_eq(r, 0);

	switch (type) {
	case AMD_IP_GFX:
		r = amdgpu_bo_alloc_and_map_uq(device_handle, ctxt->info.gfx.shadow_size,
					       ctxt->info.gfx.shadow_alignment,
					       AMDGPU_GEM_DOMAIN_GTT,
					       gtt_flags,
					       AMDGPU_VM_MTYPE_UC,
					       &ctxt->shadow.handle, NULL,
					       &ctxt->shadow.mc_addr, &ctxt->shadow.va_handle,
					       ctxt->timeline_syncobj_handle, ++ctxt->point);
		igt_assert_eq(r, 0);

		r = amdgpu_bo_alloc_and_map_uq(device_handle, ctxt->info.gfx.csa_size,
					       ctxt->info.gfx.csa_alignment,
					       AMDGPU_GEM_DOMAIN_GTT,
					       gtt_flags,
					       AMDGPU_VM_MTYPE_UC,
					       &ctxt->csa.handle, NULL,
					       &ctxt->csa.mc_addr, &ctxt->csa.va_handle,
					       ctxt->timeline_syncobj_handle, ++ctxt->point);
		igt_assert_eq(r, 0);

		gfx_mqd.shadow_va = ctxt->shadow.mc_addr;
		gfx_mqd.csa_va = ctxt->csa.mc_addr;
		mqd = &gfx_mqd;
		break;

	case AMD_IP_COMPUTE:
		r = amdgpu_bo_alloc_and_map_uq(device_handle, 256,
					       ALIGNMENT,
					       AMDGPU_GEM_DOMAIN_GTT,
					       gtt_flags,
					       AMDGPU_VM_MTYPE_UC,
					       &ctxt->eop.handle, NULL,
					       &ctxt->eop.mc_addr, &ctxt->eop.va_handle,
					       ctxt->timeline_syncobj_handle, ++ctxt->point);
		igt_assert_eq(r, 0);
		compute_mqd.eop_va = ctxt->eop.mc_addr;
		mqd = &compute_mqd;
		break;

	case AMD_IP_DMA:
		r = amdgpu_bo_alloc_and_map_uq(device_handle, ctxt->info.gfx.csa_size,
					       ctxt->info.gfx.csa_alignment,
					       AMDGPU_GEM_DOMAIN_GTT,
					       gtt_flags,
					       AMDGPU_VM_MTYPE_UC,
					       &ctxt->csa.handle, NULL,
					       &ctxt->csa.mc_addr, &ctxt->csa.va_handle,
					       ctxt->timeline_syncobj_handle, ++ctxt->point);
		igt_assert_eq(r, 0);
		sdma_mqd.csa_va = ctxt->csa.mc_addr;
		mqd = &sdma_mqd;
		break;

	default:
		igt_info("Unsupported IP for UMQ submission\n");
		return;

	}

	r = amdgpu_timeline_syncobj_wait(device_handle, ctxt->timeline_syncobj_handle,
					 ctxt->point);
	igt_assert_eq(r, 0);

	amdgpu_alloc_doorbell(device_handle, &ctxt->doorbell, PAGE_SIZE,
			      AMDGPU_GEM_DOMAIN_DOORBELL);

	ctxt->doorbell_cpu = (uint64_t *)ctxt->doorbell.ptr;

	ctxt->wptr_cpu = (uint64_t *)ctxt->wptr.ptr;

	ctxt->queue_cpu = (uint32_t *)ctxt->queue.ptr;
	memset(ctxt->queue_cpu, 0, USERMODE_QUEUE_SIZE);

	/* get db bo handle */
	amdgpu_bo_export(ctxt->doorbell.handle, amdgpu_bo_handle_type_kms, &ctxt->db_handle);

	/* Create the Usermode Queue */
	switch (type) {
	case AMD_IP_GFX:
		r = amdgpu_create_userqueue(device_handle, AMDGPU_HW_IP_GFX,
					    ctxt->db_handle, DOORBELL_INDEX,
					    ctxt->queue.mc_addr, USERMODE_QUEUE_SIZE,
					    ctxt->wptr.mc_addr, ctxt->rptr.mc_addr,
					    mqd, &ctxt->queue_id);
		igt_assert_eq(r, 0);
		break;

	case AMD_IP_COMPUTE:
		r = amdgpu_create_userqueue(device_handle, AMDGPU_HW_IP_COMPUTE,
					    ctxt->db_handle, DOORBELL_INDEX,
					    ctxt->queue.mc_addr, USERMODE_QUEUE_SIZE,
					    ctxt->wptr.mc_addr, ctxt->rptr.mc_addr,
					    mqd, &ctxt->queue_id);
		igt_assert_eq(r, 0);
		break;

	case AMD_IP_DMA:
		r = amdgpu_create_userqueue(device_handle, AMDGPU_HW_IP_DMA,
					    ctxt->db_handle, DOORBELL_INDEX,
					    ctxt->queue.mc_addr, USERMODE_QUEUE_SIZE,
					    ctxt->wptr.mc_addr, ctxt->rptr.mc_addr,
					    mqd, &ctxt->queue_id);
		igt_assert_eq(r, 0);
		break;

	default:
		igt_info("Unsupported IP, failed to create user queue\n");
		return;

	}
}
#else
int
amdgpu_bo_alloc_and_map_uq(amdgpu_device_handle device_handle, unsigned int size,
			   unsigned int alignment, unsigned int heap, uint64_t alloc_flags,
			   uint64_t mapping_flags, amdgpu_bo_handle *bo, void **cpu,
			   uint64_t *mc_address, amdgpu_va_handle *va_handle,
			   uint32_t timeline_syncobj_handle, uint64_t point)
{
	return 0;
}

int amdgpu_timeline_syncobj_wait(amdgpu_device_handle device_handle,
	uint32_t timeline_syncobj_handle, uint64_t point)
{
	return 0;
}

void amdgpu_user_queue_submit(amdgpu_device_handle device, struct amdgpu_ring_context *ring_context,
	unsigned int ip_type, uint64_t mc_address)
{
}

void amdgpu_user_queue_destroy(amdgpu_device_handle device_handle, struct amdgpu_ring_context *ctxt,
	unsigned int type)
{
}

void amdgpu_user_queue_create(amdgpu_device_handle device_handle, struct amdgpu_ring_context *ctxt,
	unsigned int type)
{
}

#endif
