/* SPDX-License-Identifier: MIT */
/*
* Copyright © 2023 Intel Corporation
*
* Authors:
*    Sai Gowtham Ch <sai.gowtham.ch@intel.com>
*/

#include "igt.h"
#include "lib/igt_syncobj.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"
#include "xe_drm.h"

/**
 * TEST: Tests to verify store dword functionality.
 * Category: Core
 * Mega feature: General Core features
 * Sub-category: CMD submission
 * Functionality: intel-bb
 * Test category: functionality test
 */

#define MAX_INSTANCE 9
#define STORE 0
#define COND_BATCH 1

struct data {
	uint32_t batch[16];
	uint64_t pad;
	uint32_t data;
	uint64_t addr;
};

static void store_dword_batch(struct data *data, uint64_t addr, int value)
{
	int b;
	uint64_t batch_offset = (char *)&(data->batch) - (char *)data;
	uint64_t batch_addr = addr + batch_offset;
	uint64_t sdi_offset = (char *)&(data->data) - (char *)data;
	uint64_t sdi_addr = addr + sdi_offset;

	b = 0;
	data->batch[b++] = MI_STORE_DWORD_IMM_GEN4;
	data->batch[b++] = sdi_addr;
	data->batch[b++] = sdi_addr >> 32;
	data->batch[b++] = value;
	data->batch[b++] = MI_BATCH_BUFFER_END;
	igt_assert(b <= ARRAY_SIZE(data->batch));

	data->addr = batch_addr;
}

static void cond_batch(struct data *data, uint64_t addr, int value,
		       uint16_t dev_id)
{
	int b;
	uint64_t batch_offset = (char *)&(data->batch) - (char *)data;
	uint64_t batch_addr = addr + batch_offset;
	uint64_t sdi_offset = (char *)&(data->data) - (char *)data;
	uint64_t sdi_addr = addr + sdi_offset;

	b = 0;
	data->batch[b++] = MI_ATOMIC | MI_ATOMIC_INC;
	data->batch[b++] = sdi_addr;
	data->batch[b++] = sdi_addr >> 32;

	if (intel_graphics_ver(dev_id) >= IP_VER(20, 0))
		data->batch[b++] = MI_MEM_FENCE | MI_WRITE_FENCE;

	data->batch[b++] = MI_CONDITIONAL_BATCH_BUFFER_END | MI_DO_COMPARE | 5 << 12 | 2;
	data->batch[b++] = value;
	data->batch[b++] = sdi_addr;
	data->batch[b++] = sdi_addr >> 32;
	data->batch[b++] = MI_BATCH_BUFFER_START | 1;
	data->batch[b++] = lower_32_bits(batch_addr);
	data->batch[b++] = upper_32_bits(batch_addr);
	igt_assert(b <= ARRAY_SIZE(data->batch));

	data->addr = batch_addr;
}

static void persistance_batch(struct data *data, uint64_t addr)
{
	int b;
	uint64_t batch_offset = (char *)&(data->batch) - (char *)data;
	uint64_t batch_addr = addr + batch_offset;
	uint64_t prt_offset = (char *)&(data->data) - (char *)data;
	uint64_t prt_addr = addr + prt_offset;

	b = 0;
	data->batch[b++] = MI_BATCH_BUFFER_START;
	data->batch[b++] = MI_PRT_BATCH_BUFFER_START;
	data->batch[b++] = prt_addr;
	data->batch[b++] = prt_addr >> 32;
	data->batch[b++] = MI_BATCH_BUFFER_END;

	data->addr = batch_addr;

}
/**
 * SUBTEST: basic-store
 * Description: Basic test to verify store dword.
 * SUBTEST: basic-cond-batch
 * Description: Basic test to verify cond batch end instruction.
 * SUBTEST: basic-all
 * Description: Test to verify store dword on all available engines.
 */
