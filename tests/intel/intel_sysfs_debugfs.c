// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <dirent.h>
#include <fcntl.h>

#include "i915/gem.h"
#include "igt.h"
#include "igt_sysfs.h"
#include "xe/xe_query.h"

struct {
	bool warn_on_not_hit;
} opt = { 0 };

/**
 * TEST: debugfs test
 * Description: Read entries from debugfs, and sysfs paths.
 * Category: Core
 * Mega feature: General Core features
 * Sub-category: uapi
 * Functionality: debugfs
 * Feature: core
 * Test category: uapi
 *
 * SUBTEST: i915-debugfs-read-all-entries
 * Description: Read all entries from debugfs path validating debugfs entries
 *
 * SUBTEST: i915-debugfs-read-all-entries-display-off
 * Description: Read all debugfs entries with display off.
 *
 * SUBTEST: i915-debugfs-read-all-entries-display-on
 * Description: Read all debugfs entries with display on.
 *
 * SUBTEST: i915-sysfs-read-all-entries
 * Description: Read all entries from sysfs path validating debugfs entries
 *
 * SUBTEST: xe-debugfs-read-all-entries
 * Description: Read all entries from debugfs path validating debugfs entries
 *
 * SUBTEST: xe-debugfs-read-all-entries-display-off
 * Description: Read all debugfs entries with display off.
 *
 * SUBTEST: xe-debugfs-read-all-entries-display-on
 * Description: Read all debugfs entries with display on.
 *
 * SUBTEST: xe-sysfs-read-all-entries
 * Description: Read all entries from sysfs path validating debugfs entries
 *
 */

IGT_TEST_DESCRIPTION("Read entries from debugfs, and sysfs paths.");

static void read_and_discard_sysfs_entries(int path_fd, int indent)
{
	struct dirent *dirent;
	DIR *dir;
	char tabs[8];
	int i;

	igt_assert(indent < sizeof(tabs) - 1);

	for (i = 0; i < indent; i++)
		tabs[i] = '\t';
	tabs[i] = '\0';

	dir = fdopendir(path_fd);
	if (!dir)
		return;

	while ((dirent = readdir(dir))) {
		if (!strcmp(dirent->d_name, ".") ||
		    !strcmp(dirent->d_name, ".."))
			continue;

		if (dirent->d_type == DT_DIR) {
			int sub_fd;

			sub_fd = openat(path_fd, dirent->d_name,
					O_RDONLY | O_DIRECTORY);
			if (sub_fd < 0)
				continue;

			igt_debug("%sEntering subdir %s\n", tabs, dirent->d_name);
			read_and_discard_sysfs_entries(sub_fd, indent + 1);
			close(sub_fd);
		} else if (dirent->d_type == DT_REG) {
			char buf[512];
			int sub_fd;
			ssize_t ret;

			igt_kmsg(KMSG_DEBUG "Reading file \"%s\"\n", dirent->d_name);
			igt_debug("%sReading file \"%s\"\n", tabs, dirent->d_name);

			sub_fd = openat(path_fd, dirent->d_name, O_RDONLY | O_NONBLOCK);
			if (sub_fd == -1) {
				igt_debug("%sCould not open file \"%s\" with error: %m\n",
					  tabs, dirent->d_name);
				continue;
			}

			do {
				ret = read(sub_fd, buf, sizeof(buf));
			} while (ret == sizeof(buf));

			if (ret == -1)
				igt_debug("%sCould not read file \"%s\" with error: %m\n",
					  tabs, dirent->d_name);

			close(sub_fd);
		}
	}
	closedir(dir);
}

