// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2024 Intel Corporation. All rights reserved.
 */

#include <fcntl.h>
#include <sys/stat.h>
#include "drmtest.h"
#include "igt_core.h"
#include "igt_device.h"
#include "igt_sriov_device.h"
#include "intel_chipset.h"
#include "linux_scaffold.h"
#include "xe/xe_mmio.h"
#include "xe/xe_query.h"

/**
 * TEST: xe_sriov_flr
 * Category: Core
 * Mega feature: SR-IOV
 * Sub-category: Reset tests
 * Functionality: FLR
 * Description: Examine behavior of SR-IOV VF FLR
 *
 * SUBTEST: flr-vf1-clear
 * Run type: BAT
 * Description:
 *   Verifies that LMEM, GGTT, and SCRATCH_REGS are properly cleared
 *   on VF1 following a Function Level Reset (FLR).
 *
 * SUBTEST: flr-each-isolation
 * Run type: FULL
 * Description:
 *   Sequentially performs FLR on each VF to verify isolation and
 *   clearing of LMEM, GGTT, and SCRATCH_REGS on the reset VF only.
 */

IGT_TEST_DESCRIPTION("Xe tests for SR-IOV VF FLR (Functional Level Reset)");

const char *SKIP_REASON = "SKIP";

/**
 * struct subcheck_data - Base structure for subcheck data.
 *
 * This structure serves as a foundational data model for various subchecks. It is designed
 * to be extended by more specific subcheck structures as needed. The structure includes
 * essential information about the subcheck environment and conditions, which are used
 * across different testing operations.
 *
 * @pf_fd: File descriptor for the Physical Function.
 * @num_vfs: Number of Virtual Functions (VFs) enabled and under test. This count is
 *           used to iterate over and manage the VFs during the testing process.
 * @gt: GT under test. This identifier is used to specify a particular GT
 *      for operations when GT-specific testing is required.
 * @stop_reason: Pointer to a string that indicates why a subcheck should skip or fail.
 *               This field is crucial for controlling the flow of subcheck execution.
 *               If set, it should prevent further execution of the current subcheck,
 *               allowing subcheck operations to check this field and return early if
 *               a skip or failure condition is indicated. This mechanism ensures
 *               that while one subcheck may stop due to a failure or a skip condition,
 *               other subchecks can continue execution.
 *
 * Example usage:
 * A typical use of this structure involves initializing it with the necessary test setup
 * parameters, checking the `stop_reason` field before proceeding with each subcheck operation,
 * and using `pf_fd`, `num_vfs`, and `gt` as needed based on the specific subcheck requirements.
 */
struct subcheck_data {
	int pf_fd;
	int num_vfs;
	int gt;
	char *stop_reason;
};

/**
 * struct subcheck - Defines operations for managing a subcheck scenario.
 *
 * This structure holds function pointers for the key operations required
 * to manage the lifecycle of a subcheck scenario. It is used by the `verify_flr`
 * function, which acts as a template method, to call these operations in a
 * specific sequence.
 *
 * @data: Shared data necessary for all operations in the subcheck.
 *
 * @name: Name of the subcheck operation, used for identification and reporting.
 *
 * @init: Initialize the subcheck environment.
 *   Sets up the initial state required for the subcheck, including preparing
 *   resources and ensuring the system is ready for testing.
 *   @param data: Shared data needed for initialization.
 *
 * @prepare_vf: Prepare subcheck data for a specific VF.
 *   Called for each VF before FLR is performed. It might involve marking
 *   specific memory regions or setting up PTE addresses.
 *   @param vf_id: Identifier of the VF being prepared.
 *   @param data: Shared common data.
 *
 * @verify_vf: Verify the state of a VF after FLR.
 *   Checks the VF's state post FLR to ensure the expected results,
 *   such as verifying that only the FLRed VF has its state reset.
 *   @param vf_id: Identifier of the VF to verify.
 *   @param flr_vf_id: Identifier of the VF that underwent FLR.
 *   @param data: Shared common data.
 *
 * @cleanup: Clean up the subcheck environment.
 *   Releases resources and restores the system to its original state
 *   after the subchecks, ensuring no resource leaks and preparing the system
 *   for subsequent tests.
 *   @param data: Shared common data.
 */
