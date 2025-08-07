// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Google LLC.
 * Copyright © 2023 Collabora, Ltd.
 * Copyright © 2024-2025 Red Hat, Inc.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "igt.h"
#include "igt_vkms.h"

#define VKMS_ROOT_DIR_NAME		"vkms"
#define VKMS_FILE_ENABLED		"enabled"

/**
 * SECTION:igt_vkms
 * @short_description: Helpers to create and configure VKMS devices
 * @title: VKMS
 * @include: igt_vkms.h
 *
 * Helpers for creating VKMS devices and configuring them dynamically.
 *
 * First, create a VKMS device, next, add pipeline items (planes, CRTCs,
 * encoders, CRTCs and connectors) compose the pipeline by attaching each item
 * using the _attach_ functions and finally, enable the VKMS device.
 */

static const char *mount_vkms_configfs(void)
{
	static char vkms_root_path[PATH_MAX];
	const char *configfs_path;
	int ret;

	configfs_path = igt_configfs_mount();
	igt_assert_f(configfs_path, "Error mounting configfs");

	ret = snprintf(vkms_root_path, sizeof(vkms_root_path), "%s/%s",
		       configfs_path, VKMS_ROOT_DIR_NAME);
	igt_assert(ret >= 0 && ret < sizeof(vkms_root_path));

	return vkms_root_path;
}

static int read_int(const char *path)
{
	FILE *file;
	int value;
	int ret;

	file = fopen(path, "r");
	igt_assert_f(file, "Error opening '%s'\n", path);

	ret = fscanf(file, "%d", &value);
	fclose(file);
	igt_assert_f(ret == 1, "Error reading integer from '%s'\n", path);

	return value;
}

static bool read_bool(const char *path)
{
	int ret;

	ret = read_int(path);

	return !!ret;
}

static void write_int(const char *path, int value)
{
	FILE *file;
	int ret;

	file = fopen(path, "w");
	igt_assert_f(file, "Error opening '%s'\n", path);

	ret = fprintf(file, "%d", value);
	igt_assert_f(ret >= 0, "Error writing to '%s'\n", path);

	fclose(file);
}

static void write_bool(const char *path, bool value)
{
	write_int(path, value ? 1 : 0);
}

/**
 * igt_require_vkms_configfs:
 *
 * Require that VKMS supports configfs configuration.
 */
void igt_require_vkms_configfs(void)
{
	const char *vkms_root_path;
	DIR *dir;

	vkms_root_path = mount_vkms_configfs();

	dir = opendir(vkms_root_path);
	igt_require(dir);
	if (dir)
		closedir(dir);
}

/**
 * igt_vkms_get_device_enabled_path:
 * @dev: Device to get the enabled path from
 * @path: Output path
 * @len: Maximum @path length
 *
 * Returns the device "enabled" file path.
 */
void igt_vkms_get_device_enabled_path(igt_vkms_t *dev, char *path, size_t len)
{
	int ret;

	ret = snprintf(path, len, "%s/%s", dev->path, VKMS_FILE_ENABLED);
	igt_assert(ret >= 0 && ret < len);
}

/**
 * igt_vkms_device_create:
 * @name: VKMS device name
 *
 * Create a directory in the ConfigFS VKMS root directory, where the entire
 * pipeline will be configured.
 */
igt_vkms_t *igt_vkms_device_create(const char *name)
{
	igt_vkms_t *dev;
	const char *vkms_root_path;
	size_t path_len;
	DIR *dir;
	int ret;

	dev = calloc(1, sizeof(*dev));

	vkms_root_path = mount_vkms_configfs();

	path_len = strlen(vkms_root_path) + strlen(name) + 2;
	dev->path = malloc(path_len);
	ret = snprintf(dev->path, path_len, "%s/%s", vkms_root_path, name);
	igt_assert(ret >= 0 && ret < path_len);

	dir = opendir(dev->path);
	if (dir) {
		igt_debug("Device at path %s already exists\n", dev->path);
		closedir(dir);
	} else {
		ret = mkdir(dev->path, 0777);
		if (ret != 0) {
			free(dev->path);
			free(dev);
			dev = NULL;
		}
	}

	return dev;
}

