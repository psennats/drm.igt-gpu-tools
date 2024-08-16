// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "igt.h"
#include "igt_core.h"
#include "igt_device.h"
#include "igt_drm_fdinfo.h"
#include "lib/igt_syncobj.h"
#include "xe_drm.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"
#include "xe/xe_spin.h"
/**
 * TEST: xe drm fdinfo
 * Description: Read and verify drm client memory consumption and engine utilization using fdinfo
 * Category: Core
 * Mega feature: General Core features
 * Sub-category: driver
 * Functionality: Per client memory and engine utilization statistics
 * Feature: SMI, core
 * Test category: SysMan
 *
 * SUBTEST: basic-memory
 * Description: Check if basic fdinfo content is present for memory
 *
 * SUBTEST: basic-engine-utilization
 * Description: Check if basic fdinfo content is present for engine utilization
 *
 * SUBTEST: drm-idle
 * Description: Check that engines show no load when idle
 *
 * SUBTEST: drm-busy-idle
 * Description: Check that engines show load when idle after busy
 *
 * SUBTEST: drm-busy-idle-isolation
 * Description: Check that engine load does not spill over to other drm clients
 *
 * SUBTEST: drm-total-resident
 * Description: Create and compare total and resident memory consumption by client
 *
 * SUBTEST: drm-shared
 * Description: Create and compare shared memory consumption by client
 *
 * SUBTEST: drm-active
 * Description: Create and compare active memory consumption by client
 */

IGT_TEST_DESCRIPTION("Read and verify drm client memory consumption and engine utilization using fdinfo");

#define BO_SIZE (65536)

/* flag masks */
#define TEST_BUSY		(1 << 0)
#define TEST_TRAILING_IDLE	(1 << 1)
#define TEST_ISOLATION		(1 << 2)

struct pceu_cycles {
	uint64_t cycles;
	uint64_t total_cycles;
};

const unsigned long batch_duration_ns = (1 * NSEC_PER_SEC) / 2;

static const char *engine_map[] = {
	"rcs",
	"bcs",
	"vcs",
	"vecs",
	"ccs",
};

static void read_engine_cycles(int xe, struct pceu_cycles *pceu)
{
	struct drm_client_fdinfo info = { };
	int class;

	igt_assert(pceu);
	igt_assert(igt_parse_drm_fdinfo(xe, &info, engine_map,
					ARRAY_SIZE(engine_map), NULL, 0));

	xe_for_each_engine_class(class) {
		pceu[class].cycles = info.cycles[class];
		pceu[class].total_cycles = info.total_cycles[class];
	}
}

