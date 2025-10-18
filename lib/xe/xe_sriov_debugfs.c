// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2024 Intel Corporation. All rights reserved.
 */

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>

#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_sriov_device.h"
#include "igt_sysfs.h"
#include "xe/xe_query.h"
#include "xe/xe_sriov_debugfs.h"
#include "xe/xe_sriov_provisioning.h"

#define SRIOV_DEBUGFS_PATH_MAX 96

enum attr_scope {
	SCOPE_TILE,
	SCOPE_GT,
};

static enum attr_scope attr_scope(const char *attr)
{
	if (!strncmp(attr, "ggtt_", 5) || !strncmp(attr, "vram_", 5))
		return SCOPE_TILE;

	return SCOPE_GT;
}

static int access_attr_path(int dirfd, const char *attr, unsigned int vf_num,
			    unsigned int tile, unsigned int gt_num,
			    char *attr_path, size_t path_size)
{
	const bool is_vf = vf_num > 0;
	int n;

	switch (attr_scope(attr)) {
	case SCOPE_TILE:
		n = is_vf ? snprintf(attr_path, path_size, "sriov/vf%u/tile%u/%s", vf_num,
				     tile, attr) :
			    snprintf(attr_path, path_size, "sriov/pf/tile%u/%s", tile, attr);
		break;
	case SCOPE_GT:
		n = is_vf ? snprintf(attr_path, path_size, "sriov/vf%u/tile%u/gt%u/%s",
				     vf_num, tile, gt_num, attr) :
			    snprintf(attr_path, path_size, "sriov/pf/tile%u/gt%u/%s", tile,
				     gt_num, attr);
		break;
	};

	igt_assert(n >= 0 && (size_t)n < path_size);

	return igt_sysfs_has_attr(dirfd, attr_path) ? 0 : -ENOENT;
}

static int access_legacy_attr_path(int dirfd, const char *attr, unsigned int vf_num,
				   unsigned int gt_num, char *attr_path,
				   size_t path_size)
{
	const bool is_vf = vf_num > 0;
	char legacy[64];
	const char *name = attr;
	int n;

	/* Map vram_* to lmem_* only for legacy layout */
	if (!strncmp(attr, "vram_", 5)) {
		n = snprintf(legacy, sizeof(legacy), "lmem_%s", attr + 5);

		igt_assert(n >= 0 && (size_t)n < sizeof(legacy));
		name = legacy;
	}

	n = is_vf ? snprintf(attr_path, path_size, "gt%u/vf%u/%s", gt_num, vf_num, name) :
		    snprintf(attr_path, path_size, "gt%u/pf/%s", gt_num, name);
	igt_assert(n >= 0 && (size_t)n < path_size);

	return igt_sysfs_has_attr(dirfd, attr_path) ? 0 : -ENOENT;
}

static int attr_path_resolve(int pf, unsigned int vf_num, unsigned int gt_num,
			     const char *attr, int dirfd, char *attr_path,
			     size_t path_size)
{
	struct xe_device *xe_dev = xe_device_get(pf);
	int tile, ret = 0;

	igt_assert(xe_dev && igt_sriov_is_pf(pf));
	tile = xe_get_tile(xe_dev, gt_num);
	if (igt_debug_on(tile < 0))
		return -ENOENT;

	ret = access_attr_path(dirfd, attr, vf_num, tile, gt_num, attr_path, path_size);
	if (ret) {
		char first_path[SRIOV_DEBUGFS_PATH_MAX];

		snprintf(first_path, sizeof(first_path), "%.*s",
			 (int)((path_size < sizeof(first_path) - 1) ?
				path_size : sizeof(first_path) - 1),
			 attr_path);

		ret = access_legacy_attr_path(dirfd, attr, vf_num, gt_num,
					      attr_path, path_size);

		igt_debug_on_f(ret, "Failed to access '%s'; tried %s, %s\n", attr,
			       first_path, attr_path);
	}

	return ret;
}

