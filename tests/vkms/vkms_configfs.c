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

igt_main
{
	struct {
		const char *name;
		void (*fn)(void);
	} tests[] = {
		{ "device-default-files", test_device_default_files },
		{ "device-default-values", test_device_default_values },
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