struct subcheck {
	struct subcheck_data *data;
	const char *name;
	void (*init)(struct subcheck_data *data);
	void (*prepare_vf)(int vf_id, struct subcheck_data *data);
	void (*verify_vf)(int vf_id, int flr_vf_id, struct subcheck_data *data);
	void (*cleanup)(struct subcheck_data *data);
};

__attribute__((format(printf, 3, 0)))
static void set_stop_reason_v(struct subcheck_data *data, const char *prefix,
			      const char *format, va_list args)
{
	char *formatted_message;
	int result;

	if (igt_warn_on_f(data->stop_reason, "Stop reason already set\n"))
		return;

	result = vasprintf(&formatted_message, format, args);
	igt_assert_neq(result, -1);

	result = asprintf(&data->stop_reason, "%s : %s", prefix,
			  formatted_message);
	igt_assert_neq(result, -1);

	free(formatted_message);
}

__attribute__((format(printf, 2, 3)))
static void set_skip_reason(struct subcheck_data *data, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	set_stop_reason_v(data, SKIP_REASON, format, args);
	va_end(args);
}

__attribute__((format(printf, 2, 3)))
static void set_fail_reason(struct subcheck_data *data, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	set_stop_reason_v(data, "FAIL", format, args);
	va_end(args);
}

static bool subcheck_can_proceed(const struct subcheck *check)
{
	return !check->data->stop_reason;
}

static int count_subchecks_with_stop_reason(struct subcheck *checks, int num_checks)
{
	int subchecks_with_stop_reason = 0;

	for (int i = 0; i < num_checks; ++i)
		if (!subcheck_can_proceed(&checks[i]))
			subchecks_with_stop_reason++;

	return subchecks_with_stop_reason;
}

static bool no_subchecks_can_proceed(struct subcheck *checks, int num_checks)
{
	return count_subchecks_with_stop_reason(checks, num_checks) == num_checks;
}

static bool is_subcheck_skipped(struct subcheck *subcheck)
{
	return subcheck->data && subcheck->data->stop_reason &&
	       !strncmp(SKIP_REASON, subcheck->data->stop_reason, strlen(SKIP_REASON));
}

static void subchecks_report_results(struct subcheck *checks, int num_checks)
{
	int fails = 0, skips = 0;

	for (int i = 0; i < num_checks; ++i) {
		if (checks[i].data->stop_reason) {
			if (is_subcheck_skipped(&checks[i])) {
				igt_info("%s: %s", checks[i].name,
					 checks[i].data->stop_reason);
				skips++;
			} else {
				igt_critical("%s: %s", checks[i].name,
					     checks[i].data->stop_reason);
				fails++;
			}
		} else {
			igt_info("%s: SUCCESS\n", checks[i].name);
		}
	}

	igt_fail_on_f(fails, "%d out of %d checks failed\n", fails, num_checks);
	igt_skip_on(skips == num_checks);
}

/**
 * verify_flr - Orchestrates the verification of Function Level Reset (FLR)
 *              across multiple Virtual Functions (VFs).
 *
 * This function performs FLR on each VF to ensure that only the reset VF has
 * its state cleared, while other VFs remain unaffected. It handles initialization,
 * preparation, verification, and cleanup for each test operation defined in `checks`.
 *
 * @pf_fd: File descriptor for the Physical Function (PF).
 * @num_vfs: Total number of Virtual Functions (VFs) to test.
 * @checks: Array of subchecks.
 * @num_checks: Number of subchecks.
 *
 * Detailed Workflow:
 * - Initializes and prepares VFs for testing.
 * - Iterates through each VF, performing FLR, and verifies that only
 *   the reset VF is affected while others remain unchanged.
 * - Reinitializes test data for the FLRed VF if there are more VFs to test.
 * - Continues the process until all VFs are tested.
 * - Handles any test failures or early exits, cleans up, and reports results.
 *
 * A timeout is used to wait for FLR operations to complete.
 */