/**
 * xe_sriov_pf_debugfs_attr_open:
 * @pf: PF device file descriptor
 * @vf_num: VF number (1-based) or 0 for PF
 * @gt_num: GT number
 * @attr: debugfs attribute name
 * @mode: mode bits as used by open()
 *
 * Opens SR-IOV debugfs attribute @attr for given PF device @pf, VF number @vf_num on GT @gt_num.
 *
 * Returns:
 * File descriptor or negative error code on failure.
 */
int xe_sriov_pf_debugfs_attr_open(int pf, unsigned int vf_num, unsigned int gt_num,
				  const char *attr, int mode)
{
	char attr_path[SRIOV_DEBUGFS_PATH_MAX];
	int dirfd, attr_fd, ret;

	dirfd = igt_debugfs_dir(pf);
	if (igt_debug_on(dirfd < 0))
		return -ENOENT;

	ret = attr_path_resolve(pf, vf_num, gt_num, attr, dirfd, attr_path, sizeof(attr_path));
	if (ret) {
		close(dirfd);
		return ret;
	}

	attr_fd = openat(dirfd, attr_path, mode);
	igt_debug_on_f(attr_fd < 0, "Failed to open '%s' (%s)\n",
		       attr, attr_path);
	close(dirfd);
	return attr_fd;
}

/**
 * xe_sriov_debugfs_provisioned_attr_name:
 * @res: The shared resource type
 *
 * Returns the name of the debugfs provisioned attribute corresponding
 * to the given shared resource type.
 *
 * Return: A string representing the debugfs provisioned attribute name if the
 *         resource type is valid, otherwise NULL.
 */
const char *xe_sriov_debugfs_provisioned_attr_name(enum xe_sriov_shared_res res)
{
	switch (res) {
	case XE_SRIOV_SHARED_RES_CONTEXTS:
		return "contexts_provisioned";
	case XE_SRIOV_SHARED_RES_DOORBELLS:
		return "doorbells_provisioned";
	case XE_SRIOV_SHARED_RES_GGTT:
		return "ggtt_provisioned";
	case XE_SRIOV_SHARED_RES_LMEM:
		return "vram_provisioned";
	}

	return NULL;
}

static int parse_provisioned_range(const char *line,
				   struct xe_sriov_provisioned_range *range,
				   enum xe_sriov_shared_res res)
{
	int ret = -1;

	switch (res) {
	case XE_SRIOV_SHARED_RES_CONTEXTS:
	case XE_SRIOV_SHARED_RES_DOORBELLS:
		if (sscanf(line, "VF%u: %" SCNu64 "-%" SCNu64, &range->vf_id, &range->start, &range->end) == 3)
			ret = 0;
		break;
	case XE_SRIOV_SHARED_RES_GGTT:
		if (sscanf(line, "VF%u: %" SCNx64 "-%" SCNx64, &range->vf_id, &range->start, &range->end) == 3)
			ret = 0;
		break;
	case XE_SRIOV_SHARED_RES_LMEM:
		/* Convert to an inclusive range as is the case for other resources.
		 * The start is always 0 and the end is the value read - 1.
		 */
		if (sscanf(line, "VF%u: %" SCNu64, &range->vf_id, &range->end) == 2)
			ret = 0;
		if (!range->end)
			return -1;
		range->end -= 1;
		range->start = 0;
		break;
	}

	return ret;
}

/**
 * xe_sriov_debugfs_pf_read_provisioned_ranges:
 * @pf_fd: PF device file descriptor
 * @res: resource
 * @gt_id: GT number
 * @ranges: pointer to array of provisioned ranges
 * @nr_ranges: pointer to number of read provisioned VFs
 *
 * Reads provisioned ranges of shared resources.
 * Allocates the space for ranges and updates
 * the nr_ranges to the number of read ranges.
 * The caller should free the allocated space.
 *
 * Return: 0 if successful in reading ranges, otherwise negative error code.
 */
