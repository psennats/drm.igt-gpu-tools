// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Google LLC.
 * Copyright © 2023 Collabora, Ltd.
 * Copyright © 2024-2025 Red Hat, Inc.
 */

/**
 * TEST: Tests for VKMS configfs support.
 * Category: Display
 * Mega feature: General Display Features
 * Sub-category: uapi
 * Functionality: vkms,configfs
 * Test category: functionality test
 */

#include <dirent.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>

#include "drmtest.h"
#include "igt.h"
#include "igt_vkms.h"

struct invalid_value {
	const char *value;
	int size;
};

static void assert_default_files(const char *path,
				 const char **files, size_t n_files,
				 const char **dirs, size_t n_dirs)
{
	DIR *dir;
	struct dirent *ent;
	int total = 0;
	int ret;

	/* Check that the number of files/directories matches the expected */
	dir = opendir(path);
	igt_assert(dir);
	while ((ent = readdir(dir)) != NULL) {
		if (strcmp(ent->d_name, ".") == 0 ||
		    strcmp(ent->d_name, "..") == 0)
			continue;

		total++;
	}
	igt_assert_eq(total, n_dirs + n_files);
	closedir(dir);

	/* Check that the files/directories are present */
	for (int i = 0; i < n_files; i++) {
		char file_path[PATH_MAX];
		struct stat buf;

		ret = snprintf(file_path, sizeof(file_path), "%s/%s", path,
			       files[i]);
		igt_assert(ret >= 0 && ret < sizeof(file_path));

		igt_assert_f(stat(file_path, &buf) == 0,
			     "File %s does not exists\n", file_path);
	}

	for (int i = 0; i < n_dirs; i++) {
		char dir_path[PATH_MAX];

		ret = snprintf(dir_path, sizeof(dir_path), "%s/%s", path,
			       dirs[i]);
		igt_assert(ret >= 0 && ret < sizeof(dir_path));

		dir = opendir(dir_path);
		igt_assert_f(dir, "Directory %s does not exists\n", dir_path);
		closedir(dir);
	}
}

static void assert_wrong_bool_values(const char *path)
{
	struct invalid_value invalid_bool_values[] = {
		{ "", 0 },
		{ "\0", 1 },
		{ "-1", 2 },
		{ "2", 1 },
		{ "o", 1 },
		{ "invalid", 8 },
	};
	int fd;
	int ret;

	for (int i = 0; i < ARRAY_SIZE(invalid_bool_values); i++) {
		struct invalid_value v = invalid_bool_values[i];

		fd = open(path, O_WRONLY);
		igt_assert_f(fd >= 0, "Error opening '%s'\n", path);

		ret = write(fd, v.value, v.size);
		igt_assert_f(ret <= 0, "Error writing '%s' to '%s'", v.value, path);

		close(fd);
	}
}

/**
 * SUBTEST: device-default-files
 * Description: Test that creating a VKMS device creates the default files and
 *              directories.
 */

static void test_device_default_files(void)
{
	igt_vkms_t *dev;

	static const char *files[] = {
		"enabled",
	};

	static const char *dirs[] = {
		"planes",
		"crtcs",
		"encoders",
		"connectors",
	};

	dev = igt_vkms_device_create(__func__);
	igt_assert(dev);

	assert_default_files(dev->path,
			     files, ARRAY_SIZE(files),
			     dirs, ARRAY_SIZE(dirs));

	igt_vkms_device_destroy(dev);
}

/**
 * SUBTEST: device-default-values
 * Description: Check that the default values for the device are correct.
 */

static void test_device_default_values(void)
{
	igt_vkms_t *dev;

	dev = igt_vkms_device_create(__func__);
	igt_assert(dev);

	igt_assert(!igt_vkms_device_is_enabled(dev));

	igt_vkms_device_destroy(dev);
}

/**
 * SUBTEST: device-wrong-values
 * Description: Check that setting unexpected values doesn't work.
 */

static void test_device_wrong_values(void)
{
	igt_vkms_t *dev;
	char path[PATH_MAX];

	/* It is not possible to create devices named "vkms" to avoid clashes
	 * with the default device created by VKMS
	 */
	dev = igt_vkms_device_create("vkms");
	igt_assert(!dev);

	/* Test invalid values for "enabled" */
	dev = igt_vkms_device_create(__func__);
	igt_assert(dev);

	igt_vkms_get_device_enabled_path(dev, path, sizeof(path));

	assert_wrong_bool_values(path);
	igt_assert(!igt_vkms_device_is_enabled(dev));

	igt_vkms_device_destroy(dev);
}

/**
 * SUBTEST: plane-default-files
 * Description: Test that creating a plane creates the default files and
 *              directories.
 */

