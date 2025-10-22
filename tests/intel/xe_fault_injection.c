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
 *
 * Fault injection tests operate at the driver level. If more than one GPU is
 * bound to the Xe driver, the fault injection tests will affect all of them,
 * which can cause strange failures and essentially ignores the GPU selection
 * with --device.
 *
 * This test includes logic to:
 * 1. Check if there is only one Xe GPU in the system, or if the user has
 *    selected a specific GPU with --device
 * 2. If multiple Xe GPUs are bound and the user selected one with --device,
 *    unbind all other Xe GPUs, leaving only the selected one bound
 * 3. After the tests, rebind all GPUs that were unbound before the tests
 */

#include <dirent.h>
#include <limits.h>

#include "igt.h"
#include "igt_device.h"
#include "igt_device_scan.h"
#include "igt_kmod.h"
#include "igt_sriov_device.h"
#include "igt_sysfs.h"
#include "lib/igt_syncobj.h"
#include "lib/intel_pat.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_oa.h"
#include "xe/xe_query.h"

#define INJECT_ERRNO	-ENOMEM
#define BO_ADDR		0x1a0000
#define BO_SIZE		(1024*1024)
#define MAX_INJECT_ITERATIONS	100
#define MAX_INJECTIONS_PER_ITER	100
#define MAX_XE_DEVICES	16

int32_t inject_iters_raw;
struct fault_injection_params {
	/* @probability: Likelihood of failure injection, in percent. */
	uint32_t probability;
	/* @interval: Specifies the interval between failures */
	uint32_t interval;
	/* @times: Specifies how many times failures may happen at most */
	int32_t times;
	/*
	 * @space: Specifies how many times fault injection is suppressed before
	 * first injection
	 */
	uint32_t space;
};

/**
 * struct xe_device_info - Information about an Xe GPU device
 * @pci_slot: PCI slot name (e.g., "0000:03:00.0")
 * @is_bound: Whether the device is currently bound to the Xe driver
 * @card: Device card structure
 */
struct xe_device_info {
	char pci_slot[NAME_MAX];
	bool is_bound;
	struct igt_device_card card;
};

/**
 * struct xe_device_context - Context for managing Xe devices
 * @devices: Array of all Xe devices found in the system
 * @count: Number of Xe devices found
 * @selected_index: Index of the device selected by user (via --device filter)
 * @unbound_devices: Array of devices we unbound (for rebinding later)
 * @unbound_count: Number of devices we unbound
 */
struct xe_device_context {
	struct xe_device_info devices[MAX_XE_DEVICES];
	int count;
	int selected_index;
	int unbound_devices[MAX_XE_DEVICES];
	int unbound_count;
};

/* Forward declarations */
static void xe_device_context_cleanup(struct xe_device_context *ctx);

/**
 * is_device_bound - Check if a device is bound to the Xe driver
 * @pci_slot: PCI slot name to check
 *
 * Returns: true if device is bound to Xe driver, false otherwise
 */
static bool is_device_bound(const char *pci_slot)
{
	char path[PATH_MAX];
	
	snprintf(path, sizeof(path), "/sys/module/xe/drivers/pci:xe/%s", pci_slot);
	return access(path, F_OK) == 0;
}

/**
 * xe_device_context_init - Initialize device context by scanning all Xe GPUs
 * @ctx: Context structure to populate
 *
 * Scans the system for all Xe-compatible GPUs and records their state.
 */
