// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Google LLC.
 * Copyright © 2023 Collabora, Ltd.
 * Copyright © 2024 Red Hat, Inc.
 * Copyright © 2025 Intel Corporation
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/mount.h>

#include "igt_aux.h"
#include "igt_configfs.h"

/**
 * SECTION:igt_configfs
 * @short_description: Support code for configfs features
 * @title: configfs
 * @include: igt_configfs.h
 *
 * This library provides helpers to access configfs features.
 */

/*
 * General configfs helpers
 */

static const char *__igt_configfs_mount(void)
{
	if (igt_is_mountpoint("/sys/kernel/config"))
		return "/sys/kernel/config";

	if (igt_is_mountpoint("/config"))
		return "/config";

	if (mount("config", "/sys/kernel/config", "configfs", 0, 0))
		return NULL;

	return "/sys/kernel/config";
}

/**
 * igt_configfs_mount:
 *
 * This attempts to locate where configfs is mounted on the filesystem,
 * and if not found, will then try to mount configfs at /sys/kernel/config.
 *
 * Returns:
 * The path to the configfs mount point (e.g. /sys/kernel/config)
 */
const char *igt_configfs_mount(void)
{
	static const char *path;

	if (!path)
		path = __igt_configfs_mount();

	return path;
}

/**
 * igt_configfs_open: open configfs path
 * @name: name of the configfs directory
 *
 * Opens the configfs directory corresponding to the name
 *
 * Returns:
 * The directory fd, or -1 on failure.
 */
int igt_configfs_open(const char *name)
{
	char path[PATH_MAX];
	const char *configfs_path;

	configfs_path = igt_configfs_mount();
	if (!configfs_path) {
		igt_debug("configfs not mounted, errno=%d\n", errno);
		return -1;
	}

	snprintf(path, sizeof(path), "%s/%s", configfs_path, name);

	return open(path, O_DIRECTORY | O_RDONLY);
}
