// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <stdio.h>
#include "intel_reg.h"
#include "ioctl_wrappers.h"
#include "lib/igt_syncobj.h"

#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"
#include "xe/xe_util.h"
#include "xe/xe_spin.h"

#define MAX_N_EXEC_QUEUES        16
enum mode { NOP, CREATE, SWITCH };

static double elapsed(const struct timespec *start,
		      const struct timespec *end)
{
	return (end->tv_sec - start->tv_sec) + 1e-9 * (end->tv_nsec - start->tv_nsec);
}

static void
test_exec(int fd, struct drm_xe_engine_class_instance *eci,
	  int n_exec_queues, int n_execs, int n_vm, unsigned int flags)
{
	struct drm_xe_sync sync[2] = {
		{ .type = DRM_XE_SYNC_TYPE_SYNCOBJ, .flags = DRM_XE_SYNC_FLAG_SIGNAL, },
		{ .type = DRM_XE_SYNC_TYPE_SYNCOBJ, .flags = DRM_XE_SYNC_FLAG_SIGNAL, },
	};
	struct drm_xe_exec exec = {
		.num_batch_buffer = 1,
		.num_syncs = 2,
		.syncs = to_user_pointer(sync),
	};

	uint64_t addr[MAX_N_EXEC_QUEUES];
	uint64_t sparse_addr[MAX_N_EXEC_QUEUES];
	uint32_t vm[MAX_N_EXEC_QUEUES];
	uint32_t exec_queues[MAX_N_EXEC_QUEUES];
	uint32_t bind_exec_queues[MAX_N_EXEC_QUEUES];
	uint32_t syncobjs[MAX_N_EXEC_QUEUES];
	uint32_t bind_syncobjs[MAX_N_EXEC_QUEUES];
	struct drm_gem_flink flink;
	struct drm_gem_open open_struct;
	uint32_t bo_flags;
	size_t bo_size;
	uint32_t bo = 0;
	struct {
		uint32_t batch[16];
		uint64_t pad;
		uint32_t data;
	} *data;
	int i, b;
	int ret;

	igt_assert_lte(n_exec_queues, MAX_N_EXEC_QUEUES);
	igt_assert_lte(n_vm, MAX_N_EXEC_QUEUES);

	for (i = 0; i < n_vm; ++i)
		vm[i] = xe_vm_create(fd, 0, 0);
	bo_size = sizeof(*data) * n_execs;
	bo_size = xe_bb_size(fd, bo_size);

	addr[0] = 0x1a0000;
	sparse_addr[0] = 0x301a0000;
	for (i = 1; i < MAX_N_EXEC_QUEUES; ++i) {
		addr[i] = addr[i - 1] + (0x1ull << 32);
		sparse_addr[i] = sparse_addr[i - 1] + (0x1ull << 32);
	}

	bo_flags = DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM;

	bo = xe_bo_create(fd, n_vm == 1 ? vm[0] : 0, bo_size,
			  vram_if_possible(fd, 0), bo_flags);

	flink.handle = bo;
	ret = igt_ioctl(fd, DRM_IOCTL_GEM_FLINK, &flink);
	igt_assert_eq(ret, 0);

	open_struct.name = flink.name;
	ret = igt_ioctl(fd, DRM_IOCTL_GEM_OPEN, &open_struct);
	igt_assert_eq(ret, 0);
	igt_assert(open_struct.handle != 0);

	data = xe_bo_map(fd, bo, bo_size);

	for (i = 0; i < n_exec_queues; i++) {
		uint32_t __vm = vm[i % n_vm];

		exec_queues[i] = xe_exec_queue_create(fd, __vm, eci, 0);

		bind_exec_queues[i] = 0;
		syncobjs[i] = syncobj_create(fd, 0);
		bind_syncobjs[i] = syncobj_create(fd, 0);
	};

	for (i = 0; i < n_vm; ++i) {
		sync[0].handle = bind_syncobjs[i];
		xe_vm_bind_async(fd, vm[i], bind_exec_queues[i], bo, 0,
				 addr[i], bo_size, sync, 1);
	}

	for (i = 0; i < n_execs; i++) {
		int cur_vm = i % n_vm;
		uint64_t __addr = addr[cur_vm];
		uint64_t batch_offset = (char *)&data[i].batch - (char *)data;
		uint64_t batch_addr = __addr + batch_offset;
		uint64_t sdi_offset = (char *)&data[i].data - (char *)data;
		uint64_t sdi_addr = __addr + sdi_offset;
		int e = i % n_exec_queues;

		b = 0;
		data[i].batch[b++] = MI_STORE_DWORD_IMM_GEN4;
		data[i].batch[b++] = sdi_addr;
		data[i].batch[b++] = sdi_addr >> 32;
		data[i].batch[b++] = 0xc0ffee;
		data[i].batch[b++] = MI_BATCH_BUFFER_END;
		igt_assert(b <= ARRAY_SIZE(data[i].batch));

		sync[0].flags &= ~DRM_XE_SYNC_FLAG_SIGNAL;
		sync[0].handle = bind_syncobjs[cur_vm];
		sync[1].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
		sync[1].handle = syncobjs[e];

		exec.exec_queue_id = exec_queues[e];
		exec.address = batch_addr;
		if (e != i)
			syncobj_reset(fd, &syncobjs[e], 1);
		xe_exec(fd, &exec);

		if (i + 1 != n_execs) {
			uint32_t __vm = vm[cur_vm];

			sync[1].flags &= ~DRM_XE_SYNC_FLAG_SIGNAL;
			xe_vm_unbind_async(fd, __vm, bind_exec_queues[e], 0,
					   __addr, bo_size, sync + 1, 1);

			sync[0].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
			addr[i % n_vm] += bo_size;
			__addr = addr[i % n_vm];
			xe_vm_bind_async(fd, __vm, bind_exec_queues[e], bo,
					 0, __addr, bo_size, sync, 1);
		}
	}

	for (i = 0; i < n_exec_queues && n_execs; i++)
		igt_assert(syncobj_wait(fd, &syncobjs[i], 1, INT64_MAX, 0,
					NULL));

	for (i = 0; i < n_vm; i++)
		igt_assert(syncobj_wait(fd, &bind_syncobjs[i], 1, INT64_MAX, 0,
					NULL));

	sync[0].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
	for (i = 0; i < n_vm; ++i) {
		syncobj_reset(fd, &sync[0].handle, 1);
		xe_vm_unbind_async(fd, vm[i], bind_exec_queues[i], 0, addr[i],
				   bo_size, sync, 1);
		igt_assert(syncobj_wait(fd, &sync[0].handle, 1,
					INT64_MAX, 0, NULL));
	}

	for (i = 0; i < n_exec_queues; i++) {
		syncobj_destroy(fd, syncobjs[i]);
		xe_exec_queue_destroy(fd, exec_queues[i]);
		if (bind_exec_queues[i])
			xe_exec_queue_destroy(fd, bind_exec_queues[i]);
	}

	munmap(data, bo_size);
	gem_close(fd, bo);

	for (i = 0; i < n_vm; ++i) {
		syncobj_destroy(fd, bind_syncobjs[i]);
		xe_vm_destroy(fd, vm[i]);
	}
}

