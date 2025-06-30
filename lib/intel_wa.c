// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>

#include "igt_debugfs.h"
#include "igt_sysfs.h"
#include "intel_wa.h"
#include "xe/xe_query.h"

/**
 * igt_has_intel_wa:
 * @drm_fd:	A drm file descriptor
 * @check_wa:	Workaround to be checked
 *
 * Returns:	0 if no WA, 1 if WA present, -1 on error
 */
int igt_has_intel_wa(int drm_fd, const char *check_wa)
{
	int ret = 0;
	int debugfs_fd;
	unsigned int xe;
	char name[256];
	char *debugfs_dump, *has_wa;

	debugfs_fd = igt_debugfs_dir(drm_fd);
	if (debugfs_fd == -1)
		return -1;

	xe_for_each_gt(drm_fd, xe) {
		sprintf(name, "gt%d/workarounds", xe);
		if (!igt_debugfs_exists(drm_fd, name, O_RDONLY)) {
			ret = -1;
			break;
		}

		debugfs_dump = igt_sysfs_get(debugfs_fd, name);
		if (debugfs_dump) {
			has_wa = strstr(debugfs_dump, check_wa);
			free(debugfs_dump);
			if (has_wa) {
				ret = 1;
				break;
			}
		}
	}

	close(debugfs_fd);
	return ret;
}