static int detach_pipeline_items(const char *path, const struct stat *info,
				 const int typeflag, struct FTW *pathinfo)
{
	/*
	 * Level 4 are the links in the possible_* directories:
	 * vkms/<dev>/<pipeline items>/<pipeline item>/<possible_*>/<links>
	 */
	if (pathinfo->level == 4 && typeflag == FTW_SL) {
		igt_debug("Detaching pipeline item %s\n", path);
		return unlink(path);
	}

	/* Ignore the other files, they are removed by remove_pipeline_items */
	return 0;
}

static int remove_pipeline_items(const char *path, const struct stat *info,
				 const int typeflag, struct FTW *pathinfo)
{
	/* Level 0 is the device root directory: vkms/<dev> */
	if (pathinfo->level == 0) {
		igt_debug("Removing pipeline item %s\n", path);
		return rmdir(path);
	}

	/*
	 * Level 2 directories are the pipeline items:
	 * vkms/<dev>/<pipeline items>/<pipeline item>
	 */
	if (pathinfo->level == 2 && typeflag == FTW_DP) {
		igt_debug("Removing pipeline item %s\n", path);
		return rmdir(path);
	}

	/* Ignore the other files, they are removed by VKMS */
	return 0;
}

static int remove_device_dir(igt_vkms_t *dev)
{
	int ret;

	ret = nftw(dev->path, detach_pipeline_items, 64, FTW_DEPTH | FTW_PHYS);
	if (ret)
		return ret;

	ret = nftw(dev->path, remove_pipeline_items, 64, FTW_DEPTH | FTW_PHYS);
	return ret;
}

/**
 * igt_vkms_device_destroy:
 * @dev: Device to destroy
 *
 * Remove and free the VKMS device.
 */
void igt_vkms_device_destroy(igt_vkms_t *dev)
{
	int ret;

	igt_assert(dev);

	igt_vkms_device_set_enabled(dev, false);

	ret = remove_device_dir(dev);
	igt_assert_f(ret == 0,
		     "Unable to rmdir device directory '%s'. Got errno=%d (%s)\n",
		     dev->path, errno, strerror(errno));

	free(dev->path);
	free(dev);
}

/**
 * igt_vkms_destroy_all_devices:
 *
 * Remove all VKMS devices created via configfs.
 */
void igt_vkms_destroy_all_devices(void)
{
	igt_vkms_t *dev;
	const char *vkms_root_path;
	DIR *dir;
	struct dirent *ent;

	vkms_root_path = mount_vkms_configfs();
	dir = opendir(vkms_root_path);
	igt_assert_f(dir, "VKMS configfs directory not available at '%s'. "
		     "Got errno=%d (%s)\n", vkms_root_path, errno,
		     strerror(errno));

	while ((ent = readdir(dir)) != NULL) {
		if (strcmp(ent->d_name, ".") == 0 ||
		    strcmp(ent->d_name, "..") == 0)
			continue;

		dev = igt_vkms_device_create(ent->d_name);
		igt_vkms_device_destroy(dev);
	}

	closedir(dir);
}

/**
 * igt_vkms_device_is_enabled:
 * @dev: The device to check
 *
 * Indicate whether a VKMS device is enabled or not.
 */
bool igt_vkms_device_is_enabled(igt_vkms_t *dev)
{
	char path[PATH_MAX];

	igt_vkms_get_device_enabled_path(dev, path, sizeof(path));

	return read_bool(path);
}

/**
 * igt_vkms_device_set_enabled:
 * @dev: Device to enable or disable
 *
 * Enable or disable a VKMS device.
 */
void igt_vkms_device_set_enabled(igt_vkms_t *dev, bool enabled)
{
	char path[PATH_MAX];

	igt_vkms_get_device_enabled_path(dev, path, sizeof(path));

	write_bool(path, enabled);
}