static void basic_inst(int fd, int inst_type, struct drm_xe_engine_class_instance *eci,
		       uint16_t dev_id)
{
	struct drm_xe_sync sync[2] = {
		{ .type = DRM_XE_SYNC_TYPE_SYNCOBJ, .flags = DRM_XE_SYNC_FLAG_SIGNAL, },
		{ .type = DRM_XE_SYNC_TYPE_SYNCOBJ, .flags = DRM_XE_SYNC_FLAG_SIGNAL, }
	};
	struct drm_xe_exec exec = {
		.num_batch_buffer = 1,
		.num_syncs = 2,
		.syncs = to_user_pointer(&sync),
	};
	struct data *data;
	uint32_t vm;
	uint32_t exec_queue;
	uint32_t bind_engine;
	uint32_t syncobj;
	size_t bo_size;
	int value = 0x123456;
	uint64_t addr = 0x100000;
	uint32_t bo = 0;

	syncobj = syncobj_create(fd, 0);
	sync[0].handle = syncobj_create(fd, 0);
	sync[1].handle = syncobj;

	vm = xe_vm_create(fd, 0, 0);
	bo_size = sizeof(*data);
	bo_size = xe_bb_size(fd, bo_size);

	bo = xe_bo_create(fd, vm, bo_size,
			  vram_if_possible(fd, eci->gt_id),
			  DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);

	exec_queue = xe_exec_queue_create(fd, vm, eci, 0);
	bind_engine = xe_bind_exec_queue_create(fd, vm, 0);
	xe_vm_bind_async(fd, vm, bind_engine, bo, 0, addr, bo_size, sync, 1);
	data = xe_bo_map(fd, bo, bo_size);

	if (inst_type == STORE)
		store_dword_batch(data, addr, value);
	else if (inst_type == COND_BATCH) {
		/* A random value where it stops at the below value. */
		value = 20 + random() % 10;
		cond_batch(data, addr, value, dev_id);
	}
	else
		igt_assert_f(inst_type < 2, "Entered wrong inst_type.\n");

	exec.exec_queue_id = exec_queue;
	exec.address = data->addr;
	sync[0].flags &= ~DRM_XE_SYNC_FLAG_SIGNAL;
	sync[1].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
	xe_exec(fd, &exec);

	igt_assert(syncobj_wait(fd, &syncobj, 1, INT64_MAX, 0, NULL));
	igt_assert_eq(data->data, value);

	syncobj_destroy(fd, sync[0].handle);
	syncobj_destroy(fd, syncobj);
	munmap(data, bo_size);
	gem_close(fd, bo);

	xe_exec_queue_destroy(fd, exec_queue);
	xe_vm_destroy(fd, vm);
}

#define PAGES 1
#define NCACHELINES (4096/64)
/**
 * SUBTEST: %s
 * Description: Verify that each engine can store a dword to different %arg[1] of a object.
 * Test category: functionality test
 *
 * arg[1]:
 *
 * @cachelines: cachelines
 * @page-sized: page-sized
 */
