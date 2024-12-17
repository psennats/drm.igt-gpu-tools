// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 *
 * Authors:
 *    Matthew Brost <matthew.brost@intel.com>
 */

#include <string.h>

#include "drmtest.h"
#include "igt.h"
#include "igt_core.h"
#include "igt_syncobj.h"
#include "intel_reg.h"
#include "xe_ioctl.h"
#include "xe_spin.h"

static uint32_t read_timestamp_frequency(int fd, int gt_id)
{
	struct xe_device *dev = xe_device_get(fd);

	igt_assert(dev && dev->gt_list && dev->gt_list->num_gt);
	igt_assert(gt_id >= 0 && gt_id <= dev->gt_list->num_gt);

	return dev->gt_list->gt_list[gt_id].reference_clock;
}

static uint64_t div64_u64_round_up(const uint64_t x, const uint64_t y)
{
	igt_assert(y > 0);
	igt_assert_lte_u64(x, UINT64_MAX - (y - 1));

	return (x + y - 1) / y;
}

/**
 * duration_to_ctx_ticks:
 * @fd: opened device
 * @gt_id: tile id
 * @duration_ns: duration in nanoseconds to be converted to context timestamp ticks
 * @return: duration converted to context timestamp ticks.
 */
uint32_t duration_to_ctx_ticks(int fd, int gt_id, uint64_t duration_ns)
{
	uint32_t f = read_timestamp_frequency(fd, gt_id);
	uint64_t ctx_ticks = div64_u64_round_up(duration_ns * f, NSEC_PER_SEC);

	igt_assert_lt_u64(ctx_ticks, XE_SPIN_MAX_CTX_TICKS);

	return ctx_ticks;
}

#define MI_SRM_CS_MMIO				(1 << 19)
#define MI_LRI_CS_MMIO				(1 << 19)
#define MI_LRR_DST_CS_MMIO			(1 << 19)
#define MI_LRR_SRC_CS_MMIO			(1 << 18)
#define CTX_TIMESTAMP 0x3a8
#define CS_GPR(x) (0x600 + 8 * (x))

enum { START_TS, NOW_TS };

/**
 * xe_spin_init:
 * @spin: pointer to mapped bo in which spinner code will be written
 * @opts: pointer to spinner initialization options
 */
