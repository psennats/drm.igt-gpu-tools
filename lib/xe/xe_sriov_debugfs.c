// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2024 Intel Corporation. All rights reserved.
 */

#include <dirent.h>
#include <fcntl.h>

#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_sriov_device.h"
#include "igt_sysfs.h"
#include "xe/xe_query.h"
#include "xe/xe_sriov_debugfs.h"
#include "xe/xe_sriov_provisioning.h"

#define SRIOV_DEBUGFS_PATH_MAX 96

static char *xe_sriov_pf_debugfs_path(int pf, unsigned int vf_num, unsigned int gt_num, char *path,
				      int pathlen)
{
	char sriov_path[SRIOV_DEBUGFS_PATH_MAX];

	if (!igt_debugfs_path(pf, path, pathlen))
		return NULL;

	if (vf_num)
		snprintf(sriov_path, SRIOV_DEBUGFS_PATH_MAX, "/gt%u/vf%u/", gt_num, vf_num);
	else
		snprintf(sriov_path, SRIOV_DEBUGFS_PATH_MAX, "/gt%u/pf/", gt_num);

	strncat(path, sriov_path, pathlen - strlen(path));

	if (access(path, F_OK))
		return NULL;

	return path;
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
 * File descriptor or -1 on failure.
 */
int xe_sriov_pf_debugfs_attr_open(int pf, unsigned int vf_num, unsigned int gt_num,
				  const char *attr, int mode)
{
	char path[PATH_MAX];
	int debugfs;

	igt_assert(igt_sriov_is_pf(pf) && is_xe_device(pf));
	igt_assert(gt_num < xe_number_gt(pf));

	if (!xe_sriov_pf_debugfs_path(pf, vf_num, gt_num, path, sizeof(path)))
		return -1;

	strncat(path, attr, sizeof(path) - strlen(path));

	debugfs = open(path, mode);
	igt_debug_on(debugfs < 0);

	return debugfs;
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
		return "lmem_provisioned";
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
		if (sscanf(line, "VF%u: %lu-%lu", &range->vf_id, &range->start, &range->end) == 3)
			ret = 0;
		break;
	case XE_SRIOV_SHARED_RES_GGTT:
		if (sscanf(line, "VF%u: %lx-%lx", &range->vf_id, &range->start, &range->end) == 3)
			ret = 0;
		break;
	case XE_SRIOV_SHARED_RES_LMEM:
		/* Convert to an inclusive range as is the case for other resources.
		 * The start is always 0 and the end is the value read - 1.
		 */
		if (sscanf(line, "VF%u: %lu", &range->vf_id, &range->end) == 2)
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
						  "%s:VF%u: %lx-%lx\n" :
						  "%s:VF%u: %lu-%lu\n",
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

static int xe_sriov_pf_debugfs_path_open(int pf, unsigned int vf_num,
					 unsigned int gt_num)
{
	char path[PATH_MAX];

	if (igt_debug_on_f(!xe_sriov_pf_debugfs_path(pf, vf_num, gt_num, path,
						     sizeof(path)),
			   "path: %s\n", path))
		return -1;

	return open(path, O_RDONLY);
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
#define DEFINE_XE_SRIOV_PF_DEBUGFS_FUNC(type, suffix, sysfs_func)		\
	int __xe_sriov_pf_debugfs_##suffix(int pf, unsigned int vf_num,		\
					   unsigned int gt_num,			\
					   const char *attr, type value)	\
	{									\
		bool ret;							\
		int dir = xe_sriov_pf_debugfs_path_open(pf, vf_num, gt_num);	\
										\
		if (igt_debug_on(dir < 0))					\
			return dir;						\
										\
		ret = sysfs_func(dir, attr, value);				\
		close(dir);							\
		return ret ? 0 : -1;						\
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
			ret = sscanf(line, "GuC contexts: %lu", value);
			break;
		case XE_SRIOV_SHARED_RES_DOORBELLS:
			ret = sscanf(line, "GuC doorbells: %lu", value);
			break;
		case XE_SRIOV_SHARED_RES_GGTT:
			ret = sscanf(line, "GGTT size: %lu", value);
			break;
		case XE_SRIOV_SHARED_RES_LMEM:
			ret = sscanf(line, "LMEM size: %lu", value);
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