int xe_sriov_pf_debugfs_read_provisioned_ranges(int pf_fd, enum xe_sriov_shared_res res,
						unsigned int gt_id,
						struct xe_sriov_provisioned_range **ranges,
						unsigned int *nr_ranges)
{
	struct xe_sriov_provisioned_range *new_ranges;
	struct xe_sriov_provisioned_range range;
	FILE *file;
	size_t n = 0;
	const char *fname;
	char *line = NULL;
	int fd, ret = 0;
	ssize_t nread;

	*nr_ranges = 0;
	*ranges = NULL;

	fname = xe_sriov_debugfs_provisioned_attr_name(res);
	if (!fname)
		return -EINVAL;

	fd = xe_sriov_pf_debugfs_attr_open(pf_fd, 0, gt_id, fname, O_RDONLY);
	if (fd < 0)
		return -ENOENT;
	file = fdopen(fd, "r");
	if (!file) {
		close(fd);
		return -errno;
	}

	while ((nread = getline(&line, &n, file)) != -1) {
		ret = parse_provisioned_range(line, &range, res);
		if (ret) {
			igt_debug("Failed to parse line: %s\n", line);
			goto cleanup;
		}

		new_ranges = realloc(*ranges, sizeof(range) * (*nr_ranges + 1));
		if (!new_ranges) {
			ret = -ENOMEM;
			goto cleanup;
		}
		*ranges = new_ranges;
		memcpy(&(*ranges)[*nr_ranges], &range, sizeof(range));
		(*nr_ranges)++;
	}

	if (ferror(file))
		ret = -EIO;

cleanup:
	free(line);
	fclose(file);

	if (ret < 0) {
		free(*ranges);
		*ranges = NULL;
		*nr_ranges = 0;
	}

	return ret;
}

static int compare_ranges_by_vf_id(const void *a, const void *b)
{
	const struct xe_sriov_provisioned_range *range_a = a;
	const struct xe_sriov_provisioned_range *range_b = b;

	return (range_a->vf_id - range_b->vf_id);
}

#define MAX_DEBUG_ENTRIES 70U

static int validate_vf_ids(enum xe_sriov_shared_res res,
			   struct xe_sriov_provisioned_range *ranges,
			   unsigned int nr_ranges, unsigned int expected_num_vfs)
{
	unsigned int current_vf_id = 0;

	/* If no VFs are expected, ensure no ranges are provided */
	if (expected_num_vfs == 0) {
		if (nr_ranges > 0) {
			unsigned int limit = min(nr_ranges, MAX_DEBUG_ENTRIES);

			igt_debug("%s: Unexpected %u ranges when expected num_vfs == 0\n",
				  xe_sriov_debugfs_provisioned_attr_name(res),
				  nr_ranges);
			for (unsigned int i = 0; i < limit; i++) {
				igt_debug((res == XE_SRIOV_SHARED_RES_GGTT) ?
						  "%s:VF%u: %" PRIx64 "-%" PRIx64 "\n" :
						  "%s:VF%u: %" PRIu64 "-%" PRIu64 "\n",
					  xe_sriov_shared_res_to_string(res),
					  ranges[i].vf_id, ranges[i].start, ranges[i].end);
			}
			igt_debug_on_f(nr_ranges > MAX_DEBUG_ENTRIES,
				       "%s: Output truncated to first %u ranges out of %u\n",
				       xe_sriov_debugfs_provisioned_attr_name(res),
				       MAX_DEBUG_ENTRIES, nr_ranges);

			return -ERANGE;
		}
		return 0; /* Valid case: no VFs, no ranges */
	}

	if (igt_debug_on_f(nr_ranges == 0,
			   "%s: No VF ranges\n",
			   xe_sriov_debugfs_provisioned_attr_name(res)))
		return -ENOENT;

	igt_assert(ranges);
	qsort(ranges, nr_ranges, sizeof(ranges[0]), compare_ranges_by_vf_id);

	for (unsigned int i = 0; i < nr_ranges; i++) {
		unsigned int vf_id = ranges[i].vf_id;

		if (igt_debug_on_f(vf_id == current_vf_id,
				   "%s: Duplicate VF%u entry found\n",
				   xe_sriov_debugfs_provisioned_attr_name(res), vf_id))
			return -EEXIST;

		if (igt_debug_on_f(vf_id < 1 || vf_id > expected_num_vfs,
				   "%s: Out of range VF%u\n",
				   xe_sriov_debugfs_provisioned_attr_name(res), vf_id))
			return -ERANGE;

		if (igt_debug_on_f(vf_id > current_vf_id + 1,
				   "%s: Missing VF%u\n",
				   xe_sriov_debugfs_provisioned_attr_name(res),
				   current_vf_id + 1))
			return -ESRCH;

		current_vf_id = vf_id;
	}

	if (igt_debug_on_f(current_vf_id != expected_num_vfs,
			   "%s: Missing VF%u\n",
			   xe_sriov_debugfs_provisioned_attr_name(res), expected_num_vfs))
		return -ESRCH;

	return 0;
}