static void verify_flr(int pf_fd, int num_vfs, struct subcheck *checks, int num_checks)
{
	const int wait_flr_ms = 200;
	int i, vf_id, flr_vf_id = -1;

	igt_sriov_disable_driver_autoprobe(pf_fd);
	igt_sriov_enable_vfs(pf_fd, num_vfs);
	if (igt_warn_on(!igt_sriov_device_reset_exists(pf_fd, 1)))
		goto disable_vfs;
	/* Refresh PCI state */
	if (igt_warn_on(igt_pci_system_reinit()))
		goto disable_vfs;

	for (i = 0; i < num_checks; ++i)
		checks[i].init(checks[i].data);

	for (vf_id = 1; vf_id <= num_vfs; ++vf_id)
		for (i = 0; i < num_checks; ++i)
			if (subcheck_can_proceed(&checks[i]))
				checks[i].prepare_vf(vf_id, checks[i].data);

	if (no_subchecks_can_proceed(checks, num_checks))
		goto cleanup;

	flr_vf_id = 1;

	do {
		if (igt_warn_on_f(!igt_sriov_device_reset(pf_fd, flr_vf_id),
				  "Initiating VF%u FLR failed\n", flr_vf_id))
			goto cleanup;

		/* assume FLR is finished after wait_flr_ms */
		usleep(wait_flr_ms * 1000);

		for (vf_id = 1; vf_id <= num_vfs; ++vf_id)
			for (i = 0; i < num_checks; ++i)
				if (subcheck_can_proceed(&checks[i]))
					checks[i].verify_vf(vf_id, flr_vf_id, checks[i].data);

		/* reinitialize test data for FLRed VF */
		if (flr_vf_id < num_vfs)
			for (i = 0; i < num_checks; ++i)
				if (subcheck_can_proceed(&checks[i]))
					checks[i].prepare_vf(flr_vf_id, checks[i].data);

		if (no_subchecks_can_proceed(checks, num_checks))
			goto cleanup;

	} while (++flr_vf_id <= num_vfs);

cleanup:
	for (i = 0; i < num_checks; ++i)
		checks[i].cleanup(checks[i].data);

disable_vfs:
	igt_sriov_disable_vfs(pf_fd);

	if (flr_vf_id > 1 || no_subchecks_can_proceed(checks, num_checks))
		subchecks_report_results(checks, num_checks);
	else
		igt_skip("No checks executed\n");
}

#define GEN12_VF_CAP_REG			0x1901f8
#define GGTT_PTE_TEST_FIELD_MASK		GENMASK_ULL(19, 12)
#define GGTT_PTE_ADDR_SHIFT			12
#define PRE_1250_IP_VER_GGTT_PTE_VFID_MASK	GENMASK_ULL(4, 2)
#define GGTT_PTE_VFID_MASK			GENMASK_ULL(11, 2)
#define GGTT_PTE_VFID_SHIFT			2

#define for_each_pte_offset(pte_offset__, ggtt_offset_range__) \
	for ((pte_offset__) = ((ggtt_offset_range__)->begin);  \
	     (pte_offset__) < ((ggtt_offset_range__)->end);    \
	     (pte_offset__) += sizeof(xe_ggtt_pte_t))

struct ggtt_ops {
	void (*set_pte)(struct xe_mmio *mmio, int gt, uint32_t pte_offset, xe_ggtt_pte_t pte);
	xe_ggtt_pte_t (*get_pte)(struct xe_mmio *mmio, int gt, uint32_t pte_offset);
};

struct ggtt_provisioned_offset_range {
	uint32_t begin;
	uint32_t end;
};

struct ggtt_data {
	struct subcheck_data base;
	struct ggtt_provisioned_offset_range *pte_offsets;
	struct xe_mmio *mmio;
	struct ggtt_ops ggtt;
};

static xe_ggtt_pte_t intel_get_pte(struct xe_mmio *mmio, int gt, uint32_t pte_offset)
{
	return xe_mmio_ggtt_read(mmio, gt, pte_offset);
}

static void intel_set_pte(struct xe_mmio *mmio, int gt, uint32_t pte_offset, xe_ggtt_pte_t pte)
{
	xe_mmio_ggtt_write(mmio, gt, pte_offset, pte);
}

static void intel_mtl_set_pte(struct xe_mmio *mmio, int gt, uint32_t pte_offset, xe_ggtt_pte_t pte)
{
	xe_mmio_ggtt_write(mmio, gt, pte_offset, pte);

	/* force flush by read some MMIO register */
	xe_mmio_gt_read32(mmio, gt, GEN12_VF_CAP_REG);
}

