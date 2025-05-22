/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 *
 * Authors:
 *    Francois Dugast <francois.dugast@intel.com>
 */

#include <stdint.h>

#include "i915/gem_create.h"
#include "igt.h"
#include "gen7_media.h"
#include "gen8_media.h"
#include "gen9_media.h"
#include "intel_compute.h"
#include "lib/igt_syncobj.h"
#include "lib/intel_reg.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"
#include "xehp_media.h"

#define PIPE_CONTROL			0x7a000004
#define MEDIA_STATE_FLUSH		0x0
#define MAX(X, Y)			(((X) > (Y)) ? (X) : (Y))

#define SIZE_DATA			64
#define SIZE_BATCH			0x10000
#define SIZE_SURFACE_STATE		0x10000
#define SIZE_DYNAMIC_STATE		0x100000
#define SIZE_INDIRECT_OBJECT		0x10000
#define SIZE_BINDING_TABLE		0x10000
#define SIZE_GENERAL_STATE		0x100000

#define ADDR_SYNC			0x010000ULL
#define ADDR_SYNC2			0x020000ULL
#define ADDR_BATCH			0x100000ULL
#define ADDR_INPUT			0x40000000ULL
#define ADDR_OUTPUT			0x80000000ULL
#define ADDR_SURFACE_STATE_BASE		0x200000ULL
#define ADDR_DYNAMIC_STATE_BASE		0x300000ULL
#define ADDR_INDIRECT_OBJECT_BASE	0x400000ULL
#define ADDR_BINDING_TABLE		(ADDR_SURFACE_STATE_BASE + OFFSET_BINDING_TABLE)
#define OFFSET_INDIRECT_DATA_START	0x3D0000ULL
#define OFFSET_KERNEL			0x3E0000ULL

#define ADDR_GENERAL_STATE_BASE		0x6000000ULL
#define ADDR_INSTRUCTION_STATE_BASE	0x8000000ULL
#define OFFSET_BINDING_TABLE		0x10000

#define XE2_ADDR_STATE_CONTEXT_DATA_BASE	0x9000000ULL
#define OFFSET_STATE_SIP			0xFFFF0000

#define USER_FENCE_VALUE			0xdeadbeefdeadbeefull
#define MAGIC_LOOP_STOP			0x12341234

#define THREADS_PER_GROUP		32
#define THREAD_GROUP_Y			1
#define THREAD_GROUP_Z			1
#define ENQUEUED_LOCAL_SIZE_X		1024
#define ENQUEUED_LOCAL_SIZE_Y		1
#define ENQUEUED_LOCAL_SIZE_Z		1

/*
 * TGP  - ThreadGroup Preemption
 * WMTP - Walker Mid Thread Preemption
 */
#define TGP_long_kernel_loop_count		10
#define WMTP_long_kernel_loop_count		1000000

struct bo_dict_entry {
	uint64_t addr;
	uint32_t size;
	void *data;
	const char *name;
	uint32_t handle;
};

struct bo_sync {
	uint64_t sync;
};

struct bo_execenv {
	int fd;
	enum intel_driver driver;

	/* Xe part */
	uint32_t vm;
	uint32_t exec_queue;
	uint32_t array_size;

	/* Xe user-fence */
	uint32_t bo;
	size_t bo_size;
	struct bo_sync *bo_sync;
	struct drm_xe_sync sync;

	/* i915 part */
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 *obj;

	struct user_execenv *user;
};

static void bo_execenv_create(int fd, struct bo_execenv *execenv,
			      struct drm_xe_engine_class_instance *eci,
			      struct user_execenv *user)
{
	igt_assert(execenv);

	memset(execenv, 0, sizeof(*execenv));
	execenv->fd = fd;
	execenv->driver = get_intel_driver(fd);

	if (execenv->driver == INTEL_DRIVER_XE) {
		if (user)
			execenv->user = user;

		if (user && user->vm)
			execenv->vm = user->vm;
		else
			execenv->vm = xe_vm_create(fd, DRM_XE_VM_CREATE_FLAG_LR_MODE, 0);

		if (user && user->array_size)
			execenv->array_size = user->array_size;
		else
			execenv->array_size = SIZE_DATA;

		if (eci) {
			execenv->exec_queue = xe_exec_queue_create(fd, execenv->vm,
								   eci, 0);
		} else {
			uint16_t engine_class;
			uint32_t devid = intel_get_drm_devid(fd);
			const struct intel_device_info *info = intel_get_device_info(devid);

			if (info->graphics_ver >= 12 && info->graphics_rel < 60)
				engine_class = DRM_XE_ENGINE_CLASS_RENDER;
			else
				engine_class = DRM_XE_ENGINE_CLASS_COMPUTE;

			execenv->exec_queue = xe_exec_queue_create_class(fd, execenv->vm,
									 engine_class);
		}
	}
}

static void bo_execenv_destroy(struct bo_execenv *execenv)
{
	igt_assert(execenv);

	if (execenv->driver == INTEL_DRIVER_XE) {
		xe_exec_queue_destroy(execenv->fd, execenv->exec_queue);
		if (!execenv->user || !execenv->user->vm)
			xe_vm_destroy(execenv->fd, execenv->vm);
	}
}

static void bo_execenv_bind(struct bo_execenv *execenv,
			    struct bo_dict_entry *bo_dict, int entries)
{
	int fd = execenv->fd;