static void xe_device_context_init(struct xe_device_context *ctx)
{
	char sysfs_path[PATH_MAX];
	DIR *dir;
	struct dirent *de;
	
	memset(ctx, 0, sizeof(*ctx));
	ctx->selected_index = -1;
	
	/* First, check if xe module is loaded */
	if (access("/sys/module/xe", F_OK) != 0) {
		igt_debug("Xe module not loaded\n");
		return;
	}
	
	/* 
	 * Enumerate all devices bound to the Xe driver by reading
	 * /sys/bus/pci/drivers/xe/ directory 
	 */
	snprintf(sysfs_path, sizeof(sysfs_path), "/sys/bus/pci/drivers/xe");
	dir = opendir(sysfs_path);
	if (!dir) {
		igt_debug("Cannot open %s\n", sysfs_path);
		return;
	}
	
	/* Scan for all devices in the xe driver directory */
	while ((de = readdir(dir)) && ctx->count < MAX_XE_DEVICES) {
		struct igt_device_card card;
		struct xe_device_info *dev;
		char filter[256];
		
		/* Skip if not a PCI device link (format: 0000:00:00.0) */
		if (de->d_type != DT_LNK || !isdigit(de->d_name[0]))
			continue;
		
		/* This is a device bound to xe, record it */
		dev = &ctx->devices[ctx->count];
		strncpy(dev->pci_slot, de->d_name, NAME_MAX - 1);
		dev->pci_slot[NAME_MAX - 1] = '\0';
		dev->is_bound = true;
		
		/* Try to get card information using PCI slot filter */
		snprintf(filter, sizeof(filter), "pci:slot=%s", de->d_name);
		if (igt_device_card_match_pci(filter, &card)) {
			memcpy(&dev->card, &card, sizeof(struct igt_device_card));
		} else {
			/* If we can't get card info, just store the PCI slot */
			memset(&dev->card, 0, sizeof(struct igt_device_card));
			strncpy(dev->card.pci_slot_name, de->d_name, PCI_SLOT_NAME_SIZE);
		}
		
		igt_debug("Found Xe device %d: %s (bound: yes)\n",
			  ctx->count, dev->pci_slot);
		ctx->count++;
	}
	
	closedir(dir);
	
	igt_info("Found %d device(s) bound to Xe driver\n", ctx->count);
}

/**
 * xe_device_context_find_selected - Find which device was selected by user
 * @ctx: Device context
 * @selected_pci_slot: PCI slot of the device that was selected (opened)
 *
 * Identifies which device in our context matches the one selected by the user.
 * Returns: index in devices array, or -1 if not found
 */
static int xe_device_context_find_selected(struct xe_device_context *ctx,
					   const char *selected_pci_slot)
{
	for (int i = 0; i < ctx->count; i++) {
		if (strcmp(ctx->devices[i].pci_slot, selected_pci_slot) == 0) {
			return i;
		}
	}
	return -1;
}

/**
 * xe_device_context_check_and_prepare - Validate device selection and prepare for testing
 * @ctx: Device context
 * @selected_pci_slot: PCI slot of the device that was selected (opened)
 *
 * Checks if the device configuration is valid for fault injection testing.
 * If multiple devices are bound, unbinds all except the selected one.
 *
 * Returns: true if ready to run tests, false if tests should be skipped
 */
static bool xe_device_context_check_and_prepare(struct xe_device_context *ctx,
						const char *selected_pci_slot)
{
	int bound_count = 0;
	int selected_idx;
	
	/* Find which device was selected */
	selected_idx = xe_device_context_find_selected(ctx, selected_pci_slot);
	if (selected_idx < 0) {
		igt_warn("Selected device %s not found in Xe device list\n",
			 selected_pci_slot);
		return false;
	}
	ctx->selected_index = selected_idx;
	
	/* Count how many devices are currently bound */
	for (int i = 0; i < ctx->count; i++) {
		if (ctx->devices[i].is_bound)
			bound_count++;
	}
	
	igt_info("Found %d Xe device(s), %d bound to Xe driver\n",
		 ctx->count, bound_count);
	
	/* If only one device is bound and it's the selected one, we're good */
	if (bound_count == 1 && ctx->devices[selected_idx].is_bound) {
		igt_info("Only one Xe device bound (the selected one), proceeding with tests\n");
		return true;
	}
	
	/* If the selected device is not bound, we can't proceed */
	if (!ctx->devices[selected_idx].is_bound) {
		igt_warn("Selected device %s is not bound to Xe driver\n",
			 selected_pci_slot);
		return false;
	}
	
	/* Multiple devices are bound - need to unbind non-selected ones */
	if (bound_count > 1) {
		igt_info("Multiple Xe devices bound, unbinding non-selected devices\n");
		
		for (int i = 0; i < ctx->count; i++) {
			if (i == selected_idx || !ctx->devices[i].is_bound)
				continue;
				
			igt_info("Unbinding device %s\n", ctx->devices[i].pci_slot);
			
			/* Attempt to unbind */
			if (igt_kmod_unbind("xe", ctx->devices[i].pci_slot) != 0) {
				igt_warn("Failed to unbind device %s\n",
					 ctx->devices[i].pci_slot);
				/* Try to rebind devices we've already unbound */
				xe_device_context_cleanup(ctx);
				return false;
			}
			
			/* Verify it was unbound */
			if (is_device_bound(ctx->devices[i].pci_slot)) {
				igt_warn("Device %s still bound after unbind attempt\n",
					 ctx->devices[i].pci_slot);
				/* Try to rebind devices we've already unbound */
				xe_device_context_cleanup(ctx);
				return false;
			}
			
			/* Record that we unbound this device */
			ctx->unbound_devices[ctx->unbound_count++] = i;
			ctx->devices[i].is_bound = false;
			
			igt_info("Successfully unbound device %s\n", ctx->devices[i].pci_slot);
		}
		
		igt_info("Successfully prepared system with only selected device bound\n");
		return true;
	}
	
	return true;
}