static bool set_pte_gpa(struct ggtt_ops *ggtt, struct xe_mmio *mmio, int gt, uint32_t pte_offset,
			uint8_t gpa, xe_ggtt_pte_t *out)
{
	xe_ggtt_pte_t pte;

	pte = ggtt->get_pte(mmio, gt, pte_offset);
	pte &= ~GGTT_PTE_TEST_FIELD_MASK;
	pte |= ((xe_ggtt_pte_t)gpa << GGTT_PTE_ADDR_SHIFT) & GGTT_PTE_TEST_FIELD_MASK;
	ggtt->set_pte(mmio, gt, pte_offset, pte);
	*out = ggtt->get_pte(mmio, gt, pte_offset);

	return *out == pte;
}

static bool check_pte_gpa(struct ggtt_ops *ggtt, struct xe_mmio *mmio, int gt, uint32_t pte_offset,
			  uint8_t expected_gpa, xe_ggtt_pte_t *out)
{
	uint8_t val;

	*out = ggtt->get_pte(mmio, gt, pte_offset);
	val = (uint8_t)((*out & GGTT_PTE_TEST_FIELD_MASK) >> GGTT_PTE_ADDR_SHIFT);

	return val == expected_gpa;
}

static bool is_intel_mmio_initialized(const struct intel_mmio_data *mmio)
{
	return mmio->dev;
}

static uint64_t get_vfid_mask(int pf_fd)
{
	uint16_t dev_id = intel_get_drm_devid(pf_fd);

	return (intel_graphics_ver(dev_id) >= IP_VER(12, 50)) ?
		GGTT_PTE_VFID_MASK : PRE_1250_IP_VER_GGTT_PTE_VFID_MASK;
}

static bool pte_contains_vfid(const xe_ggtt_pte_t pte, const unsigned int vf_id,
			      const uint64_t vfid_mask)
{
	return ((pte & vfid_mask) >> GGTT_PTE_VFID_SHIFT) == vf_id;
}

static bool is_offset_in_range(uint32_t offset,
			       const struct ggtt_provisioned_offset_range *ranges,
			       size_t num_ranges)
{
	for (size_t i = 0; i < num_ranges; i++)
		if (offset >= ranges[i].begin && offset < ranges[i].end)
			return true;

	return false;
}

static void find_ggtt_provisioned_ranges(struct ggtt_data *gdata)
{
	uint32_t limit = gdata->mmio->intel_mmio.mmio_size - SZ_8M > SZ_8M ?
				 SZ_8M :
				 gdata->mmio->intel_mmio.mmio_size - SZ_8M;
	uint64_t vfid_mask = get_vfid_mask(gdata->base.pf_fd);
	xe_ggtt_pte_t pte;

	gdata->pte_offsets = calloc(gdata->base.num_vfs + 1, sizeof(*gdata->pte_offsets));
	igt_assert(gdata->pte_offsets);

	for (int vf_id = 1; vf_id <= gdata->base.num_vfs; vf_id++) {
		uint32_t range_begin = 0;
		int adjacent = 0;
		int num_ranges = 0;

		for (uint32_t offset = 0; offset < limit; offset += sizeof(xe_ggtt_pte_t)) {
			/* Skip already found ranges */
			if (is_offset_in_range(offset, gdata->pte_offsets, vf_id))
				continue;

			pte = xe_mmio_ggtt_read(gdata->mmio, gdata->base.gt, offset);

			if (pte_contains_vfid(pte, vf_id, vfid_mask)) {
				if (adjacent == 0)
					range_begin = offset;

				adjacent++;
			} else if (adjacent > 0) {
				uint32_t range_end = range_begin +
						     adjacent * sizeof(xe_ggtt_pte_t);

				igt_debug("Found VF%d ggtt range begin=%#x end=%#x num_ptes=%d\n",
					  vf_id, range_begin, range_end, adjacent);

				if (adjacent > gdata->pte_offsets[vf_id].end -
					       gdata->pte_offsets[vf_id].begin) {
					gdata->pte_offsets[vf_id].begin = range_begin;
					gdata->pte_offsets[vf_id].end = range_end;
				}

				adjacent = 0;
				num_ranges++;
			}
		}

		if (adjacent > 0) {
			uint32_t range_end = range_begin + adjacent * sizeof(xe_ggtt_pte_t);

			igt_debug("Found VF%d ggtt range begin=%#x end=%#x num_ptes=%d\n",
				  vf_id, range_begin, range_end, adjacent);

			if (adjacent > gdata->pte_offsets[vf_id].end -
				       gdata->pte_offsets[vf_id].begin) {
				gdata->pte_offsets[vf_id].begin = range_begin;
				gdata->pte_offsets[vf_id].end = range_end;
			}
			num_ranges++;
		}

		if (num_ranges == 0) {
			set_fail_reason(&gdata->base,
					"Failed to find VF%d provisioned ggtt range\n", vf_id);
			return;
		}
		igt_warn_on_f(num_ranges > 1, "Found %d ranges for VF%d\n", num_ranges, vf_id);
	}
}