	if (execenv->driver == INTEL_DRIVER_XE) {
		uint32_t vm = execenv->vm;
		uint32_t exec_queue = execenv->exec_queue;
		struct bo_sync *bo_sync;
		size_t bo_size = sizeof(*bo_sync);
		uint32_t bo = 0;
		struct drm_xe_sync sync = {
			.type = DRM_XE_SYNC_TYPE_USER_FENCE,
			.flags = DRM_XE_SYNC_FLAG_SIGNAL,
			.timeline_value = USER_FENCE_VALUE,
		};

		bo_size = xe_bb_size(fd, bo_size);
		bo = xe_bo_create(fd, execenv->vm, bo_size, vram_if_possible(fd, 0),
				  DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
		bo_sync = xe_bo_map(fd, bo, bo_size);
		sync.addr = to_user_pointer(&bo_sync->sync);

		for (int i = 0; i < entries; i++) {
			bo_sync->sync = 0;
			bo_dict[i].handle = xe_bo_create(fd, execenv->vm, bo_dict[i].size,
							 vram_if_possible(fd, 0),
							 DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
			bo_dict[i].data = xe_bo_map(fd, bo_dict[i].handle, bo_dict[i].size);
			xe_vm_bind_async(fd, vm, 0, bo_dict[i].handle, 0, bo_dict[i].addr,
					 bo_dict[i].size, &sync, 1);
			xe_wait_ufence(fd, &bo_sync->sync, USER_FENCE_VALUE, exec_queue,
				       INT64_MAX);
			memset(bo_dict[i].data, 0, bo_dict[i].size);

			igt_debug("[i: %2d name: %20s] data: %p, addr: %16llx, size: %llx\n",
				  i, bo_dict[i].name, bo_dict[i].data,
				  (long long)bo_dict[i].addr,
				  (long long)bo_dict[i].size);
		}

		munmap(bo_sync, bo_size);
		gem_close(fd, bo);
	} else {
		struct drm_i915_gem_execbuffer2 *execbuf = &execenv->execbuf;
		struct drm_i915_gem_exec_object2 *obj;

		obj = calloc(entries, sizeof(*obj));
		execenv->obj = obj;

		for (int i = 0; i < entries; i++) {
			bo_dict[i].handle = gem_create(fd, bo_dict[i].size);
			bo_dict[i].data = gem_mmap__device_coherent(fd, bo_dict[i].handle,
								    0, bo_dict[i].size,
								    PROT_READ | PROT_WRITE);
			igt_debug("[i: %2d name: %20s] handle: %u, data: %p, addr: %16llx, size: %llx\n",
				  i, bo_dict[i].name,
				  bo_dict[i].handle, bo_dict[i].data,
				  (long long)bo_dict[i].addr,
				  (long long)bo_dict[i].size);

			obj[i].handle = bo_dict[i].handle;
			obj[i].offset = CANONICAL(bo_dict[i].addr);
			obj[i].flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS;
			if (bo_dict[i].addr == ADDR_OUTPUT)
				obj[i].flags |= EXEC_OBJECT_WRITE;
		}

		execbuf->buffers_ptr = to_user_pointer(obj);
		execbuf->buffer_count = entries;
	}
}

static void bo_execenv_unbind(struct bo_execenv *execenv,
			      struct bo_dict_entry *bo_dict, int entries)
{
	int fd = execenv->fd;

	if (execenv->driver == INTEL_DRIVER_XE) {
		uint32_t vm = execenv->vm;
		uint32_t exec_queue = execenv->exec_queue;
		struct bo_sync *bo_sync;
		size_t bo_size = sizeof(*bo_sync);
		uint32_t bo = 0;
		struct drm_xe_sync sync = {
			.type = DRM_XE_SYNC_TYPE_USER_FENCE,
			.flags = DRM_XE_SYNC_FLAG_SIGNAL,
			.timeline_value = USER_FENCE_VALUE,
		};

		bo_size = xe_bb_size(fd, bo_size);
		bo = xe_bo_create(fd, execenv->vm, bo_size, vram_if_possible(fd, 0),
				  DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
		bo_sync = xe_bo_map(fd, bo, bo_size);
		sync.addr = to_user_pointer(&bo_sync->sync);

		for (int i = 0; i < entries; i++) {
			bo_sync->sync = 0;
			xe_vm_unbind_async(fd, vm, 0, 0, bo_dict[i].addr, bo_dict[i].size, &sync, 1);
			xe_wait_ufence(fd, &bo_sync->sync, USER_FENCE_VALUE, exec_queue,
				       INT64_MAX);
			munmap(bo_dict[i].data, bo_dict[i].size);
			gem_close(fd, bo_dict[i].handle);
		}

		munmap(bo_sync, bo_size);
		gem_close(fd, bo);
	} else {
		for (int i = 0; i < entries; i++) {
			gem_close(fd, bo_dict[i].handle);
			munmap(bo_dict[i].data, bo_dict[i].size);
		}
		free(execenv->obj);
	}
}

static void __bo_execenv_exec(struct bo_execenv *execenv, uint64_t start_addr)
{
	int fd = execenv->fd;

	if (execenv->driver == INTEL_DRIVER_XE) {
		uint32_t exec_queue = execenv->exec_queue;
		size_t bo_size = ALIGN(sizeof(struct bo_sync),
				       xe_get_default_alignment(fd));

		execenv->bo_size = bo_size;
		execenv->bo = xe_bo_create(fd, execenv->vm, bo_size, vram_if_possible(fd, 0),
					   DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
		execenv->bo_sync = xe_bo_map(fd, execenv->bo, bo_size);
		execenv->sync.type = DRM_XE_SYNC_TYPE_USER_FENCE;
		execenv->sync.flags = DRM_XE_SYNC_FLAG_SIGNAL;
		execenv->sync.timeline_value = USER_FENCE_VALUE;
		execenv->sync.addr = to_user_pointer(&execenv->bo_sync->sync);
		xe_vm_bind_async(fd, execenv->vm, 0, execenv->bo, 0, ADDR_SYNC,
				 bo_size, &execenv->sync, 1);
		xe_wait_ufence(fd, &execenv->bo_sync->sync, USER_FENCE_VALUE,
			       exec_queue, INT64_MAX);

		execenv->sync.addr = ADDR_SYNC;
		execenv->bo_sync->sync = 0;

		xe_exec_sync(fd, exec_queue, start_addr, &execenv->sync, 1);
	} else {
		struct drm_i915_gem_execbuffer2 *execbuf = &execenv->execbuf;

		execbuf->flags = I915_EXEC_RENDER;
		gem_execbuf(fd, execbuf);
	}
}

static void bo_execenv_sync(struct bo_execenv *execenv)
{
	int fd = execenv->fd;

	if (execenv->driver == INTEL_DRIVER_XE) {
		xe_wait_ufence(fd, &execenv->bo_sync->sync,
			       USER_FENCE_VALUE, execenv->exec_queue, INT64_MAX);
		munmap(execenv->bo_sync, execenv->bo_size);
		gem_close(fd, execenv->bo);
	} else {
		struct drm_i915_gem_execbuffer2 *execbuf = &execenv->execbuf;
		struct drm_i915_gem_exec_object2 *obj = execenv->obj;
		int num_objects = execbuf->buffer_count;

		gem_sync(fd, obj[num_objects - 1].handle); /* batch handle */
	}
}

static void bo_execenv_exec_async(struct bo_execenv *execenv, uint64_t start_addr)
{
	__bo_execenv_exec(execenv, start_addr);
}

static void bo_execenv_exec(struct bo_execenv *execenv, uint64_t start_addr)
{
	bo_execenv_exec_async(execenv, start_addr);
	bo_execenv_sync(execenv);
}

static uint32_t size_thread_group_x(uint32_t work_size)
{
	return MAX(1, work_size / (ENQUEUED_LOCAL_SIZE_X *
				   ENQUEUED_LOCAL_SIZE_Y *
				   ENQUEUED_LOCAL_SIZE_Z));
}

static size_t size_input(uint32_t work_size)
{
	return MAX(sizeof(float) * work_size, 0x10000);
}

static size_t size_output(uint32_t work_size)
{
	return MAX(sizeof(float) * work_size, 0x10000);
}

/*
 * TGL compatible batch
 */

/**
 * create_indirect_data:
 * @addr_bo_buffer_batch: pointer to batch buffer
 * @addr_input: input buffer gpu offset
 * @addr_output: output buffer gpu offset
 *
 * Prepares indirect data for compute pipeline.
 */
static void create_indirect_data(uint32_t *addr_bo_buffer_batch,
				 uint64_t addr_input,
				 uint64_t addr_output,
				 uint32_t end_value,
				 unsigned int loop_count)
{
	uint32_t val = 0;
	int b = 0, curr = 0;

	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000200;

	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = addr_input & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_input >> 32;
	addr_bo_buffer_batch[b++] = addr_output & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_output >> 32;

	addr_bo_buffer_batch[b++] = loop_count;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = 0x00000200;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	/*
	 * Runtime prepares 32 16-bit incremented values packed to single dword.
	 * Then it lefts 32 dword gap filled with zeroes. Pattern looks the
	 * same for tgl and dg1 (apart of number of values).
	 */
	while (val < end_value) {
		addr_bo_buffer_batch[b++] = val | ((val + 1) << 16);
		val += 2;
		if (++curr % 16 == 0)
			b += 32;
	}
}

/**
 * create_surface_state:
 * @addr_bo_buffer_batch: pointer to batch buffer
 * @addr_input: input buffer gpu offset
 * @addr_output: output buffer gpu offset
 *
 * Prepares surface state for compute pipeline.
 */
static void create_surface_state(uint32_t *addr_bo_buffer_batch,
				 uint64_t addr_input,
				 uint64_t addr_output)
{
	int b = 0;

	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x87FD4000;
	addr_bo_buffer_batch[b++] = 0x04000000;
	addr_bo_buffer_batch[b++] = 0x001F007F;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00004000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = addr_input & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_input >> 32;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x87FD4000;
	addr_bo_buffer_batch[b++] = 0x04000000;
	addr_bo_buffer_batch[b++] = 0x001F007F;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00004000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = addr_output & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_output >> 32;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000040;
	addr_bo_buffer_batch[b++] = 0x00000080;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
}

/**
 * create_dynamic_state:
 * @addr_bo_buffer_batch: pointer to batch buffer
 * @offset_kernel: gpu offset of the shader
 *
 * Prepares dynamic state for compute pipeline.
 */
static void create_dynamic_state(uint32_t *addr_bo_buffer_batch,
				 uint64_t offset_kernel)
{
	int b = 0;

	addr_bo_buffer_batch[b++] = offset_kernel;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00180000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x000000C0;
	addr_bo_buffer_batch[b++] = 0x00060000;
	addr_bo_buffer_batch[b++] = 0x00000010;
	addr_bo_buffer_batch[b++] = 0x00000003;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
}

/**
 * tgllp_compute_exec_compute:
 * @addr_bo_buffer_batch: pointer to batch buffer
 * @addr_surface_state_base: gpu offset of surface state data
 * @addr_dynamic_state_base: gpu offset of dynamic state data
 * @addr_indirect_object_base: gpu offset of indirect object data
 * @offset_indirect_data_start: gpu offset of indirect data start
 *
 * Prepares compute pipeline.
 */
static void tgllp_compute_exec_compute(uint32_t *addr_bo_buffer_batch,
				       uint64_t addr_surface_state_base,
				       uint64_t addr_dynamic_state_base,
				       uint64_t addr_indirect_object_base,
				       uint64_t offset_indirect_data_start)
{
	int b = 0;

	addr_bo_buffer_batch[b++] = MI_LOAD_REGISTER_IMM(1);
	addr_bo_buffer_batch[b++] = 0x00002580;
	addr_bo_buffer_batch[b++] = 0x00060002;
	addr_bo_buffer_batch[b++] = PIPELINE_SELECT;
	addr_bo_buffer_batch[b++] = MI_LOAD_REGISTER_IMM(1);
	addr_bo_buffer_batch[b++] = 0x00007034;
	addr_bo_buffer_batch[b++] = 0x60000321;
	addr_bo_buffer_batch[b++] = PIPE_CONTROL;
	addr_bo_buffer_batch[b++] = 0x00100000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = MI_LOAD_REGISTER_IMM(1);
	addr_bo_buffer_batch[b++] = 0x0000E404;
	addr_bo_buffer_batch[b++] = 0x00000100;
	addr_bo_buffer_batch[b++] = PIPE_CONTROL;
	addr_bo_buffer_batch[b++] = 0x00101021;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = MEDIA_VFE_STATE | (9 - 2);
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00A70100;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x07820000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = PIPE_CONTROL;
	addr_bo_buffer_batch[b++] = 0x00100420;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = STATE_BASE_ADDRESS | (16 - 2);
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00040000;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0x1;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = (addr_dynamic_state_base & 0xffffffff) | 0x1;
	addr_bo_buffer_batch[b++] = addr_dynamic_state_base >> 32;
	addr_bo_buffer_batch[b++] = (addr_indirect_object_base & 0xffffffff) | 0x1;
	addr_bo_buffer_batch[b++] = (addr_indirect_object_base >> 32) | 0xffff0000;
	addr_bo_buffer_batch[b++] = (addr_indirect_object_base & 0xffffffff) | 0x41;
	addr_bo_buffer_batch[b++] = addr_indirect_object_base >> 32;
	addr_bo_buffer_batch[b++] = 0xFFFFF001;
	addr_bo_buffer_batch[b++] = 0x00010001;
	addr_bo_buffer_batch[b++] = 0xFFFFF001;
	addr_bo_buffer_batch[b++] = 0xFFFFF001;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0x1;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x003BF000;
	addr_bo_buffer_batch[b++] = 0x00000041;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = PIPE_CONTROL;
	addr_bo_buffer_batch[b++] = 0x00100000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = PIPE_CONTROL;
	addr_bo_buffer_batch[b++] = 0x00100000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = MEDIA_STATE_FLUSH;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = MEDIA_INTERFACE_DESCRIPTOR_LOAD | (4 - 2);
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000020;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = GPGPU_WALKER | 13;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000c80;
	addr_bo_buffer_batch[b++] = offset_indirect_data_start;
	addr_bo_buffer_batch[b++] = 0x8000000f;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000002;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0xffffffff;
	addr_bo_buffer_batch[b++] = 0xffffffff;
	addr_bo_buffer_batch[b++] = MEDIA_STATE_FLUSH;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = PIPE_CONTROL;
	addr_bo_buffer_batch[b++] = 0x00100000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = PIPE_CONTROL;
	addr_bo_buffer_batch[b++] = 0x00100120;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = MI_BATCH_BUFFER_END;
}

/**
 * dg1_compute_exec_compute:
 * @addr_bo_buffer_batch: pointer to batch buffer
 * @addr_surface_state_base: gpu offset of surface state data
 * @addr_dynamic_state_base: gpu offset of dynamic state data
 * @addr_indirect_object_base: gpu offset of indirect object data
 * @offset_indirect_data_start: gpu offset of indirect data start
 *
 * Prepares compute pipeline.
 */
static void dg1_compute_exec_compute(uint32_t *addr_bo_buffer_batch,
				     uint64_t addr_surface_state_base,
				     uint64_t addr_dynamic_state_base,
				     uint64_t addr_indirect_object_base,
				     uint64_t offset_indirect_data_start)
{
	int b = 0;

	addr_bo_buffer_batch[b++] = XEHP_STATE_COMPUTE_MODE;
	addr_bo_buffer_batch[b++] = 0x00180010;

	addr_bo_buffer_batch[b++] = MEDIA_VFE_STATE | (9 - 2);
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x02FF0100;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x04000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = MI_LOAD_REGISTER_IMM(1);
	addr_bo_buffer_batch[b++] = 0x00002580;
	addr_bo_buffer_batch[b++] = 0x00060002;

	addr_bo_buffer_batch[b++] = STATE_BASE_ADDRESS | 0x14;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x000A0000;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0x1;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = (addr_dynamic_state_base & 0xffffffff) | 0x1;
	addr_bo_buffer_batch[b++] = addr_dynamic_state_base >> 32;
	addr_bo_buffer_batch[b++] = (addr_indirect_object_base & 0xffffffff) | 0x1;
	addr_bo_buffer_batch[b++] = (addr_indirect_object_base >> 32) | 0xffff0000;
	addr_bo_buffer_batch[b++] = (addr_indirect_object_base & 0xffffffff) | 0xA1;
	addr_bo_buffer_batch[b++] = addr_indirect_object_base >> 32;
	addr_bo_buffer_batch[b++] = 0xFFFFF001;
	addr_bo_buffer_batch[b++] = 0x00010001;
	addr_bo_buffer_batch[b++] = 0xFFFFF001;
	addr_bo_buffer_batch[b++] = 0xFFFFF001;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0xA1;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x003BF000;
	addr_bo_buffer_batch[b++] = 0x000000A1;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = MEDIA_INTERFACE_DESCRIPTOR_LOAD | (4 - 2);
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000020;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = GPGPU_WALKER | 13;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000c80;
	addr_bo_buffer_batch[b++] = offset_indirect_data_start;
	addr_bo_buffer_batch[b++] = 0x8000000f;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000002;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0xffffffff;
	addr_bo_buffer_batch[b++] = 0xffffffff;

	addr_bo_buffer_batch[b++] = MEDIA_STATE_FLUSH;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = MI_BATCH_BUFFER_END;
}

/**
 * compute_exec - run a pipeline compatible with Tiger Lake and DG1
 *
 * @fd: file descriptor of the opened DRM device
 * @kernel: GPU Kernel binary to be executed
 * @size: size of @kernel.
 * @eci: Xe engine class instance if device is Xe
 * @user: user-provided execution environment
 */
static void compute_exec(int fd, const unsigned char *kernel,
			 unsigned int size,
			 struct drm_xe_engine_class_instance *eci,
			 struct user_execenv *user)
{
#define BO_DICT_ENTRIES 7
	struct bo_dict_entry bo_dict[BO_DICT_ENTRIES] = {
		{ .addr = ADDR_INDIRECT_OBJECT_BASE + OFFSET_KERNEL,
		  .name = "kernel" },
		{ .addr = ADDR_DYNAMIC_STATE_BASE,
		  .size = SIZE_DYNAMIC_STATE,
		  .name = "dynamic state base" },
		{ .addr = ADDR_SURFACE_STATE_BASE,
		  .size = SIZE_SURFACE_STATE,
		  .name = "surface state base" },
		{ .addr = ADDR_INDIRECT_OBJECT_BASE + OFFSET_INDIRECT_DATA_START,
		  .size = SIZE_INDIRECT_OBJECT,
		  .name = "indirect data start" },
		{ .addr = ADDR_INPUT,
		  .name = "input" },
		{ .addr = ADDR_OUTPUT,
		  .name = "output" },
		{ .addr = ADDR_BATCH,
		  .size = SIZE_BATCH,
		  .name = "batch" },
	};
	struct bo_execenv execenv;
	float *input_data, *output_data;
	uint64_t bind_input_addr = (user && user->input_addr) ? user->input_addr : ADDR_INPUT;
	uint64_t bind_output_addr = (user && user->output_addr) ? user->output_addr : ADDR_OUTPUT;
	uint16_t devid = intel_get_drm_devid(fd);

	bo_execenv_create(fd, &execenv, eci, user);

	/* Set dynamic sizes */
	bo_dict[0].size = ALIGN(size, 0x1000);
	bo_dict[4].size = size_input(execenv.array_size);
	bo_dict[5].size = size_output(execenv.array_size);

	bo_execenv_bind(&execenv, bo_dict, BO_DICT_ENTRIES);

	memcpy(bo_dict[0].data, kernel, size);
	create_dynamic_state(bo_dict[1].data, OFFSET_KERNEL);
	create_surface_state(bo_dict[2].data, bind_input_addr, bind_output_addr);
	create_indirect_data(bo_dict[3].data, bind_input_addr, bind_output_addr,
			     IS_DG1(devid) ? 0x200 : 0x40, execenv.array_size);

	if (user && user->input_addr) {
		input_data = from_user_pointer(user->input_addr);
	} else {
		input_data = (float *) bo_dict[4].data;
		srand(time(NULL));

		for (int i = 0; i < execenv.array_size; i++)
			input_data[i] = rand() / (float)RAND_MAX;
	}

	if (user && user->output_addr)
		output_data = from_user_pointer(user->output_addr);
	else
		output_data = (float *) bo_dict[5].data;

	if (IS_DG1(devid))
		dg1_compute_exec_compute(bo_dict[6].data,
					 ADDR_SURFACE_STATE_BASE,
					 ADDR_DYNAMIC_STATE_BASE,
					 ADDR_INDIRECT_OBJECT_BASE,
					 OFFSET_INDIRECT_DATA_START);
	else
		tgllp_compute_exec_compute(bo_dict[6].data,
					   ADDR_SURFACE_STATE_BASE,
					   ADDR_DYNAMIC_STATE_BASE,
					   ADDR_INDIRECT_OBJECT_BASE,
					   OFFSET_INDIRECT_DATA_START);

	bo_execenv_exec(&execenv, ADDR_BATCH);

	for (int i = 0; i < execenv.array_size; i++) {
		float input = input_data[i];
		float output = output_data[i];
		float expected_output = input * input;

		if (output != expected_output)
			igt_debug("[%4d] input:%f output:%f expected_output:%f\n",
				  i, input, output, expected_output);
		if (!user || (user && !user->skip_results_check))
			igt_assert_eq_double(output, expected_output);
	}

	bo_execenv_unbind(&execenv, bo_dict, BO_DICT_ENTRIES);
	bo_execenv_destroy(&execenv);
}

static void xehp_create_indirect_data(uint32_t *addr_bo_buffer_batch,
				      uint64_t addr_input,
				      uint64_t addr_output,
				      unsigned int loop_count)
{
	int b = 0;

	addr_bo_buffer_batch[b++] = addr_input & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_input >> 32;
	addr_bo_buffer_batch[b++] = addr_output & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_output >> 32;
	addr_bo_buffer_batch[b++] = loop_count;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = ENQUEUED_LOCAL_SIZE_X;
	addr_bo_buffer_batch[b++] = ENQUEUED_LOCAL_SIZE_Y;
	addr_bo_buffer_batch[b++] = ENQUEUED_LOCAL_SIZE_Z;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
}

static void xehp_create_surface_state(uint32_t *addr_bo_buffer_batch,
				      uint64_t addr_input,
				      uint64_t addr_output)
{
	int b = 0;

	addr_bo_buffer_batch[b++] = 0x87FDC000;
	addr_bo_buffer_batch[b++] = 0x06000000;
	addr_bo_buffer_batch[b++] = 0x001F007F;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00002000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = addr_input & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_input >> 32;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = 0x87FDC000;
	addr_bo_buffer_batch[b++] = 0x06000000;
	addr_bo_buffer_batch[b++] = 0x001F007F;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00002000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = addr_output & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_output >> 32;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = 0x00010000;
	addr_bo_buffer_batch[b++] = 0x00010040;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
}

static void xehp_compute_exec_compute(uint32_t *addr_bo_buffer_batch,
				      uint64_t addr_general_state_base,
				      uint64_t addr_surface_state_base,
				      uint64_t addr_dynamic_state_base,
				      uint64_t addr_instruction_state_base,
				      uint64_t offset_indirect_data_start,
				      uint64_t kernel_start_pointer)
{
	int b = 0;

	igt_debug("general   state base: %"PRIx64"\n", addr_general_state_base);
	igt_debug("surface   state base: %"PRIx64"\n", addr_surface_state_base);
	igt_debug("dynamic   state base: %"PRIx64"\n", addr_dynamic_state_base);
	igt_debug("instruct   base addr: %"PRIx64"\n", addr_instruction_state_base);
	igt_debug("bindless   base addr: %"PRIx64"\n", addr_surface_state_base);
	igt_debug("offset indirect addr: %"PRIx64"\n", offset_indirect_data_start);
	igt_debug("kernel start pointer: %"PRIx64"\n", kernel_start_pointer);

	addr_bo_buffer_batch[b++] = GEN7_PIPELINE_SELECT | GEN9_PIPELINE_SELECTION_MASK |
				    PIPELINE_SELECT_GPGPU;

	addr_bo_buffer_batch[b++] = XEHP_STATE_COMPUTE_MODE;
	addr_bo_buffer_batch[b++] = 0x80180010;

	addr_bo_buffer_batch[b++] = XEHP_CFE_STATE;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x0c008800;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = MI_LOAD_REGISTER_IMM(1);
	addr_bo_buffer_batch[b++] = 0x00002580;
	addr_bo_buffer_batch[b++] = 0x00060002;

	addr_bo_buffer_batch[b++] = STATE_BASE_ADDRESS | 0x14;
	addr_bo_buffer_batch[b++] = (addr_general_state_base & 0xffffffff) | 0x61;
	addr_bo_buffer_batch[b++] = addr_general_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x0106c000;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0x61;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = (addr_dynamic_state_base & 0xffffffff) | 0x61;
	addr_bo_buffer_batch[b++] = addr_dynamic_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = (addr_instruction_state_base & 0xffffffff) | 0x61;
	addr_bo_buffer_batch[b++] = addr_instruction_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0xfffff001;
	addr_bo_buffer_batch[b++] = 0x00010001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0xfffff001;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0x61;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x00007fbf;
	addr_bo_buffer_batch[b++] = 0x00000061;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = GEN8_3DSTATE_BINDING_TABLE_POOL_ALLOC | 2;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0x6;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x001ff000;

	addr_bo_buffer_batch[b++] = XEHP_COMPUTE_WALKER | 0x25;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000040;
	addr_bo_buffer_batch[b++] = offset_indirect_data_start;
	addr_bo_buffer_batch[b++] = 0xbe040000;
	addr_bo_buffer_batch[b++] = 0xffffffff;
	addr_bo_buffer_batch[b++] = 0x0000003f;
	addr_bo_buffer_batch[b++] = 0x00000010;

	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = kernel_start_pointer;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00180000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00010080;
	addr_bo_buffer_batch[b++] = 0x0c000002;

	addr_bo_buffer_batch[b++] = 0x00000008;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00001027;
	addr_bo_buffer_batch[b++] = ADDR_BATCH;
	addr_bo_buffer_batch[b++] = ADDR_BATCH >> 32;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000040;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = MI_BATCH_BUFFER_END;
}

/**
 * xehp_compute_exec - run a pipeline compatible with XEHP
 *
 * @fd: file descriptor of the opened DRM device
 * @kernel: GPU Kernel binary to be executed
 * @size: size of @kernel.
 * @eci: Xe engine class instance if device is Xe
 * @user: user-provided execution environment
 */
static void xehp_compute_exec(int fd, const unsigned char *kernel,
			      unsigned int size,
			      struct drm_xe_engine_class_instance *eci,
			      struct user_execenv *user)
{
#define XEHP_BO_DICT_ENTRIES 9
	struct bo_dict_entry bo_dict[XEHP_BO_DICT_ENTRIES] = {
		{ .addr = ADDR_INSTRUCTION_STATE_BASE + OFFSET_KERNEL,
		  .name = "instr state base"},
		{ .addr = ADDR_DYNAMIC_STATE_BASE,
		  .size = SIZE_DYNAMIC_STATE,
		  .name = "dynamic state base"},
		{ .addr = ADDR_SURFACE_STATE_BASE,
		  .size = SIZE_SURFACE_STATE,
		  .name = "surface state base"},
		{ .addr = ADDR_GENERAL_STATE_BASE + OFFSET_INDIRECT_DATA_START,
		  .size = SIZE_INDIRECT_OBJECT,
		  .name = "indirect object base"},
		{ .addr = ADDR_INPUT,
		  .name = "addr input"},
		{ .addr = ADDR_OUTPUT,
		  .name = "addr output" },
		{ .addr = ADDR_GENERAL_STATE_BASE,
		  .size = SIZE_GENERAL_STATE,
		  .name = "general state base" },
		{ .addr = ADDR_BINDING_TABLE,
		  .size = SIZE_BINDING_TABLE,
		  .name = "binding table" },
		{ .addr = ADDR_BATCH, .size = SIZE_BATCH,
		  .name = "batch" },
	};
	struct bo_execenv execenv;
	float *input_data, *output_data;
	uint64_t bind_input_addr = (user && user->input_addr) ? user->input_addr : ADDR_INPUT;
	uint64_t bind_output_addr = (user && user->output_addr) ? user->output_addr : ADDR_OUTPUT;

	bo_execenv_create(fd, &execenv, eci, user);

	/* Set dynamic sizes */
	bo_dict[0].size = ALIGN(size, xe_get_default_alignment(fd));
	bo_dict[4].size = size_input(execenv.array_size);
	bo_dict[5].size = size_output(execenv.array_size);

	bo_execenv_bind(&execenv, bo_dict, XEHP_BO_DICT_ENTRIES);

	memcpy(bo_dict[0].data, kernel, size);
	create_dynamic_state(bo_dict[1].data, OFFSET_KERNEL);
	xehp_create_surface_state(bo_dict[2].data, bind_input_addr, bind_output_addr);
	xehp_create_indirect_data(bo_dict[3].data, bind_input_addr, bind_output_addr,
				  execenv.array_size);
	xehp_create_surface_state(bo_dict[7].data, bind_input_addr, bind_output_addr);

	if (user && user->input_addr) {
		input_data = from_user_pointer(user->input_addr);
	} else {
		input_data = (float *) bo_dict[4].data;
		srand(time(NULL));

		for (int i = 0; i < execenv.array_size; i++)
			input_data[i] = rand() / (float)RAND_MAX;
	}

	if (user && user->output_addr)
		output_data = from_user_pointer(user->output_addr);
	else
		output_data = (float *) bo_dict[5].data;

	xehp_compute_exec_compute(bo_dict[8].data,
				  ADDR_GENERAL_STATE_BASE,
				  ADDR_SURFACE_STATE_BASE,
				  ADDR_DYNAMIC_STATE_BASE,
				  ADDR_INSTRUCTION_STATE_BASE,
				  OFFSET_INDIRECT_DATA_START,
				  OFFSET_KERNEL);

	bo_execenv_exec(&execenv, ADDR_BATCH);

	for (int i = 0; i < execenv.array_size; i++) {
		float input = input_data[i];
		float output = output_data[i];
		float expected_output = input * input;

		if (output != expected_output)
			igt_debug("[%4d] input:%f output:%f expected_output:%f\n",
				  i, input, output, expected_output);
		if (!user || (user && !user->skip_results_check))
			igt_assert_eq_double(output, expected_output);
	}

	bo_execenv_unbind(&execenv, bo_dict, XEHP_BO_DICT_ENTRIES);
	bo_execenv_destroy(&execenv);
}

static void xehpc_create_indirect_data(uint32_t *addr_bo_buffer_batch,
				       uint64_t addr_input,
				       uint64_t addr_output,
				       unsigned int loop_count)
{
	int b = 0;

	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = ENQUEUED_LOCAL_SIZE_X;
	addr_bo_buffer_batch[b++] = ENQUEUED_LOCAL_SIZE_Y;
	addr_bo_buffer_batch[b++] = ENQUEUED_LOCAL_SIZE_Z;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = addr_input & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_input >> 32;
	addr_bo_buffer_batch[b++] = addr_output & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_output >> 32;
	addr_bo_buffer_batch[b++] = loop_count;
	addr_bo_buffer_batch[b++] = ENQUEUED_LOCAL_SIZE_X;
	addr_bo_buffer_batch[b++] = ENQUEUED_LOCAL_SIZE_Y;
	addr_bo_buffer_batch[b++] = ENQUEUED_LOCAL_SIZE_Z;
}

static void xehpc_compute_exec_compute(uint32_t *addr_bo_buffer_batch,
				       uint64_t addr_general_state_base,
				       uint64_t addr_surface_state_base,
				       uint64_t addr_dynamic_state_base,
				       uint64_t addr_instruction_state_base,
				       uint64_t offset_indirect_data_start,
				       uint64_t kernel_start_pointer)
{
	int b = 0;

	igt_debug("general   state base: %"PRIx64"\n", addr_general_state_base);
	igt_debug("surface   state base: %"PRIx64"\n", addr_surface_state_base);
	igt_debug("dynamic   state base: %"PRIx64"\n", addr_dynamic_state_base);
	igt_debug("instruct   base addr: %"PRIx64"\n", addr_instruction_state_base);
	igt_debug("bindless   base addr: %"PRIx64"\n", addr_surface_state_base);
	igt_debug("offset indirect addr: %"PRIx64"\n", offset_indirect_data_start);
	igt_debug("kernel start pointer: %"PRIx64"\n", kernel_start_pointer);

	addr_bo_buffer_batch[b++] = GEN7_PIPELINE_SELECT | GEN9_PIPELINE_SELECTION_MASK |
				    PIPELINE_SELECT_GPGPU;

	addr_bo_buffer_batch[b++] = XEHP_STATE_COMPUTE_MODE;
	addr_bo_buffer_batch[b++] = 0xE0186010;

	addr_bo_buffer_batch[b++] = XEHP_CFE_STATE | 0x4;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x10008800;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = MI_LOAD_REGISTER_IMM(1);
	addr_bo_buffer_batch[b++] = 0x00002580;
	addr_bo_buffer_batch[b++] = 0x00060002;

	addr_bo_buffer_batch[b++] = STATE_BASE_ADDRESS | 0x14;
	addr_bo_buffer_batch[b++] = (addr_general_state_base & 0xffffffff) | 0x41;
	addr_bo_buffer_batch[b++] = addr_general_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x00044000;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0x41;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = (addr_dynamic_state_base & 0xffffffff) | 0x41;
	addr_bo_buffer_batch[b++] = addr_dynamic_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = (addr_instruction_state_base & 0xffffffff) | 0x41;
	addr_bo_buffer_batch[b++] = addr_instruction_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0xfffff001;
	addr_bo_buffer_batch[b++] = 0x00010001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0xfffff001;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0x41;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x00007fbf;
	addr_bo_buffer_batch[b++] = 0x00000041;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = GEN8_3DSTATE_BINDING_TABLE_POOL_ALLOC | 2;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = XEHP_COMPUTE_WALKER | 0x25;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000040;
	addr_bo_buffer_batch[b++] = offset_indirect_data_start;
	addr_bo_buffer_batch[b++] = 0xbe040000;
	addr_bo_buffer_batch[b++] = 0xffffffff;
	addr_bo_buffer_batch[b++] = 0x0000003f;
	addr_bo_buffer_batch[b++] = 0x00000010;

	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = kernel_start_pointer;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00180000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x0c000000 | THREADS_PER_GROUP;

	addr_bo_buffer_batch[b++] = 0x00000008;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00001047;
	addr_bo_buffer_batch[b++] = ADDR_BATCH;
	addr_bo_buffer_batch[b++] = ADDR_BATCH >> 32;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000040;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = MI_BATCH_BUFFER_END;
}

/**
 * xehpc_compute_exec - run a pipeline compatible with XEHP
 *
 * @fd: file descriptor of the opened DRM device
 * @kernel: GPU Kernel binary to be executed
 * @size: size of @kernel.
 * @eci: Xe engine class instance if device is Xe
 * @user: user-provided execution environment
 */
static void xehpc_compute_exec(int fd, const unsigned char *kernel,
			       unsigned int size,
			       struct drm_xe_engine_class_instance *eci,
			       struct user_execenv *user)
{
#define XEHPC_BO_DICT_ENTRIES 6
	struct bo_dict_entry bo_dict[XEHPC_BO_DICT_ENTRIES] = {
		{ .addr = ADDR_INSTRUCTION_STATE_BASE + OFFSET_KERNEL,
		  .name = "instr state base"},
		{ .addr = ADDR_GENERAL_STATE_BASE + OFFSET_INDIRECT_DATA_START,
		  .size = SIZE_INDIRECT_OBJECT,
		  .name = "indirect object base"},
		{ .addr = ADDR_INPUT,
		  .name = "addr input"},
		{ .addr = ADDR_OUTPUT,
		  .name = "addr output" },
		{ .addr = ADDR_GENERAL_STATE_BASE,
		  .size = SIZE_GENERAL_STATE,
		  .name = "general state base" },
		{ .addr = ADDR_BATCH, .size = SIZE_BATCH,
		  .name = "batch" },
	};
	struct bo_execenv execenv;
	float *input_data, *output_data;
	uint64_t bind_input_addr = (user && user->input_addr) ? user->input_addr : ADDR_INPUT;
	uint64_t bind_output_addr = (user && user->output_addr) ? user->output_addr : ADDR_OUTPUT;

	bo_execenv_create(fd, &execenv, eci, user);

	/* Set dynamic sizes */
	bo_dict[0].size = ALIGN(size, xe_get_default_alignment(fd));
	bo_dict[2].size = size_input(execenv.array_size);
	bo_dict[3].size = size_output(execenv.array_size);

	bo_execenv_bind(&execenv, bo_dict, XEHPC_BO_DICT_ENTRIES);

	memcpy(bo_dict[0].data, kernel, size);
	xehpc_create_indirect_data(bo_dict[1].data, bind_input_addr, bind_output_addr,
				   execenv.array_size);

	if (user && user->input_addr) {
		input_data = from_user_pointer(user->input_addr);
	} else {
		input_data = (float *) bo_dict[2].data;
		srand(time(NULL));

		for (int i = 0; i < execenv.array_size; i++)
			input_data[i] = rand() / (float)RAND_MAX;
	}

	if (user && user->output_addr)
		output_data = from_user_pointer(user->output_addr);
	else
		output_data = (float *) bo_dict[3].data;

	xehpc_compute_exec_compute(bo_dict[5].data,
				   ADDR_GENERAL_STATE_BASE,
				   ADDR_SURFACE_STATE_BASE,
				   ADDR_DYNAMIC_STATE_BASE,
				   ADDR_INSTRUCTION_STATE_BASE,
				   OFFSET_INDIRECT_DATA_START,
				   OFFSET_KERNEL);

	bo_execenv_exec(&execenv, ADDR_BATCH);

	for (int i = 0; i < execenv.array_size; i++) {
		float input = input_data[i];
		float output = output_data[i];
		float expected_output = input * input;

		if (output != expected_output)
			igt_debug("[%4d] input:%f output:%f expected_output:%f\n",
				  i, input, output, expected_output);
		if (!user || (user && !user->skip_results_check))
			igt_assert_eq_double(output, expected_output);
	}

	bo_execenv_unbind(&execenv, bo_dict, XEHPC_BO_DICT_ENTRIES);
	bo_execenv_destroy(&execenv);
}

static void xelpg_create_indirect_data(uint32_t *addr_bo_buffer_batch,
				      uint64_t addr_input,
				      uint64_t addr_output,
				      unsigned int loop_count)
{
	int b = 0;

	addr_bo_buffer_batch[b++] = addr_input & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_input >> 32;
	addr_bo_buffer_batch[b++] = addr_output & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_output >> 32;
	addr_bo_buffer_batch[b++] = loop_count;
	addr_bo_buffer_batch[b++] = ENQUEUED_LOCAL_SIZE_X;
	addr_bo_buffer_batch[b++] = ENQUEUED_LOCAL_SIZE_Y;
	addr_bo_buffer_batch[b++] = ENQUEUED_LOCAL_SIZE_Z;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
}

static void xelpg_compute_exec_compute(uint32_t *addr_bo_buffer_batch,
				       uint64_t addr_general_state_base,
				       uint64_t addr_surface_state_base,
				       uint64_t addr_dynamic_state_base,
				       uint64_t addr_instruction_state_base,
				       uint64_t offset_indirect_data_start,
				       uint64_t kernel_start_pointer,
				       uint32_t work_size)
{
	int b = 0;

	igt_debug("general   state base: %"PRIx64"\n", addr_general_state_base);
	igt_debug("surface   state base: %"PRIx64"\n", addr_surface_state_base);
	igt_debug("dynamic   state base: %"PRIx64"\n", addr_dynamic_state_base);
	igt_debug("instruct   base addr: %"PRIx64"\n", addr_instruction_state_base);
	igt_debug("bindless   base addr: %"PRIx64"\n", addr_surface_state_base);
	igt_debug("offset indirect addr: %"PRIx64"\n", offset_indirect_data_start);
	igt_debug("kernel start pointer: %"PRIx64"\n", kernel_start_pointer);

	addr_bo_buffer_batch[b++] = GEN7_PIPELINE_SELECT | GEN9_PIPELINE_SELECTION_MASK |
				    PIPELINE_SELECT_GPGPU;

	addr_bo_buffer_batch[b++] = XEHP_STATE_COMPUTE_MODE;
	addr_bo_buffer_batch[b++] = 0x80000000;

	addr_bo_buffer_batch[b++] = XEHP_CFE_STATE | 0x4;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x03808800;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = MI_LOAD_REGISTER_IMM(1);
	addr_bo_buffer_batch[b++] = 0x00002580;
	addr_bo_buffer_batch[b++] = 0x00060002;

	addr_bo_buffer_batch[b++] = STATE_BASE_ADDRESS | 0x14;
	addr_bo_buffer_batch[b++] = (addr_general_state_base & 0xffffffff) | 0x21;
	addr_bo_buffer_batch[b++] = addr_general_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x00028000;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0x21;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = (addr_dynamic_state_base & 0xffffffff) | 0x21;
	addr_bo_buffer_batch[b++] = addr_dynamic_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = (addr_instruction_state_base & 0xffffffff) | 0x21;
	addr_bo_buffer_batch[b++] = addr_instruction_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0xfffff001;
	addr_bo_buffer_batch[b++] = 0x00010001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0xfffff001;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0x21;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x00007fbf;
	addr_bo_buffer_batch[b++] = 0x5E70F021;
	addr_bo_buffer_batch[b++] = 0x00007F6A;
	addr_bo_buffer_batch[b++] = 0x00010000;

	addr_bo_buffer_batch[b++] = GEN8_3DSTATE_BINDING_TABLE_POOL_ALLOC | 0x2;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0x2;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x001ff000;

	addr_bo_buffer_batch[b++] = XEHP_COMPUTE_WALKER | 0x25;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000040;
	addr_bo_buffer_batch[b++] = offset_indirect_data_start;
	addr_bo_buffer_batch[b++] = 0xbe040000;
	addr_bo_buffer_batch[b++] = 0xffffffff;
	addr_bo_buffer_batch[b++] = 0x000003ff;
	addr_bo_buffer_batch[b++] = size_thread_group_x(work_size);

	addr_bo_buffer_batch[b++] = THREAD_GROUP_Y;
	addr_bo_buffer_batch[b++] = THREAD_GROUP_Z;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = kernel_start_pointer;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00010080;
	addr_bo_buffer_batch[b++] = 0x0c000000 | THREADS_PER_GROUP;

	addr_bo_buffer_batch[b++] = 0x00000008;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00001087;
	addr_bo_buffer_batch[b++] = ADDR_BATCH;
	addr_bo_buffer_batch[b++] = ADDR_BATCH >> 32;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = ENQUEUED_LOCAL_SIZE_X;
	addr_bo_buffer_batch[b++] = ENQUEUED_LOCAL_SIZE_Y;
	addr_bo_buffer_batch[b++] = ENQUEUED_LOCAL_SIZE_Z;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = PIPE_CONTROL;
	addr_bo_buffer_batch[b++] = 0x00100000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = MI_BATCH_BUFFER_END;
}

static void xe2lpg_compute_exec_compute(uint32_t *addr_bo_buffer_batch,
					uint64_t addr_general_state_base,
					uint64_t addr_surface_state_base,
					uint64_t addr_dynamic_state_base,
					uint64_t addr_instruction_state_base,
					uint64_t addr_state_contect_data_base,
					uint64_t offset_indirect_data_start,
					uint64_t kernel_start_pointer,
					uint64_t sip_start_pointer,
					bool	 threadgroup_preemption,
					uint32_t work_size)
{
	int b = 0;

	igt_debug("general   state base: %"PRIx64"\n", addr_general_state_base);
	igt_debug("surface   state base: %"PRIx64"\n", addr_surface_state_base);
	igt_debug("dynamic   state base: %"PRIx64"\n", addr_dynamic_state_base);
	igt_debug("instruct   base addr: %"PRIx64"\n", addr_instruction_state_base);
	igt_debug("bindless   base addr: %"PRIx64"\n", addr_surface_state_base);
	igt_debug("state context data base addr: %"PRIx64"\n", addr_state_contect_data_base);
	igt_debug("offset indirect addr: %"PRIx64"\n", offset_indirect_data_start);
	igt_debug("kernel start pointer: %"PRIx64"\n", kernel_start_pointer);
	igt_debug("sip start pointer: %"PRIx64"\n", sip_start_pointer);

	addr_bo_buffer_batch[b++] = GEN7_PIPELINE_SELECT | GEN9_PIPELINE_SELECTION_MASK |
				    PIPELINE_SELECT_GPGPU;

	addr_bo_buffer_batch[b++] = XEHP_STATE_COMPUTE_MODE | 0x1;
	addr_bo_buffer_batch[b++] = 0xE0004000;
	addr_bo_buffer_batch[b++] = 0x00000000;

#define XE2_STATE_CONTEXT_DATA_BASE_ADDRESS ((3 << 29) | (0 << 27) | (1 << 24) | (11 << 16) | (1 << 0))
	addr_bo_buffer_batch[b++] = XE2_STATE_CONTEXT_DATA_BASE_ADDRESS;
	// Split into low and high 32 bits
	addr_bo_buffer_batch[b++] = addr_state_contect_data_base & 0xFFFFFFFF; // Mask the low 32 bits ;
	addr_bo_buffer_batch[b++] = (addr_state_contect_data_base >> 32) & 0xFFFFFFFF;

	addr_bo_buffer_batch[b++] = XEHP_CFE_STATE | 0x4;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x03808800;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = STATE_BASE_ADDRESS | 0x14;
	addr_bo_buffer_batch[b++] = (addr_general_state_base & 0xffffffff) | 0x21;
	addr_bo_buffer_batch[b++] = addr_general_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x0002C000;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0x21;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = (addr_dynamic_state_base & 0xffffffff) | 0x21;
	addr_bo_buffer_batch[b++] = addr_dynamic_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = (addr_instruction_state_base & 0xffffffff) | 0x21;
	addr_bo_buffer_batch[b++] = addr_instruction_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0xfffff001;
	addr_bo_buffer_batch[b++] = 0x00010001;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0xfffff001;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0x21;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x00007fbe;
	addr_bo_buffer_batch[b++] = 0x00000021;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = GEN8_3DSTATE_BINDING_TABLE_POOL_ALLOC | 2;
	addr_bo_buffer_batch[b++] = (addr_surface_state_base & 0xffffffff) | 0x2;
	addr_bo_buffer_batch[b++] = addr_surface_state_base >> 32;
	addr_bo_buffer_batch[b++] = 0x001ff000;

	if (sip_start_pointer) {
		addr_bo_buffer_batch[b++] = XE2_STATE_SIP | 0x1;
		addr_bo_buffer_batch[b++] = sip_start_pointer;
		addr_bo_buffer_batch[b++] = 0x00000000;
	}

	addr_bo_buffer_batch[b++] = XEHP_COMPUTE_WALKER | 0x26;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000040;
	addr_bo_buffer_batch[b++] = offset_indirect_data_start;
	addr_bo_buffer_batch[b++] = 0xbe040000;
	addr_bo_buffer_batch[b++] = 0xffffffff;
	addr_bo_buffer_batch[b++] = 0x000003ff; // Local X/Y/Z Dimension

	if (threadgroup_preemption)
		/*
		 * Create multiple threadgroups using higher gloabl workgroup size
		 * Global Workgroup size = Local X * Thread Group X +  Local Y * Thread Group Y + Local Z * Thread Group Z
		 */
		addr_bo_buffer_batch[b++] = 0x00200000; // Thread Group ID X Dimension
	else
		addr_bo_buffer_batch[b++] = size_thread_group_x(work_size);

	addr_bo_buffer_batch[b++] = THREAD_GROUP_Y;
	addr_bo_buffer_batch[b++] = THREAD_GROUP_Z;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = kernel_start_pointer;
	addr_bo_buffer_batch[b++] = 0x00000000;

	if (threadgroup_preemption)
		addr_bo_buffer_batch[b++] = 0x00000000;
	else
		addr_bo_buffer_batch[b++] = 0x00100000; // Enable Mid Thread Preemption BitField:20

	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x0c000000 | THREADS_PER_GROUP;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00001047;
	addr_bo_buffer_batch[b++] = ADDR_BATCH;
	addr_bo_buffer_batch[b++] = ADDR_BATCH >> 32;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = ENQUEUED_LOCAL_SIZE_X;
	addr_bo_buffer_batch[b++] = ENQUEUED_LOCAL_SIZE_Y;
	addr_bo_buffer_batch[b++] = ENQUEUED_LOCAL_SIZE_Z;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;

	addr_bo_buffer_batch[b++] = MI_BATCH_BUFFER_END;
}

static void xe2_create_indirect_data_inc_kernel(uint32_t *addr_bo_buffer_batch,
						uint64_t addr_input,
						uint64_t addr_output,
						unsigned int loop_count)
{
	int b = 0;

	addr_bo_buffer_batch[b++] = addr_input & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_input >> 32;
	addr_bo_buffer_batch[b++] = addr_output & 0xffffffff;
	addr_bo_buffer_batch[b++] = addr_output >> 32;
	addr_bo_buffer_batch[b++] = loop_count;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = ENQUEUED_LOCAL_SIZE_X;
	addr_bo_buffer_batch[b++] = ENQUEUED_LOCAL_SIZE_Y;
	addr_bo_buffer_batch[b++] = ENQUEUED_LOCAL_SIZE_Z;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
	addr_bo_buffer_batch[b++] = 0x00000000;
}

/**
 * xelpg_compute_exec - run a pipeline compatible with MTL
 *
 * @fd: file descriptor of the opened DRM device
 * @kernel: GPU Kernel binary to be executed
 * @size: size of @kernel.
 * @eci: xelpg engine class instance if device is MTL
 * @user: user-provided execution environment
 */
static void xelpg_compute_exec(int fd, const unsigned char *kernel,
				unsigned int size,
			       struct drm_xe_engine_class_instance *eci,
			       struct user_execenv *user)
{
#define XELPG_BO_DICT_ENTRIES 9
	struct bo_dict_entry bo_dict[XELPG_BO_DICT_ENTRIES] = {
		{ .addr = ADDR_INSTRUCTION_STATE_BASE + OFFSET_KERNEL,
		  .name = "instr state base"},
		{ .addr = ADDR_DYNAMIC_STATE_BASE,
		  .size = SIZE_DYNAMIC_STATE,
		  .name = "dynamic state base"},
		{ .addr = ADDR_SURFACE_STATE_BASE,
		  .size = SIZE_SURFACE_STATE,
		  .name = "surface state base"},
		{ .addr = ADDR_GENERAL_STATE_BASE + OFFSET_INDIRECT_DATA_START,
		  .size = SIZE_INDIRECT_OBJECT,
		  .name = "indirect object base"},
		{ .addr = ADDR_INPUT,
		  .name = "addr input"},
		{ .addr = ADDR_OUTPUT,
		  .name = "addr output" },
		{ .addr = ADDR_GENERAL_STATE_BASE,
		  .size = SIZE_GENERAL_STATE,
		  .name = "general state base" },
		{ .addr = ADDR_BINDING_TABLE,
		  .size = SIZE_BINDING_TABLE,
		  .name = "binding table" },
		{ .addr = ADDR_BATCH,
		  .size = SIZE_BATCH,
		  .name = "batch" },
	};

	struct bo_execenv execenv;
	float *input_data, *output_data;
	uint64_t bind_input_addr = (user && user->input_addr) ? user->input_addr : ADDR_INPUT;
	uint64_t bind_output_addr = (user && user->output_addr) ? user->output_addr : ADDR_OUTPUT;

	bo_execenv_create(fd, &execenv, eci, user);

	/* Set dynamic sizes */
	bo_dict[0].size = ALIGN(size, 0x10000);
	bo_dict[4].size = size_input(execenv.array_size);
	bo_dict[5].size = size_output(execenv.array_size);

	bo_execenv_bind(&execenv, bo_dict, XELPG_BO_DICT_ENTRIES);

	memcpy(bo_dict[0].data, kernel, size);

	create_dynamic_state(bo_dict[1].data, OFFSET_KERNEL);
	xehp_create_surface_state(bo_dict[2].data, bind_input_addr, bind_output_addr);
	xelpg_create_indirect_data(bo_dict[3].data, bind_input_addr, bind_output_addr,
				   execenv.array_size);
	xehp_create_surface_state(bo_dict[7].data, bind_input_addr, bind_output_addr);

	if (user && user->input_addr) {
		input_data = from_user_pointer(user->input_addr);
	} else {
		input_data = (float *) bo_dict[4].data;
		srand(time(NULL));

		for (int i = 0; i < execenv.array_size; i++)
			input_data[i] = rand() / (float)RAND_MAX;
	}

	if (user && user->output_addr)
		output_data = from_user_pointer(user->output_addr);
	else
		output_data = (float *) bo_dict[5].data;

	xelpg_compute_exec_compute(bo_dict[8].data,
				   ADDR_GENERAL_STATE_BASE,
				   ADDR_SURFACE_STATE_BASE,
				   ADDR_DYNAMIC_STATE_BASE,
				   ADDR_INSTRUCTION_STATE_BASE,
				   OFFSET_INDIRECT_DATA_START,
				   OFFSET_KERNEL,
				   execenv.array_size);

	bo_execenv_exec(&execenv, ADDR_BATCH);

	for (int i = 0; i < execenv.array_size; i++) {
		float input = input_data[i];
		float output = output_data[i];
		float expected_output = input * input;

		if (output != expected_output)
			igt_debug("[%4d] input:%f output:%f expected_output:%f\n",
				  i, input, output, expected_output);
		if (!user || (user && !user->skip_results_check))
			igt_assert_eq_double(output, expected_output);
	}

	bo_execenv_unbind(&execenv, bo_dict, XELPG_BO_DICT_ENTRIES);
	bo_execenv_destroy(&execenv);
}

/**
 * xe2lpg_compute_exec - run a pipeline compatible with XE2
 *
 * @fd: file descriptor of the opened DRM device
 * @kernel: GPU Kernel binary to be executed
 * @size: size of @kernel.
 * @user: user-provided execution environment
 */
static void xe2lpg_compute_exec(int fd, const unsigned char *kernel,
				unsigned int size,
				struct drm_xe_engine_class_instance *eci,
				struct user_execenv *user)
{
#define XE2_BO_DICT_ENTRIES 10
	struct bo_dict_entry bo_dict[XE2_BO_DICT_ENTRIES] = {
		{ .addr = ADDR_INSTRUCTION_STATE_BASE + OFFSET_KERNEL,
		  .name = "instr state base"},
		{ .addr = ADDR_DYNAMIC_STATE_BASE,
		  .size = SIZE_DYNAMIC_STATE,
		  .name = "dynamic state base"},
		{ .addr = ADDR_SURFACE_STATE_BASE,
		  .size = SIZE_SURFACE_STATE,
		  .name = "surface state base"},
		{ .addr = ADDR_GENERAL_STATE_BASE + OFFSET_INDIRECT_DATA_START,
		  .size = SIZE_INDIRECT_OBJECT,
		  .name = "indirect object base"},
		{ .addr = ADDR_INPUT,
		  .name = "addr input"},
		{ .addr = ADDR_OUTPUT,
		  .name = "addr output" },
		{ .addr = ADDR_GENERAL_STATE_BASE,
		  .size = SIZE_GENERAL_STATE,
		  .name = "general state base" },
		{ .addr = ADDR_BINDING_TABLE,
		  .size = SIZE_BINDING_TABLE,
		  .name = "binding table" },
		{ .addr = ADDR_BATCH,
		  .size = SIZE_BATCH,
		  .name = "batch" },
		{ .addr = XE2_ADDR_STATE_CONTEXT_DATA_BASE,
		  .size = 0x10000,
		  .name = "state context data base"},
	};

	struct bo_execenv execenv;
	float *input_data, *output_data;
	uint64_t bind_input_addr = (user && user->input_addr) ? user->input_addr : ADDR_INPUT;
	uint64_t bind_output_addr = (user && user->output_addr) ? user->output_addr : ADDR_OUTPUT;

	bo_execenv_create(fd, &execenv, eci, user);

	/* Set dynamic sizes */
	bo_dict[0].size = ALIGN(size, 0x1000);
	bo_dict[4].size = size_input(execenv.array_size);
	bo_dict[5].size = size_output(execenv.array_size);

	bo_execenv_bind(&execenv, bo_dict, XE2_BO_DICT_ENTRIES);

	memcpy(bo_dict[0].data, kernel, size);
	create_dynamic_state(bo_dict[1].data, OFFSET_KERNEL);
	xehp_create_surface_state(bo_dict[2].data, bind_input_addr, bind_output_addr);
	xelpg_create_indirect_data(bo_dict[3].data, bind_input_addr, bind_output_addr,
				   execenv.array_size);
	xehp_create_surface_state(bo_dict[7].data, bind_input_addr, bind_output_addr);

	if (user && user->input_addr) {
		input_data = from_user_pointer(user->input_addr);
	} else {
		input_data = (float *) bo_dict[4].data;
		srand(time(NULL));

		for (int i = 0; i < execenv.array_size; i++)
			input_data[i] = rand() / (float)RAND_MAX;
	}

	if (user && user->output_addr)
		output_data = from_user_pointer(user->output_addr);
	else
		output_data = (float *) bo_dict[5].data;

	xe2lpg_compute_exec_compute(bo_dict[8].data,
				    ADDR_GENERAL_STATE_BASE,
				    ADDR_SURFACE_STATE_BASE,
				    ADDR_DYNAMIC_STATE_BASE,
				    ADDR_INSTRUCTION_STATE_BASE,
				    XE2_ADDR_STATE_CONTEXT_DATA_BASE,
				    OFFSET_INDIRECT_DATA_START,
				    OFFSET_KERNEL, 0, false,
				    execenv.array_size);

	bo_execenv_exec(&execenv, ADDR_BATCH);

	for (int i = 0; i < execenv.array_size; i++) {
		float input = input_data[i];
		float output = output_data[i];
		float expected_output = input * input;

		if (output != expected_output)
			igt_debug("[%4d] input:%f output:%f expected_output:%f\n",
				  i, input, output, expected_output);
		if (!user || (user && !user->skip_results_check))
			igt_assert_eq_double(output, expected_output);
	}

	bo_execenv_unbind(&execenv, bo_dict, XE2_BO_DICT_ENTRIES);
	bo_execenv_destroy(&execenv);
}

/*
 * Compatibility flags.
 *
 * There will be some time period in which both drivers (i915 and xe)
 * will support compute runtime tests. Lets define compat flags to allow
 * the code to be shared between two drivers allowing disabling this in
 * the future.
 */
#define COMPAT_DRIVER_FLAG(f) (1 << (f))
#define COMPAT_DRIVER_I915 COMPAT_DRIVER_FLAG(INTEL_DRIVER_I915)
#define COMPAT_DRIVER_XE   COMPAT_DRIVER_FLAG(INTEL_DRIVER_XE)

static const struct {
	unsigned int ip_ver;
	void (*compute_exec)(int fd, const unsigned char *kernel,
			     unsigned int size,
			     struct drm_xe_engine_class_instance *eci,
			     struct user_execenv *user);
	uint32_t compat;
} intel_compute_batches[] = {
	{
		.ip_ver = IP_VER(12, 0),
		.compute_exec = compute_exec,
		.compat = COMPAT_DRIVER_I915 | COMPAT_DRIVER_XE,
	},
	{
		.ip_ver = IP_VER(12, 10),
		.compute_exec = compute_exec,
		.compat = COMPAT_DRIVER_I915,
	},
	{
		.ip_ver = IP_VER(12, 55),
		.compute_exec = xehp_compute_exec,
		.compat = COMPAT_DRIVER_I915 | COMPAT_DRIVER_XE,
	},
	{
		.ip_ver = IP_VER(12, 60),
		.compute_exec = xehpc_compute_exec,
		.compat = COMPAT_DRIVER_XE,
	},
	{
		.ip_ver = IP_VER(12, 70),
		.compute_exec = xelpg_compute_exec,
		.compat = COMPAT_DRIVER_I915 | COMPAT_DRIVER_XE,
	},
	{
		.ip_ver = IP_VER(20, 01),
		.compute_exec = xe2lpg_compute_exec,
		.compat = COMPAT_DRIVER_XE,
	},
	{
		.ip_ver = IP_VER(20, 04),
		.compute_exec = xe2lpg_compute_exec,
		.compat = COMPAT_DRIVER_XE,
	},
	{
		.ip_ver = IP_VER(30, 00),
		.compute_exec = xe2lpg_compute_exec,
		.compat = COMPAT_DRIVER_XE,
	},
};

static bool __run_intel_compute_kernel(int fd,
				       struct drm_xe_engine_class_instance *eci,
				       struct user_execenv *user)
{
	unsigned int ip_ver = intel_graphics_ver(intel_get_drm_devid(fd));
	unsigned int batch;
	const struct intel_compute_kernels *kernels = intel_compute_square_kernels;
	enum intel_driver driver = get_intel_driver(fd);
	const unsigned char *kernel;
	unsigned int kernel_size;

	for (batch = 0; batch < ARRAY_SIZE(intel_compute_batches); batch++) {
		if (ip_ver == intel_compute_batches[batch].ip_ver)
			break;
	}
	if (batch == ARRAY_SIZE(intel_compute_batches)) {
		igt_debug("GPU version 0x%x not supported\n", ip_ver);
		return false;
	}

	if (!(COMPAT_DRIVER_FLAG(driver) & intel_compute_batches[batch].compat)) {
		igt_debug("Driver is not supported: flags %x & %x\n",
			  COMPAT_DRIVER_FLAG(driver),
			  intel_compute_batches[batch].compat);
		return false;
	}

	/* If the user provides a kernel, use it */
	if (user && user->kernel) {
		kernel = user->kernel;
		kernel_size = user->kernel_size;
	} else {
		while (kernels->kernel) {
			if (ip_ver == kernels->ip_ver)
				break;
			kernels++;
		}
		if (!kernels->kernel)
			return false;
		kernel = kernels->kernel;
		kernel_size = kernels->size;
	}

	intel_compute_batches[batch].compute_exec(fd, kernel,
						  kernel_size, eci, user);

	return true;
}

bool run_intel_compute_kernel(int fd, struct user_execenv *user)
{
	return __run_intel_compute_kernel(fd, NULL, user);
}

/**
 * xe_run_intel_compute_kernel_on_engine - runs compute kernel on specified
 * engine on Xe device.
 *
 * @fd: file descriptor of the opened DRM Xe device
 * @eci: Xe engine class instance
 * @user: user-provided execution environment
 *
 * Returns true on success, false otherwise.
 */
bool xe_run_intel_compute_kernel_on_engine(int fd,
					   struct drm_xe_engine_class_instance *eci,
					   struct user_execenv *user)
{
	if (!is_xe_device(fd)) {
		igt_debug("Xe device expected\n");
		return false;
	}

	if (eci == NULL) {
		igt_debug("No engine specified\n");
		return false;
	}

	if (eci->engine_class != DRM_XE_ENGINE_CLASS_COMPUTE &&
	    eci->engine_class != DRM_XE_ENGINE_CLASS_RENDER) {
		igt_debug("%s engine class not supported\n",
			  xe_engine_class_string(eci->engine_class));
		return false;
	}

	return __run_intel_compute_kernel(fd, eci, user);
}

/**
 * xe2lpg_compute_preempt_exec - run a pipeline compatible with XE2 and
 * submit long and short kernels for preemption occurrence.
 *
 * @fd: file descriptor of the opened DRM device
 * @long_kernel: GPU long kernel binary to be executed
 * @long_kernel_size: size of @long_kernel
 * @short_kernel: GPU short kernel binary to be executed
 * @short_kernel_size: size of @short_kernel
 * @sip_kernel: WMTP sip kernel which does save restore during preemption
 * @sip_kernel_size: size of @sip_kernel
 * @loop_kernel: loop kernel binary stoppable by cpu write
 * @loop_kernel_size: size of @loop_kernel
 */
static void xe2lpg_compute_preempt_exec(int fd, const unsigned char *long_kernel,
					unsigned int long_kernel_size,
					const unsigned char *short_kernel,
					unsigned int short_kernel_size,
					const unsigned char *sip_kernel,
					unsigned int sip_kernel_size,
					const unsigned char *loop_kernel,
					unsigned int loop_kernel_size,
					struct drm_xe_engine_class_instance *eci,
					bool threadgroup_preemption)
{
#define XE2_BO_PREEMPT_DICT_ENTRIES 11
	struct bo_dict_entry bo_dict_long[XE2_BO_PREEMPT_DICT_ENTRIES] = {
		{ .addr = ADDR_INSTRUCTION_STATE_BASE + OFFSET_KERNEL,
		  .name = "instr state base"},
		{ .addr = ADDR_DYNAMIC_STATE_BASE,
		  .size = SIZE_DYNAMIC_STATE,
		  .name = "dynamic state base"},
		{ .addr = ADDR_SURFACE_STATE_BASE,
		  .size = SIZE_SURFACE_STATE,
		  .name = "surface state base"},
		{ .addr = ADDR_GENERAL_STATE_BASE + OFFSET_INDIRECT_DATA_START,
		  .size = SIZE_INDIRECT_OBJECT,
		  .name = "indirect object base"},
		{ .addr = ADDR_INPUT, .size = MAX(sizeof(float) * SIZE_DATA, 0x10000),
		  .name = "addr input"},
		{ .addr = ADDR_OUTPUT, .size = MAX(sizeof(float) * SIZE_DATA, 0x10000),
		  .name = "addr output" },
		{ .addr = ADDR_GENERAL_STATE_BASE,
		  .size = SIZE_GENERAL_STATE,
		  .name = "general state base" },
		{ .addr = ADDR_BINDING_TABLE,
		  .size = SIZE_BINDING_TABLE,
		  .name = "binding table" },
		{ .addr = ADDR_BATCH,
		  .size = SIZE_BATCH,
		  .name = "batch" },
		{ .addr = XE2_ADDR_STATE_CONTEXT_DATA_BASE,
		  .size = 0x6400000,
		  .name = "state context data base"},
		{ .addr = ADDR_INSTRUCTION_STATE_BASE + OFFSET_STATE_SIP,
		  .name = "sip kernel"},
	};

	struct bo_dict_entry bo_dict_short[XE2_BO_PREEMPT_DICT_ENTRIES];
	struct bo_execenv execenv_short, execenv_long;
	float *input_data, *output_data;
	unsigned int long_kernel_loop_count;
	struct drm_xe_sync sync_long = {
		.type = DRM_XE_SYNC_TYPE_USER_FENCE,
		.flags = DRM_XE_SYNC_FLAG_SIGNAL,
		.timeline_value = USER_FENCE_VALUE,
	};
	struct bo_sync *bo_sync_long;
	size_t bo_size_long = sizeof(*bo_sync_long);
	uint32_t bo_long = 0;
	struct drm_xe_sync sync_short = {
		.type = DRM_XE_SYNC_TYPE_USER_FENCE,
		.flags = DRM_XE_SYNC_FLAG_SIGNAL,
		.timeline_value = USER_FENCE_VALUE,
	};
	struct bo_sync *bo_sync_short;
	size_t bo_size_short = sizeof(*bo_sync_short);
	uint32_t bo_short = 0;
	int64_t timeout_short = 1;
	bool use_loop_kernel = loop_kernel && !threadgroup_preemption;

	if (threadgroup_preemption)
		long_kernel_loop_count = TGP_long_kernel_loop_count;
	else
		long_kernel_loop_count = WMTP_long_kernel_loop_count;

	for (int i = 0; i < XE2_BO_PREEMPT_DICT_ENTRIES; ++i)
		bo_dict_short[i] = bo_dict_long[i];

	bo_execenv_create(fd, &execenv_short, eci, NULL);
	bo_execenv_create(fd, &execenv_long, eci, NULL);

	/* Prepare sync object for long */
	bo_size_long = xe_bb_size(fd, bo_size_long);
	bo_long = xe_bo_create(fd, execenv_long.vm, bo_size_long, vram_if_possible(fd, 0),
			       DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
	bo_sync_long = xe_bo_map(fd, bo_long, bo_size_long);
	sync_long.addr = to_user_pointer(&bo_sync_long->sync);
	xe_vm_bind_async(fd, execenv_long.vm, 0, bo_long, 0, ADDR_SYNC, bo_size_long,
			 &sync_long, 1);
	xe_wait_ufence(fd, &bo_sync_long->sync, USER_FENCE_VALUE, execenv_long.exec_queue,
		       INT64_MAX);
	bo_sync_long->sync = 0;
	sync_long.addr = ADDR_SYNC;

	/* Prepare sync object for short */
	bo_size_short = xe_bb_size(fd, bo_size_short);
	bo_short = xe_bo_create(fd, execenv_short.vm, bo_size_short, vram_if_possible(fd, 0),
			       DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
	bo_sync_short = xe_bo_map(fd, bo_short, bo_size_short);
	sync_short.addr = to_user_pointer(&bo_sync_short->sync);
	xe_vm_bind_async(fd, execenv_short.vm, 0, bo_short, 0, ADDR_SYNC2, bo_size_short,
			 &sync_short, 1);
	xe_wait_ufence(fd, &bo_sync_short->sync, USER_FENCE_VALUE, execenv_short.exec_queue,
		       INT64_MAX);
	bo_sync_short->sync = 0;
	sync_short.addr = ADDR_SYNC2;

	if (use_loop_kernel)
		bo_dict_long[0].size = ALIGN(loop_kernel_size, 0x1000);
	else
		bo_dict_long[0].size = ALIGN(long_kernel_size, 0x1000);
	bo_dict_short[0].size = ALIGN(short_kernel_size, 0x1000);

	bo_dict_long[10].size = ALIGN(sip_kernel_size, 0x1000);
	bo_dict_short[10].size = ALIGN(sip_kernel_size, 0x1000);

	bo_execenv_bind(&execenv_long, bo_dict_long, XE2_BO_PREEMPT_DICT_ENTRIES);
	bo_execenv_bind(&execenv_short, bo_dict_short, XE2_BO_PREEMPT_DICT_ENTRIES);

	if (use_loop_kernel)
		memcpy(bo_dict_long[0].data, loop_kernel, loop_kernel_size);
	else
		memcpy(bo_dict_long[0].data, long_kernel, long_kernel_size);
	memcpy(bo_dict_short[0].data, short_kernel, short_kernel_size);

	memcpy(bo_dict_long[10].data, sip_kernel, sip_kernel_size);
	memcpy(bo_dict_short[10].data, sip_kernel, sip_kernel_size);

	create_dynamic_state(bo_dict_long[1].data, OFFSET_KERNEL);
	xehp_create_surface_state(bo_dict_long[2].data, ADDR_INPUT, ADDR_OUTPUT);
	xe2_create_indirect_data_inc_kernel(bo_dict_long[3].data, ADDR_INPUT, ADDR_OUTPUT,
					    long_kernel_loop_count);
	xehp_create_surface_state(bo_dict_long[7].data, ADDR_INPUT, ADDR_OUTPUT);

	create_dynamic_state(bo_dict_short[1].data, OFFSET_KERNEL);
	xehp_create_surface_state(bo_dict_short[2].data, ADDR_INPUT, ADDR_OUTPUT);
	xelpg_create_indirect_data(bo_dict_short[3].data, ADDR_INPUT, ADDR_OUTPUT, SIZE_DATA);
	xehp_create_surface_state(bo_dict_short[7].data, ADDR_INPUT, ADDR_OUTPUT);

	input_data = (float *) bo_dict_long[4].data;
	output_data = (float *) bo_dict_short[5].data;
	srand(time(NULL));

	for (int i = 0; i < SIZE_DATA; i++)
		input_data[i] = rand() / (float)RAND_MAX;

	input_data = (float *) bo_dict_short[4].data;

	for (int i = 0; i < SIZE_DATA; i++)
		input_data[i] = rand() / (float)RAND_MAX;

	xe2lpg_compute_exec_compute(bo_dict_long[8].data, ADDR_GENERAL_STATE_BASE,
				    ADDR_SURFACE_STATE_BASE, ADDR_DYNAMIC_STATE_BASE,
				    ADDR_INSTRUCTION_STATE_BASE, XE2_ADDR_STATE_CONTEXT_DATA_BASE,
				    OFFSET_INDIRECT_DATA_START, OFFSET_KERNEL, OFFSET_STATE_SIP,
				    threadgroup_preemption, SIZE_DATA);

	xe2lpg_compute_exec_compute(bo_dict_short[8].data, ADDR_GENERAL_STATE_BASE,
				    ADDR_SURFACE_STATE_BASE, ADDR_DYNAMIC_STATE_BASE,
				    ADDR_INSTRUCTION_STATE_BASE, XE2_ADDR_STATE_CONTEXT_DATA_BASE,
				    OFFSET_INDIRECT_DATA_START, OFFSET_KERNEL, OFFSET_STATE_SIP,
				    false, SIZE_DATA);

	xe_exec_sync(fd, execenv_long.exec_queue, ADDR_BATCH, &sync_long, 1);

	/* Wait until multiple LR jobs will start to occupy gpu */
	if (use_loop_kernel)
		sleep(1);

	xe_exec_sync(fd, execenv_short.exec_queue, ADDR_BATCH, &sync_short, 1);

	xe_wait_ufence(fd, &bo_sync_short->sync, USER_FENCE_VALUE, execenv_short.exec_queue,
		       INT64_MAX);

	/* Check that the long kernel has not completed yet */
	igt_assert_neq(0, __xe_wait_ufence(fd, &bo_sync_long->sync, USER_FENCE_VALUE,
					   execenv_long.exec_queue, &timeout_short));
	if (use_loop_kernel)
		((int *)bo_dict_long[4].data)[0] = MAGIC_LOOP_STOP;

	xe_wait_ufence(fd, &bo_sync_long->sync, USER_FENCE_VALUE, execenv_long.exec_queue,
		       INT64_MAX);

	munmap(bo_sync_long, bo_size_long);
	gem_close(fd, bo_long);

	munmap(bo_sync_short, bo_size_short);
	gem_close(fd, bo_short);

	for (int i = use_loop_kernel ? 1 : 0; i < SIZE_DATA; i++) {
		float input = input_data[i];
		float output = output_data[i];
		float expected_output = input * input;

		if (output != expected_output)
			igt_debug("[%4d] input:%f output:%f expected_output:%f\n",
				  i, input, output, expected_output);
		igt_assert_eq_double(output, expected_output);
	}

	for (int i = 0; i < SIZE_DATA; i++) {
		float f1;

		f1 = ((float *) bo_dict_long[5].data)[i];

		if (threadgroup_preemption) {
			if (f1 < long_kernel_loop_count)
				igt_debug("[%4d] f1: %f != %u\n", i, f1, long_kernel_loop_count);

			/* Final incremented value should be greater than loop count
			 * as the kernel is ran by multiple threads and output variable
			 * is shared among all threads. This enusres multiple threadgroup
			 * workload execution
			 */
			igt_assert(f1 > long_kernel_loop_count);
		} else {
			if (!loop_kernel) {
				if (f1 != long_kernel_loop_count)
					igt_debug("[%4d] f1: %f != %u\n", i, f1, long_kernel_loop_count);
				igt_assert(f1 == long_kernel_loop_count);
			}
		}
	}

	bo_execenv_unbind(&execenv_short, bo_dict_short, XE2_BO_PREEMPT_DICT_ENTRIES);
	bo_execenv_unbind(&execenv_long, bo_dict_long, XE2_BO_PREEMPT_DICT_ENTRIES);

	bo_execenv_destroy(&execenv_short);
	bo_execenv_destroy(&execenv_long);
}

static const struct {
	unsigned int ip_ver;
	void (*compute_exec)(int fd, const unsigned char *long_kernel,
			     unsigned int long_kernel_size,
			     const unsigned char *short_kernel,
			     unsigned int short_kernel_size,
			     const unsigned char *sip_kernel,
			     unsigned int sip_kernel_size,
			     const unsigned char *loop_kernel,
			     unsigned int loop_kernel_size,
			     struct drm_xe_engine_class_instance *eci,
			     bool threadgroup_preemption);
	uint32_t compat;
} intel_compute_preempt_batches[] = {
	{
		.ip_ver = IP_VER(20, 01),
		.compute_exec = xe2lpg_compute_preempt_exec,
		.compat = COMPAT_DRIVER_XE,
	},
	{
		.ip_ver = IP_VER(20, 04),
		.compute_exec = xe2lpg_compute_preempt_exec,
		.compat = COMPAT_DRIVER_XE,
	},
	{
		.ip_ver = IP_VER(30, 00),
		.compute_exec = xe2lpg_compute_preempt_exec,
		.compat = COMPAT_DRIVER_XE,
	},
};

static bool __run_intel_compute_kernel_preempt(int fd,
		struct drm_xe_engine_class_instance *eci,
		bool threadgroup_preemption)
{
	unsigned int ip_ver = intel_graphics_ver(intel_get_drm_devid(fd));
	unsigned int batch;
	const struct intel_compute_kernels *kernels = intel_compute_square_kernels;
	enum intel_driver driver = get_intel_driver(fd);

	for (batch = 0; batch < ARRAY_SIZE(intel_compute_preempt_batches); batch++)
		if (ip_ver == intel_compute_preempt_batches[batch].ip_ver)
			break;


	if (batch == ARRAY_SIZE(intel_compute_preempt_batches)) {
		igt_debug("GPU version 0x%x not supported\n", ip_ver);
		return false;
	}

	if (!(COMPAT_DRIVER_FLAG(driver) & intel_compute_preempt_batches[batch].compat)) {
		igt_debug("Driver is not supported: flags %x & %x\n",
			  COMPAT_DRIVER_FLAG(driver),
			  intel_compute_preempt_batches[batch].compat);
		return false;
	}

	while (kernels->kernel) {
		if (ip_ver == kernels->ip_ver)
			break;
		kernels++;
	}

	if (!kernels->kernel || !kernels->sip_kernel || !kernels->long_kernel)
		return 0;

	intel_compute_preempt_batches[batch].compute_exec(fd, kernels->long_kernel,
							  kernels->long_kernel_size,
							  kernels->kernel, kernels->size,
							  kernels->sip_kernel,
							  kernels->sip_kernel_size,
							  kernels->loop_kernel,
							  kernels->loop_kernel_size,
							  eci,
							  threadgroup_preemption);

	return true;
}
/**
 * run_intel_compute_kernel_preempt - runs compute kernels to
 * exercise preemption scenario.
 *
 * @fd: file descriptor of the opened DRM Xe device
 * @eci: engine class instance
 * @thread_preemption: enable/disable threadgroup preemption test
 *
 * Returns true on success, false otherwise.
 */
bool run_intel_compute_kernel_preempt(int fd,
		struct drm_xe_engine_class_instance *eci,
		bool threadgroup_preemption)
{
	return __run_intel_compute_kernel_preempt(fd, eci, threadgroup_preemption);
}
