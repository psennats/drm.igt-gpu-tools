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

#include <regex.h>

#include "igt.h"
#include "igt_device.h"
#include "igt_kmod.h"
#include "igt_sysfs.h"

#define MAX_LINE_SIZE			1024
#define PATH_FUNCTIONS_INJECTABLE	"/sys/kernel/debug/fail_function/injectable"
#define PATH_FUNCTIONS_INJECT		"/sys/kernel/debug/fail_function/inject"
#define PATH_FUNCTIONS_RETVAL		"/sys/kernel/debug/fail_function/%s/retval"
#define REGEX_XE_FUNCTIONS		"^(.+)\\[xe\\]"
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

static void injection_list_do(enum injection_list_action action, char function_name[])
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

static void set_retval(char function_name[], long long retval)
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
 * SUBTEST: inject-fault-probe
 * Description: inject an error in the injectable function then reprobe driver
 * Functionality: fault
 */
static void
inject_fault_try_bind(int fd, char pci_slot[], char function_name[])
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
	FILE *file_injectable;
	char line[MAX_LINE_SIZE];
	char function_name[64];
	char pci_slot[MAX_LINE_SIZE];
	regex_t regex;
	regmatch_t pmatch[2];

	igt_fixture {
		igt_require(function_error_injection_enabled());
		fd = drm_open_driver(DRIVER_XE);
		igt_device_get_pci_slot_name(fd, pci_slot);
		setup_injection_fault();
		file_injectable = fopen(PATH_FUNCTIONS_INJECTABLE, "r");
		igt_assert(file_injectable);
		xe_sysfs_driver_do(fd, pci_slot, XE_SYSFS_DRIVER_UNBIND);
		igt_assert_eq(regcomp(&regex, REGEX_XE_FUNCTIONS, REG_EXTENDED), 0);
	}

	/*
	 * Iterate over each error injectable function of the xe module
	 */
	igt_subtest_with_dynamic("inject-fault-probe") {
		while ((fgets(line, MAX_LINE_SIZE, file_injectable)) != NULL) {
			if (regexec(&regex, line, 2, pmatch, 0) == 0) {
				strcpy(function_name, line);
				function_name[pmatch[1].rm_eo - 1] = '\0';
				igt_dynamic_f("function-%s", function_name)
					inject_fault_try_bind(fd, pci_slot, function_name);
			}
		}
	}

	igt_fixture {
		regfree(&regex);
		fclose(file_injectable);
		cleanup_injection_fault();
		drm_close_driver(fd);
		xe_sysfs_driver_do(fd, pci_slot, XE_SYSFS_DRIVER_BIND);
	}
}
