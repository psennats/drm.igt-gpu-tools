// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2024 Intel Corporation. All rights reserved.
 */

#include <dirent.h>
#include <fcntl.h>

#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_sriov_device.h"
#include "xe/xe_sriov_debugfs.h"
#include "xe/xe_query.h"

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