/**
 * xe_device_context_cleanup - Rebind any devices that were unbound
 * @ctx: Device context
 *
 * Rebinds all devices that were unbound during preparation.
 */
static void xe_device_context_cleanup(struct xe_device_context *ctx)
{
	if (ctx->unbound_count == 0)
		return;
		
	igt_info("Rebinding %d device(s) that were unbound\n", ctx->unbound_count);
	
	for (int i = 0; i < ctx->unbound_count; i++) {
		int dev_idx = ctx->unbound_devices[i];
		const char *pci_slot = ctx->devices[dev_idx].pci_slot;
		
		igt_info("Rebinding device %s\n", pci_slot);
		
		if (igt_kmod_bind("xe", pci_slot) != 0) {
			igt_warn("Failed to rebind device %s\n", pci_slot);
			continue;
		}
		
		/* Verify it was rebound */
		if (!is_device_bound(pci_slot)) {
			igt_warn("Device %s not bound after bind attempt\n", pci_slot);
			continue;
		}
		
		ctx->devices[dev_idx].is_bound = true;
		igt_info("Successfully rebound device %s\n", pci_slot);
	}
	
	ctx->unbound_count = 0;
}

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

static void ignore_dmesg_errors_from_dut(const char pci_slot[])
{
	/*
	 * Driver probe is expected to fail in all cases.
	 * Additionally, error-level reports are expected,
	 * so ignore these in igt_runner.
	 */
	static const char *store = "probe with driver xe failed with error|\\*ERROR\\*";
	char regex[1024];

	/* Only block dmesg reports that target the pci slot of the given fd */
	snprintf(regex, sizeof(regex), "%s:.*(%s)", pci_slot, store);

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

static void injection_list_add(const char function_name[])
{
	int dir;

	dir = fail_function_open();

	igt_assert_lte(0, dir);
	igt_assert_lte(0, igt_sysfs_printf(dir, "inject", "%s", function_name));

	close(dir);
}

static void injection_list_append(const char function_name[])
{
	int dir, fd, ret;

	dir = fail_function_open();
	igt_assert_lte(0, dir);

	fd = openat(dir, "inject", O_WRONLY | O_APPEND);
	igt_assert_lte(0, fd);
	ret = write(fd, function_name, strlen(function_name));
	igt_assert_lte(0, ret);

	close(fd);
	close(dir);
}

static void injection_list_remove(const char function_name[])
{
	int dir;

	dir = fail_function_open();

	igt_assert_lte(0, dir);
	igt_assert_lte(0, igt_sysfs_printf(dir, "inject", "!%s", function_name));

	close(dir);
}

static void injection_list_clear(void)
{
	/* If nothing specified (‘’) injection list is cleared */
	return injection_list_add("");
}

/*
 * Default fault injection parameters which injects fault on first call to the
 * configured fail_function.
 */
static const struct fault_injection_params default_fault_params = {
	.probability = 100,
	.interval = 0,
	.times = -1,
	.space = 0
};

/*
 * See https://docs.kernel.org/fault-injection/fault-injection.html#application-examples
 */
static void setup_injection_fault(const struct fault_injection_params *fault_params)
{
	int dir;

	if (!fault_params)
		fault_params = &default_fault_params;

	igt_assert(fault_params->probability >= 0);
	igt_assert(fault_params->probability <= 100);


	dir = fail_function_open();
	igt_assert_lte(0, dir);

	igt_debug("probability = %d, interval = %d, times = %d, space = %u\n",
			fault_params->probability, fault_params->interval,
			fault_params->times, fault_params->space);

	igt_assert_lte(0, igt_sysfs_printf(dir, "task-filter", "N"));
	igt_sysfs_set_u32(dir, "probability", fault_params->probability);
	igt_sysfs_set_u32(dir, "interval", fault_params->interval);
	igt_sysfs_set_s32(dir, "times", fault_params->times);
	igt_sysfs_set_u32(dir, "space", fault_params->space);
	igt_sysfs_set_u32(dir, "verbose", 1);

	close(dir);
}

static void cleanup_injection_fault(int sig)
{
	injection_list_clear();
}

static int get_remaining_injection_count(void)
{
	int dir, val;

	dir = fail_function_open();
	igt_assert_lte(0, dir);

	val = igt_sysfs_get_s32(dir, "times");

	close(dir);
	return val;
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

static void ignore_fail_dump_in_dmesg(const char function_name[], bool enable)
{
	if (strstr(function_name, "send_recv")) {
		if (enable) {
			injection_list_append("xe_is_injection_active");
			set_retval("xe_is_injection_active", INJECT_ERRNO);
		} else {
			injection_list_remove("xe_is_injection_active");
		}
	}
}

/**
 * SUBTEST: inject-fault-probe-function-%s
 * Description: inject an error in the injectable function %arg[1] then
 *		reprobe driver
 * Functionality: fault
 *
 * arg[1]:
 * @guc_wait_ucode:			guc_wait_ucode
 * @wait_for_lmem_ready:		wait_for_lmem_ready
 * @xe_add_hw_engine_class_defaults:	xe_add_hw_engine_class_defaults
 * @xe_device_create:			xe_device_create
 * @xe_device_probe_early:		xe_device_probe_early
 * @xe_ggtt_init_early:			xe_ggtt_init_early
 * @xe_guc_ads_init:			xe_guc_ads_init
 * @xe_guc_ct_init:			xe_guc_ct_init
 * @xe_guc_log_init:			xe_guc_log_init
 * @xe_guc_relay_init:			xe_guc_relay_init
 * @xe_mmio_probe_early:		xe_mmio_probe_early
 * @xe_pcode_probe_early:		xe_pcode_probe_early
 * @xe_pm_init_early:			xe_pm_init_early
 * @xe_sriov_init:			xe_sriov_init
 * @xe_tile_init_early:			xe_tile_init_early
 * @xe_uc_fw_init:			xe_uc_fw_init
 * @xe_wa_gt_init:			xe_wa_gt_init
 * @xe_wopcm_init:			xe_wopcm_init
 */
static int
inject_fault_probe(int fd, const char pci_slot[], const char function_name[])
{
	int err = 0;
	igt_info("Injecting error \"%s\" (%d) in function \"%s\"\n",
		 strerror(-INJECT_ERRNO), INJECT_ERRNO, function_name);

	ignore_dmesg_errors_from_dut(pci_slot);
	injection_list_add(function_name);
	set_retval(function_name, INJECT_ERRNO);
	ignore_fail_dump_in_dmesg(function_name, true);

	igt_kmod_bind("xe", pci_slot);

	err = -errno;
	injection_list_remove(function_name);
	ignore_fail_dump_in_dmesg(function_name, false);

	return err;
}

/**
 * SUBTEST: probe-fail-guc-%s
 * Description: inject an error in the injectable function %arg[1] then reprobe driver
 * Functionality: fault
 *
 * arg[1]:
 * @xe_guc_mmio_send_recv:     Inject an error when calling xe_guc_mmio_send_recv
 * @xe_guc_ct_send_recv:       Inject an error when calling xe_guc_ct_send_recv
 */
static void probe_fail_guc(int fd, const char pci_slot[], const char function_name[],
			   struct fault_injection_params *fault_params)
{
	int iter_start = 0, iter_end = 0, iter = 0;

	igt_assert(fault_params);

	/* inject_iters_raw will have zero if unset / set to <=0 or malformed.
	   When set to > 0 it will have iteration number and will run single n-th
	   iteration only.
	*/
	iter = inject_iters_raw;
	iter_start = iter ? : 0;
	iter_end = iter ? iter + 1 : MAX_INJECT_ITERATIONS;
	igt_debug("Injecting error for %d - %d iterations\n", iter_start, iter_end);
	for (int i = iter_start; i < iter_end; i++) {
		fault_params->space = i;
		fault_params->times = MAX_INJECTIONS_PER_ITER;
		setup_injection_fault(fault_params);
		inject_fault_probe(fd, pci_slot, function_name);
		igt_kmod_unbind("xe", pci_slot);

		/*
		 * if no injection occurred we've tested all the injection
		 * points for this function and can therefore stop iterating.
		 */
		if (get_remaining_injection_count() == MAX_INJECTIONS_PER_ITER)
			break;
	}

	/*
	 * In the unlikely case where we haven't covered all the injection
	 * points for the function (because there are more of them than
	 * MAX_INJECT_ITERATIONS) fail the test so that we know we need to do an
	 * update and/or split it in two parts.
	 */
	igt_assert_f(inject_iters_raw || iter != MAX_INJECT_ITERATIONS,
		     "Loop exited without covering all injection points!\n");
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
 * @xe_pxp_exec_queue_add:                xe_pxp_exec_queue_add
 */

#define EXEC_QUEUE_LR	BIT(0)
#define EXEC_QUEUE_PXP	BIT(1)
static void
exec_queue_create_fail(int fd, struct drm_xe_engine_class_instance *instance,
		       const char pci_slot[], const char function_name[],
		       unsigned int flags)
{
	uint32_t exec_queue_id;
	struct drm_xe_ext_set_property ext = { 0 };
	uint64_t ext_ptr = 0;
	uint32_t vm;

	if (flags & EXEC_QUEUE_PXP) {
		igt_require(xe_wait_for_pxp_init(fd) == 0);

		ext.base.name = DRM_XE_EXEC_QUEUE_EXTENSION_SET_PROPERTY,
		ext.property = DRM_XE_EXEC_QUEUE_SET_PROPERTY_PXP_TYPE,
		ext.value = DRM_XE_PXP_TYPE_HWDRM;
		ext_ptr = to_user_pointer(&ext);
	}

	vm = xe_vm_create(fd, flags & EXEC_QUEUE_LR ? DRM_XE_VM_CREATE_FLAG_LR_MODE : 0, 0);

	/* sanity check */
	igt_assert_eq(__xe_exec_queue_create(fd, vm, 1, 1, instance, ext_ptr, &exec_queue_id), 0);
	xe_exec_queue_destroy(fd, exec_queue_id);

	ignore_dmesg_errors_from_dut(pci_slot);
	injection_list_add(function_name);
	set_retval(function_name, INJECT_ERRNO);
	igt_assert(__xe_exec_queue_create(fd, vm, 1, 1, instance, ext_ptr, &exec_queue_id) != 0);
	injection_list_remove(function_name);

	igt_assert_eq(__xe_exec_queue_create(fd, vm, 1, 1, instance, ext_ptr, &exec_queue_id), 0);
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
vm_create_fail(int fd, const char pci_slot[],
	       const char function_name[], unsigned int flags)
{
	igt_assert_eq(simple_vm_create(fd, flags), 0);

	ignore_dmesg_errors_from_dut(pci_slot);
	injection_list_add(function_name);
	set_retval(function_name, INJECT_ERRNO);
	igt_assert(simple_vm_create(fd, flags) != 0);
	injection_list_remove(function_name);

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
vm_bind_fail(int fd, const char pci_slot[], const char function_name[])
{
	uint32_t vm = xe_vm_create(fd, 0, 0);

	igt_assert_eq(simple_vm_bind(fd, vm), 0);

	ignore_dmesg_errors_from_dut(pci_slot);
	injection_list_add(function_name);
	set_retval(function_name, INJECT_ERRNO);
	igt_assert(simple_vm_bind(fd, vm) != 0);
	injection_list_remove(function_name);

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
oa_add_config_fail(int fd, int sysfs, int devid,
		   const char pci_slot[], const char function_name[])
{
	char path[512];
	uint64_t config_id;
#define SAMPLE_MUX_REG (intel_graphics_ver(devid) >= IP_VER(20, 0) ?	\
			0x13000 /* PES* */ : 0x9888 /* NOA_WRITE */)

	uint32_t mux_regs[] = { SAMPLE_MUX_REG, 0x0 };
	struct drm_xe_oa_config config;
	const char *uuid = "01234567-0123-0123-0123-0123456789ab";
	int ret;

	snprintf(path, sizeof(path), "metrics/%s/id", uuid);
	/* Destroy previous configuration if present */
	if (igt_sysfs_scanf(sysfs, path, "%" PRIu64, &config_id) == 1)
		igt_assert_eq(intel_xe_perf_ioctl(fd, DRM_XE_OBSERVATION_OP_REMOVE_CONFIG,
						  &config_id), 0);

	memset(&config, 0, sizeof(config));
	memcpy(config.uuid, uuid, sizeof(config.uuid));
	config.n_regs = 1;
	config.regs_ptr = to_user_pointer(mux_regs);

	ret = intel_xe_perf_ioctl(fd, DRM_XE_OBSERVATION_OP_ADD_CONFIG, &config);
	igt_skip_on_f(ret == -1 && errno == ENODEV, "Xe OA interface not available\n");

	igt_assert_lt(0, ret);
	igt_assert(igt_sysfs_scanf(sysfs, path, "%" PRIu64, &config_id) == 1);
	igt_assert_eq(intel_xe_perf_ioctl(fd, DRM_XE_OBSERVATION_OP_REMOVE_CONFIG, &config_id), 0);

	ignore_dmesg_errors_from_dut(pci_slot);
	injection_list_add(function_name);
	set_retval(function_name, INJECT_ERRNO);
	igt_assert_lt(intel_xe_perf_ioctl(fd, DRM_XE_OBSERVATION_OP_ADD_CONFIG, &config), 0);
	injection_list_remove(function_name);

	igt_assert_lt(0, intel_xe_perf_ioctl(fd, DRM_XE_OBSERVATION_OP_ADD_CONFIG, &config));
	igt_assert(igt_sysfs_scanf(sysfs, path, "%" PRIu64, &config_id) == 1);
	igt_assert_eq(intel_xe_perf_ioctl(fd, DRM_XE_OBSERVATION_OP_REMOVE_CONFIG, &config_id), 0);
}

static int opt_handler(int opt, int opt_index, void *data)
{
	int in_param;
	switch (opt) {
	case 'I':
		/* Update to 0 if not exported / -ve value */
		in_param = atoi(optarg);
		if (!in_param || in_param <= 0 || in_param > MAX_INJECT_ITERATIONS)
			inject_iters_raw = 0;
		else
			inject_iters_raw = in_param;
		break;
	default:
		return IGT_OPT_HANDLER_ERROR;
	}

	return IGT_OPT_HANDLER_SUCCESS;
}

const char *help_str =
	"  -I\tIf set, an error will be injected at specific function call.\n\
	If not set, an error will be injected in every possible function call\
	starting from first up to 100.";

igt_main_args("I:", NULL, help_str, opt_handler, NULL)
{
	int fd, sysfs;
	struct drm_xe_engine_class_instance *hwe;
	struct fault_injection_params fault_params;
	static uint32_t devid;
	char pci_slot[NAME_MAX];
	bool is_vf_device;
	struct xe_device_context dev_ctx;
	const struct section {
		const char *name;
		unsigned int flags;
		bool pf_only;
	} probe_fail_functions[] = {
		{ "guc_wait_ucode", 0, true },
		{ "wait_for_lmem_ready" },
		{ "xe_add_hw_engine_class_defaults" },
		{ "xe_device_create" },
		{ "xe_device_probe_early" },
		{ "xe_ggtt_init_early" },
		{ "xe_guc_ads_init", 0, true },
		{ "xe_guc_ct_init" },
		{ "xe_guc_log_init", 0, true },
		{ "xe_guc_relay_init" },
		{ "xe_mmio_probe_early" },
		{ "xe_pcode_probe_early" },
		{ "xe_pm_init_early" },
		{ "xe_sriov_init" },
		{ "xe_tile_init_early" },
		{ "xe_uc_fw_init" },
		{ "xe_wa_gt_init" },
		{ "xe_wopcm_init", 0, true },
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
		{ "xe_vm_add_compute_exec_queue", EXEC_QUEUE_LR },
		{ "xe_pxp_exec_queue_add", EXEC_QUEUE_PXP },
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

	const struct section guc_fail_functions[] = {
		{ "xe_guc_mmio_send_recv" },
		{ "xe_guc_ct_send_recv" },
		{ }
	};

	igt_fixture {
		igt_require(fail_function_injection_enabled());
		
		/* Initialize device context and scan for all Xe GPUs */
		xe_device_context_init(&dev_ctx);
		
		/* Open the Xe device (this will use --device filter if provided) */
		fd = drm_open_driver(DRIVER_XE);
		devid = intel_get_drm_devid(fd);
		sysfs = igt_sysfs_open(fd);
		igt_device_get_pci_slot_name(fd, pci_slot);
		
		/* Validate device selection and prepare for testing */
		igt_require_f(xe_device_context_check_and_prepare(&dev_ctx, pci_slot),
			      "Fault injection requires exactly one Xe GPU bound, "
			      "or user selection of one GPU with --device\n");
		
		setup_injection_fault(&default_fault_params);
		igt_install_exit_handler(cleanup_injection_fault);
		is_vf_device = intel_is_vf_device(fd);
	}

	for (const struct section *s = vm_create_fail_functions; s->name; s++)
		igt_subtest_f("vm-create-fail-%s", s->name)
			vm_create_fail(fd, pci_slot, s->name, s->flags);

	for (const struct section *s = vm_bind_fail_functions; s->name; s++)
		igt_subtest_f("vm-bind-fail-%s", s->name)
			vm_bind_fail(fd, pci_slot, s->name);

	for (const struct section *s = exec_queue_create_fail_functions; s->name; s++)
		igt_subtest_f("exec-queue-create-fail-%s", s->name)
			xe_for_each_engine(fd, hwe)
				if (hwe->engine_class != DRM_XE_ENGINE_CLASS_VM_BIND)
					exec_queue_create_fail(fd, hwe, pci_slot,
							       s->name, s->flags);

	for (const struct section *s = exec_queue_create_vmbind_fail_functions; s->name; s++)
		igt_subtest_f("exec-queue-create-fail-%s", s->name)
			xe_for_each_engine(fd, hwe)
				if (hwe->engine_class == DRM_XE_ENGINE_CLASS_VM_BIND)
					exec_queue_create_fail(fd, hwe, pci_slot,
							       s->name, s->flags);

	for (const struct section *s = oa_add_config_fail_functions; s->name; s++)
		igt_subtest_f("oa-add-config-fail-%s", s->name)
			oa_add_config_fail(fd, sysfs, devid, pci_slot, s->name);

	igt_fixture {
		igt_kmod_unbind("xe", pci_slot);
	}

	for (const struct section *s = probe_fail_functions; s->name; s++)
		igt_subtest_f("inject-fault-probe-function-%s", s->name) {
			bool should_pass = s->pf_only && is_vf_device;
			int err;

			err = inject_fault_probe(fd, pci_slot, s->name);

			igt_assert_eq(should_pass ? 0 : INJECT_ERRNO, err);
			igt_kmod_unbind("xe", pci_slot);
		}

	for (const struct section *s = guc_fail_functions; s->name; s++)
		igt_subtest_f("probe-fail-guc-%s", s->name) {
			memcpy(&fault_params, &default_fault_params,
					sizeof(struct fault_injection_params));
			probe_fail_guc(fd, pci_slot, s->name, &fault_params);
		}

	igt_fixture {
		close(sysfs);
		drm_close_driver(fd);
		igt_kmod_bind("xe", pci_slot);
		
		/* Rebind any devices that were unbound for testing */
		xe_device_context_cleanup(&dev_ctx);
	}
}
