// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

/**
 * TEST: Check fault injection
 * Category: Core
 * Mega feature: General Core features
 * Sub-category: driver
 * Test category: fault injection
 */

#include <limits.h>

#include "igt.h"
#include "igt_device.h"
#include "igt_kmod.h"
#include "igt_sysfs.h"
#include "lib/igt_syncobj.h"
#include "lib/intel_pat.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_oa.h"
#include "xe/xe_query.h"

#define INJECT_ERRNO	-ENOMEM
#define BO_ADDR		0x1a0000
#define BO_SIZE		(1024*1024)

enum injection_list_action {
	INJECTION_LIST_ADD,
	INJECTION_LIST_REMOVE,
};

static int fail_function_open(void)
{
	int debugfs_fail_function_dir_fd;
	const char *debugfs_root;
	char path[96];

	debugfs_root = igt_debugfs_mount();
	igt_assert(debugfs_root);

	sprintf(path, "%s/fail_function", debugfs_root);

	if (access(path, F_OK))
		return -1;

	debugfs_fail_function_dir_fd = open(path, O_RDONLY);
	igt_debug_on_f(debugfs_fail_function_dir_fd < 0, "path: %s\n", path);

	return debugfs_fail_function_dir_fd;
}

static bool function_is_part_of_guc(const char function_name[])
{
	return strstr(function_name, "_guc_") != NULL ||
	       strstr(function_name, "_uc_") != NULL ||
	       strstr(function_name, "_wopcm_") != NULL;
}

static void ignore_faults_in_dmesg(const char function_name[])
{
	/* Driver probe is expected to fail in all cases, so ignore in igt_runner */
	char regex[1024] = "probe with driver xe failed with error -12";

	/*
	 * If GuC module fault is injected, GuC is expected to fail,
	 * so also ignore GuC init failures in igt_runner.
	 */
	if (function_is_part_of_guc(function_name)) {
		strcat(regex, "|GT[0-9a-fA-F]*: GuC init failed with -ENOMEM");
		strcat(regex, "|GT[0-9a-fA-F]*: Failed to initialize uC .-ENOMEM");
	}

	igt_emit_ignore_dmesg_regex(regex);
}

/*
 * The injectable file requires CONFIG_FUNCTION_ERROR_INJECTION in kernel.
 */
static bool fail_function_injection_enabled(void)
{
	char *contents;
	int dir;

	dir = fail_function_open();
	if (dir < 0)
		return false;

	contents = igt_sysfs_get(dir, "injectable");
	if (contents == NULL)
		return false;

	free(contents);

	return true;
}

static void injection_list_do(enum injection_list_action action, const char function_name[])
{
	int dir;

	dir = fail_function_open();
	igt_assert_lte(0, dir);

	switch(action) {
	case INJECTION_LIST_ADD:
		igt_assert_lte(0, igt_sysfs_printf(dir, "inject", "%s", function_name));
		break;
	case INJECTION_LIST_REMOVE:
		igt_assert_lte(0, igt_sysfs_printf(dir, "inject", "!%s", function_name));
		break;
	default:
		igt_assert(!"missing");
	}

	close(dir);
}

/*
 * See https://docs.kernel.org/fault-injection/fault-injection.html#application-examples
 */
static void setup_injection_fault(void)
{
	int dir;

	dir = fail_function_open();
	igt_assert_lte(0, dir);

	igt_assert_lte(0, igt_sysfs_printf(dir, "task-filter", "N"));
	igt_sysfs_set_u32(dir, "probability", 100);
	igt_sysfs_set_u32(dir, "interval", 0);
	igt_sysfs_set_s32(dir, "times", -1);
	igt_sysfs_set_u32(dir, "space", 0);
	igt_sysfs_set_u32(dir, "verbose", 1);

	close(dir);
}

static void cleanup_injection_fault(int sig)
{
	/* If nothing specified (‘’) injection list is cleared */
	injection_list_do(INJECTION_LIST_ADD, "");
}

static void set_retval(const char function_name[], long long retval)
{
	char path[96];
	int dir;

	dir = fail_function_open();
	igt_assert_lte(0, dir);

	sprintf(path, "%s/retval", function_name);
	igt_assert_lte(0, igt_sysfs_printf(dir, path, "%#016llx", retval));

	close(dir);
}