/**
 * xe_sriov_pf_debugfs_read_check_ranges:
 * @pf_fd: PF device file descriptor
 * @res: resource
 * @gt_id: GT number
 * @ranges: pointer to array of provisioned ranges
 * @expected_num_vfs: expected number of provisioned VFs
 *
 * Reads and validates provisioned ranges of shared resources.
 * If successfully validated, returns num_vfs allocated ranges
 * sorted by VF id.
 * The caller should free the allocated space.
 *
 * Return: 0 if successful in reading valid ranges, otherwise negative error code.
 */
int xe_sriov_pf_debugfs_read_check_ranges(int pf_fd, enum xe_sriov_shared_res res,
					  unsigned int gt_id,
					  struct xe_sriov_provisioned_range **ranges,
					  unsigned int expected_num_vfs)
{
	unsigned int nr_ranges;
	int ret;

	ret = xe_sriov_pf_debugfs_read_provisioned_ranges(pf_fd, res, gt_id,
							  ranges, &nr_ranges);
	if (ret)
		return ret;

	ret = validate_vf_ids(res, *ranges, nr_ranges, expected_num_vfs);
	if (ret) {
		free(*ranges);
		*ranges = NULL;
	}

	return ret;
}

/**
 * DEFINE_XE_SRIOV_PF_DEBUGFS_FUNC - Define a function for accessing debugfs attributes
 * @type: Data type of the value to read or write (e.g., `uint32_t`, `bool`, etc.)
 * @suffix: Function name suffix appended to `__xe_sriov_pf_debugfs_`
 * @sysfs_func: The sysfs helper function to perform the actual read or write operation
 *
 * Generates a function for accessing a debugfs attribute of a PF device.
 * It handles opening the debugfs path, performing the sysfs operation, and closing the
 * debugfs directory.
 *
 * The generated function has the following signature:
 *
 *	int __xe_sriov_pf_debugfs_<suffix>(int pf, unsigned int vf_num,
 *					   unsigned int gt_num,
 *					   const char *attr, type value)
 *
 * where:
 * - `pf` is the PF device file descriptor.
 * - `vf_num` is the VF number.
 * - `gt_num` is the GT number.
 * - `attr` is the name of the debugfs attribute.
 * - `value` is the data to read or write, depending on the sysfs function.
 *
 * Example:
 *
 *	DEFINE_XE_SRIOV_PF_DEBUGFS_FUNC(uint32_t, set_u32, __igt_sysfs_set_u32);
 *
 * This expands to a function:
 *
 *	int __xe_sriov_pf_debugfs_set_u32(int pf, unsigned int vf_num,
 *					  unsigned int gt_num,
 *					  const char *attr, uint32_t value);
 *
 * The function returns:
 * - `0` on success
 * - Negative error code on failure
 */