static void store_cachelines(int fd, struct drm_xe_engine_class_instance *eci,
			     unsigned int flags)
{
	struct drm_xe_sync sync[2] = {
		{ .type = DRM_XE_SYNC_TYPE_SYNCOBJ, .flags = DRM_XE_SYNC_FLAG_SIGNAL, },
		{ .type = DRM_XE_SYNC_TYPE_SYNCOBJ, .flags = DRM_XE_SYNC_FLAG_SIGNAL, }
	};

	struct drm_xe_exec exec = {
		.num_batch_buffer = 1,
		.num_syncs = 2,
		.syncs = to_user_pointer(&sync),
	};

	int count = flags & PAGES ? NCACHELINES + 1 : 2;
	int i, object_index, b = 0;
	uint64_t dst_offset[count];
	uint32_t exec_queues, vm, syncobjs;
	uint32_t bo[count], *bo_map[count];
	uint32_t value[NCACHELINES], *ptr[NCACHELINES], delta;
	uint64_t offset[NCACHELINES];
	uint64_t ahnd;
	uint32_t *batch_map;
	size_t bo_size = 4096;

	bo_size = xe_bb_size(fd, bo_size);
	vm = xe_vm_create(fd, 0, 0);
	ahnd = intel_allocator_open(fd, 0, INTEL_ALLOCATOR_SIMPLE);
	exec_queues = xe_exec_queue_create(fd, vm, eci, 0);
	syncobjs = syncobj_create(fd, 0);
	sync[0].handle = syncobj_create(fd, 0);

	for (i = 0; i < count; i++) {
		bo[i] = xe_bo_create(fd, vm, bo_size,
				     vram_if_possible(fd, eci->gt_id),
				     DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
		bo_map[i] = xe_bo_map(fd, bo[i], bo_size);
		dst_offset[i] = intel_allocator_alloc_with_strategy(ahnd, bo[i],
								    bo_size, 0,
								    ALLOC_STRATEGY_LOW_TO_HIGH);
		xe_vm_bind_async(fd, vm, 0, bo[i], 0, dst_offset[i], bo_size, sync, 1);
	}

	batch_map = xe_bo_map(fd, bo[i-1], bo_size);
	exec.address = dst_offset[i-1];

	for (unsigned int n = 0; n < NCACHELINES; n++) {
		delta = 4 * (n * 16 + n % 16);
		value[n] = n | ~n << 16;
		offset[n] = dst_offset[n % (count - 1)] + delta;

		batch_map[b++] = MI_STORE_DWORD_IMM_GEN4;
		batch_map[b++] = offset[n];
		batch_map[b++] = offset[n] >> 32;
		batch_map[b++] = value[n];
	}
	batch_map[b++] = MI_BATCH_BUFFER_END;
	sync[0].flags &= ~DRM_XE_SYNC_FLAG_SIGNAL;
	sync[1].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
	sync[1].handle = syncobjs;
	exec.exec_queue_id = exec_queues;
	xe_exec(fd, &exec);
	igt_assert(syncobj_wait(fd, &syncobjs, 1, INT64_MAX, 0, NULL));

	for (unsigned int n = 0; n < NCACHELINES; n++) {
		delta = 4 * (n * 16 + n % 16);
		value[n] = n | ~n << 16;
		object_index = n % (count - 1);
		ptr[n]  = bo_map[object_index] + delta / 4;

		igt_assert_eq_u32(*ptr[n], value[n]);
	}

	for (i = 0; i < count; i++) {
		munmap(bo_map[i], bo_size);
		gem_close(fd, bo[i]);
	}

	munmap(batch_map, bo_size);
	put_ahnd(ahnd);
	syncobj_destroy(fd, sync[0].handle);
	syncobj_destroy(fd, syncobjs);
	xe_exec_queue_destroy(fd, exec_queues);
	xe_vm_destroy(fd, vm);
}

/**
 * SUBTEST: persistent
 * DESCRIPTION: Validate MI_PRT_BATCH_BUFFER_START functionality
 */
static void persistent(int fd)
{
	struct drm_xe_sync sync = {
		.type = DRM_XE_SYNC_TYPE_SYNCOBJ,
		.flags = DRM_XE_SYNC_FLAG_SIGNAL,
	};
	struct drm_xe_exec exec = {
		.num_batch_buffer = 1,
		.num_syncs = 1,
		.syncs = to_user_pointer(&sync),
	};
	struct data *sd_data;
	struct data *prt_data;
	struct drm_xe_engine *engine;
	uint32_t vm, exec_queue, syncobj;
	uint32_t sd_batch, prt_batch;
	uint64_t addr = 0x100000;
	int value = 0x123456;
	size_t batch_size = 4096;

	syncobj = syncobj_create(fd, 0);
	sync.handle = syncobj;

	vm = xe_vm_create(fd, 0, 0);
	batch_size = xe_bb_size(fd, batch_size);

	engine = xe_engine(fd, 1);
	sd_batch = xe_bo_create(fd, vm, batch_size,
			      vram_if_possible(fd, engine->instance.gt_id),
			      DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
	prt_batch = xe_bo_create(fd, vm, batch_size,
			      vram_if_possible(fd, engine->instance.gt_id),
			      DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);

	xe_vm_bind_sync(fd, vm, sd_batch, 0, addr, batch_size);
	sd_data = xe_bo_map(fd, sd_batch, batch_size);
	prt_data = xe_bo_map(fd, prt_batch, batch_size);

	store_dword_batch(sd_data, addr, value);
	persistance_batch(prt_data, addr);

	exec_queue = xe_exec_queue_create(fd, vm, &engine->instance, 0);
	exec.exec_queue_id = exec_queue;
	exec.address = prt_data->addr;
	sync.flags &= DRM_XE_SYNC_FLAG_SIGNAL;
	xe_exec(fd, &exec);

	igt_assert(syncobj_wait(fd, &syncobj, 1, INT64_MAX, 0, NULL));
	igt_assert_eq(sd_data->data, value);

	syncobj_destroy(fd, syncobj);
	munmap(sd_data, batch_size);
	munmap(prt_data, batch_size);
	gem_close(fd, sd_batch);
	gem_close(fd, prt_batch);

	xe_exec_queue_destroy(fd, exec_queue);
	xe_vm_destroy(fd, vm);
}

igt_main
{
	struct drm_xe_engine_class_instance *hwe;
	int fd;
	uint16_t dev_id;
	struct drm_xe_engine *engine;

	igt_fixture {
		fd = drm_open_driver(DRIVER_XE);
		xe_device_get(fd);
		dev_id = intel_get_drm_devid(fd);
	}

	igt_subtest("basic-store") {
		engine = xe_engine(fd, 1);
		basic_inst(fd, STORE, &engine->instance, dev_id);
	}

	igt_subtest("basic-cond-batch") {
		engine = xe_engine(fd, 1);
		basic_inst(fd, COND_BATCH, &engine->instance, dev_id);
	}

	igt_subtest_with_dynamic("basic-all") {
		xe_for_each_engine(fd, hwe) {
			igt_dynamic_f("Engine-%s-Instance-%d-Tile-%d",
				      xe_engine_class_string(hwe->engine_class),
				      hwe->engine_instance,
				      hwe->gt_id);
			basic_inst(fd, STORE, hwe, dev_id);
		}
	}

	igt_subtest("cachelines")
		xe_for_each_engine(fd, hwe)
			store_cachelines(fd, hwe, 0);

	igt_subtest("page-sized")
		xe_for_each_engine(fd, hwe)
			store_cachelines(fd, hwe, PAGES);

	igt_subtest("persistent")
		persistent(fd);

	igt_fixture {
		xe_device_put(fd);
		close(fd);
	}
}
