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
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"

#define INJECT_ERRNO			-ENOMEM

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
 * Description: inject an error in the injectable function %arg[1] then reprobe driver
 * Functionality: fault
 *
 * arg[1]:
 * @wait_for_lmem_ready:	wait_for_lmem_ready
 * @xe_device_create:		xe_device_create
 * @xe_ggtt_init_early:		xe_ggtt_init_early
 * @xe_guc_ads_init:		xe_guc_ads_init
 * @xe_guc_ct_init:		xe_guc_ct_init
 * @xe_guc_log_init:		xe_guc_log_init
 * @xe_guc_relay_init:		xe_guc_relay_init
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

	injection_list_do(INJECTION_LIST_ADD, function_name);
	set_retval(function_name, INJECT_ERRNO);
	xe_sysfs_driver_do(fd, pci_slot, XE_SYSFS_DRIVER_TRY_BIND);
	igt_assert_eq(-errno, INJECT_ERRNO);
	injection_list_do(INJECTION_LIST_REMOVE, function_name);
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

	injection_list_do(INJECTION_LIST_ADD, function_name);
	set_retval(function_name, INJECT_ERRNO);
	igt_assert(simple_vm_create(fd, flags) != 0);
	injection_list_do(INJECTION_LIST_REMOVE, function_name);

	igt_assert_eq(simple_vm_create(fd, flags), 0);
}

igt_main
{
	int fd;
	char pci_slot[NAME_MAX];
	const struct section {
		const char *name;
		unsigned int flags;
	} probe_fail_functions[] = {
		{ "wait_for_lmem_ready" },
		{ "xe_device_create" },
		{ "xe_ggtt_init_early" },
		{ "xe_guc_ads_init" },
		{ "xe_guc_ct_init" },
		{ "xe_guc_log_init" },
		{ "xe_guc_relay_init" },
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

	igt_fixture {
		igt_require(fail_function_injection_enabled());
		fd = drm_open_driver(DRIVER_XE);
		igt_device_get_pci_slot_name(fd, pci_slot);
		setup_injection_fault();
	}

	for (const struct section *s = vm_create_fail_functions; s->name; s++)
		igt_subtest_f("vm-create-fail-%s", s->name)
			vm_create_fail(fd, s->name, s->flags);

	igt_fixture {
		xe_sysfs_driver_do(fd, pci_slot, XE_SYSFS_DRIVER_UNBIND);
	}

	for (const struct section *s = probe_fail_functions; s->name; s++)
		igt_subtest_f("inject-fault-probe-function-%s", s->name)
			inject_fault_probe(fd, pci_slot, s->name);

	igt_fixture {
		drm_close_driver(fd);
		xe_sysfs_driver_do(fd, pci_slot, XE_SYSFS_DRIVER_BIND);
	}


}
