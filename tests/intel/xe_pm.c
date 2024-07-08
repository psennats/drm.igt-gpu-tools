// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

/**
 * TEST: Check Power Management functionality
 * Category: Core
 * Mega feature: Power management
 * Sub-category: Power management tests
 * Test category: functionality test
 */

#include <limits.h>
#include <fcntl.h>
#include <string.h>

#include "igt.h"
#include "lib/igt_device.h"
#include "lib/igt_pm.h"
#include "lib/igt_sysfs.h"
#include "lib/igt_syncobj.h"
#include "lib/intel_reg.h"

#include "xe_drm.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"

#define MAX_N_EXEC_QUEUES 16
#define NO_SUSPEND -1
#define NO_RPM -1

#define SIZE (4096 * 1024)
#define MAGIC_1 0xc0ffee
#define MAGIC_2 0xdeadbeef

#define USERPTR (0x1 << 0)
#define PREFETCH (0x1 << 1)
#define UNBIND_ALL (0x1 << 2)

enum mem_op {
	READ,
	WRITE,
};

typedef struct {
	int fd_xe;
	struct pci_device *pci_xe;
	struct pci_device *pci_root;
	char pci_slot_name[NAME_MAX];
	drmModeResPtr res;
} device_t;

uint64_t orig_threshold;
int fw_handle = -1;

static void dpms_on_off(device_t device, int mode)
{
	int i;

	if (!device.res)
		return;

	for (i = 0; i < device.res->count_connectors; i++) {
		drmModeConnector *connector = drmModeGetConnectorCurrent(device.fd_xe,
									 device.res->connectors[i]);

		if (!connector)
			continue;

		if (connector->connection == DRM_MODE_CONNECTED)
			kmstest_set_connector_dpms(device.fd_xe, connector, mode);

		drmModeFreeConnector(connector);
	}
}

/* runtime_usage is only available if kernel build CONFIG_PM_ADVANCED_DEBUG */
static bool runtime_usage_available(struct pci_device *pci)
{
	char name[PATH_MAX];
	snprintf(name, PATH_MAX, "/sys/bus/pci/devices/%04x:%02x:%02x.%01x/runtime_usage",
		 pci->domain, pci->bus, pci->dev, pci->func);
	return access(name, F_OK) == 0;
}

static uint64_t get_vram_d3cold_threshold(int sysfs)
{
	uint64_t threshold;
	char path[64];
	int ret;

	sprintf(path, "device/vram_d3cold_threshold");
	igt_require_f(!faccessat(sysfs, path, R_OK, 0), "vram_d3cold_threshold is not present\n");

	ret = igt_sysfs_scanf(sysfs, path, "%lu", &threshold);
	igt_assert(ret > 0);

	return threshold;
}

static void set_vram_d3cold_threshold(int sysfs, uint64_t threshold)
{
	char path[64];
	int ret;

	sprintf(path, "device/vram_d3cold_threshold");

	if (!faccessat(sysfs, path, R_OK | W_OK, 0))
		ret = igt_sysfs_printf(sysfs, path, "%lu", threshold);
	else
		igt_warn("vram_d3cold_threshold is not present\n");

	igt_assert(ret > 0);
}

static void vram_d3cold_threshold_restore(int sig)
{
	int fd, sysfs_fd;

	fd = drm_open_driver(DRIVER_XE);
	sysfs_fd = igt_sysfs_open(fd);

	set_vram_d3cold_threshold(sysfs_fd, orig_threshold);

	close(sysfs_fd);
	close(fd);
}

static bool setup_d3(device_t device, enum igt_acpi_d_state state)
{
	dpms_on_off(device, DRM_MODE_DPMS_OFF);

	/*
	 * The drm calls used for dpms status above will result in IOCTLs
	 * that might wake up the device. Let's ensure the device is back
	 * to a stable suspended state before we can proceed with the
	 * configuration below, since some strange failures were seen
	 * when d3cold_allowed is toggle while runtime is in a transition
	 * state.
	 */
	igt_wait_for_pm_status(IGT_RUNTIME_PM_STATUS_SUSPENDED);

	switch (state) {
	case IGT_ACPI_D3Cold:
		igt_require(igt_pm_acpi_d3cold_supported(device.pci_root));
		igt_pm_enable_pci_card_runtime_pm(device.pci_root, NULL);
		igt_pm_set_d3cold_allowed(device.pci_slot_name, 1);
		return true;
	case IGT_ACPI_D3Hot:
		igt_pm_set_d3cold_allowed(device.pci_slot_name, 0);
		return true;
	default:
		igt_debug("Invalid D3 Selection\n");
	}

	return false;
}

