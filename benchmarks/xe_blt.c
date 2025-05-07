// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "drm.h"
#include "igt_syncobj.h"
#include "intel_blt.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"
#include "xe/xe_util.h"

#define COPY_BLT_CMD	(2 << 29 | 0x53 << 22 | 0x6)

static double
elapsed(const struct timespec *start, const struct timespec *end)
{
	return (end->tv_sec - start->tv_sec) +
			1e-9 * (end->tv_nsec - start->tv_nsec);
}

static uint64_t emit_blt_src_copy(int fd, uint64_t ahnd,
				  const struct blt_copy_data *blt,
				  uint64_t bb_pos, bool emit_bbe,
				  uint64_t dst_offset, uint64_t src_offset,
				  uint32_t height)
{
	uint32_t b[12];
	uint32_t bbe = MI_BATCH_BUFFER_END;
	uint32_t *bb;
	int i = 0;

	src_offset += blt->src.plane_offset;
	dst_offset += blt->dst.plane_offset;

	b[i++] = COPY_BLT_CMD | XY_SRC_COPY_BLT_WRITE_ALPHA | XY_SRC_COPY_BLT_WRITE_RGB;
	b[i - 1] += 2;
	b[i++] = 0xcc << 16 | 1 << 25 | 1 << 24 | (16 * 1024);
	b[i++] = 0;
	b[i++] = height << 16 | (4 * 1024);
	b[i++] = dst_offset;
	b[i++] = dst_offset >> 32; /* FIXME */
	b[i++] = 0;
	b[i++] = 16 * 1024;
	b[i++] = src_offset;
	b[i++] = src_offset >> 32; /* FIXME */

	bb = xe_bo_map(fd, blt->bb.handle, blt->bb.size);

	igt_assert(bb_pos + sizeof(b) < blt->bb.size);
	memcpy(bb + bb_pos, &b, sizeof(b));
	bb_pos += sizeof(b);

	if (emit_bbe) {
		igt_assert(bb_pos + sizeof(uint32_t) < blt->bb.size);
		memcpy(bb + bb_pos, &bbe, sizeof(bbe));
		bb_pos += sizeof(uint32_t);
	}

	munmap(bb, blt->bb.size);

	return bb_pos;
}

static int count;
/*
 * val = True To check BB is working fine or not
 * count_val = To get the counter value how many buffers we can send in 0.1 sec
 * loop_count = count_val time we have to submit the buffers
 */
static int blt_src_copy(int fd,
			const intel_ctx_t *ctx,
			const struct intel_execution_engine2 *e, uint64_t ahnd,
			const struct blt_copy_data *blt, uint32_t height,
			bool val, bool count_val, bool loop_count)
{
	uint64_t dst_offset = 0, src_offset = 0, bb_offset = 0;
	int ret = 0;
	uint64_t bb_pos = 0;
	struct timespec start, end;

	igt_assert_f(ahnd, "src-copy supports softpin only\n");
	igt_assert_f(blt, "src-copy requires data to do src-copy blit\n");
	igt_assert_neq(blt->driver, 0);

	if (!val) {
		src_offset = get_offset_pat_index(ahnd, blt->src.handle,
						  blt->src.size, 0,
						  blt->src.pat_index);
		dst_offset = get_offset_pat_index(ahnd, blt->dst.handle,
						  blt->dst.size, 0,
						  blt->dst.pat_index);
		bb_offset = get_offset(ahnd, blt->bb.handle, blt->bb.size, 0);
	}

	bb_pos = emit_blt_src_copy(fd, ahnd, blt, bb_pos, true, src_offset,
				   dst_offset, height);
	if (count_val) {
		clock_gettime(CLOCK_MONOTONIC, &start);
		do {
			if (blt->driver == INTEL_DRIVER_XE)
				intel_ctx_xe_exec(ctx, ahnd,
						  CANONICAL(bb_offset));
			count++;
			clock_gettime(CLOCK_MONOTONIC, &end);
			if (elapsed(&start, &end) > (100 / 1000.))
				break;
		} while (1);
	} else if (loop_count) {
		for (int loop = 0; loop < count; loop++) {
			if (blt->driver == INTEL_DRIVER_XE)
				intel_ctx_xe_exec(ctx, ahnd,
						  CANONICAL(bb_offset));
		}

	} else {
		if (blt->driver == INTEL_DRIVER_XE)
			intel_ctx_xe_exec(ctx, ahnd, CANONICAL(bb_offset));
	}
	return ret;
}

static int src_copy(int xe, const intel_ctx_t *ctx,
		    uint32_t width, uint32_t height, uint32_t region1,
		    uint32_t region2, bool val, bool count_value, bool loop)
{
	struct blt_copy_data blt = {};
	struct blt_copy_object *src, *dst;
	const uint32_t bpp = 32;
	uint64_t bb_size = xe_bb_size(xe, SZ_4K);
	uint64_t ahnd = intel_allocator_open_full(xe, ctx->vm, 0, 0,
			INTEL_ALLOCATOR_SIMPLE,
			ALLOC_STRATEGY_LOW_TO_HIGH, 0);
	uint32_t bb;
	int ret = 0;

	bb = xe_bo_create(xe, 0, bb_size, region1, 0);
	blt_copy_init(xe, &blt);
	src = blt_create_object(&blt, region1, width, height, bpp, 0,
				T_LINEAR, COMPRESSION_DISABLED, 0, true);
	dst = blt_create_object(&blt, region1, width, height, bpp, 0,
				T_LINEAR, COMPRESSION_DISABLED, 0, true);
	igt_assert(src->size == dst->size);

	blt_set_copy_object(&blt.src, src);
	blt_set_copy_object(&blt.dst, dst);
	blt_set_batch(&blt.bb, bb, bb_size, region1);

	ret = blt_src_copy(xe, ctx, NULL, ahnd, &blt, height, val, count_value, loop);

	put_offset(ahnd, src->handle);
	put_offset(ahnd, dst->handle);
	put_offset(ahnd, bb);
	intel_allocator_bind(ahnd, 0, 0);
	blt_destroy_object(xe, src);
	blt_destroy_object(xe, dst);
	gem_close(xe, bb);
	put_ahnd(ahnd);
	return ret;
}

