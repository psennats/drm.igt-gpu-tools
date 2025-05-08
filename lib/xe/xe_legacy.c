// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "lib/igt_syncobj.h"
#include "linux_scaffold.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_legacy.h"
#include "xe/xe_spin.h"

/* Batch buffer element count, in number of dwords(u32) */
#define BATCH_DW_COUNT			16
#define CAT_ERROR			(0x1 << 5)
#define CLOSE_EXEC_QUEUES		(0x1 << 2)
#define CLOSE_FD			(0x1 << 1)
/* Batch buffer element count, in number of dwords(u32) */
#define GT_RESET			(0x1 << 0)
#define MAX_N_EXECQUEUES		16

/**
 * xe_legacy_test_mode:
 * @fd: file descriptor
 * @eci: engine class instance
 * @n_exec_queues: number of exec queues
 * @n_execs: number of execs
 * @flags: flags for the test
 * @addr: address for the test
 * @use_capture_mode: use capture mode or not
 *
 * Returns: void
 */
void
xe_legacy_test_mode(int fd, struct drm_xe_engine_class_instance *eci,
		    int n_exec_queues, int n_execs, unsigned int flags,
		    u64 addr, bool use_capture_mode)
{
	u32 vm;
	struct drm_xe_sync sync[2] = {
		{ .type = DRM_XE_SYNC_TYPE_SYNCOBJ, .flags = DRM_XE_SYNC_FLAG_SIGNAL, },
		{ .type = DRM_XE_SYNC_TYPE_SYNCOBJ, .flags = DRM_XE_SYNC_FLAG_SIGNAL, },
	};
	struct drm_xe_exec exec = {
		.num_batch_buffer = 1,
		.num_syncs = 2,
		.syncs = to_user_pointer(sync),
	};
	u32 exec_queues[MAX_N_EXECQUEUES];
	u32 syncobjs[MAX_N_EXECQUEUES];
	size_t bo_size;
	u32 bo = 0;
	struct {
		struct xe_spin spin;
		u32 batch[BATCH_DW_COUNT];
		u64 pad;
		u32 data;
	} *data;
	struct xe_spin_opts spin_opts = { .preempt = false };
	int i, b;

	igt_assert_lte(n_exec_queues, MAX_N_EXECQUEUES);

	if (flags & CLOSE_FD)
		fd = drm_open_driver(DRIVER_XE);

	vm = xe_vm_create(fd, 0, 0);
	bo_size = sizeof(*data) * n_execs;
	bo_size = xe_bb_size(fd, bo_size);

	bo = xe_bo_create(fd, vm, bo_size,
			  vram_if_possible(fd, eci->gt_id),
			  DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
	data = xe_bo_map(fd, bo, bo_size);

	for (i = 0; i < n_exec_queues; i++) {
		exec_queues[i] = xe_exec_queue_create(fd, vm, eci, 0);
		syncobjs[i] = syncobj_create(fd, 0);
	}

	sync[0].handle = syncobj_create(fd, 0);

	/* Binding mechanism based on use_capture_mode */
	if (use_capture_mode) {
		__xe_vm_bind_assert(fd, vm, 0, bo, 0, addr, bo_size,
				    DRM_XE_VM_BIND_OP_MAP, flags, sync, 1, 0, 0);
	} else {
		xe_vm_bind_async(fd, vm, 0, bo, 0, addr, bo_size, sync, 1);
	}

	for (i = 0; i < n_execs; i++) {
		u64 base_addr = (!use_capture_mode && (flags & CAT_ERROR) && !i)
			? (addr + bo_size * 128) : addr;
		u64 batch_offset = (char *)&data[i].batch - (char *)data;
		u64 batch_addr = base_addr + batch_offset;
		u64 spin_offset = (char *)&data[i].spin - (char *)data;
		u64 sdi_offset = (char *)&data[i].data - (char *)data;
		u64 sdi_addr = base_addr + sdi_offset;
		u64 exec_addr;
		int e = i % n_exec_queues;

		if (!i) {
			spin_opts.addr = base_addr + spin_offset;
			xe_spin_init(&data[i].spin, &spin_opts);
			exec_addr = spin_opts.addr;
		} else {
			b = 0;
			data[i].batch[b++] = MI_STORE_DWORD_IMM_GEN4;
			data[i].batch[b++] = sdi_addr;
			data[i].batch[b++] = sdi_addr >> 32;
			data[i].batch[b++] = 0xc0ffee;
			data[i].batch[b++] = MI_BATCH_BUFFER_END;
			igt_assert(b <= ARRAY_SIZE(data[i].batch));

			exec_addr = batch_addr;
		}

		sync[0].flags &= ~DRM_XE_SYNC_FLAG_SIGNAL;
		sync[1].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
		sync[1].handle = syncobjs[e];

		exec.exec_queue_id = exec_queues[e];
		exec.address = exec_addr;

		if (e != i)
			syncobj_reset(fd, &syncobjs[e], 1);

		xe_exec(fd, &exec);

		if (!i && !(flags & CAT_ERROR) && !use_capture_mode)
			xe_spin_wait_started(&data[i].spin);
	}

	if (flags & GT_RESET)
		xe_force_gt_reset_async(fd, eci->gt_id);

	if (flags & CLOSE_FD) {
		if (flags & CLOSE_EXEC_QUEUES) {
			for (i = 0; i < n_exec_queues; i++)
				xe_exec_queue_destroy(fd, exec_queues[i]);
		}
		drm_close_driver(fd);
		/* FIXME: wait for idle */
		usleep(150000);
		return;
	}

	for (i = 0; i < n_exec_queues && n_execs; i++)
		igt_assert(syncobj_wait(fd, &syncobjs[i], 1, INT64_MAX, 0, NULL));

	igt_assert(syncobj_wait(fd, &sync[0].handle, 1, INT64_MAX, 0, NULL));

	sync[0].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
	xe_vm_unbind_async(fd, vm, 0, 0, addr, bo_size, sync, 1);
	igt_assert(syncobj_wait(fd, &sync[0].handle, 1, INT64_MAX, 0, NULL));

	if (!use_capture_mode && !(flags & GT_RESET)) {
		for (i = 1; i < n_execs; i++)
			igt_assert_eq(data[i].data, 0xc0ffee);
	}

	syncobj_destroy(fd, sync[0].handle);
	for (i = 0; i < n_exec_queues; i++) {
		syncobj_destroy(fd, syncobjs[i]);
		xe_exec_queue_destroy(fd, exec_queues[i]);
	}

	munmap(data, bo_size);
	gem_close(fd, bo);
	xe_vm_destroy(fd, vm);
}