/**
 * SUBTEST: inject-fault-probe-function-%s
 * Description: inject an error in the injectable function %arg[1] then
 *		reprobe driver
 * Functionality: fault
 *
 * arg[1]:
 * @wait_for_lmem_ready:	wait_for_lmem_ready
 * @xe_device_create:		xe_device_create
 * @xe_device_probe_early:	xe_device_probe_early
 * @xe_ggtt_init_early:		xe_ggtt_init_early
 * @xe_guc_ads_init:		xe_guc_ads_init
 * @xe_guc_ct_init:		xe_guc_ct_init
 * @xe_guc_log_init:		xe_guc_log_init
 * @xe_guc_relay_init:		xe_guc_relay_init
 * @xe_mmio_probe_early:	xe_mmio_probe_early
 * @xe_pcode_probe_early:	xe_pcode_probe_early
 * @xe_pm_init_early:		xe_pm_init_early
 * @xe_sriov_init:		xe_sriov_init
 * @xe_tile_init_early:		xe_tile_init_early
 * @xe_uc_fw_init:		xe_uc_fw_init
 * @xe_wa_init:			xe_wa_init
 * @xe_wopcm_init:		xe_wopcm_init
 */
static void
inject_fault_probe(int fd, char pci_slot[], const char function_name[])
{
	igt_info("Injecting error \"%s\" (%d) in function \"%s\"\n",
		 strerror(-INJECT_ERRNO), INJECT_ERRNO, function_name);

	ignore_faults_in_dmesg(function_name);
	injection_list_do(INJECTION_LIST_ADD, function_name);
	set_retval(function_name, INJECT_ERRNO);
	xe_sysfs_driver_do(fd, pci_slot, XE_SYSFS_DRIVER_TRY_BIND);
	igt_assert_eq(-errno, INJECT_ERRNO);
	injection_list_do(INJECTION_LIST_REMOVE, function_name);
}

/**
 * SUBTEST: exec-queue-create-fail-%s
 * Description: inject an error in function %arg[1] used in exec queue create IOCTL to make it fail
 * Functionality: fault
 *
 * arg[1]:
 * @xe_exec_queue_create:                 xe_exec_queue_create
 * @xe_hw_engine_group_add_exec_queue:    xe_hw_engine_group_add_exec_queue
 * @xe_vm_add_compute_exec_queue:         xe_vm_add_compute_exec_queue
 * @xe_exec_queue_create_bind:            xe_exec_queue_create_bind
 */
static void
exec_queue_create_fail(int fd, struct drm_xe_engine_class_instance *instance,
		const char function_name[], unsigned int flags)
{
	uint32_t exec_queue_id;
	uint32_t vm = xe_vm_create(fd, flags, 0);
	/* sanity check */
	igt_assert_eq(__xe_exec_queue_create(fd, vm, 1, 1, instance, 0, &exec_queue_id), 0);
	xe_exec_queue_destroy(fd, exec_queue_id);

	ignore_faults_in_dmesg(function_name);
	injection_list_do(INJECTION_LIST_ADD, function_name);
	set_retval(function_name, INJECT_ERRNO);
	igt_assert(__xe_exec_queue_create(fd, vm, 1, 1, instance, 0, &exec_queue_id) != 0);
	injection_list_do(INJECTION_LIST_REMOVE, function_name);

	igt_assert_eq(__xe_exec_queue_create(fd, vm, 1, 1, instance, 0, &exec_queue_id), 0);
	xe_exec_queue_destroy(fd, exec_queue_id);
}

static int
simple_vm_create(int fd, unsigned int flags)
{
	struct drm_xe_vm_create create = {
		.flags = flags,
	};

	return igt_ioctl(fd, DRM_IOCTL_XE_VM_CREATE, &create);
}

/**
 * SUBTEST: vm-create-fail-%s
 * Description: inject an error in function %arg[1] used in vm create IOCTL to make it fail
 * Functionality: fault
 *
 * arg[1]:
 * @xe_exec_queue_create_bind:	xe_exec_queue_create_bind
 * @xe_pt_create:		xe_pt_create
 * @xe_vm_create_scratch:	xe_vm_create_scratch
 */
static void
vm_create_fail(int fd, const char function_name[], unsigned int flags)
{
	igt_assert_eq(simple_vm_create(fd, flags), 0);

	ignore_faults_in_dmesg(function_name);
	injection_list_do(INJECTION_LIST_ADD, function_name);
	set_retval(function_name, INJECT_ERRNO);
	igt_assert(simple_vm_create(fd, flags) != 0);
	injection_list_do(INJECTION_LIST_REMOVE, function_name);

	igt_assert_eq(simple_vm_create(fd, flags), 0);
}