#define SYNC 0x1

static int run(int width, int batch, int time, int reps, int ncpus,
	       unsigned int flags)
{
	int xe;
	int height = width / (16 * 1024);
	struct drm_xe_engine_class_instance inst = {
		.engine_class = DRM_XE_ENGINE_CLASS_COPY,
	};
	intel_ctx_t *ctx;
	double *shared;
	uint32_t region1, region2;
	uint32_t vm, exec_queue;
	int ret;

	shared = mmap(0, 4096, PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);

	xe = drm_open_driver(DRIVER_XE);
	xe_device_get(xe);

	intel_allocator_multiprocess_start();
	region1 = 1;
	region2 = 2;

	vm = xe_vm_create(xe, 0, 0);
	exec_queue = xe_exec_queue_create(xe, vm, &inst, 0);
	ctx = intel_ctx_xe(xe, vm, exec_queue, 0, 0, 0);
	ret = src_copy(xe, ctx, width, height, region1, region2, true, false,
		       false);
	xe_exec_queue_destroy(xe, exec_queue);
	free(ctx);
	if (!ret) {
		exec_queue = xe_exec_queue_create(xe, vm, &inst, 0);
		ctx = intel_ctx_xe(xe, vm, exec_queue, 0, 0, 0);
		ret = src_copy(xe, ctx, width, height, region1, region2,
			       false, false, false);
		xe_exec_queue_destroy(xe, exec_queue);
		free(ctx);
	}
	if (batch > 1) {
		for (int i = 1; i < batch; i++) {
			exec_queue = xe_exec_queue_create(xe, vm, &inst, 0);
			ctx = intel_ctx_xe(xe, vm, exec_queue, 0, 0, 0);
			ret = src_copy(xe, ctx, width, height, region1, region2,
				       false, false, false);
			xe_exec_queue_destroy(xe, exec_queue);
			free(ctx);
		}
	}
	exec_queue = xe_exec_queue_create(xe, vm, &inst, 0);
	ctx = intel_ctx_xe(xe, vm, exec_queue, 0, 0, 0);
	ret = src_copy(xe, ctx, width, height, region1, region2,
		       false, true, false);
	xe_exec_queue_destroy(xe, exec_queue);
	free(ctx);
	if (flags & SYNC) {
		time *= count / 2;
		count = 1;
	}

	while (reps--) {
		memset(shared, 0, 4096);

		igt_fork(child, ncpus) {
			double min = HUGE_VAL;

			for (int s = 0; s <= time / 100; s++) {
				struct timespec start, end;
				double t;

				clock_gettime(CLOCK_MONOTONIC, &start);
				exec_queue = xe_exec_queue_create(xe, vm,
								  &inst, 0);
				ctx = intel_ctx_xe(xe, vm, exec_queue, 0, 0, 0);
				ret = src_copy(xe, ctx, width, height, region1,
					       region2, false, false, true);
				free(ctx);
				xe_exec_queue_destroy(xe, exec_queue);
				clock_gettime(CLOCK_MONOTONIC, &end);

				t = elapsed(&start, &end);
				if (t < min)
					min = t;
			}

			shared[child] = width / (1024 * 1024.) * batch * count / min;
		}
		igt_waitchildren();

		for (int child = 0; child < ncpus; child++)
			shared[ncpus] += shared[child];
		printf("%7.3f\n", shared[ncpus] / ncpus);
	}
	intel_allocator_multiprocess_stop();

	xe_vm_destroy(xe, vm);
	close(xe);
	return 0;
}

int main(int argc, char **argv)
{
	int size = 1024 * 1024;
	int reps = 13;
	int time = 2000;
	int ncpus = 1;
	int batch = 1;
	unsigned int flags = 0;
	int c;

	while ((c = getopt(argc, argv, "s:S:t:r:b:f")) != -1) {
		switch (c) {
		case 's':
			size = atoi(optarg);
			size = ALIGN(size, 4);
			if (size < 4)
				size = 4;
			break;

		case 'S':
			flags |= SYNC;
			break;

		case 't':
			time = atoi(optarg);
			if (time < 1)
				time = 1;
			break;

		case 'r':
			reps = atoi(optarg);
			if (reps < 1)
				reps = 1;
			break;

		case 'b':
			batch = atoi(optarg);
			if (batch < 1)
				batch = 1;
			break;

		case 'f':
			ncpus = sysconf(_SC_NPROCESSORS_ONLN);
			break;

		default:
			break;
		}
	}

	return run(size, batch, time, reps, ncpus, flags);
}
