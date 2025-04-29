// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "igt.h"
#include "igt_syncobj.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"

#define OBJECT_SIZE (1 << 23)

static double elapsed(const struct timespec *start,
		      const struct timespec *end)
{
	return (end->tv_sec - start->tv_sec) + 1e-9 * (end->tv_nsec - start->tv_nsec);
}

static void test_exec(int fd, int busy, int size)
{
	uint32_t vm, bo;
	uint64_t bo_size = xe_bb_size(fd, size);
	int err;

	vm = xe_vm_create(fd, 0, 0);
	bo = xe_bo_create(fd, vm, bo_size, system_memory(fd), 0);

	if (busy) {
		uint32_t *batch, exec_queue_id;
		uint64_t addr = 0x1a0000;
		struct drm_xe_sync sync = {
			.type = DRM_XE_SYNC_TYPE_SYNCOBJ,
			.flags = DRM_XE_SYNC_FLAG_SIGNAL,
			.handle = syncobj_create(fd, 0),
		};
		struct drm_xe_exec exec = {
			.num_syncs = 1,
			.syncs = to_user_pointer(&sync),
			.address = addr,
			.num_batch_buffer = 1,
		};

		batch = xe_bo_map(fd, bo, bo_size);
		*batch = MI_BATCH_BUFFER_END;
		munmap(batch, bo_size);
		xe_vm_bind_sync(fd, vm, bo, 0, addr, bo_size);

		err = __xe_exec_queue_create(fd, vm, 1, 1, &xe_engine(fd, 0)->instance, 0,
					     &exec_queue_id);
		igt_assert_f(!err, "Failed to create exec queue (%d)\n", err);

		exec.exec_queue_id = exec_queue_id;
		err = __xe_exec(fd, &exec);
		igt_assert_f(!err, "Failed to execute batch (%d)\n", err);

		err = syncobj_wait(fd, &sync.handle, 1, INT64_MAX, 0, NULL);
		igt_assert_f(err, "Timeout while waiting for syncobj signal  (%d)\n", err);

		xe_exec_queue_destroy(fd, exec_queue_id);
		syncobj_destroy(fd, sync.handle);
	}

	gem_close(fd, bo);
	xe_vm_destroy(fd, vm);
}

int main(int argc, char **argv)
{
	int fd = drm_open_driver(DRIVER_XE);
	int size = 0;
	int busy = 0;
	int reps = 13;
	int ncpus = 1;
	int c, n, s;

	while ((c = getopt(argc, argv, "s:b:r:f")) != -1) {
		switch (c) {
		case 's':
			size = atoi(optarg);
			break;

		case 'r':
			reps = atoi(optarg);
			if (reps < 1)
				reps = 1;
			break;

		case 'f':
			ncpus = sysconf(_SC_NPROCESSORS_ONLN);
			break;

		case 'b':
			busy = true;
			break;

		default:
			break;
		}
	}

	if (size == 0) {
		for (s = 4096; s <=  OBJECT_SIZE; s <<= 1) {
			igt_stats_t stats;

			igt_stats_init_with_size(&stats, reps);
			for (n = 0; n < reps; n++) {
				struct timespec start, end;
				uint64_t count = 0;

				clock_gettime(CLOCK_MONOTONIC, &start);
				do {
					for (c = 0; c < 1000; c++)
						test_exec(fd, busy, s);

					count += c;
					clock_gettime(CLOCK_MONOTONIC, &end);
				} while (end.tv_sec - start.tv_sec < 2);

				igt_stats_push_float(&stats, count / elapsed(&start, &end));
			}
			printf("%f\n", igt_stats_get_trimean(&stats));
			igt_stats_fini(&stats);
		}
	} else {
		double *shared;

		shared = mmap(0, 4096, PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
		for (n = 0; n < reps; n++) {
			memset(shared, 0, 4096);

			igt_fork(child, ncpus) {
				struct timespec start, end;
				uint64_t count = 0;

				clock_gettime(CLOCK_MONOTONIC, &start);
				do {
					for (c = 0; c < 1000; c++)
						test_exec(fd, busy, size);

					count += c;
					clock_gettime(CLOCK_MONOTONIC, &end);
				} while (end.tv_sec - start.tv_sec < 2);

				shared[child] = count / elapsed(&start, &end);
			}
			igt_waitchildren();

			for (int child = 0; child < ncpus; child++)
				shared[ncpus] += shared[child];

			printf("%7.3f\n", shared[ncpus]);
		}
	}

	return 0;
}