/* Subtests */
static void test_active(int fd, struct drm_xe_engine *engine)
{
	struct drm_xe_mem_region *memregion;
	uint64_t memreg = all_memory_regions(fd), region;
	struct drm_client_fdinfo info = { };
	uint32_t vm;
	uint64_t addr = 0x1a0000;
	struct drm_xe_sync sync[2] = {
		{ .type = DRM_XE_SYNC_TYPE_SYNCOBJ, .flags = DRM_XE_SYNC_FLAG_SIGNAL, },
		{ .type = DRM_XE_SYNC_TYPE_SYNCOBJ, .flags = DRM_XE_SYNC_FLAG_SIGNAL, },
	};
	struct drm_xe_exec exec = {
		.num_batch_buffer = 1,
		.num_syncs = 2,
		.syncs = to_user_pointer(sync),
	};
#define N_EXEC_QUEUES   2
	uint32_t exec_queues[N_EXEC_QUEUES];
	uint32_t bind_exec_queues[N_EXEC_QUEUES];
	uint32_t syncobjs[N_EXEC_QUEUES + 1];
	size_t bo_size;
	uint32_t bo = 0;
	struct {
		struct xe_spin spin;
		uint32_t batch[16];
		uint64_t pad;
		uint32_t data;
	} *data;
	struct xe_spin_opts spin_opts = { .preempt = true };
	int i, b, ret;

	vm = xe_vm_create(fd, 0, 0);
	bo_size = sizeof(*data) * N_EXEC_QUEUES;
	bo_size = xe_bb_size(fd, bo_size);

	xe_for_each_mem_region(fd, memreg, region) {
		uint64_t pre_size;

		memregion = xe_mem_region(fd, region);

		ret = igt_parse_drm_fdinfo(fd, &info, NULL, 0, NULL, 0);
		igt_assert_f(ret != 0, "failed with err:%d\n", errno);
		pre_size = info.region_mem[memregion->instance + 1].active;

		bo = xe_bo_create(fd, vm, bo_size, region, 0);
		data = xe_bo_map(fd, bo, bo_size);

		for (i = 0; i < N_EXEC_QUEUES; i++) {
			exec_queues[i] = xe_exec_queue_create(fd, vm,
							      &engine->instance, 0);
			bind_exec_queues[i] = xe_bind_exec_queue_create(fd, vm, 0);
			syncobjs[i] = syncobj_create(fd, 0);
		}
		syncobjs[N_EXEC_QUEUES] = syncobj_create(fd, 0);

		sync[0].handle = syncobj_create(fd, 0);
		xe_vm_bind_async(fd, vm, bind_exec_queues[0], bo, 0, addr, bo_size,
				 sync, 1);

		for (i = 0; i < N_EXEC_QUEUES; i++) {
			uint64_t spin_offset = (char *)&data[i].spin - (char *)data;
			uint64_t spin_addr = addr + spin_offset;
			int e = i;

			if (i == 0) {
				/* Cork 1st exec_queue with a spinner */
				spin_opts.addr = spin_addr;
				xe_spin_init(&data[i].spin, &spin_opts);
				exec.exec_queue_id = exec_queues[e];
				exec.address = spin_opts.addr;
				sync[0].flags &= ~DRM_XE_SYNC_FLAG_SIGNAL;
				sync[1].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
				sync[1].handle = syncobjs[e];
				xe_exec(fd, &exec);
				xe_spin_wait_started(&data[i].spin);

				addr += bo_size;
				sync[1].flags &= ~DRM_XE_SYNC_FLAG_SIGNAL;
				sync[1].handle = syncobjs[e];
				xe_vm_bind_async(fd, vm, bind_exec_queues[e], bo, 0, addr,
						 bo_size, sync + 1, 1);
				addr += bo_size;
			} else {
				sync[0].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
				xe_vm_bind_async(fd, vm, bind_exec_queues[e], bo, 0, addr,
						 bo_size, sync, 1);
			}
		}

		b = igt_parse_drm_fdinfo(fd, &info, NULL, 0, NULL, 0);
		igt_assert_f(b != 0, "failed with err:%d\n", errno);

		/* Client memory consumption includes public objects
		 * as well as internal objects hence if bo is active on
		 * N_EXEC_QUEUES active memory consumption should be
		 * > = bo_size
		 */
		igt_info("total:%ld active:%ld pre_size:%ld bo_size:%ld\n",
			 info.region_mem[memregion->instance + 1].total,
			 info.region_mem[memregion->instance + 1].active,
			 pre_size,
			 bo_size);
		igt_assert(info.region_mem[memregion->instance + 1].active >=
			   pre_size + bo_size);

		xe_spin_end(&data[0].spin);

		syncobj_destroy(fd, sync[0].handle);
		sync[0].handle = syncobj_create(fd, 0);
		sync[0].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
		xe_vm_unbind_all_async(fd, vm, 0, bo, sync, 1);
		igt_assert(syncobj_wait(fd, &sync[0].handle, 1, INT64_MAX, 0, NULL));

		syncobj_destroy(fd, sync[0].handle);
		for (i = 0; i < N_EXEC_QUEUES; i++) {
			syncobj_destroy(fd, syncobjs[i]);
			xe_exec_queue_destroy(fd, exec_queues[i]);
			xe_exec_queue_destroy(fd, bind_exec_queues[i]);
		}

		munmap(data, bo_size);
		gem_close(fd, bo);
	}
	xe_vm_destroy(fd, vm);
}