#define DEFINE_XE_SRIOV_PF_DEBUGFS_FUNC(type, suffix, sysfs_func)\
	int __xe_sriov_pf_debugfs_##suffix(int pf, unsigned int vf_num,				\
					   unsigned int gt_num,					\
					   const char *attr, type value)			\
	{											\
		char attr_path[SRIOV_DEBUGFS_PATH_MAX];						\
		int err, dirfd = igt_debugfs_dir(pf);						\
		bool ok;									\
												\
		if (igt_debug_on(dirfd < 0))							\
			return dirfd;								\
		err = attr_path_resolve(pf, vf_num, gt_num, attr,				\
					dirfd, attr_path, sizeof(attr_path));			\
		if (err) {									\
			close(dirfd);								\
			return err;								\
		}										\
												\
		ok = sysfs_func(dirfd, attr_path, value);					\
		close(dirfd);									\
		return ok ? 0 : -1;								\
	}

/**
 * __xe_sriov_pf_debugfs_get_u32 - Get a 32-bit unsigned integer from debugfs
 * @pf: PF device file descriptor
 * @vf_num: VF number
 * @gt_num: GT number
 * @attr: Debugfs attribute to read
 * @value: Pointer to store the retrieved value
 *
 * Reads a 32-bit unsigned integer from the specified debugfs attribute.
 *
 * Return: 0 on success, negative error code on failure.
 */
DEFINE_XE_SRIOV_PF_DEBUGFS_FUNC(uint32_t *, get_u32, __igt_sysfs_get_u32)

/**
 * __xe_sriov_pf_debugfs_set_u32 - Set a 32-bit unsigned integer in debugfs
 * @pf: PF device file descriptor
 * @vf_num: VF number
 * @gt_num: GT number
 * @attr: Debugfs attribute to write to
 * @value: The value to set
 *
 * Writes a 32-bit unsigned integer to the specified debugfs attribute.
 *
 * Return: 0 on success, negative error code on failure.
 */
DEFINE_XE_SRIOV_PF_DEBUGFS_FUNC(uint32_t, set_u32, __igt_sysfs_set_u32)

/**
 * __xe_sriov_pf_debugfs_get_u64 - Get a 64-bit unsigned integer from debugfs
 * @pf: PF device file descriptor
 * @vf_num: VF number
 * @gt_num: GT number
 * @attr: Debugfs attribute to read
 * @value: Pointer to store the retrieved value
 *
 * Reads a 64-bit unsigned integer from the specified debugfs attribute.
 *
 * Return: 0 on success, negative error code on failure.
 */
DEFINE_XE_SRIOV_PF_DEBUGFS_FUNC(uint64_t *, get_u64, __igt_sysfs_get_u64)

/**
 * __xe_sriov_pf_debugfs_set_u64 - Set a 64-bit unsigned integer in debugfs
 * @pf: PF device file descriptor
 * @vf_num: VF number
 * @gt_num: GT number
 * @attr: Debugfs attribute to write to
 * @value: The value to set
 *
 * Writes a 64-bit unsigned integer to the specified debugfs attribute.
 *
 * Return: 0 on success, negative error code on failure.
 */
DEFINE_XE_SRIOV_PF_DEBUGFS_FUNC(uint64_t, set_u64, __igt_sysfs_set_u64)

/**
 * __xe_sriov_pf_debugfs_get_boolean - Get a boolean value from debugfs
 * @pf: PF device file descriptor
 * @vf_num: VF number
 * @gt_num: GT number
 * @attr: Debugfs attribute to read
 * @value: Pointer to store the retrieved value
 *
 * Reads a boolean value from the specified debugfs attribute.
 *
 * Return: 0 on success, negative error code on failure.
 */
DEFINE_XE_SRIOV_PF_DEBUGFS_FUNC(bool *, get_boolean, __igt_sysfs_get_boolean)

/**
 * __xe_sriov_pf_debugfs_set_boolean - Set a boolean value in debugfs
 * @pf: PF device file descriptor
 * @vf_num: VF number
 * @gt_num: GT number
 * @attr: Debugfs attribute to write to
 * @value: The value to set
 *
 * Writes a boolean value to the specified debugfs attribute.
 *
 * Return: 0 on success, negative error code on failure.
 */
DEFINE_XE_SRIOV_PF_DEBUGFS_FUNC(bool, set_boolean, __igt_sysfs_set_boolean)