static int
simple_vm_bind(int fd, uint32_t vm)
{
	struct {
		uint32_t batch[16];
		uint64_t pad;
		uint32_t data;
	} *data;
	struct drm_xe_sync syncobj = {
		.type = DRM_XE_SYNC_TYPE_SYNCOBJ,
		.flags = DRM_XE_SYNC_FLAG_SIGNAL,
		.handle = syncobj_create(fd, 0),
	};
	struct drm_xe_vm_bind bind = {
		.vm_id = vm,
		.num_binds = 1,
		.bind.obj = 0,
		.bind.range = BO_SIZE,
		.bind.addr = BO_ADDR,
		.bind.op = DRM_XE_VM_BIND_OP_MAP_USERPTR,
		.bind.pat_index = intel_get_pat_idx_wb(fd),
		.bind.flags = 0,
		.num_syncs = 1,
		.syncs = (uintptr_t)&syncobj,
		.exec_queue_id = 0,
	};

	data = aligned_alloc(xe_get_default_alignment(fd), BO_SIZE);
	bind.bind.obj_offset = to_user_pointer(data);

	return igt_ioctl(fd, DRM_IOCTL_XE_VM_BIND, &bind);
}

/**
 * SUBTEST: vm-bind-fail-%s
 * Description: inject an error in function %arg[1] used in vm bind IOCTL
 *		to make it fail
 * Functionality: fault
 *
 * arg[1]:
 * @vm_bind_ioctl_ops_create:		vm_bind_ioctl_ops_create
 * @vm_bind_ioctl_ops_execute:		vm_bind_ioctl_ops_execute
 * @xe_pt_update_ops_prepare:		xe_pt_update_ops_prepare
 * @xe_pt_update_ops_run:		xe_pt_update_ops_run
 * @xe_vma_ops_alloc:			xe_vma_ops_alloc
 * @xe_sync_entry_parse:		xe_sync_entry_parse
 */
static void
vm_bind_fail(int fd, const char function_name[])
{
	uint32_t vm = xe_vm_create(fd, 0, 0);

	igt_assert_eq(simple_vm_bind(fd, vm), 0);

	ignore_faults_in_dmesg(function_name);
	injection_list_do(INJECTION_LIST_ADD, function_name);
	set_retval(function_name, INJECT_ERRNO);
	igt_assert(simple_vm_bind(fd, vm) != 0);
	injection_list_do(INJECTION_LIST_REMOVE, function_name);

	igt_assert_eq(simple_vm_bind(fd, vm), 0);
}

/**
 * SUBTEST: oa-add-config-fail-%s
 * Description: inject an error in function %arg[1] used in oa add config IOCTL to make it fail
 * Functionality: fault
 *
 * arg[1]:
 * @xe_oa_alloc_regs:		xe_oa_alloc_regs
 */
static void
oa_add_config_fail(int fd, int sysfs, int devid, const char function_name[])
{
	char path[512];
	uint64_t config_id;
#define SAMPLE_MUX_REG (intel_graphics_ver(devid) >= IP_VER(20, 0) ?	\
			0x13000 /* PES* */ : 0x9888 /* NOA_WRITE */)

	uint32_t mux_regs[] = { SAMPLE_MUX_REG, 0x0 };
	struct drm_xe_oa_config config;
	const char *uuid = "01234567-0123-0123-0123-0123456789ab";

	snprintf(path, sizeof(path), "metrics/%s/id", uuid);
	/* Destroy previous configuration if present */
	if (igt_sysfs_scanf(sysfs, path, "%" PRIu64, &config_id) == 1)
		igt_assert_eq(intel_xe_perf_ioctl(fd, DRM_XE_OBSERVATION_OP_REMOVE_CONFIG,
						  &config_id), 0);

	memset(&config, 0, sizeof(config));
	memcpy(config.uuid, uuid, sizeof(config.uuid));
	config.n_regs = 1;
	config.regs_ptr = to_user_pointer(mux_regs);

	igt_assert_lt(0, intel_xe_perf_ioctl(fd, DRM_XE_OBSERVATION_OP_ADD_CONFIG, &config));
	igt_assert(igt_sysfs_scanf(sysfs, path, "%" PRIu64, &config_id) == 1);
	igt_assert_eq(intel_xe_perf_ioctl(fd, DRM_XE_OBSERVATION_OP_REMOVE_CONFIG, &config_id), 0);

	ignore_faults_in_dmesg(function_name);
	injection_list_do(INJECTION_LIST_ADD, function_name);
	set_retval(function_name, INJECT_ERRNO);
	igt_assert_lt(intel_xe_perf_ioctl(fd, DRM_XE_OBSERVATION_OP_ADD_CONFIG, &config), 0);
	injection_list_do(INJECTION_LIST_REMOVE, function_name);

	igt_assert_lt(0, intel_xe_perf_ioctl(fd, DRM_XE_OBSERVATION_OP_ADD_CONFIG, &config));
	igt_assert(igt_sysfs_scanf(sysfs, path, "%" PRIu64, &config_id) == 1);
	igt_assert_eq(intel_xe_perf_ioctl(fd, DRM_XE_OBSERVATION_OP_REMOVE_CONFIG, &config_id), 0);
}