static void ggtt_subcheck_init(struct subcheck_data *data)
{
	struct ggtt_data *gdata = (struct ggtt_data *)data;

	if (xe_is_media_gt(data->pf_fd, data->gt)) {
		set_skip_reason(data, "GGTT unavailable on media GT\n");
		return;
	}

	gdata->ggtt.get_pte = intel_get_pte;
	if (IS_METEORLAKE(intel_get_drm_devid(data->pf_fd)))
		gdata->ggtt.set_pte = intel_mtl_set_pte;
	else
		gdata->ggtt.set_pte = intel_set_pte;

	if (gdata->mmio) {
		if (!is_intel_mmio_initialized(&gdata->mmio->intel_mmio))
			xe_mmio_vf_access_init(data->pf_fd, 0 /*PF*/, gdata->mmio);

		find_ggtt_provisioned_ranges(gdata);
	} else {
		set_fail_reason(data, "xe_mmio is NULL\n");
	}
}

static void ggtt_subcheck_prepare_vf(int vf_id, struct subcheck_data *data)
{
	struct ggtt_data *gdata = (struct ggtt_data *)data;
	xe_ggtt_pte_t pte;
	uint32_t pte_offset;

	if (data->stop_reason)
		return;

	igt_debug("Prepare gpa on VF%u offset range [%#x-%#x]\n", vf_id,
		  gdata->pte_offsets[vf_id].begin,
		  gdata->pte_offsets[vf_id].end);

	for_each_pte_offset(pte_offset, &gdata->pte_offsets[vf_id]) {
		if (!set_pte_gpa(&gdata->ggtt, gdata->mmio, data->gt, pte_offset,
				 (uint8_t)vf_id, &pte)) {
			set_fail_reason(data,
					"Prepare VF%u failed, unexpected gpa: Read PTE: %#lx at offset: %#x\n",
					vf_id, pte, pte_offset);
			return;
		}
	}
}

static void ggtt_subcheck_verify_vf(int vf_id, int flr_vf_id, struct subcheck_data *data)
{
	struct ggtt_data *gdata = (struct ggtt_data *)data;
	uint8_t expected = (vf_id == flr_vf_id) ? 0 : vf_id;
	xe_ggtt_pte_t pte;
	uint32_t pte_offset;

	if (data->stop_reason)
		return;

	for_each_pte_offset(pte_offset, &gdata->pte_offsets[vf_id]) {
		if (!check_pte_gpa(&gdata->ggtt, gdata->mmio, data->gt, pte_offset,
				   expected, &pte)) {
			set_fail_reason(data,
					"GGTT check after VF%u FLR failed on VF%u: Read PTE: %#lx at offset: %#x\n",
					flr_vf_id, vf_id, pte, pte_offset);
			return;
		}
	}
}

static void ggtt_subcheck_cleanup(struct subcheck_data *data)
{
	struct ggtt_data *gdata = (struct ggtt_data *)data;

	free(gdata->pte_offsets);
	if (gdata->mmio && is_intel_mmio_initialized(&gdata->mmio->intel_mmio))
		xe_mmio_access_fini(gdata->mmio);
}

struct lmem_data {
	struct subcheck_data base;
	size_t *vf_lmem_size;
};

struct lmem_info {
	/* pointer to the mapped area */
	char *addr;
	/* size of mapped area */
	size_t size;
};

const size_t STEP = SZ_1M;