static int loop(unsigned int ring,
		int reps,
		enum mode mode,
		int ncpus,
		unsigned int flags)
{
	int fd;
	struct drm_xe_engine_class_instance *hwe;
	double *shared;

	fd = drm_open_driver(DRIVER_XE);
	shared = mmap(0, 4096, PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);

	xe_for_each_engine(fd, hwe)
		if (hwe->engine_class == ring)
			test_exec(fd, hwe, 1, 1, 1, 0);

	while (reps--) {
		sleep(1); /* wait for the hw to go back to sleep */

		memset(shared, 0, 4096);

		igt_fork(child, ncpus) {
			struct timespec start, end;
			unsigned int count = 0;

		clock_gettime(CLOCK_MONOTONIC, &start);
		do {
			switch (mode) {
			case CREATE:
				xe_for_each_engine(fd, hwe)
					if (hwe->engine_class == ring)
						test_exec(fd, hwe, 1, 1, 1, 0);
				break;

			case SWITCH:
				xe_for_each_engine(fd, hwe)
					if (hwe->engine_class == ring)
						test_exec(fd, hwe, 16, 64, 1, 1);
				break;

			case NOP:
				break;
			}
			count++;
			clock_gettime(CLOCK_MONOTONIC, &end);
		} while (elapsed(&start, &end) < 2.);

		clock_gettime(CLOCK_MONOTONIC, &end);
		shared[child] = 1e6 * elapsed(&start, &end) / count;
		}
	}
	igt_waitchildren();

	for (int child = 0; child < ncpus; child++)
		shared[ncpus] += shared[child];
	printf("%7.3f\n", shared[ncpus] / ncpus);

	return 0;
}

int main(int argc, char **argv)
{
	unsigned int ring = DRM_XE_ENGINE_CLASS_RENDER;
	unsigned int flags = 0;
	enum mode mode = NOP;
	int reps = 1;
	int ncpus = 1;
	int c;

	while ((c = getopt(argc, argv, "e:r:b:f")) != -1) {
		switch (c) {
		case 'e':
			if (strcmp(optarg, "rcs") == 0)
				ring = DRM_XE_ENGINE_CLASS_RENDER;
			else if (strcmp(optarg, "vcs") == 0)
				ring = DRM_XE_ENGINE_CLASS_VIDEO_DECODE;
			else if (strcmp(optarg, "bcs") == 0)
				ring = DRM_XE_ENGINE_CLASS_COPY;
			else if (strcmp(optarg, "vecs") == 0)
				ring = DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE;
			else if (strcmp(optarg, "ccs") == 0)
				ring = DRM_XE_ENGINE_CLASS_COMPUTE;
			else
				ring = atoi(optarg);
			break;

		case 'b':
			if (strcmp(optarg, "create") == 0)
				mode = CREATE;
			else if (strcmp(optarg, "switch") == 0)
				mode = SWITCH;
			else if (strcmp(optarg, "nop") == 0)
				mode = NOP;
			else
				abort();
			break;

		case 'f':
			ncpus = sysconf(_SC_NPROCESSORS_ONLN);
			break;

		case 'r':
			reps = atoi(optarg);
			if (reps < 1)
				reps = 1;
			break;

		default:
			break;
		}
	}

	return loop(ring, reps, mode, ncpus, flags);
}