static void test_plane_default_files(void)
{
	igt_vkms_t *dev;
	char path[PATH_MAX];

	static const char *files[] = {
		"type",
	};

	static const char *dirs[] = {
		"possible_crtcs",
	};

	dev = igt_vkms_device_create(__func__);
	igt_assert(dev);

	igt_vkms_device_add_plane(dev, "plane0");
	igt_vkms_get_plane_path(dev, "plane0", path, sizeof(path));

	assert_default_files(path,
			     files, ARRAY_SIZE(files),
			     dirs, ARRAY_SIZE(dirs));

	igt_vkms_device_destroy(dev);
}

/**
 * SUBTEST: plane-default-values
 * Description: Check that the default values for the plane are correct.
 */

static void test_plane_default_values(void)
{
	igt_vkms_t *dev;

	dev = igt_vkms_device_create(__func__);
	igt_assert(dev);

	igt_vkms_device_add_plane(dev, "plane0");

	igt_assert_eq(igt_vkms_plane_get_type(dev, "plane0"),
		      DRM_PLANE_TYPE_OVERLAY);

	igt_vkms_device_destroy(dev);
}

/**
 * SUBTEST: plane-wrong-values
 * Description: Check that setting unexpected values doesn't work.
 */

static void test_plane_wrong_values(void)
{
	struct invalid_value invalid_type_values[] = {
		{ "", 0 },
		{ "\0", 1 },
		{ "-1", 2 },
		{ "4", 1 },
		{ "primary", 8 },
		{ "overlay", 8 },
	};
	igt_vkms_t *dev;
	char path[PATH_MAX];
	int fd;
	int ret;

	/* Create a device with a primary plane */
	dev = igt_vkms_device_create(__func__);
	igt_assert(dev);

	igt_vkms_device_add_plane(dev, "plane0");
	igt_vkms_plane_set_type(dev, "plane0", DRM_PLANE_TYPE_PRIMARY);
	igt_assert_eq(igt_vkms_plane_get_type(dev, "plane0"),
		      DRM_PLANE_TYPE_PRIMARY);
	igt_vkms_get_plane_type_path(dev, "plane0", path, sizeof(path));

	/* Test invalid values for "type" */
	for (int i = 0; i < ARRAY_SIZE(invalid_type_values); i++) {
		struct invalid_value v = invalid_type_values[i];

		fd = open(path, O_WRONLY);
		igt_assert_f(fd >= 0, "Error opening '%s'\n", path);

		ret = write(fd, v.value, v.size);
		igt_assert_f(ret <= 0, "Error writing '%s' to '%s'", v.value, path);

		close(fd);
	}

	igt_assert_eq(igt_vkms_plane_get_type(dev, "plane0"),
		      DRM_PLANE_TYPE_PRIMARY);

	igt_vkms_device_destroy(dev);
}

/**
 * SUBTEST: plane-valid-values
 * Description: Check that setting valid values works.
 */

static void test_plane_valid_values(void)
{
	igt_vkms_t *dev;

	dev = igt_vkms_device_create(__func__);
	igt_assert(dev);

	igt_vkms_device_add_plane(dev, "plane0");

	/* Test valid values for "type" */
	igt_vkms_plane_set_type(dev, "plane0", DRM_PLANE_TYPE_OVERLAY);
	igt_assert_eq(igt_vkms_plane_get_type(dev, "plane0"),
		      DRM_PLANE_TYPE_OVERLAY);

	igt_vkms_plane_set_type(dev, "plane0", DRM_PLANE_TYPE_PRIMARY);
	igt_assert_eq(igt_vkms_plane_get_type(dev, "plane0"),
		      DRM_PLANE_TYPE_PRIMARY);

	igt_vkms_plane_set_type(dev, "plane0", DRM_PLANE_TYPE_CURSOR);
	igt_assert_eq(igt_vkms_plane_get_type(dev, "plane0"),
		      DRM_PLANE_TYPE_CURSOR);

	igt_vkms_device_destroy(dev);
}

igt_main
{
	struct {
		const char *name;
		void (*fn)(void);
	} tests[] = {
		{ "device-default-files", test_device_default_files },
		{ "device-default-values", test_device_default_values },
		{ "device-wrong-values", test_device_wrong_values },
		{ "plane-default-files", test_plane_default_files },
		{ "plane-default-values", test_plane_default_values },
		{ "plane-wrong-values", test_plane_wrong_values },
		{ "plane-valid-values", test_plane_valid_values },
	};

	igt_fixture {
		drm_load_module(DRIVER_VKMS);
		igt_require_vkms();
		igt_require_vkms_configfs();
		igt_vkms_destroy_all_devices();
	}

	for (int i = 0; i < ARRAY_SIZE(tests); i++) {
		igt_subtest(tests[i].name)
			tests[i].fn();
	}

	igt_fixture {
		igt_require_vkms();
		igt_require_vkms_configfs();
		igt_vkms_destroy_all_devices();
	}
}