static void *mmap_vf_lmem(int pf_fd, int vf_num, size_t length, int prot, off_t offset)
{
	int open_flags = ((prot & PROT_WRITE) != 0) ? O_RDWR : O_RDONLY;
	struct stat st;
	int sysfs, fd;
	void *addr;

	sysfs = igt_sriov_device_sysfs_open(pf_fd, vf_num);
	if (sysfs < 0) {
		igt_debug("Failed to open sysfs for VF%d: %s\n", vf_num, strerror(errno));
		return NULL;
	}

	fd = openat(sysfs, "resource2", open_flags | O_SYNC);
	close(sysfs);
	if (fd < 0) {
		igt_debug("Failed to open resource2 for VF%d: %s\n", vf_num, strerror(errno));
		return NULL;
	}

	if (fstat(fd, &st)) {
		igt_debug("Failed to stat resource2 for VF%d: %s\n", vf_num, strerror(errno));
		close(fd);
		return NULL;
	}

	if (st.st_size < length) {
		igt_debug("Mapping length (%lu) exceeds BAR2 size (%lu)\n", length, st.st_size);
		close(fd);
		return NULL;
	}

	addr = mmap(NULL, length, prot, MAP_SHARED, fd, offset);
	close(fd);
	if (addr == MAP_FAILED) {
		igt_debug("Failed mmap resource2 for VF%d: %s\n", vf_num, strerror(errno));
		return NULL;
	}

	return addr;
}

static uint64_t get_vf_lmem_size(int pf_fd, int vf_num)
{
	/* limit to first two pages
	 * TODO: Extend to full range when the proper interface (lmem_provisioned) is added
	 */
	return SZ_4M;
}

static void munmap_vf_lmem(struct lmem_info *lmem)
{
	igt_debug_on_f(munmap(lmem->addr, lmem->size),
		       "Failed munmap %p: %s\n", lmem->addr, strerror(errno));
}

static char lmem_read(const char *addr, size_t idx)
{
	return READ_ONCE(*(addr + idx));
}

static char lmem_write_readback(char *addr, size_t idx, char value)
{
	WRITE_ONCE(*(addr + idx), value);
	return lmem_read(addr, idx);
}

static bool lmem_write_pattern(struct lmem_info *lmem, char value, size_t start, size_t step)
{
	char read;

	for (; start < lmem->size; start += step) {
		read = lmem_write_readback(lmem->addr, start, value);
		if (igt_debug_on_f(read != value, "LMEM[%lu]=%u != %u\n", start, read, value))
			return false;
	}
	return true;
}

static bool lmem_contains_expected_values_(struct lmem_info *lmem,
					   char expected, size_t start,
					   size_t step)
{
	char read;

	for (; start < lmem->size; start += step) {
		read = lmem_read(lmem->addr, start);
		if (igt_debug_on_f(read != expected,
				   "LMEM[%lu]=%u != %u\n", start, read, expected))
			return false;
	}
	return true;
}

static bool lmem_contains_expected_values(int pf_fd, int vf_num, size_t length,
					  char expected)
{
	struct lmem_info lmem = { .size = length };
	bool result;

	lmem.addr = mmap_vf_lmem(pf_fd, vf_num, length, PROT_READ | PROT_WRITE, 0);
	if (igt_debug_on(!lmem.addr))
		return false;

	result = lmem_contains_expected_values_(&lmem, expected, 0, STEP);
	munmap_vf_lmem(&lmem);

	return result;
}

static bool lmem_mmap_write_munmap(int pf_fd, int vf_num, size_t length, char value)
{
	struct lmem_info lmem;
	bool result;

	lmem.size = length;
	lmem.addr = mmap_vf_lmem(pf_fd, vf_num, length, PROT_READ | PROT_WRITE, 0);
	if (igt_debug_on(!lmem.addr))
		return false;
	result = lmem_write_pattern(&lmem, value, 0, STEP);
	munmap_vf_lmem(&lmem);

	return result;
}

static void lmem_subcheck_init(struct subcheck_data *data)
{
	struct lmem_data *ldata = (struct lmem_data *)data;

	igt_assert_fd(data->pf_fd);
	igt_assert(data->num_vfs);

	if (!xe_has_vram(data->pf_fd)) {
		set_skip_reason(data, "No LMEM\n");
		return;
	}

	ldata->vf_lmem_size = calloc(data->num_vfs + 1, sizeof(size_t));
	igt_assert(ldata->vf_lmem_size);

	for (int vf_id = 1; vf_id <= data->num_vfs; ++vf_id)
		ldata->vf_lmem_size[vf_id] = get_vf_lmem_size(ldata->base.pf_fd, vf_id);
}

