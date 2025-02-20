/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#include <fcntl.h>

#include "igt.h"

#include "intel_fbc.h"

#define FBC_STATUS_BUF_LEN 128

/**
 * intel_fbc_supported_on_chipset:
 * @device: fd of the device
 * @pipe: Display pipe
 *
 * Check if FBC is supported by chipset on given pipe.
 *
 * Returns:
 * true if FBC is supported and false otherwise.
 */
bool intel_fbc_supported_on_chipset(int device, enum pipe pipe)
{
	char buf[FBC_STATUS_BUF_LEN];
	int dir;

	dir = igt_debugfs_pipe_dir(device, pipe, O_DIRECTORY);
	igt_require_fd(dir);
	igt_debugfs_simple_read(dir, "i915_fbc_status", buf, sizeof(buf));
	close(dir);
	if (*buf == '\0')
		return false;

	return !strstr(buf, "FBC unsupported on this chipset\n") &&
		!strstr(buf, "stolen memory not initialised\n");
}

static bool _intel_fbc_is_enabled(int device, enum pipe pipe, int log_level, char *last_fbc_buf)
{
	char buf[FBC_STATUS_BUF_LEN];
	bool print = true;
	int dir;

	dir = igt_debugfs_pipe_dir(device, pipe, O_DIRECTORY);
	igt_require_fd(dir);
	igt_debugfs_simple_read(dir, "i915_fbc_status", buf, sizeof(buf));
	close(dir);
	if (log_level != IGT_LOG_DEBUG)
		last_fbc_buf[0] = '\0';
	else if (strcmp(last_fbc_buf, buf))
		strcpy(last_fbc_buf, buf);
	else
		print = false;

	if (print)
		igt_log(IGT_LOG_DOMAIN, log_level, "fbc_is_enabled():\n%s\n", buf);

	return strstr(buf, "FBC enabled\n");
}

/**
 * intel_fbc_is_enabled:
 * @device: fd of the device
 * @pipe: Display pipe
 * @log_level: Wanted loglevel
 *
 * Check if FBC is enabled on given pipe. Loglevel can be used to
 * control at which loglevel current state is printed out.
 *
 * Returns:
 * true if FBC is enabled.
 */
bool intel_fbc_is_enabled(int device, enum pipe pipe, int log_level)
{
	char last_fbc_buf[FBC_STATUS_BUF_LEN] = {'\0'};

	return _intel_fbc_is_enabled(device, pipe, log_level, last_fbc_buf);
}

/**
 * intel_fbc_wait_until_enabled:
 * @device: fd of the device
 * @pipe: Display pipe
 *
 * Wait until fbc is enabled. Used timeout is constant 2 seconds.
 *
 * Returns:
 * true if FBC got enabled.
 */
bool intel_fbc_wait_until_enabled(int device, enum pipe pipe)
{
	char last_fbc_buf[FBC_STATUS_BUF_LEN] = {'\0'};
	bool enabled = igt_wait(_intel_fbc_is_enabled(device, pipe, IGT_LOG_DEBUG, last_fbc_buf), 2000, 1);

	if (!enabled)
		igt_info("FBC is not enabled: \n%s\n", last_fbc_buf);

	return enabled;
}

/**
 * intel_fbc_max_plane_size
 *
 * @fd: fd of the device
 * @width: To get the max supported width
 * @height: To get the max supported height
 *
 * Function to update maximum plane size supported by FBC per platform
 *
 * Returns:
 * None
 */
void intel_fbc_max_plane_size(int fd, uint32_t *width, uint32_t *height)
{
	const uint32_t dev_id = intel_get_drm_devid(fd);
	const struct intel_device_info *info = intel_get_device_info(dev_id);
	int ver = info->graphics_ver;

	if (ver >= 10) {
		*width = 5120;
		*height = 4096;
	} else if (ver >= 8 || IS_HASWELL(fd)) {
		*width = 4096;
		*height = 4096;
	} else if (IS_G4X(fd) || ver >= 5) {
		*width = 4096;
		*height = 2048;
	} else {
		*width = 2048;
		*height = 1536;
	}
}


/**
 * intel_fbc_plane_size_supported
 *
 * @fd: fd of the device
 * @width: width of the plane to be checked
 * @height: height of the plane to be checked
 *
 * Checks if the plane size is supported for FBC
 *
 * Returns:
 * true if plane size is within the range as per the FBC supported size restrictions per platform
 */
bool intel_fbc_plane_size_supported(int fd, uint32_t width, uint32_t height)
{
	unsigned int max_w, max_h;

	intel_fbc_max_plane_size(fd, &max_w, &max_h);

	return width <= max_w && height <= max_h;
}

/**
 * intel_fbc_psr_combo_supported
 *
 * @fd: fd of the device
 *
 * FBC PSR combination support depends on the display version.
 *
 * Returns:
 * true if FBC and PSR can be enabled together in a platform
 */
bool intel_fbc_psr_combo_supported(int device)
{
	int ver = intel_display_ver(intel_get_drm_devid(device));

	/* In Xe3 FBC PSR combo not supported because of FBC dirty rect */
	if (ver >= 20 && ver < 30)
		return true;

	return false;
}