static void test_shared(int xe)
{
	struct drm_xe_mem_region *memregion;
	uint64_t memreg = all_memory_regions(xe), region;
	struct drm_client_fdinfo info = { };
	struct drm_gem_flink flink;
	struct drm_gem_open open_struct;
	uint32_t bo;
	int ret;

	xe_for_each_mem_region(xe, memreg, region) {
		uint64_t pre_size;

		memregion = xe_mem_region(xe, region);

		ret = igt_parse_drm_fdinfo(xe, &info, NULL, 0, NULL, 0);
		igt_assert_f(ret != 0, "failed with err:%d\n", errno);
		pre_size = info.region_mem[memregion->instance + 1].shared;

		bo = xe_bo_create(xe, 0, BO_SIZE, region, 0);

		flink.handle = bo;
		ret = igt_ioctl(xe, DRM_IOCTL_GEM_FLINK, &flink);
		igt_assert_eq(ret, 0);

		open_struct.name = flink.name;
		ret = igt_ioctl(xe, DRM_IOCTL_GEM_OPEN, &open_struct);
		igt_assert_eq(ret, 0);
		igt_assert(open_struct.handle != 0);

		ret = igt_parse_drm_fdinfo(xe, &info, NULL, 0, NULL, 0);
		igt_assert_f(ret != 0, "failed with err:%d\n", errno);

		igt_info("total:%ld pre_size:%ld shared:%ld\n",
			 info.region_mem[memregion->instance + 1].total,
			 pre_size,
			 info.region_mem[memregion->instance + 1].shared);
		igt_assert(info.region_mem[memregion->instance + 1].shared >=
			   pre_size + BO_SIZE);

		gem_close(xe, open_struct.handle);
		gem_close(xe, bo);
	}
}

static void test_total_resident(int xe)
{
	struct drm_xe_mem_region *memregion;
	uint64_t memreg = all_memory_regions(xe), region;
	struct drm_client_fdinfo info = { };
	uint32_t vm;
	uint32_t handle;
	uint64_t addr = 0x1a0000;
	int ret;

	vm = xe_vm_create(xe, DRM_XE_VM_CREATE_FLAG_SCRATCH_PAGE, 0);

	xe_for_each_mem_region(xe, memreg, region) {
		uint64_t pre_size;

		memregion = xe_mem_region(xe, region);

		ret = igt_parse_drm_fdinfo(xe, &info, NULL, 0, NULL, 0);
		igt_assert_f(ret != 0, "failed with err:%d\n", errno);
		pre_size = info.region_mem[memregion->instance + 1].shared;

		handle = xe_bo_create(xe, vm, BO_SIZE, region, 0);
		xe_vm_bind_sync(xe, vm, handle, 0, addr, BO_SIZE);

		ret = igt_parse_drm_fdinfo(xe, &info, NULL, 0, NULL, 0);
		igt_assert_f(ret != 0, "failed with err:%d\n", errno);
		/* currently xe KMD maps memory class system region to
		 * XE_PL_TT thus we need memregion->instance + 1
		 */
		igt_info("total:%ld resident:%ld pre_size:%ld bo_size:%d\n",
			 info.region_mem[memregion->instance + 1].total,
			 info.region_mem[memregion->instance + 1].resident,
			 pre_size, BO_SIZE);
		/* Client memory consumption includes public objects
		 * as well as internal objects hence it should be
		 * >= pre_size + BO_SIZE
		 */
		igt_assert(info.region_mem[memregion->instance + 1].total >=
			   pre_size + BO_SIZE);
		igt_assert(info.region_mem[memregion->instance + 1].resident >=
			   pre_size + BO_SIZE);
		xe_vm_unbind_sync(xe, vm, 0, addr, BO_SIZE);
		gem_close(xe, handle);
	}

	xe_vm_destroy(xe, vm);
}

static void basic_memory(int xe)
{
	struct drm_xe_mem_region *memregion;
	uint64_t memreg = all_memory_regions(xe), region;
	struct drm_client_fdinfo info = { };
	unsigned int ret;

	ret = igt_parse_drm_fdinfo(xe, &info, NULL, 0, NULL, 0);
	igt_assert_f(ret != 0, "failed with err:%d\n", errno);

	igt_assert(!strcmp(info.driver, "xe"));

	xe_for_each_mem_region(xe, memreg, region) {
		memregion = xe_mem_region(xe, region);
		igt_assert(info.region_mem[memregion->instance + 1].total >=
			   0);
		igt_assert(info.region_mem[memregion->instance + 1].shared >=
			   0);
		igt_assert(info.region_mem[memregion->instance + 1].resident >=
			   0);
		igt_assert(info.region_mem[memregion->instance + 1].active >=
			   0);
		if (memregion->instance == 0)
			igt_assert(info.region_mem[memregion->instance].purgeable >=
				   0);
	}
}

