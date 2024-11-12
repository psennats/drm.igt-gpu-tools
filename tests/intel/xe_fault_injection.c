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

#include "igt.h"
#include "igt_device.h"
#include "igt_kmod.h"
#include "igt_sysfs.h"

#define MAX_LINE_SIZE			1024
#define PATH_FUNCTIONS_INJECTABLE	"/sys/kernel/debug/fail_function/injectable"
#define PATH_FUNCTIONS_INJECT		"/sys/kernel/debug/fail_function/inject"
#define PATH_FUNCTIONS_RETVAL		"/sys/kernel/debug/fail_function/%s/retval"
#define INJECT_ERRNO			-ENOMEM

enum injection_list_action {
	INJECTION_LIST_ADD,
	INJECTION_LIST_REMOVE,
};

/*
 * The injectable file requires CONFIG_FUNCTION_ERROR_INJECTION in kernel.
 */
static bool function_error_injection_enabled(void)
{
	FILE *file = fopen(PATH_FUNCTIONS_INJECTABLE, "rw");

	if (file) {
		fclose(file);
		return true;
	}

	return false;
}

static void injection_list_do(enum injection_list_action action, const char function_name[])
{
	FILE *file_inject;

	file_inject = fopen(PATH_FUNCTIONS_INJECT, "w");
	igt_assert(file_inject);

	switch(action) {
	case INJECTION_LIST_ADD:
		fprintf(file_inject, "%s", function_name);
		break;
	case INJECTION_LIST_REMOVE:
		fprintf(file_inject, "!%s", function_name);
		break;
	default:
		igt_assert(!"missing");
	}

	fclose(file_inject);
}

/*
 * See https://docs.kernel.org/fault-injection/fault-injection.html#application-examples
 */
static void setup_injection_fault(void)
{
	FILE *file;

	file = fopen("/sys/kernel/debug/fail_function/task-filter", "w");
	igt_assert(file);
	fprintf(file, "N");
	fclose(file);

	file = fopen("/sys/kernel/debug/fail_function/probability", "w");
	igt_assert(file);
	fprintf(file, "100");
	fclose(file);

	file = fopen("/sys/kernel/debug/fail_function/interval", "w");
	igt_assert(file);
	fprintf(file, "0");
	fclose(file);

	file = fopen("/sys/kernel/debug/fail_function/times", "w");
	igt_assert(file);
	fprintf(file, "-1");
	fclose(file);

	file = fopen("/sys/kernel/debug/fail_function/space", "w");
	igt_assert(file);
	fprintf(file, "0");
	fclose(file);

	file = fopen("/sys/kernel/debug/fail_function/verbose", "w");
	igt_assert(file);
	fprintf(file, "1");
	fclose(file);
}

static void cleanup_injection_fault(void)
{
	FILE *file;

	file = fopen(PATH_FUNCTIONS_INJECT, "w");
	igt_assert(file);
	fprintf(file, "\n");
	fclose(file);
}

static void set_retval(const char function_name[], long long retval)
{
	FILE *file_retval;
	char file_path[MAX_LINE_SIZE];

	sprintf(file_path, PATH_FUNCTIONS_RETVAL, function_name);

	file_retval = fopen(file_path, "w");
	igt_assert(file_retval);

	fprintf(file_retval, "%#016llx", retval);
	fclose(file_retval);
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

igt_main
{
	int fd;
	char pci_slot[MAX_LINE_SIZE];
	const struct section {
		const char *name;
	} probe_function_sections[] = {
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

	igt_fixture {
		igt_require(function_error_injection_enabled());
		fd = drm_open_driver(DRIVER_XE);
		igt_device_get_pci_slot_name(fd, pci_slot);
		setup_injection_fault();
		xe_sysfs_driver_do(fd, pci_slot, XE_SYSFS_DRIVER_UNBIND);
	}

	for (const struct section *s = probe_function_sections; s->name; s++)
		igt_subtest_f("inject-fault-probe-function-%s", s->name)
			inject_fault_probe(fd, pci_slot, s->name);

	igt_fixture {
		cleanup_injection_fault();
		drm_close_driver(fd);
		xe_sysfs_driver_do(fd, pci_slot, XE_SYSFS_DRIVER_BIND);
	}
}