static void lmem_subcheck_prepare_vf(int vf_id, struct subcheck_data *data)
{
	struct lmem_data *ldata = (struct lmem_data *)data;

	if (data->stop_reason)
		return;

	igt_assert(vf_id > 0 && vf_id <= data->num_vfs);

	if (!lmem_mmap_write_munmap(data->pf_fd, vf_id,
				    ldata->vf_lmem_size[vf_id], vf_id)) {
		set_fail_reason(data, "LMEM write failed on VF%u\n", vf_id);
	}
}

static void lmem_subcheck_verify_vf(int vf_id, int flr_vf_id, struct subcheck_data *data)
{
	struct lmem_data *ldata = (struct lmem_data *)data;
	char expected = (vf_id == flr_vf_id) ? 0 : vf_id;

	if (data->stop_reason)
		return;

	if (!lmem_contains_expected_values(data->pf_fd, vf_id,
					   ldata->vf_lmem_size[vf_id], expected)) {
		set_fail_reason(data,
				"LMEM check after VF%u FLR failed on VF%u\n",
				flr_vf_id, vf_id);
	}
}

static void lmem_subcheck_cleanup(struct subcheck_data *data)
{
	struct lmem_data *ldata = (struct lmem_data *)data;

	free(ldata->vf_lmem_size);
}

#define SCRATCH_REG		0x190240
#define SCRATCH_REG_COUNT	4
#define MED_SCRATCH_REG		0x190310
#define MED_SCRATCH_REG_COUNT	4

struct regs_data {
	struct subcheck_data base;
	struct intel_mmio_data *mmio;
	uint32_t reg_addr;
	int reg_count;
};

static void regs_subcheck_init(struct subcheck_data *data)
{
	struct regs_data *rdata = (struct regs_data *)data;

	if (!xe_has_media_gt(data->pf_fd) &&
	    rdata->reg_addr == MED_SCRATCH_REG) {
		set_skip_reason(data, "No media GT\n");
	}
}

static void regs_subcheck_prepare_vf(int vf_id, struct subcheck_data *data)
{
	struct regs_data *rdata = (struct regs_data *)data;
	uint32_t reg;
	int i;

	if (data->stop_reason)
		return;

	if (!is_intel_mmio_initialized(&rdata->mmio[vf_id])) {
		struct pci_device *pci_dev = __igt_device_get_pci_device(data->pf_fd, vf_id);

		if (!pci_dev) {
			set_fail_reason(data, "No PCI device found for VF%u\n", vf_id);
			return;
		}

		if (intel_register_access_init(&rdata->mmio[vf_id], pci_dev, false)) {
			set_fail_reason(data, "Failed to get access to VF%u MMIO\n", vf_id);
			return;
		}
	}

	for (i = 0; i < rdata->reg_count; i++) {
		reg = rdata->reg_addr + i * 4;

		intel_register_write(&rdata->mmio[vf_id], reg, vf_id);
		if (intel_register_read(&rdata->mmio[vf_id], reg) != vf_id) {
			set_fail_reason(data, "Registers write/read check failed on VF%u\n", vf_id);
			return;
		}
	}
}

static void regs_subcheck_verify_vf(int vf_id, int flr_vf_id, struct subcheck_data *data)
{
	struct regs_data *rdata = (struct regs_data *)data;
	uint32_t expected = (vf_id == flr_vf_id) ? 0 : vf_id;
	uint32_t reg;
	int i;

	if (data->stop_reason)
		return;

	for (i = 0; i < rdata->reg_count; i++) {
		reg = rdata->reg_addr + i * 4;

		if (intel_register_read(&rdata->mmio[vf_id], reg) != expected) {
			set_fail_reason(data,
					"Registers check after VF%u FLR failed on VF%u\n",
					flr_vf_id, vf_id);
			return;
		}
	}
}

static void regs_subcheck_cleanup(struct subcheck_data *data)
{
	struct regs_data *rdata = (struct regs_data *)data;
	int i;

	if (rdata->mmio)
		for (i = 1; i <= data->num_vfs; ++i)
			if (is_intel_mmio_initialized(&rdata->mmio[i]))
				intel_register_access_fini(&rdata->mmio[i]);
}