void xe_spin_init(struct xe_spin *spin, struct xe_spin_opts *opts)
{
	uint64_t loop_addr;
	uint64_t start_addr = opts->addr + offsetof(struct xe_spin, start);
	uint64_t end_addr = opts->addr + offsetof(struct xe_spin, end);
	uint64_t ticks_delta_addr = opts->addr + offsetof(struct xe_spin, ticks_delta);
	uint64_t pad_addr = opts->addr + offsetof(struct xe_spin, pad);
	uint64_t timestamp_addr = opts->addr + offsetof(struct xe_spin, timestamp);
	int b = 0;

	spin->start = 0;
	spin->end = 0xffffffff;
	spin->ticks_delta = 0;

	if (opts->ctx_ticks) {
		/* store start timestamp */
		spin->batch[b++] = MI_LOAD_REGISTER_IMM(1) | MI_LRI_CS_MMIO;
		spin->batch[b++] = CS_GPR(START_TS) + 4;
		spin->batch[b++] = 0;
		spin->batch[b++] = MI_LOAD_REGISTER_REG | MI_LRR_DST_CS_MMIO | MI_LRR_SRC_CS_MMIO;
		spin->batch[b++] = CTX_TIMESTAMP;
		spin->batch[b++] = CS_GPR(START_TS);
	}

	loop_addr = opts->addr + b * sizeof(uint32_t);

	spin->batch[b++] = MI_STORE_DWORD_IMM_GEN4;
	spin->batch[b++] = start_addr;
	spin->batch[b++] = start_addr >> 32;
	spin->batch[b++] = 0xc0ffee;

	if (opts->preempt)
		spin->batch[b++] = MI_ARB_CHECK;

	if (opts->write_timestamp) {
		spin->batch[b++] = MI_LOAD_REGISTER_REG | MI_LRR_DST_CS_MMIO | MI_LRR_SRC_CS_MMIO;
		spin->batch[b++] = CTX_TIMESTAMP;
		spin->batch[b++] = CS_GPR(NOW_TS);

		spin->batch[b++] = MI_STORE_REGISTER_MEM_GEN8 | MI_SRM_CS_MMIO;
		spin->batch[b++] = CS_GPR(NOW_TS);
		spin->batch[b++] = timestamp_addr;
		spin->batch[b++] = timestamp_addr >> 32;
	}

	if (opts->ctx_ticks) {
		spin->batch[b++] = MI_LOAD_REGISTER_IMM(1) | MI_LRI_CS_MMIO;
		spin->batch[b++] = CS_GPR(NOW_TS) + 4;
		spin->batch[b++] = 0;
		spin->batch[b++] = MI_LOAD_REGISTER_REG | MI_LRR_DST_CS_MMIO | MI_LRR_SRC_CS_MMIO;
		spin->batch[b++] = CTX_TIMESTAMP;
		spin->batch[b++] = CS_GPR(NOW_TS);

		/* delta = now - start; inverted to match COND_BBE */
		spin->batch[b++] = MI_MATH(4);
		spin->batch[b++] = MI_MATH_LOAD(MI_MATH_REG_SRCA, MI_MATH_REG(NOW_TS));
		spin->batch[b++] = MI_MATH_LOAD(MI_MATH_REG_SRCB, MI_MATH_REG(START_TS));
		spin->batch[b++] = MI_MATH_SUB;
		spin->batch[b++] = MI_MATH_STOREINV(MI_MATH_REG(NOW_TS), MI_MATH_REG_ACCU);

		/* Save delta for reading by COND_BBE */
		spin->batch[b++] = MI_STORE_REGISTER_MEM_GEN8 | MI_SRM_CS_MMIO;
		spin->batch[b++] = CS_GPR(NOW_TS);
		spin->batch[b++] = ticks_delta_addr;
		spin->batch[b++] = ticks_delta_addr >> 32;

		/* Delay between SRM and COND_BBE to post the writes */
		for (int n = 0; n < 8; n++) {
			spin->batch[b++] = MI_STORE_DWORD_IMM_GEN4;
			spin->batch[b++] = pad_addr;
			spin->batch[b++] = pad_addr >> 32;
			spin->batch[b++] = 0xc0ffee;
		}

		/* Break if delta [time elapsed] > ns */
		spin->batch[b++] = MI_COND_BATCH_BUFFER_END | MI_DO_COMPARE | 2;
		spin->batch[b++] = ~(opts->ctx_ticks);
		spin->batch[b++] = ticks_delta_addr;
		spin->batch[b++] = ticks_delta_addr >> 32;
	}

	spin->batch[b++] = MI_COND_BATCH_BUFFER_END | MI_DO_COMPARE | 2;
	spin->batch[b++] = 0;
	spin->batch[b++] = end_addr;
	spin->batch[b++] = end_addr >> 32;

	spin->batch[b++] = MI_BATCH_BUFFER_START | 1 << 8 | 1;
	spin->batch[b++] = loop_addr;
	spin->batch[b++] = loop_addr >> 32;

	igt_assert(b <= ARRAY_SIZE(spin->batch));
}

/**
 * xe_spin_started:
 * @spin: pointer to spinner mapped bo
 *
 * Returns: true if spinner is running, otherwise false.
 */
bool xe_spin_started(struct xe_spin *spin)
{
	return spin->start != 0;
}

/**
 * xe_spin_wait_started:
 * @spin: pointer to spinner mapped bo
 *
 * Wait in userspace code until spinner won't start.
 */
void xe_spin_wait_started(struct xe_spin *spin)
{
	while (!xe_spin_started(spin))
		;
}

void xe_spin_end(struct xe_spin *spin)
{
	spin->end = 0;
}

/**
 * xe_spin_create:
 * @opt: controlling options such as allocator handle, exec_queue, vm etc
 *
 * igt_spin_new for xe, xe_spin_create submits a batch using xe_spin_init
 * which wraps around vm bind and unbinding the object associated to it.
 *
 * This returns a spinner after submitting a dummy load.
 */
