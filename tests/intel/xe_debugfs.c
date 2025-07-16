// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <dirent.h>
#include <fcntl.h>

#include "igt.h"
#include "igt_debugfs.h"
#include "igt_dir.h"
#include "igt_sysfs.h"
#include "xe/xe_query.h"

struct {
	bool warn_on_not_hit;
} opt = { 0 };

/**
 * TEST: Xe debugfs test
 * Description: Xe-specific debugfs tests. These are complementary to the
 * core_debugfs and core_debugfs_display_on_off tests.
 *
 * Category: Core
 * Mega feature: General Core features
 * Sub-category: uapi
 * Functionality: debugfs
 * Feature: core
 * Test category: uapi
 *
 */

IGT_TEST_DESCRIPTION("Read entries from debugfs, and sysfs paths.");

static int xe_validate_entries(igt_dir_t *igt_dir,
			       const char * const str_val[], int str_cnt)
{
	igt_dir_file_list_t *file_list_entry;

	if (!igt_dir)
		return -1;

	igt_dir_scan_dirfd(igt_dir, -1);

	for (int i = 0; i < str_cnt; i++) {
		int hit = 0;

		igt_list_for_each_entry(file_list_entry,
					&igt_dir->file_list_head, link) {
			if (strcmp(file_list_entry->relative_path,
				   str_val[i]) == 0) {
				hit = 1;
				break;
			}
		}

		if (!hit && opt.warn_on_not_hit)
			igt_warn("no test for: %s\n", str_val[i]);
	}

	return 0;
}

/**
 * SUBTEST: xe-base
 * Description: Check if various debugfs devnodes exist and test reading them
 */
static void
xe_test_base(int fd, struct drm_xe_query_config *config, igt_dir_t *igt_dir)
{
	uint16_t devid = intel_get_drm_devid(fd);
	static const char * const expected_files[] = {
		"gt0",
		"gt1",
		"stolen_mm",
		"gtt_mm",
		"vram0_mm",
		"forcewake_all",
		"info",
		"gem_names",
		"clients",
		"name"
	};
	char reference[4096];
	int val = 0;

	igt_assert(config);
	sprintf(reference, "devid 0x%llx",
		config->info[DRM_XE_QUERY_CONFIG_REV_AND_DEVICE_ID] & 0xffff);
	igt_assert(igt_debugfs_search(fd, "info", reference));

	sprintf(reference, "revid %lld",
		config->info[DRM_XE_QUERY_CONFIG_REV_AND_DEVICE_ID] >> 16);
	igt_assert(igt_debugfs_search(fd, "info", reference));

	sprintf(reference, "is_dgfx %s", config->info[DRM_XE_QUERY_CONFIG_FLAGS] &
		DRM_XE_QUERY_CONFIG_FLAG_HAS_VRAM ? "yes" : "no");

	igt_assert(igt_debugfs_search(fd, "info", reference));

	if (intel_gen(devid) < 20) {
		switch (config->info[DRM_XE_QUERY_CONFIG_VA_BITS]) {
		case 48:
			val = 3;
			break;
		case 57:
			val = 4;
			break;
		}

		sprintf(reference, "vm_max_level %d", val);
		igt_assert(igt_debugfs_search(fd, "info", reference));
	}

	snprintf(reference, sizeof(reference), "tile_count %d", xe_sysfs_get_num_tiles(fd));
	igt_assert(igt_debugfs_search(fd, "info", reference));

	igt_assert(igt_debugfs_exists(fd, "gt0", O_RDONLY));

	igt_assert(igt_debugfs_exists(fd, "gtt_mm", O_RDONLY));
	igt_debugfs_dump(fd, "gtt_mm");

	if (config->info[DRM_XE_QUERY_CONFIG_FLAGS] & DRM_XE_QUERY_CONFIG_FLAG_HAS_VRAM) {
		igt_assert(igt_debugfs_exists(fd, "vram0_mm", O_RDONLY));
		igt_debugfs_dump(fd, "vram0_mm");
	}

	if (igt_debugfs_exists(fd, "stolen_mm", O_RDONLY))
		igt_debugfs_dump(fd, "stolen_mm");

	igt_assert(igt_debugfs_exists(fd, "clients", O_RDONLY));
	igt_debugfs_dump(fd, "clients");

	igt_assert(igt_debugfs_exists(fd, "gem_names", O_RDONLY));
	igt_debugfs_dump(fd, "gem_names");

	xe_validate_entries(igt_dir, expected_files,
			    ARRAY_SIZE(expected_files));
}

/**
 * SUBTEST: xe-forcewake
 * Description: Check forcewake debugfs devnode
 */
static void
xe_test_forcewake(int fd)
{
	int handle = igt_debugfs_open(fd, "forcewake_all", O_WRONLY);

	igt_assert_neq(handle, -1);
	close(handle);
}

const char *help_str =
	"  -w\t--warn-not-hit Produce warnings if it founds a devfs node without tests";

struct option long_options[] = {
	{ "--warn-not-hit", no_argument, NULL, 'w'},
	{ 0, 0, 0, 0 }
};

static int opt_handler(int option, int option_index, void *input)
{
	switch (option) {
	case 'w':
		opt.warn_on_not_hit = true;
		break;
	default:
		return IGT_OPT_HANDLER_ERROR;
	}

	return IGT_OPT_HANDLER_SUCCESS;
}

igt_main_args("", long_options, help_str, opt_handler, NULL)
{
	int debugfs = -1;
	int fd = -1;
	igt_dir_t *igt_dir = NULL;

	igt_fixture {
		fd = drm_open_driver_master(DRIVER_XE);
		__igt_debugfs_dump(fd, "info", IGT_LOG_INFO);
		debugfs = igt_debugfs_dir(fd);

		igt_dir = igt_dir_create(debugfs);
		igt_require(igt_dir);

		kmstest_set_vt_graphics_mode();
	}

	igt_describe("Check if various debugfs devnodes exist and test reading them.");
	igt_subtest("xe-base") {
		xe_test_base(fd, xe_config(fd), igt_dir);
	}

	igt_describe("Check forcewake debugfs devnode");
	igt_subtest("xe-forcewake") {
		xe_test_forcewake(fd);
	}

	igt_fixture {
		igt_dir_destroy(igt_dir);
		close(debugfs);
		drm_close_driver(fd);
	}
}
