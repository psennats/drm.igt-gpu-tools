// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2024 Intel Corporation. All rights reserved.
 */

#include <dirent.h>
#include <fcntl.h>

#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_sriov_device.h"
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