static void basic_engine_utilization(int xe)
{
	struct drm_client_fdinfo info = { };
	unsigned int ret;

	ret = igt_parse_drm_fdinfo(xe, &info, engine_map,
				   ARRAY_SIZE(engine_map), NULL, 0);
	igt_assert_f(ret != 0, "failed with err:%d\n", errno);
	igt_assert(!strcmp(info.driver, "xe"));
	igt_require(info.num_engines);
}

struct spin_ctx {
	uint32_t vm;
	uint64_t addr;
	struct drm_xe_sync sync[2];
	struct drm_xe_exec exec;
	uint32_t exec_queue;
	size_t bo_size;
	uint32_t bo;
	struct xe_spin *spin;
	struct xe_spin_opts spin_opts;
	bool ended;
	uint16_t class;
};

static struct spin_ctx *
spin_ctx_init(int fd, struct drm_xe_engine_class_instance *hwe, uint32_t vm)
{
	struct spin_ctx *ctx = calloc(1, sizeof(*ctx));

	ctx->class = hwe->engine_class;
	ctx->vm = vm;
	ctx->addr = 0x100000;

	ctx->exec.num_batch_buffer = 1;
	ctx->exec.num_syncs = 2;
	ctx->exec.syncs = to_user_pointer(ctx->sync);

	ctx->sync[0].type = DRM_XE_SYNC_TYPE_SYNCOBJ;
	ctx->sync[0].flags = DRM_XE_SYNC_FLAG_SIGNAL;
	ctx->sync[0].handle = syncobj_create(fd, 0);

	ctx->sync[1].type = DRM_XE_SYNC_TYPE_SYNCOBJ;
	ctx->sync[1].flags = DRM_XE_SYNC_FLAG_SIGNAL;
	ctx->sync[1].handle = syncobj_create(fd, 0);

	ctx->bo_size = sizeof(struct xe_spin);
	ctx->bo_size = xe_bb_size(fd, ctx->bo_size);
	ctx->bo = xe_bo_create(fd, ctx->vm, ctx->bo_size,
			       vram_if_possible(fd, hwe->gt_id),
			       DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
	ctx->spin = xe_bo_map(fd, ctx->bo, ctx->bo_size);

	igt_assert_eq(__xe_exec_queue_create(fd, ctx->vm, 1, 1,
					     hwe, 0, &ctx->exec_queue), 0);

	xe_vm_bind_async(fd, ctx->vm, 0, ctx->bo, 0, ctx->addr, ctx->bo_size,
			 ctx->sync, 1);

	return ctx;
}

static void
spin_sync_start(int fd, struct spin_ctx *ctx)
{
	if (!ctx)
		return;

	ctx->spin_opts.addr = ctx->addr;
	ctx->spin_opts.preempt = true;
	xe_spin_init(ctx->spin, &ctx->spin_opts);

	/* re-use sync[0] for exec */
	ctx->sync[0].flags &= ~DRM_XE_SYNC_FLAG_SIGNAL;

	ctx->exec.exec_queue_id = ctx->exec_queue;
	ctx->exec.address = ctx->addr;
	xe_exec(fd, &ctx->exec);

	xe_spin_wait_started(ctx->spin);
	igt_assert(!syncobj_wait(fd, &ctx->sync[1].handle, 1, 1, 0, NULL));

	igt_debug("%s: spinner started\n", engine_map[ctx->class]);
}

static void
spin_sync_end(int fd, struct spin_ctx *ctx)
{
	if (!ctx || ctx->ended)
		return;

	xe_spin_end(ctx->spin);

	igt_assert(syncobj_wait(fd, &ctx->sync[1].handle, 1, INT64_MAX, 0, NULL));
	igt_assert(syncobj_wait(fd, &ctx->sync[0].handle, 1, INT64_MAX, 0, NULL));

	ctx->sync[0].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
	xe_vm_unbind_async(fd, ctx->vm, 0, 0, ctx->addr, ctx->bo_size, ctx->sync, 1);
	igt_assert(syncobj_wait(fd, &ctx->sync[0].handle, 1, INT64_MAX, 0, NULL));

	ctx->ended = true;
	igt_debug("%s: spinner ended\n", engine_map[ctx->class]);
}

static void
spin_ctx_destroy(int fd, struct spin_ctx *ctx)
{
	if (!ctx)
		return;

	syncobj_destroy(fd, ctx->sync[0].handle);
	syncobj_destroy(fd, ctx->sync[1].handle);
	xe_exec_queue_destroy(fd, ctx->exec_queue);

	munmap(ctx->spin, ctx->bo_size);
	gem_close(fd, ctx->bo);

	free(ctx);
}

static void
check_results(struct pceu_cycles *s1, struct pceu_cycles *s2,
	      int class, unsigned int flags)
{
	double percent;

	igt_debug("%s: sample 1: cycles %lu, total_cycles %lu\n",
		  engine_map[class], s1[class].cycles, s1[class].total_cycles);
	igt_debug("%s: sample 2: cycles %lu, total_cycles %lu\n",
		  engine_map[class], s2[class].cycles, s2[class].total_cycles);

	percent = ((s2[class].cycles - s1[class].cycles) * 100) /
		  ((s2[class].total_cycles + 1) - s1[class].total_cycles);

	igt_debug("%s: percent: %f\n", engine_map[class], percent);

	if (flags & TEST_BUSY)
		igt_assert(percent >= 95 && percent <= 100);
	else
		igt_assert(!percent);
}

static void
single(int fd, struct drm_xe_engine_class_instance *hwe, unsigned int flags)
{
	struct pceu_cycles pceu1[2][DRM_XE_ENGINE_CLASS_COMPUTE + 1];
	struct pceu_cycles pceu2[2][DRM_XE_ENGINE_CLASS_COMPUTE + 1];
	struct spin_ctx *ctx = NULL;
	uint32_t vm;
	int new_fd;

	if (flags & TEST_ISOLATION)
		new_fd = drm_reopen_driver(fd);

	vm = xe_vm_create(fd, 0, 0);
	if (flags & TEST_BUSY) {
		ctx = spin_ctx_init(fd, hwe, vm);
		spin_sync_start(fd, ctx);
	}

	read_engine_cycles(fd, pceu1[0]);
	if (flags & TEST_ISOLATION)
		read_engine_cycles(new_fd, pceu1[1]);

	usleep(batch_duration_ns / 1000);
	if (flags & TEST_TRAILING_IDLE)
		spin_sync_end(fd, ctx);

	read_engine_cycles(fd, pceu2[0]);
	if (flags & TEST_ISOLATION)
		read_engine_cycles(new_fd, pceu2[1]);

	check_results(pceu1[0], pceu2[0], hwe->engine_class, flags);

	if (flags & TEST_ISOLATION) {
		check_results(pceu1[1], pceu2[1], hwe->engine_class, 0);
		close(new_fd);
	}

	spin_sync_end(fd, ctx);
	spin_ctx_destroy(fd, ctx);
	xe_vm_destroy(fd, vm);
}

igt_main
{
	struct drm_xe_engine_class_instance *hwe;
	int xe;

	igt_fixture {
		struct drm_client_fdinfo info = { };

		xe = drm_open_driver(DRIVER_XE);
		igt_require_xe(xe);
		igt_require(igt_parse_drm_fdinfo(xe, &info, NULL, 0, NULL, 0));
	}

	igt_describe("Check if basic fdinfo content is present for memory");
	igt_subtest("basic-memory")
		basic_memory(xe);

	igt_describe("Check if basic fdinfo content is present for engine utilization");
	igt_subtest("basic-engine-utilization")
		basic_engine_utilization(xe);

	igt_subtest("drm-idle")
		xe_for_each_engine(xe, hwe)
			single(xe, hwe, 0);

	igt_subtest("drm-busy-idle")
		xe_for_each_engine(xe, hwe)
			single(xe, hwe, TEST_BUSY | TEST_TRAILING_IDLE);

	igt_subtest("drm-busy-idle-isolation")
		xe_for_each_engine(xe, hwe)
			single(xe, hwe, TEST_BUSY | TEST_TRAILING_IDLE | TEST_ISOLATION);

	igt_describe("Create and compare total and resident memory consumption by client");
	igt_subtest("drm-total-resident")
		test_total_resident(xe);

	igt_describe("Create and compare shared memory consumption by client");
	igt_subtest("drm-shared")
		test_shared(xe);

	igt_describe("Create and compare active memory consumption by client");
	igt_subtest("drm-active")
		test_active(xe, xe_engine(xe, 0));

	igt_fixture {
		drm_close_driver(xe);
	}
}