static void cleanup_d3(device_t device)
{
	dpms_on_off(device, DRM_MODE_DPMS_ON);
}

static bool in_d3(device_t device, enum igt_acpi_d_state state)
{
	uint16_t val;

	/* We need to wait for the autosuspend to kick in before we can check */
	if (!igt_wait_for_pm_status(IGT_RUNTIME_PM_STATUS_SUSPENDED))
		return false;

	if (runtime_usage_available(device.pci_xe) &&
	    igt_pm_get_runtime_usage(device.pci_xe) != 0)
		return false;

	switch (state) {
	case IGT_ACPI_D3Hot:
		igt_assert_eq(pci_device_cfg_read_u16(device.pci_xe,
						      &val, 0xd4), 0);
		return (val & 0x3) == 0x3;
	case IGT_ACPI_D3Cold:
		return igt_wait(igt_pm_get_acpi_real_d_state(device.pci_root) ==
				IGT_ACPI_D3Cold, 10000, 100);
	default:
		igt_info("Invalid D3 State\n");
		igt_assert(0);
	}

	return true;
}

static void close_fw_handle(int sig)
{
	if (fw_handle < 0)
		return;

	close(fw_handle);
}

#define MAX_VMAS 2
/**
 * SUBTEST: %s-basic
 * Description: test CPU/GPU in and out of s/d state from %arg[1]
 * Functionality: pm - %arg[1]
 * GPU requirements: D3 feature should be supported
 *
 * SUBTEST: %s-basic-exec
 * Description: test exec on %arg[1] state once without RPM
 * Functionality: pm - %arg[1]
 * GPU requirements: D3 feature should be supported
 *
 * SUBTEST: %s-multiple-execs
 * Description: test exec on %arg[1] state multiple times without RPM
 * Functionality: pm - %arg[1]
 * GPU requirements: D3 feature should be supported
 *
 * arg[1]:
 *
 * @s2idle:	s2idle
 * @s3:		s3
 * @s4:		s4
 * @d3hot:	d3hot
 * @d3cold:	d3cold
 */

/**
 * SUBTEST: %s-exec-after
 * Description: suspend/autoresume on %arg[1] state and exec after RPM
 * Functionality: pm - %arg[1]
 *
 * arg[1]:
 *
 * @s2idle:	s2idle
 * @s3:		s3
 * @s4:		s4
 */

/**
 * SUBTEST: %s-%s-basic-exec
 * Description:
 *	Setup GPU on %arg[2] state then test exec on %arg[1] state
 * 	without RPM
 * Functionality: pm - %arg[1]
 * GPU requirements: D3 feature should be supported
 *
 * arg[1]:
 *
 * @s2idle:	s2idle
 * @s3:		s3
 * @s4:		s4
 *
 * arg[2]:
 *
 * @d3hot:	d3hot
 * @d3cold:	d3cold
 */
/**
 * SUBTEST: %s-vm-bind-%s
 * DESCRIPTION: Test to check suspend/autoresume on %arg[1] state
 * 		with vm bind %arg[2] combination
 * Functionality: pm - %arg[1]
 *
 * arg[1]:
 *
 * @s2idle:     s2idle
 * @s3:         s3
 * @s4:         s4
 *
 * arg[2]:
 *
 * @userptr:	userptr
 * @prefetch:	prefetch
 * @unbind-all:	unbind-all
 */
static void
test_exec(device_t device, struct drm_xe_engine_class_instance *eci,
	  int n_exec_queues, int n_execs, enum igt_suspend_state s_state,
	  enum igt_acpi_d_state d_state, unsigned int flags)
{
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
	int n_vmas = flags & UNBIND_ALL ? MAX_VMAS : 1;
	uint32_t exec_queues[MAX_N_EXEC_QUEUES];
	uint32_t bind_exec_queues[MAX_N_EXEC_QUEUES];
	uint32_t syncobjs[MAX_N_EXEC_QUEUES];
	size_t bo_size;
	uint32_t bo = 0;
	struct {
		uint32_t batch[16];
		uint64_t pad;
		uint32_t data;
	} *data;
	int i, b;
	uint64_t active_time;
	bool check_rpm = (d_state == IGT_ACPI_D3Hot ||
			  d_state == IGT_ACPI_D3Cold);