static void kms_tests(int fd, int debugfs, const char *card_name)
{
	igt_display_t display;
	struct igt_fb fb[IGT_MAX_PIPES];
	enum pipe pipe;
	int ret;
	char test_name[64];

	igt_fixture
		igt_display_require(&display, fd);

	snprintf(test_name, sizeof(test_name),
		 "%s-debugfs-read-all-entries-display-on", card_name);

	igt_subtest(test_name) {
		/* try to light all pipes */
retry:
		for_each_pipe(&display, pipe) {
			igt_output_t *output;

			for_each_valid_output_on_pipe(&display, pipe, output) {
				igt_plane_t *primary;
				drmModeModeInfo *mode;

				if (output->pending_pipe != PIPE_NONE)
					continue;

				igt_output_set_pipe(output, pipe);
				primary = igt_output_get_plane_type(output, DRM_PLANE_TYPE_PRIMARY);
				mode = igt_output_get_mode(output);
				igt_create_pattern_fb(display.drm_fd,
						      mode->hdisplay, mode->vdisplay,
						      DRM_FORMAT_XRGB8888,
						      DRM_FORMAT_MOD_LINEAR, &fb[pipe]);

				/* Set a valid fb as some debugfs like to
				 * inspect it on a active pipe
				 */
				igt_plane_set_fb(primary, &fb[pipe]);
				break;
			}
		}

		if (display.is_atomic)
			ret = igt_display_try_commit_atomic(&display,
					DRM_MODE_ATOMIC_TEST_ONLY |
					DRM_MODE_ATOMIC_ALLOW_MODESET,
					NULL);
		else
			ret = igt_display_try_commit2(&display, COMMIT_LEGACY);

		if (ret) {
			igt_output_t *output;
			bool found = igt_override_all_active_output_modes_to_fit_bw(&display);

			igt_require_f(found, "No valid mode combo found.\n");

			for_each_connected_output(&display, output)
				igt_output_set_pipe(output, PIPE_NONE);

			goto retry;
		}

		igt_display_commit2(&display, display.is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);

		read_and_discard_sysfs_entries(debugfs, 0);
	}

	snprintf(test_name, sizeof(test_name),
		 "%s-debugfs-read-all-entries-display-off", card_name);

	igt_subtest(test_name) {
		igt_output_t *output;
		igt_plane_t *plane;

		for_each_connected_output(&display, output)
			igt_output_set_pipe(output, PIPE_NONE);

		for_each_pipe(&display, pipe)
			for_each_plane_on_pipe(&display, pipe, plane)
				igt_plane_set_fb(plane, NULL);

		igt_display_commit2(&display, display.is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);

		read_and_discard_sysfs_entries(debugfs, 0);
	}

	igt_fixture
		igt_display_fini(&display);
}

static int xe_validate_entries(int fd, const char *add_path,
			       const char * const str_val[], int str_cnt)
{
	int i;
	int hit;
	int found = 0;
	int not_found = 0;
	DIR *dir;
	struct dirent *de;
	char path[PATH_MAX];

	if (!igt_debugfs_path(fd, path, sizeof(path)))
		return -1;

	strcat(path, add_path);
	dir = opendir(path);
	if (!dir)
		return -1;

	while ((de = readdir(dir))) {
		if (de->d_name[0] == '.')
			continue;
		hit = 0;
		for (i = 0; i < str_cnt; i++) {
			if (!strcmp(str_val[i], de->d_name)) {
				hit = 1;
				break;
			}
		}
		if (hit) {
			found++;
		} else if (opt.warn_on_not_hit) {
			not_found++;
			igt_warn("no test for: %s/%s\n", path, de->d_name);
		}
	}
	closedir(dir);
	return 0;
}

/**
 * SUBTEST: xe-base
 * Description: Check if various debugfs devnodes exist and test reading them
 */
static void
xe_test_base(int fd, struct drm_xe_query_config *config)
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

	xe_validate_entries(fd, "", expected_files, ARRAY_SIZE(expected_files));
}

/**
 * SUBTEST: xe-gt
 * Description: Check all gt debugfs devnodes
 * TODO: add support for ``force_reset`` entries
 */