static void clear_tests(int pf_fd, int num_vfs)
{
	struct xe_mmio xemmio = { };
	const unsigned int num_gts = xe_number_gt(pf_fd);
	struct ggtt_data gdata[num_gts];
	struct lmem_data ldata = {
		.base = { .pf_fd = pf_fd, .num_vfs = num_vfs }
	};
	struct intel_mmio_data mmio[num_vfs + 1];
	struct regs_data scratch_data = {
		.base = { .pf_fd = pf_fd, .num_vfs = num_vfs },
		.mmio = mmio,
		.reg_addr = SCRATCH_REG,
		.reg_count = SCRATCH_REG_COUNT
	};
	struct regs_data media_scratch_data = {
		.base = { .pf_fd = pf_fd, .num_vfs = num_vfs },
		.mmio = mmio,
		.reg_addr = MED_SCRATCH_REG,
		.reg_count = MED_SCRATCH_REG_COUNT
	};
	const unsigned int num_checks = num_gts + 3;
	struct subcheck checks[num_checks];
	int i;

	memset(mmio, 0, sizeof(mmio));

	for (i = 0; i < num_gts; ++i) {
		gdata[i] = (struct ggtt_data){
			.base = { .pf_fd = pf_fd, .num_vfs = num_vfs, .gt = i },
			.mmio = &xemmio
		};
		checks[i] = (struct subcheck){
			.data = (struct subcheck_data *)&gdata[i],
			.name = "clear-ggtt",
			.init = ggtt_subcheck_init,
			.prepare_vf = ggtt_subcheck_prepare_vf,
			.verify_vf = ggtt_subcheck_verify_vf,
			.cleanup = ggtt_subcheck_cleanup
		};
	}
	checks[i++] = (struct subcheck) {
		.data = (struct subcheck_data *)&ldata,
		.name = "clear-lmem",
		.init = lmem_subcheck_init,
		.prepare_vf = lmem_subcheck_prepare_vf,
		.verify_vf = lmem_subcheck_verify_vf,
		.cleanup = lmem_subcheck_cleanup };
	checks[i++] = (struct subcheck) {
		.data = (struct subcheck_data *)&scratch_data,
		.name = "clear-scratch-regs",
		.init = regs_subcheck_init,
		.prepare_vf = regs_subcheck_prepare_vf,
		.verify_vf = regs_subcheck_verify_vf,
		.cleanup = regs_subcheck_cleanup };
	checks[i++] = (struct subcheck) {
		.data = (struct subcheck_data *)&media_scratch_data,
		.name = "clear-media-scratch-regs",
		.init = regs_subcheck_init,
		.prepare_vf = regs_subcheck_prepare_vf,
		.verify_vf = regs_subcheck_verify_vf,
		.cleanup = regs_subcheck_cleanup
	};
	igt_assert_eq(i, num_checks);

	verify_flr(pf_fd, num_vfs, checks, num_checks);
}

igt_main
{
	int pf_fd;
	bool autoprobe;

	igt_fixture {
		pf_fd = drm_open_driver(DRIVER_XE);
		igt_require(igt_sriov_is_pf(pf_fd));
		igt_require(igt_sriov_get_enabled_vfs(pf_fd) == 0);
		autoprobe = igt_sriov_is_driver_autoprobe_enabled(pf_fd);
	}

	igt_describe("Verify LMEM, GGTT, and SCRATCH_REGS are properly cleared after VF1 FLR");
	igt_subtest("flr-vf1-clear") {
		clear_tests(pf_fd, 1);
	}

	igt_describe("Perform sequential FLR on each VF, verifying that LMEM, GGTT, and SCRATCH_REGS are cleared only on the reset VF.");
	igt_subtest("flr-each-isolation") {
		unsigned int total_vfs = igt_sriov_get_total_vfs(pf_fd);

		igt_require(total_vfs > 1);

		clear_tests(pf_fd, total_vfs > 3 ? 3 : total_vfs);
	}

	igt_fixture {
		igt_sriov_disable_vfs(pf_fd);
		/* abort to avoid execution of next tests with enabled VFs */
		igt_abort_on_f(igt_sriov_get_enabled_vfs(pf_fd) > 0, "Failed to disable VF(s)");
		autoprobe ? igt_sriov_enable_driver_autoprobe(pf_fd) :
			    igt_sriov_disable_driver_autoprobe(pf_fd);
		igt_abort_on_f(autoprobe != igt_sriov_is_driver_autoprobe_enabled(pf_fd),
			       "Failed to restore sriov_drivers_autoprobe value\n");
		close(pf_fd);
	}
}