igt_spin_t *
xe_spin_create(int fd, const struct igt_spin_factory *opt)
{
	size_t bo_size = xe_bb_size(fd, SZ_4K);
	uint64_t ahnd = opt->ahnd, addr;
	struct igt_spin *spin;
	struct xe_spin *xe_spin;
	struct drm_xe_sync sync = {
		.type = DRM_XE_SYNC_TYPE_SYNCOBJ, .flags = DRM_XE_SYNC_FLAG_SIGNAL,
	};
	struct drm_xe_exec exec = {
		.num_batch_buffer = 1,
		.num_syncs = 1,
		.syncs = to_user_pointer(&sync),
	};

	igt_assert(ahnd);
	spin = calloc(1, sizeof(struct igt_spin));
	igt_assert(spin);

	spin->driver = INTEL_DRIVER_XE;
	spin->syncobj = syncobj_create(fd, 0);
	spin->vm = opt->vm;
	spin->engine = opt->engine;
	spin->timerfd = -1;

	if (!spin->vm)
		spin->vm = xe_vm_create(fd, 0, 0);

	if (!spin->engine) {
		if (opt->hwe)
			spin->engine = xe_exec_queue_create(fd, spin->vm, opt->hwe, 0);
		else
			spin->engine = xe_exec_queue_create_class(fd, spin->vm, DRM_XE_ENGINE_CLASS_COPY);
	}

	spin->handle = xe_bo_create(fd, spin->vm, bo_size,
				    vram_if_possible(fd, 0),
				    DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
	xe_spin = xe_bo_map(fd, spin->handle, bo_size);
	addr = intel_allocator_alloc_with_strategy(ahnd, spin->handle, bo_size, 0, ALLOC_STRATEGY_LOW_TO_HIGH);
	xe_vm_bind_sync(fd, spin->vm, spin->handle, 0, addr, bo_size);

	xe_spin_init_opts(xe_spin, .addr = addr, .preempt = !(opt->flags & IGT_SPIN_NO_PREEMPTION));
	exec.exec_queue_id = spin->engine;
	exec.address = addr;
	sync.handle = spin->syncobj;
	igt_assert_eq(igt_ioctl(fd, DRM_IOCTL_XE_EXEC, &exec), 0);
	xe_spin_wait_started(xe_spin);

	spin->bo_size = bo_size;
	spin->address = addr;
	spin->xe_spin = xe_spin;
	spin->opts = *opt;

	return spin;
}

void xe_spin_sync_wait(int fd, struct igt_spin *spin)
{
	igt_assert(syncobj_wait(fd, &spin->syncobj, 1, INT64_MAX, 0, NULL));
}

/*
 * xe_spin_free:
 * @spin: spin state from igt_spin_new()
 *
 * Wrapper to free spinner created by xe_spin_create. It will
 * destroy vm, exec_queue and unbind the vm which was binded to
 * the exec_queue and bo.
 */
void xe_spin_free(int fd, struct igt_spin *spin)
{
	igt_assert(spin->driver == INTEL_DRIVER_XE);

	if (spin->timerfd >= 0) {
		pthread_cancel(spin->timer_thread);
		igt_assert(pthread_join(spin->timer_thread, NULL) == 0);
		close(spin->timerfd);
	}

	xe_spin_end(spin->xe_spin);
	xe_spin_sync_wait(fd, spin);
	xe_vm_unbind_sync(fd, spin->vm, 0, spin->address, spin->bo_size);
	syncobj_destroy(fd, spin->syncobj);
	gem_munmap(spin->xe_spin, spin->bo_size);
	gem_close(fd, spin->handle);

	if (!spin->opts.engine)
		xe_exec_queue_destroy(fd, spin->engine);

	if (!spin->opts.vm)
		xe_vm_destroy(fd, spin->vm);

	free(spin);
}

/**
 * xe_cork_create:
 * @fd: xe device fd
 * @hwe: Xe engine class instance if device is Xe
 * @vm: vm handle
 * @width: number of batch buffers
 * @num_placements: number of valid placements for this exec queue
 * @opts: controlling options such as allocator handle, debug.
 *
 * xe_cork_create create vm, bo, exec_queue and bind the buffer
 * using vmbind
 *
 * This returns xe_cork after binding buffer object.
 */

struct xe_cork *
xe_cork_create(int fd, struct drm_xe_engine_class_instance *hwe,
		uint32_t vm, uint16_t width, uint16_t num_placements,
		struct xe_cork_opts *opts)
{
	struct xe_cork *ctx = calloc(1, sizeof(*ctx));

	igt_assert(ctx);
	igt_assert(width && num_placements &&
		   (width == 1 || num_placements == 1));
	igt_assert_lt(width, XE_MAX_ENGINE_INSTANCE);

	ctx->class = hwe->engine_class;
	ctx->width = width;
	ctx->num_placements = num_placements;
	ctx->vm = vm;
	ctx->cork_opts = *opts;

	ctx->exec.num_batch_buffer = width;
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
	if (ctx->cork_opts.ahnd) {
		for (unsigned int i = 0; i < width; i++)
			ctx->addr[i] = intel_allocator_alloc_with_strategy(ctx->cork_opts.ahnd,
					ctx->bo, ctx->bo_size, 0,
					ALLOC_STRATEGY_LOW_TO_HIGH);
	} else {
		for (unsigned int i = 0; i < width; i++)
			ctx->addr[i] = 0x100000 + 0x100000 * hwe->engine_class;
	}

	ctx->spin = xe_bo_map(fd, ctx->bo, ctx->bo_size);

	igt_assert_eq(__xe_exec_queue_create(fd, ctx->vm, width, num_placements,
					     hwe, 0, &ctx->exec_queue), 0);

	xe_vm_bind_async(fd, ctx->vm, 0, ctx->bo, 0, ctx->addr[0], ctx->bo_size,
			 ctx->sync, 1);

	return ctx;
}

/**
 * xe_cork_sync_start:
 *
 * @fd: xe device fd
 * @ctx: pointer to xe_cork structure
 *
 * run the spinner using xe_spin_init submit batch using xe_exec
 * and wait for fence using syncobj_wait
 */
void xe_cork_sync_start(int fd, struct xe_cork *ctx)
{
	igt_assert(ctx);

	ctx->spin_opts.addr = ctx->addr[0];
	ctx->spin_opts.write_timestamp = true;
	ctx->spin_opts.preempt = true;
	xe_spin_init(ctx->spin, &ctx->spin_opts);

	/* reuse sync[0] as in-fence for exec */
	ctx->sync[0].flags &= ~DRM_XE_SYNC_FLAG_SIGNAL;

	ctx->exec.exec_queue_id = ctx->exec_queue;

	if (ctx->width > 1)
		ctx->exec.address = to_user_pointer(ctx->addr);
	else
		ctx->exec.address = ctx->addr[0];

	xe_exec(fd, &ctx->exec);

	xe_spin_wait_started(ctx->spin);
	igt_assert(!syncobj_wait(fd, &ctx->sync[1].handle, 1, 1, 0, NULL));

	if (ctx->cork_opts.debug)
		igt_info("%d: spinner started\n", ctx->class);
}

/*
 * xe_cork_sync_end
 *
 * @fd: xe device fd
 * @ctx: pointer to xe_cork structure
 *
 * Wrapper to end spinner created by xe_cork_create. It will
 * unbind the vm which was binded to the exec_queue and bo.
 */
void xe_cork_sync_end(int fd, struct xe_cork *ctx)
{
	igt_assert(ctx);

	if (ctx->ended)
		igt_warn("Don't attempt call end twice %d\n", ctx->ended);

	xe_spin_end(ctx->spin);

	igt_assert(syncobj_wait(fd, &ctx->sync[1].handle, 1, INT64_MAX, 0, NULL));

	ctx->sync[0].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
	syncobj_reset(fd, &ctx->sync[0].handle, 1);

	xe_vm_unbind_async(fd, ctx->vm, 0, 0, ctx->addr[0], ctx->bo_size, ctx->sync, 1);
	igt_assert(syncobj_wait(fd, &ctx->sync[0].handle, 1, INT64_MAX, 0, NULL));

	ctx->ended = true;

	if (ctx->cork_opts.debug)
		igt_info("%d: spinner ended (timestamp=%u)\n", ctx->class,
			ctx->spin->timestamp);
}

/*
 * xe_cork_destroy
 *
 * @fd: xe device fd
 * @ctx: pointer to xe_cork structure
 *
 * It will destroy vm, exec_queue and free the ctx.
 */
void xe_cork_destroy(int fd, struct xe_cork *ctx)
{
	igt_assert(ctx);

	syncobj_destroy(fd, ctx->sync[0].handle);
	syncobj_destroy(fd, ctx->sync[1].handle);
	xe_exec_queue_destroy(fd, ctx->exec_queue);

	if (ctx->cork_opts.ahnd)
		intel_allocator_free(ctx->cork_opts.ahnd, ctx->bo);

	munmap(ctx->spin, ctx->bo_size);
	gem_close(fd, ctx->bo);

	free(ctx);
}