	igt_assert(n_exec_queues <= MAX_N_EXEC_QUEUES);
	igt_assert(n_execs > 0);

	if (check_rpm) {
		igt_assert(in_d3(device, d_state));
		active_time = igt_pm_get_runtime_active_time(device.pci_xe);
	}

	vm = xe_vm_create(device.fd_xe, 0, 0);

	if (check_rpm)
		igt_assert(igt_pm_get_runtime_active_time(device.pci_xe) >
			   active_time);

	bo_size = sizeof(*data) * n_execs;
	bo_size = xe_bb_size(device.fd_xe, bo_size);

	if (flags & USERPTR) {
		data = aligned_alloc(xe_get_default_alignment(device.fd_xe), bo_size);
		memset(data, 0, bo_size);
	} else {
		if (flags & PREFETCH)
			bo = xe_bo_create(device.fd_xe, 0, bo_size,
					  all_memory_regions(device.fd_xe) |
					  vram_if_possible(device.fd_xe, 0),
					  DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
		else
			bo = xe_bo_create(device.fd_xe, vm, bo_size,
					  vram_if_possible(device.fd_xe, eci->gt_id),
					  DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
		data = xe_bo_map(device.fd_xe, bo, bo_size);
	}

	for (i = 0; i < n_exec_queues; i++) {
		exec_queues[i] = xe_exec_queue_create(device.fd_xe, vm, eci, 0);
		bind_exec_queues[i] = 0;
		syncobjs[i] = syncobj_create(device.fd_xe, 0);
	};

	sync[0].handle = syncobj_create(device.fd_xe, 0);

	if (bo) {
		for (i = 0; i < n_vmas; i++)
			xe_vm_bind_async(device.fd_xe, vm, bind_exec_queues[0], bo, 0,
					 addr + i * bo_size, bo_size, sync, 1);
	} else {
		xe_vm_bind_userptr_async(device.fd_xe, vm, bind_exec_queues[0],
					 to_user_pointer(data), addr, bo_size, sync, 1);
	}

	if (flags & PREFETCH)
		xe_vm_prefetch_async(device.fd_xe, vm, bind_exec_queues[0], 0, addr,
				     bo_size, sync, 1, 0);

	if (check_rpm) {
		igt_assert(in_d3(device, d_state));
		active_time = igt_pm_get_runtime_active_time(device.pci_xe);
	}

	for (i = 0; i < n_execs; i++) {
		uint64_t batch_offset = (char *)&data[i].batch - (char *)data;
		uint64_t batch_addr = addr + batch_offset;
		uint64_t sdi_offset = (char *)&data[i].data - (char *)data;
		uint64_t sdi_addr = addr + sdi_offset;
		int e = i % n_exec_queues;

		b = 0;
		data[i].batch[b++] = MI_STORE_DWORD_IMM_GEN4;
		data[i].batch[b++] = sdi_addr;
		data[i].batch[b++] = sdi_addr >> 32;
		data[i].batch[b++] = 0xc0ffee;
		data[i].batch[b++] = MI_BATCH_BUFFER_END;
		igt_assert(b <= ARRAY_SIZE(data[i].batch));

		sync[0].flags &= ~DRM_XE_SYNC_FLAG_SIGNAL;
		sync[1].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
		sync[1].handle = syncobjs[e];

		exec.exec_queue_id = exec_queues[e];
		exec.address = batch_addr;

		if (e != i)
			syncobj_reset(device.fd_xe, &syncobjs[e], 1);

		xe_exec(device.fd_xe, &exec);

		igt_assert(syncobj_wait(device.fd_xe, &syncobjs[e], 1,
					INT64_MAX, 0, NULL));
		igt_assert_eq(data[i].data, 0xc0ffee);

		if (i == n_execs / 2 && s_state != NO_SUSPEND) {
			enum igt_suspend_test test = s_state == SUSPEND_STATE_DISK ?
				SUSPEND_TEST_DEVICES : SUSPEND_TEST_NONE;

			igt_system_suspend_autoresume(s_state, test);
		}
	}

	igt_assert(syncobj_wait(device.fd_xe, &sync[0].handle, 1, INT64_MAX, 0,
				NULL));

	sync[0].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
	if (n_vmas > 1)
		xe_vm_unbind_all_async(device.fd_xe, vm, 0, bo, sync, 1);
	else
		xe_vm_unbind_async(device.fd_xe, vm, bind_exec_queues[0], 0, addr,
				   bo_size, sync, 1);
	igt_assert(syncobj_wait(device.fd_xe, &sync[0].handle, 1, INT64_MAX, 0,
NULL));

	for (i = 0; i < n_execs; i++)
		igt_assert_eq(data[i].data, 0xc0ffee);

	syncobj_destroy(device.fd_xe, sync[0].handle);
	for (i = 0; i < n_exec_queues; i++) {
		syncobj_destroy(device.fd_xe, syncobjs[i]);
		xe_exec_queue_destroy(device.fd_xe, exec_queues[i]);
		if (bind_exec_queues[i])
			xe_exec_queue_destroy(device.fd_xe, bind_exec_queues[i]);
	}

	if (bo) {
		munmap(data, bo_size);
		gem_close(device.fd_xe, bo);
	} else {
		free(data);
	}

	xe_vm_destroy(device.fd_xe, vm);

	if (check_rpm) {
		igt_assert(igt_pm_get_runtime_active_time(device.pci_xe) >
			   active_time);
		igt_assert(in_d3(device, d_state));
	}
}

/**
 * SUBTEST: vram-d3cold-threshold
 * Functionality: pm - d3cold
 * Description:
 *	Validate whether card is limited to d3hot while vram used
 *	is greater than vram_d3cold_threshold.
 */
static void test_vram_d3cold_threshold(device_t device, int sysfs_fd)
{
	struct drm_xe_query_mem_regions *mem_regions;
	struct drm_xe_device_query query = {
		.extensions = 0,
		.query = DRM_XE_DEVICE_QUERY_MEM_REGIONS,
		.size = 0,
		.data = 0,
	};
	uint64_t vram_used_mb = 0, vram_total_mb = 0, threshold;
	uint32_t bo, placement;
	bool active;
	void *map;
	int i;

	igt_require(xe_has_vram(device.fd_xe));

	placement = vram_memory(device.fd_xe, 0);
	igt_require_f(placement, "Device doesn't support vram memory region\n");

	igt_assert_eq(igt_ioctl(device.fd_xe, DRM_IOCTL_XE_DEVICE_QUERY, &query), 0);
	igt_assert_neq(query.size, 0);

	mem_regions = malloc(query.size);
	igt_assert(mem_regions);

	query.data = to_user_pointer(mem_regions);
	igt_assert_eq(igt_ioctl(device.fd_xe, DRM_IOCTL_XE_DEVICE_QUERY, &query), 0);

	for (i = 0; i < mem_regions->num_mem_regions; i++) {
		if (mem_regions->mem_regions[i].mem_class == DRM_XE_MEM_REGION_CLASS_VRAM) {
			vram_used_mb +=  (mem_regions->mem_regions[i].used / (1024 * 1024));
			vram_total_mb += (mem_regions->mem_regions[i].total_size / (1024 * 1024));
		}
	}

	threshold = vram_used_mb + (SIZE / 1024 /1024);
	igt_require(threshold < vram_total_mb);

	bo = xe_bo_create(device.fd_xe, 0, SIZE, placement, 0);
	map = xe_bo_map(device.fd_xe, bo, SIZE);
	memset(map, 0, SIZE);
	munmap(map, SIZE);
	set_vram_d3cold_threshold(sysfs_fd, threshold);

	/* Setup D3Cold but card should be in D3hot */
	igt_assert(setup_d3(device, IGT_ACPI_D3Cold));
	sleep(1);
	igt_assert(in_d3(device, IGT_ACPI_D3Hot));
	igt_assert(igt_pm_get_acpi_real_d_state(device.pci_root) == IGT_ACPI_D0);
	gem_close(device.fd_xe, bo);

	/*
	 * XXX: Xe gem_close() doesn't get any mem_access ref count to wake
	 * the device from runtime suspend.
	 * Therefore open and close fw handle to wake the device.
	 */
	fw_handle = igt_debugfs_open(device.fd_xe, "forcewake_all", O_RDONLY);
	igt_assert(fw_handle >= 0);
	active = igt_get_runtime_pm_status() == IGT_RUNTIME_PM_STATUS_ACTIVE;
	close(fw_handle);
	igt_assert(active);

	/* Test D3Cold again after freeing up the Xe BO */
	igt_assert(in_d3(device, IGT_ACPI_D3Cold));
}

/**
 * SUBTEST: %s-mmap-%s
 * Description:
 *	Validate mmap memory mapping with %arg[1] state, for %arg[2] region,
 *	if supported by device.
 *
 * arg[1]:
 *
 * @d3hot:	d3hot
 * @d3cold:	d3cold
 *
 * arg[2]:
 *
 * @vram:	vram region
 * @system:	system region
 *
 * Functionality: pm-d3
 */
static void test_mmap(device_t device, uint32_t placement, uint32_t flags,
		      enum mem_op first_op, enum igt_acpi_d_state d_state)
{
	size_t bo_size = 8192;
	uint32_t *map = NULL;
	uint32_t bo;
	uint64_t active_time;
	int i;

	igt_require_f(placement, "Device doesn't support such memory region\n");

	igt_assert(in_d3(device, d_state));
	active_time = igt_pm_get_runtime_active_time(device.pci_xe);

	bo_size = ALIGN(bo_size, xe_get_default_alignment(device.fd_xe));

	bo = xe_bo_create(device.fd_xe, 0, bo_size, placement, flags);
	map = xe_bo_map(device.fd_xe, bo, bo_size);
	igt_assert(map);
	memset(map, 0, bo_size);

	fw_handle = igt_debugfs_open(device.fd_xe, "forcewake_all", O_RDONLY);

	igt_assert(fw_handle >= 0);
	igt_assert(igt_pm_get_runtime_active_time(device.pci_xe) >
		   active_time);

	for (i = 0; i < bo_size / sizeof(*map); i++)
		map[i] = MAGIC_1;

	for (i = 0; i < bo_size / sizeof(*map); i++)
		igt_assert(map[i] == MAGIC_1);

	/* Runtime suspend and validate the pattern and changed the pattern */
	close(fw_handle);
	sleep(1);

	igt_assert(in_d3(device, d_state));
	active_time = igt_pm_get_runtime_active_time(device.pci_xe);

	for (i = 0; i < bo_size / sizeof(*map); i++) {
		if (first_op == READ)
			igt_assert(map[i] == MAGIC_1);
		else
			map[i] = MAGIC_2;
	}

	/* dgfx page-fault on mmaping should wake the gpu */
	if (xe_has_vram(device.fd_xe) && flags & DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM)
		igt_assert(igt_pm_get_runtime_active_time(device.pci_xe) >
			   active_time);

	igt_assert(in_d3(device, d_state));
	active_time = igt_pm_get_runtime_active_time(device.pci_xe);

	for (i = 0; i < bo_size / sizeof(*map); i++) {
		if (first_op == READ)
			map[i] = MAGIC_2;
		else
			igt_assert(map[i] == MAGIC_2);
	}

	igt_assert(in_d3(device, d_state));

	/* Runtime resume and check the pattern */
	fw_handle = igt_debugfs_open(device.fd_xe, "forcewake_all", O_RDONLY);
	igt_assert(fw_handle >= 0);
	igt_assert(igt_get_runtime_pm_status() == IGT_RUNTIME_PM_STATUS_ACTIVE);
	for (i = 0; i < bo_size / sizeof(*map); i++)
		igt_assert(map[i] == MAGIC_2);

	igt_assert(munmap(map, bo_size) == 0);
	gem_close(device.fd_xe, bo);
	close(fw_handle);
}

/**
 * SUBTEST: mocs-rpm
 * Description:
 *     Validate mocs register contents over suspend resume
 *
 * Functionality: mocs registers
 * Run type: FULL
 */
/**
 * SUBTEST: %s-mocs
 * Description: test checks for mocs state before and after %arg[1] state
 * Functionality: pm - %arg[1]
 *
 * arg[1]:
 *
 * @s2idle:	s2idle
 * @s3:		s3
 * @s4:		s4
 */
static void test_mocs_suspend_resume(device_t device, int s_state)
{
	int gt;

	xe_for_each_gt(device.fd_xe, gt) {
		char path[256];

		// Mocs debugfs contents before and after suspend-resume
		char mocs_content_pre[4096], mocs_contents_post[4096];

		sprintf(path, "gt%d/mocs", gt);
		igt_assert(igt_debugfs_exists(device.fd_xe, path, O_RDONLY));
		igt_debugfs_dump(device.fd_xe, path);
		igt_debugfs_read(device.fd_xe, path, mocs_content_pre);

		if (s_state == NO_SUSPEND) {
			fw_handle = igt_debugfs_open(device.fd_xe, "forcewake_all", O_RDONLY);
			igt_assert(fw_handle >= 0);
			igt_assert(igt_get_runtime_pm_status() == IGT_RUNTIME_PM_STATUS_ACTIVE);

			/* Make sure runtime pm goes back to suspended status after closing forcewake_all */
			close(fw_handle);
			igt_assert(igt_wait_for_pm_status(IGT_RUNTIME_PM_STATUS_SUSPENDED));
		} else {
			enum igt_suspend_test test = s_state == SUSPEND_STATE_DISK ?
				SUSPEND_TEST_DEVICES : SUSPEND_TEST_NONE;

			igt_system_suspend_autoresume(s_state, test);
		}
		igt_assert(igt_debugfs_exists(device.fd_xe, path, O_RDONLY));
		igt_debugfs_dump(device.fd_xe, path);
		igt_debugfs_read(device.fd_xe, path, mocs_contents_post);

		igt_assert(strcmp(mocs_content_pre, mocs_contents_post) == 0);
	}
}

igt_main
{
	struct drm_xe_engine_class_instance *hwe;
	device_t device;
	uint32_t d3cold_allowed;
	int sysfs_fd;

	const struct s_state {
		const char *name;
		enum igt_suspend_state state;
	} s_states[] = {
		{ "s2idle", SUSPEND_STATE_FREEZE },
		{ "s3", SUSPEND_STATE_S3 },
		{ "s4", SUSPEND_STATE_DISK },
		{ NULL },
	};
	const struct d_state {
		const char *name;
		enum igt_acpi_d_state state;
	} d_states[] = {
		{ "d3hot", IGT_ACPI_D3Hot },
		{ "d3cold", IGT_ACPI_D3Cold },
		{ NULL },
	};
	const struct vm_op {
		const char *name;
		unsigned int flags;
	} vm_op[] = {
		{ "userptr", USERPTR },
		{ "prefetch", PREFETCH },
		{ "unbind-all", UNBIND_ALL },
		{ NULL },
	};

	igt_fixture {
		memset(&device, 0, sizeof(device));
		device.fd_xe = drm_open_driver(DRIVER_XE);
		device.pci_xe = igt_device_get_pci_device(device.fd_xe);
		device.pci_root = igt_device_get_pci_root_port(device.fd_xe);
		igt_device_get_pci_slot_name(device.fd_xe, device.pci_slot_name);

		/* Always perform initial once-basic exec checking for health */
		xe_for_each_engine(device.fd_xe, hwe)
			test_exec(device, hwe, 1, 1, NO_SUSPEND, NO_RPM, 0);

		igt_pm_get_d3cold_allowed(device.pci_slot_name, &d3cold_allowed);
		igt_assert(igt_setup_runtime_pm(device.fd_xe));
		sysfs_fd = igt_sysfs_open(device.fd_xe);
		device.res = drmModeGetResources(device.fd_xe);
	}

	for (const struct s_state *s = s_states; s->name; s++) {
		igt_subtest_f("%s-basic", s->name) {
			enum igt_suspend_test test = s->state == SUSPEND_STATE_DISK ?
				SUSPEND_TEST_DEVICES : SUSPEND_TEST_NONE;

			igt_system_suspend_autoresume(s->state, test);
		}

		igt_subtest_f("%s-basic-exec", s->name) {
			xe_for_each_engine(device.fd_xe, hwe)
				test_exec(device, hwe, 1, 2, s->state,
					  NO_RPM, 0);
		}

		igt_subtest_f("%s-exec-after", s->name) {
			enum igt_suspend_test test = s->state == SUSPEND_STATE_DISK ?
				SUSPEND_TEST_DEVICES : SUSPEND_TEST_NONE;

			igt_system_suspend_autoresume(s->state, test);
			xe_for_each_engine(device.fd_xe, hwe)
				test_exec(device, hwe, 1, 2, NO_SUSPEND,
					  NO_RPM, 0);
		}

		igt_subtest_f("%s-multiple-execs", s->name) {
			xe_for_each_engine(device.fd_xe, hwe)
				test_exec(device, hwe, 16, 32, s->state,
					  NO_RPM, 0);
		}

		for (const struct vm_op *op = vm_op; op->name; op++) {
			igt_subtest_f("%s-vm-bind-%s", s->name, op->name) {
				xe_for_each_engine(device.fd_xe, hwe)
					test_exec(device, hwe, 16, 32, s->state,
						  NO_RPM, op->flags);
			}
		}

		for (const struct d_state *d = d_states; d->name; d++) {
			igt_subtest_f("%s-%s-basic-exec", s->name, d->name) {
				igt_assert(setup_d3(device, d->state));
				xe_for_each_engine(device.fd_xe, hwe)
					test_exec(device, hwe, 1, 2, s->state,
						  NO_RPM, 0);
				cleanup_d3(device);
			}
		}

		igt_subtest_f("%s-mocs", s->name)
			test_mocs_suspend_resume(device, s->state);
	}

	igt_fixture {
		igt_install_exit_handler(close_fw_handle);
	}

	for (const struct d_state *d = d_states; d->name; d++) {
		igt_subtest_f("%s-basic", d->name) {
			igt_assert(setup_d3(device, d->state));
			igt_assert(in_d3(device, d->state));
			cleanup_d3(device);
		}

		igt_subtest_f("%s-basic-exec", d->name) {
			igt_assert(setup_d3(device, d->state));
			xe_for_each_engine(device.fd_xe, hwe)
				test_exec(device, hwe, 1, 1,
					  NO_SUSPEND, d->state, 0);
			cleanup_d3(device);
		}

		igt_subtest_f("%s-multiple-execs", d->name) {
			igt_assert(setup_d3(device, d->state));
			xe_for_each_engine(device.fd_xe, hwe)
				test_exec(device, hwe, 16, 32,
					  NO_SUSPEND, d->state, 0);
			cleanup_d3(device);
		}

		igt_describe_f("Validate mmap memory mappings with system region,"
			       "when device along with parent bridge in %s", d->name);
		igt_subtest_f("%s-mmap-system", d->name) {
			igt_assert(setup_d3(device, d->state));
			test_mmap(device, system_memory(device.fd_xe), 0,
				  READ, d->state);
			test_mmap(device, system_memory(device.fd_xe), 0,
				  WRITE, d->state);
			cleanup_d3(device);
		}

		igt_describe_f("Validate mmap memory mappings with vram region,"
			     "when device along with parent bridge in %s", d->name);
		igt_subtest_f("%s-mmap-vram", d->name) {
			int delay_ms = igt_pm_get_autosuspend_delay(device.pci_xe);

			/* Give some auto suspend delay to validate rpm active during page fault */
			igt_pm_set_autosuspend_delay(device.pci_xe, 1000);
			igt_assert(setup_d3(device, d->state));
			test_mmap(device, vram_memory(device.fd_xe, 0),
				  DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM,
				  READ, d->state);
			test_mmap(device, vram_memory(device.fd_xe, 0),
				  DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM,
				  WRITE, d->state);
			cleanup_d3(device);

			igt_pm_set_autosuspend_delay(device.pci_xe, delay_ms);
		}
	}

	igt_describe("Validate whether card is limited to d3hot,"
		     "if vram used > vram threshold");
	igt_subtest("vram-d3cold-threshold") {
		orig_threshold = get_vram_d3cold_threshold(sysfs_fd);
		igt_install_exit_handler(vram_d3cold_threshold_restore);
		test_vram_d3cold_threshold(device, sysfs_fd);
	}

	igt_subtest("mocs-rpm") {
		dpms_on_off(device, DRM_MODE_DPMS_OFF);
		test_mocs_suspend_resume(device, NO_SUSPEND);
		dpms_on_off(device, DRM_MODE_DPMS_ON);
	}

	igt_fixture {
		close(sysfs_fd);
		igt_pm_set_d3cold_allowed(device.pci_slot_name, d3cold_allowed);
		igt_restore_runtime_pm();
		drmModeFreeResources(device.res);
		drm_close_driver(device.fd_xe);
	}
}