igt_main
{
	int fd, sysfs;
	struct drm_xe_engine_class_instance *hwe;
	static uint32_t devid;
	char pci_slot[NAME_MAX];
	const struct section {
		const char *name;
		unsigned int flags;
	} probe_fail_functions[] = {
		{ "wait_for_lmem_ready" },
		{ "xe_device_create" },
		{ "xe_device_probe_early" },
		{ "xe_ggtt_init_early" },
		{ "xe_guc_ads_init" },
		{ "xe_guc_ct_init" },
		{ "xe_guc_log_init" },
		{ "xe_guc_relay_init" },
		{ "xe_mmio_probe_early" },
		{ "xe_pcode_probe_early" },
		{ "xe_pm_init_early" },
		{ "xe_sriov_init" },
		{ "xe_tile_init_early" },
		{ "xe_uc_fw_init" },
		{ "xe_wa_init" },
		{ "xe_wopcm_init" },
		{ }
	};
	const struct section vm_create_fail_functions[] = {
		{ "xe_exec_queue_create_bind", 0 },
		{ "xe_pt_create", 0 },
		{ "xe_vm_create_scratch", DRM_XE_VM_CREATE_FLAG_SCRATCH_PAGE },
		{ }
	};
	const struct section vm_bind_fail_functions[] = {
		{ "vm_bind_ioctl_ops_create" },
		{ "vm_bind_ioctl_ops_execute" },
		{ "xe_pt_update_ops_prepare" },
		{ "xe_pt_update_ops_run" },
		{ "xe_vma_ops_alloc" },
		{ "xe_sync_entry_parse" },
		{ }
	};

	const struct section exec_queue_create_fail_functions[] = {
		{ "xe_exec_queue_create", 0 },
		{ "xe_hw_engine_group_add_exec_queue", 0 },
		{ "xe_vm_add_compute_exec_queue", DRM_XE_VM_CREATE_FLAG_LR_MODE },
		{ }
	};

	const struct section exec_queue_create_vmbind_fail_functions[] = {
		{ "xe_exec_queue_create_bind", 0 },
		{ }
	};

	const struct section oa_add_config_fail_functions[] = {
		{ "xe_oa_alloc_regs"},
		{ }
	};

	igt_fixture {
		igt_require(fail_function_injection_enabled());
		fd = drm_open_driver(DRIVER_XE);
		devid = intel_get_drm_devid(fd);
		sysfs = igt_sysfs_open(fd);
		igt_device_get_pci_slot_name(fd, pci_slot);
		setup_injection_fault();
		igt_install_exit_handler(cleanup_injection_fault);
	}

	for (const struct section *s = vm_create_fail_functions; s->name; s++)
		igt_subtest_f("vm-create-fail-%s", s->name)
			vm_create_fail(fd, s->name, s->flags);

	for (const struct section *s = vm_bind_fail_functions; s->name; s++)
		igt_subtest_f("vm-bind-fail-%s", s->name)
			vm_bind_fail(fd, s->name);

	for (const struct section *s = exec_queue_create_fail_functions; s->name; s++)
		igt_subtest_f("exec-queue-create-fail-%s", s->name)
			xe_for_each_engine(fd, hwe)
				if (hwe->engine_class != DRM_XE_ENGINE_CLASS_VM_BIND)
					exec_queue_create_fail(fd, hwe, s->name, s->flags);

	for (const struct section *s = exec_queue_create_vmbind_fail_functions; s->name; s++)
		igt_subtest_f("exec-queue-create-fail-%s", s->name)
			xe_for_each_engine(fd, hwe)
				if (hwe->engine_class == DRM_XE_ENGINE_CLASS_VM_BIND)
					exec_queue_create_fail(fd, hwe, s->name, s->flags);

	for (const struct section *s = oa_add_config_fail_functions; s->name; s++)
		igt_subtest_f("oa-add-config-fail-%s", s->name)
			oa_add_config_fail(fd, sysfs, devid, s->name);

	igt_fixture {
		igt_kmod_unbind("xe", pci_slot);
	}

	for (const struct section *s = probe_fail_functions; s->name; s++)
		igt_subtest_f("inject-fault-probe-function-%s", s->name)
			inject_fault_probe(fd, pci_slot, s->name);

	igt_fixture {
		close(sysfs);
		drm_close_driver(fd);
		xe_sysfs_driver_do(fd, pci_slot, XE_SYSFS_DRIVER_BIND);
	}


}