static void
xe_test_gt(int fd, int gt_id)
{
	char name[256];
	static const char * const expected_files[] = {
		"uc",
		"steering",
		"topology",
		"sa_info",
		"hw_engines",
		"pat",
		"mocs",
//		"force_reset"
		"ggtt",
		"register-save-restore",
		"workarounds",
		"default_lrc_rcs",
		"default_lrc_ccs",
		"default_lrc_bcs",
		"default_lrc_vcs",
		"default_lrc_vecs",
		"hwconfig"

	};
	static const char * const expected_files_uc[] = {
		"huc_info",
		"guc_log",
		"guc_info",
//		"guc_ct_selftest"
	};

	for (int i = 0; i < ARRAY_SIZE(expected_files); i++) {
		sprintf(name, "gt%d/%s", gt_id, expected_files[i]);
		igt_assert(igt_debugfs_exists(fd, name, O_RDONLY));
		if (igt_debugfs_is_dir(fd, expected_files[i], gt_id))
			continue;
		igt_debugfs_dump(fd, name);
	}

	for (int i = 0; i < ARRAY_SIZE(expected_files_uc); i++) {
		sprintf(name, "gt%d/uc/%s", gt_id, expected_files_uc[i]);
		igt_assert(igt_debugfs_exists(fd, name, O_RDONLY));
		igt_debugfs_dump(fd, name);
	}

	sprintf(name, "/gt%d", gt_id);
	xe_validate_entries(fd, name, expected_files, ARRAY_SIZE(expected_files));

	sprintf(name, "/gt%d/uc", gt_id);
	xe_validate_entries(fd, name, expected_files_uc, ARRAY_SIZE(expected_files_uc));
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
	char devnode[PATH_MAX];
	int fd = -1;
	int gt;
	int sysfs = -1;

	igt_subtest_group {
		igt_fixture {
			fd = drm_open_driver_master(DRIVER_INTEL);
			igt_require_gem(fd);
			debugfs = igt_debugfs_dir(fd);
			sysfs = igt_sysfs_open(fd);

			kmstest_set_vt_graphics_mode();
		}

		igt_describe("Read all entries from sysfs path.");
		igt_subtest("i915-sysfs-read-all-entries")
			read_and_discard_sysfs_entries(sysfs, 0);
		igt_describe("Read all entries from debugfs path.");
		igt_subtest("i915-debugfs-read-all-entries")
			read_and_discard_sysfs_entries(debugfs, 0);

		igt_describe("Read all debugfs entries with display on/off.");
		igt_subtest_group
			kms_tests(fd, debugfs, "i915");

		igt_fixture {
			close(sysfs);
			close(debugfs);
			drm_close_driver(fd);
		}
	}

	igt_subtest_group {
		igt_fixture {
			fd = drm_open_driver_master(DRIVER_XE);
			__igt_debugfs_dump(fd, "info", IGT_LOG_INFO);
			debugfs = igt_debugfs_dir(fd);
			sysfs = igt_sysfs_open(fd);

			kmstest_set_vt_graphics_mode();
		}

		igt_describe("Read all entries from sysfs path.");
		igt_subtest("xe-sysfs-read-all-entries")
			read_and_discard_sysfs_entries(sysfs, 0);
		igt_describe("Read all entries from debugfs path.");
		igt_subtest("xe-debugfs-read-all-entries")
			read_and_discard_sysfs_entries(debugfs, 0);

		igt_describe("Read all debugfs entries with display on/off.");
		igt_subtest_group
			kms_tests(fd, debugfs, "xe");

		igt_describe("Check if various debugfs devnodes exist and test reading them.");
		igt_subtest("xe-base") {
			xe_test_base(fd, xe_config(fd));
		}

		igt_describe("Check all gt debugfs devnodes");
		igt_subtest("xe-gt") {
			xe_for_each_gt(fd, gt) {
				snprintf(devnode, sizeof(devnode), "gt%d", gt);
				igt_require(igt_debugfs_exists(fd, devnode, O_RDONLY));
				xe_test_gt(fd, gt);
			}
		}

		igt_describe("Check forcewake debugfs devnode");
		igt_subtest("xe-forcewake") {
			xe_test_forcewake(fd);
		}

		igt_fixture {
			close(sysfs);
			close(debugfs);
			drm_close_driver(fd);
		}
	}
}