/**
 * __xe_sriov_vf_debugfs_get_selfconfig - Read VF's configuration data.
 * @vf: VF device file descriptor
 * @res: Shared resource type (see enum xe_sriov_shared_res)
 * @gt_num: GT number
 * @value: Pointer to store the read value
 *
 * Reads the specified shared resource @res from selfconfig of given VF device
 * @vf on GT @gt_num.
 *
 * Return: 0 on success, negative error code on failure.
 */
int __xe_sriov_vf_debugfs_get_selfconfig(int vf, enum xe_sriov_shared_res res,
					 unsigned int gt_num, uint64_t *value)
{
	FILE *file;
	size_t n = 0;
	char *line = NULL;
	int fd, ret = 0;

	fd = igt_debugfs_gt_open(vf, gt_num, "vf/self_config", O_RDONLY);
	if (fd < 0)
		return fd;
	file = fdopen(fd, "r");
	if (!file) {
		close(fd);
		return -errno;
	}

	while (getline(&line, &n, file) >= 0) {
		switch (res) {
		case XE_SRIOV_SHARED_RES_CONTEXTS:
			ret = sscanf(line, "GuC contexts: %" SCNu64, value);
			break;
		case XE_SRIOV_SHARED_RES_DOORBELLS:
			ret = sscanf(line, "GuC doorbells: %" SCNu64, value);
			break;
		case XE_SRIOV_SHARED_RES_GGTT:
			ret = sscanf(line, "GGTT size: %" SCNu64, value);
			break;
		case XE_SRIOV_SHARED_RES_LMEM:
			ret = sscanf(line, "LMEM size: %" SCNu64, value);
			break;
		}

		if (ret > 0)
			break;
	}

	free(line);
	fclose(file);

	return ret ? 0 : -1;
}

/**
 * xe_sriov_vf_debugfs_get_selfconfig - Read VF's configuration data.
 * @pf: PF device file descriptor
 * @res: Shared resource type (see enum xe_sriov_shared_res)
 * @gt_num: GT number
 *
 * A throwing version of __xe_sriov_vf_debugfs_get_selfconfig().
 * Instead of returning an error code, it returns the quota value and asserts
 * in case of an error.
 *
 * Return: The quota for the given shared resource.
 *         Asserts in case of failure.
 */
uint64_t xe_sriov_vf_debugfs_get_selfconfig(int vf, enum xe_sriov_shared_res res,
					    unsigned int gt_num)
{
	uint64_t value;

	igt_fail_on(__xe_sriov_vf_debugfs_get_selfconfig(vf, res, gt_num, &value));

	return value;
}

/**
 * xe_sriov_pf_debugfs_supports_restore_auto_provisioning - Check if PF
 * supports restoring the default auto-provisioning state via debugfs.
 * @pf: PF device file descriptor
 *
 * The helper probes for a writable
 * <debugfs>/sriov/restore_auto_provisioning attribute on the PF.
 *
 * Return: true if the attribute exists and is writable, false otherwise.
 */
bool xe_sriov_pf_debugfs_supports_restore_auto_provisioning(int pf)
{
	return igt_debugfs_exists(pf, "sriov/restore_auto_provisioning", O_WRONLY);
}

/**
 * xe_sriov_pf_debugfs_restore_auto_provisioning - Request PF to restore the
 * default auto-provisioning state after VF teardown.
 * @pf: PF device file descriptor
 *
 * This helper writes "1" to <debugfs>/sriov/restore_auto_provisioning.
 * Intended for use in test teardown after disabling VFs, so future
 * provisioning starts from a clean, default state.
 *
 * Return: 0 on success, or a negative errno on failure.
 */
int xe_sriov_pf_debugfs_restore_auto_provisioning(int pf)
{
	int dirfd = igt_debugfs_dir(pf);
	int ret;

	igt_assert_fd(dirfd);

	ret = igt_sysfs_write(dirfd, "sriov/restore_auto_provisioning", "1", 1);

	close(dirfd);

	return ret == 1 ? 0 : ret;
}
