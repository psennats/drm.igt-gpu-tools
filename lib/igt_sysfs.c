/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <inttypes.h>
#include <sys/stat.h>
#ifdef __linux__
#include <sys/sysmacros.h>
#endif
#include <sys/mount.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <i915_drm.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <xe_drm.h>

#include "drmtest.h"
#include "igt_core.h"
#include "igt_sysfs.h"
#include "igt_device.h"
#include "igt_fs.h"
#include "intel_chipset.h"
#include "xe/xe_query.h"

/**
 * SECTION:igt_sysfs
 * @short_description: Support code for sysfs features
 * @title: sysfs
 * @include: igt.h
 *
 * This library provides helpers to access sysfs features. Right now it only
 * provides basic support for like igt_sysfs_open().
 */

enum {
	GT,
	RPS,

	SYSFS_NUM_TYPES,
};

static const char *i915_attr_name[SYSFS_NUM_TYPES][SYSFS_NUM_ATTR] = {
	{
		"gt_act_freq_mhz",
		"gt_cur_freq_mhz",
		"gt_min_freq_mhz",
		"gt_max_freq_mhz",
		"gt_RP0_freq_mhz",
		"gt_RP1_freq_mhz",
		"gt_RPn_freq_mhz",
		"gt_idle_freq_mhz",
		"gt_boost_freq_mhz",
		"power/rc6_enable",
		"power/rc6_residency_ms",
		"power/rc6p_residency_ms",
		"power/rc6pp_residency_ms",
		"power/media_rc6_residency_ms",
	},
	{
		"rps_act_freq_mhz",
		"rps_cur_freq_mhz",
		"rps_min_freq_mhz",
		"rps_max_freq_mhz",
		"rps_RP0_freq_mhz",
		"rps_RP1_freq_mhz",
		"rps_RPn_freq_mhz",
		"rps_idle_freq_mhz",
		"rps_boost_freq_mhz",
		"rc6_enable",
		"rc6_residency_ms",
		"rc6p_residency_ms",
		"rc6pp_residency_ms",
		"media_rc6_residency_ms",
	},
};

/**
 * igt_sysfs_dir_id_to_name:
 * @dir: sysfs directory fd
 * @id: sysfs attribute id
 *
 * Returns attribute name corresponding to attribute id in either the
 * per-gt or legacy per-device sysfs
 *
 * Returns:
 * Attribute name in sysfs
 */
const char *igt_sysfs_dir_id_to_name(int dir, enum i915_attr_id id)
{
	igt_assert((uint32_t)id < SYSFS_NUM_ATTR);

	if (igt_sysfs_has_attr(dir, i915_attr_name[RPS][id]))
		return i915_attr_name[RPS][id];

	return i915_attr_name[GT][id];
}

/**
 * igt_sysfs_path_id_to_name:
 * @path: sysfs directory path
 * @id: sysfs attribute id
 *
 * Returns attribute name corresponding to attribute id in either the
 * per-gt or legacy per-device sysfs
 *
 * Returns:
 * Attribute name in sysfs
 */
const char *igt_sysfs_path_id_to_name(const char *path, enum i915_attr_id id)
{
	int dir;
	const char *name;

	dir = open(path, O_RDONLY);
	igt_assert(dir);

	name = igt_sysfs_dir_id_to_name(dir, id);
	close(dir);

	return name;
}

/**
 * igt_sysfs_has_attr:
 * @dir: sysfs directory fd
 * @attr: attr inside sysfs dir that needs to be checked for existence
 *
 * This checks if specified attr exists in device sysfs directory.
 *
 * Returns:
 * true if attr exists in sysfs, false otherwise.
 */
bool igt_sysfs_has_attr(int dir, const char *attr)
{
	return !faccessat(dir, attr, F_OK, 0);
}

/**
 * igt_sysfs_path:
 * @device: fd of the device
 * @path: buffer to fill with the sysfs path to the device
 * @pathlen: length of @path buffer
 *
 * This finds the sysfs directory corresponding to @device.
 *
 * Returns:
 * The directory path, or NULL on failure.
 */
char *igt_sysfs_path(int device, char *path, int pathlen)
{
	struct stat st;

	if (igt_debug_on(device < 0))
		return NULL;

	if (igt_debug_on(fstat(device, &st)) || igt_debug_on(!S_ISCHR(st.st_mode)))
		return NULL;

	snprintf(path, pathlen, "/sys/dev/char/%d:%d",
		 major(st.st_rdev), minor(st.st_rdev));

	if (igt_debug_on(access(path, F_OK)))
		return NULL;

	return path;
}

/**
 * igt_sysfs_open:
 * @device: fd of the device
 *
 * This opens the sysfs directory corresponding to device for use
 * with igt_sysfs_set() and igt_sysfs_get().
 *
 * Returns:
 * The directory fd, or -1 on failure.
 */
int igt_sysfs_open(int device)
{
	char path[80];

	if (igt_debug_on(!igt_sysfs_path(device, path, sizeof(path))))
		return -1;

	return open(path, O_RDONLY);
}

/**
 * xe_sysfs_gt_path:
 * @xe_device: fd of the device
 * @gt: gt number
 * @path: buffer to fill with the sysfs gt path to the device
 * @pathlen: length of @path buffer
 *
 * Returns:
 * The directory path, or NULL on failure.
 */
char *xe_sysfs_gt_path(int xe_device, int gt, char *path, int pathlen)
{
	struct stat st;

	if (xe_device < 0)
		return NULL;

	if (igt_debug_on(fstat(xe_device, &st)) || igt_debug_on(!S_ISCHR(st.st_mode)))
		return NULL;

	if (IS_PONTEVECCHIO(intel_get_drm_devid(xe_device)))
		snprintf(path, pathlen, "/sys/dev/char/%d:%d/device/tile%d/gt%d",
			 major(st.st_rdev), minor(st.st_rdev), gt, gt);
	else
		snprintf(path, pathlen, "/sys/dev/char/%d:%d/device/tile0/gt%d",
			 major(st.st_rdev), minor(st.st_rdev), gt);

	if (!access(path, F_OK))
		return path;

	return NULL;
}

/**
 * xe_sysfs_gt_open:
 * @xe_device: fd of the device
 * @gt: gt number
 *
 * This opens the sysfs gt directory corresponding to device and tile for use
 *
 * Returns:
 * The directory fd, or -1 on failure.
 */
int xe_sysfs_gt_open(int xe_device, int gt)
{
	char path[96];

	if (!xe_sysfs_gt_path(xe_device, gt, path, sizeof(path)))
		return -1;

	return open(path, O_RDONLY);
}

/**
 * xe_sysfs_gt_has_node:
 * @xe_device: fd of the device
 * @gt: gt number
 * @node: node inside sysfs gt dir that needs to be checked for existence
 *
 * This checks if specified node exists in device sysfs gt directory.
 *
 * Returns:
 * true if node exists in sysfs, false otherwise.
 */
bool xe_sysfs_gt_has_node(int xe_device, int gt, const char *node)
{
	bool has_node;
	int gt_fd;

	gt_fd = xe_sysfs_gt_open(xe_device, gt);
	if (gt_fd < 0)
		return false;

	has_node = igt_sysfs_has_attr(gt_fd, node);
	close(gt_fd);

	return has_node;
}

/**
 * xe_sysfs_engine_path:
 * @xe_device: fd of the device
 * @gt: gt number
 * @class: engine class
 * @path: buffer to fill with the sysfs gt path to the device
 * @pathlen: length of @path buffer
 *
 * Returns:
 * The directory path, or NULL on failure.
 */
char *
xe_sysfs_engine_path(int xe_device, int gt, int class, char *path, int pathlen)
{
	struct stat st;
	int tile = IS_PONTEVECCHIO(intel_get_drm_devid(xe_device)) ? gt : 0;

	if (xe_device < 0)
		return NULL;

	if (igt_debug_on(fstat(xe_device, &st)) || igt_debug_on(!S_ISCHR(st.st_mode)))
		return NULL;

	snprintf(path, pathlen, "/sys/dev/char/%d:%d/device/tile%d/gt%d/engines/%s",
		 major(st.st_rdev), minor(st.st_rdev), tile, gt,
		 xe_engine_class_short_string(class));

	if (!access(path, F_OK))
		return path;

	return NULL;
}

/**
 * xe_sysfs_engine_open:
 * @xe_device: fd of the device
 * @gt: gt number
 * @class: engine class
 *
 * This opens the sysfs gt directory corresponding to device and tile for use
 *
 * Returns:
 * The directory fd, or -1 on failure.
 */
int xe_sysfs_engine_open(int xe_device, int gt, int class)
{
	char path[96];

	if (!xe_sysfs_engine_path(xe_device, gt, class, path, sizeof(path)))
		return -1;

	return open(path, O_RDONLY);
}

/**
 * igt_sysfs_gt_path:
 * @device: fd of the device
 * @gt: gt number
 * @path: buffer to fill with the sysfs gt path to the device
 * @pathlen: length of @path buffer
 *
 * This finds the sysfs directory corresponding to @device and @gt. If the gt
 * specific directory is not available and gt is 0, path is filled with sysfs
 * base directory.
 *
 * Returns:
 * The directory path, or NULL on failure.
 */
char *igt_sysfs_gt_path(int device, int gt, char *path, int pathlen)
{
	struct stat st;

	if (device < 0)
		return NULL;

	if (igt_debug_on(fstat(device, &st)) || igt_debug_on(!S_ISCHR(st.st_mode)))
		return NULL;

	snprintf(path, pathlen, "/sys/dev/char/%d:%d/gt/gt%d",
		 major(st.st_rdev), minor(st.st_rdev), gt);

	if (!access(path, F_OK))
		return path;
	if (!gt)
		return igt_sysfs_path(device, path, pathlen);
	return NULL;
}

/**
 * igt_sysfs_gt_open:
 * @device: fd of the device
 * @gt: gt number
 *
 * This opens the sysfs gt directory corresponding to device and gt for use
 * with igt_sysfs_set() and igt_sysfs_get().
 *
 * Returns:
 * The directory fd, or -1 on failure.
 */
int igt_sysfs_gt_open(int device, int gt)
{
	char path[96];

	if (!igt_sysfs_gt_path(device, gt, path, sizeof(path)))
		return -1;

	return open(path, O_RDONLY);
}

/**
 * igt_sysfs_get_num_gt:
 * @device: fd of the device
 *
 * Reads number of GT sysfs entries.
 * Asserts for atleast one GT entry.
 * (see igt_sysfs_gt_path).
 *
 * Returns: Number of GTs.
 */
int igt_sysfs_get_num_gt(int device)
{
	int num_gts = 0;
	char path[96];

	while (igt_sysfs_gt_path(device, num_gts, path, sizeof(path)))
		++num_gts;

	igt_assert_f(num_gts > 0, "No GT sysfs entry is found.");

	return num_gts;
}

/**
 * igt_sysfs_drm_module_params_open:
 *
 * This opens the sysfs directory corresponding to drm module
 * parameters.
 *
 * Returns:
 * The directory fd, or -1 on failure.
 */
int igt_sysfs_drm_module_params_open(void)
{
	char path[] = "/sys/module/drm/parameters";

	if (access(path, F_OK))
		return -1;

	return open(path, O_RDONLY);
}

static int log_level = -1;

/**
 * igt_drm_debug_level_get:
 *
 * This reads the current debug log level of the machine on
 * which the test is currently executing.
 *
 * Returns:
 * The current log level, or -1 on error.
 */
int igt_drm_debug_level_get(int dir)
{
	char buf[20];

	if (log_level >= 0)
		return log_level;

	if (igt_sysfs_read(dir, "debug", buf, sizeof(buf) - 1) < 0)
		return -1;

	return atoi(buf);
}

/**
 * igt_drm_debug_level_reset:
 *
 * This modifies the current debug log level of the machine
 * to the default value post-test.
 *
 */
void igt_drm_debug_level_reset(void)
{
	char buf[20];
	int dir;

	if (log_level < 0)
		return;

	dir = igt_sysfs_drm_module_params_open();
	if (dir < 0)
		return;

	igt_debug("Resetting DRM debug level to %d\n", log_level);
	snprintf(buf, sizeof(buf), "%d", log_level);
	igt_assert(igt_sysfs_set(dir, "debug", buf));

	close(dir);
}

static void igt_drm_debug_level_reset_exit_handler(int sig)
{
	igt_drm_debug_level_reset();
}

/**
 * igt_drm_debug_level_update:
 * @debug_level: new debug level to set
 *
 * This modifies the current drm debug log level to the new value.
 */
void igt_drm_debug_level_update(unsigned int new_log_level)
{
	char buf[20];
	int dir;

	dir = igt_sysfs_drm_module_params_open();
	if (dir < 0)
		return;

	log_level = igt_drm_debug_level_get(dir);
	if (log_level < 0) {
		close(dir);
		return;
	}

	igt_debug("Setting DRM debug level to %d\n", new_log_level);
	snprintf(buf, sizeof(buf), "%d", new_log_level);
	igt_assert(igt_sysfs_set(dir, "debug", buf));

	close(dir);

	/*
	 * TODO: Check whether multiple exit handlers will get installed,
	 * if we call this api multiple times
	 */
	igt_install_exit_handler(igt_drm_debug_level_reset_exit_handler);
}

/**
 * igt_sysfs_write:
 * @dir: sysfs directory
 * @attr: name of the sysfs node to open
 * @data: the block to write from
 * @len: the length to write
 *
 * This writes @len bytes from @data to the sysfs file.  Contrary to
 * igt_sysfs_set(), this does not automatically write a null char if len is 0.
 * It's caller responsibility to pass the right len according to the data being
 * written.
 *
 * Returns:
 * The number of bytes written, or -errno on error.
 */
int igt_sysfs_write(int dir, const char *attr, const void *data, int len)
{
	int fd;

	fd = openat(dir, attr, O_WRONLY);
	if (igt_debug_on(fd < 0))
		return -errno;

	len = igt_writen(fd, data, len);
	close(fd);

	return len;
}

/**
 * igt_sysfs_read:
 * @dir: sysfs directory
 * @attr: name of the sysfs node to open
 * @data: the block to read into
 * @len: the maximum length to read
 *
 * This reads @len bytes from the sysfs file to @data
 *
 * Returns:
 * The length read, -errno on failure.
 */
int igt_sysfs_read(int dir, const char *attr, void *data, int len)
{
	int fd;

	fd = openat(dir, attr, O_RDONLY);
	if (igt_debug_on(fd < 0))
		return -errno;

	len = igt_readn(fd, data, len);
	close(fd);

	return len;
}

/**
 * igt_sysfs_set:
 * @dir: sysfs directory
 * @attr: name of the sysfs node to open
 * @value: the string to write
 *
 * This writes the value to the sysfs file.
 *
 * Returns:
 * True on success, false on failure.
 */
bool igt_sysfs_set(int dir, const char *attr, const char *value)
{
	int len = strlen(value);

	/*
	 * Always write at least 1 char, the null byte, otherwise it
	 * won't write anything on sysfs.
	 */
	if (!len)
		len = 1;

	return igt_sysfs_write(dir, attr, value, len) == len;
}

/**
 * igt_sysfs_get:
 * @dir: sysfs directory
 * @attr: name of the sysfs node to open
 *
 * This reads the value from the sysfs file.
 *
 * Returns:
 * A nul-terminated string, must be freed by caller after use, or NULL
 * on failure.
 */
char *igt_sysfs_get(int dir, const char *attr)
{
	char *buf;
	size_t len, offset, rem;
	ssize_t ret;
	int fd;

	fd = openat(dir, attr, O_RDONLY);
	if (igt_debug_on(fd < 0))
		return NULL;

	offset = 0;
	len = 64;
	rem = len - offset - 1;
	buf = malloc(len);
	if (igt_debug_on(!buf))
		goto out;

	while ((ret = igt_readn(fd, buf + offset, rem)) == rem) {
		char *newbuf;

		newbuf = realloc(buf, 2*len);
		if (igt_debug_on(!newbuf))
			break;

		buf = newbuf;
		len *= 2;
		offset += ret;
		rem = len - offset - 1;
	}

	if (ret > 0)
		offset += ret;
	buf[offset] = '\0';
	while (offset > 0 && buf[offset-1] == '\n')
		buf[--offset] = '\0';

out:
	close(fd);
	return buf;
}

/**
 * igt_sysfs_scanf:
 * @dir: directory for the device from igt_sysfs_open()
 * @attr: name of the sysfs node to open
 * @fmt: scanf format string
 * @...: Additional paramaters to store the scaned input values
 *
 * scanf() wrapper for sysfs.
 * 
 * Returns:
 * Number of values successfully scanned (which can be 0), EOF on errors or
 * premature end of file.
 */
int igt_sysfs_scanf(int dir, const char *attr, const char *fmt, ...)
{
	FILE *file;
	int fd;
	int ret = -1;

	fd = openat(dir, attr, O_RDONLY);
	if (igt_debug_on(fd < 0))
		return -1;

	file = fdopen(fd, "r");
	if (!igt_debug_on(!file)) {
		va_list ap;

		va_start(ap, fmt);
		ret = vfscanf(file, fmt, ap);
		va_end(ap);

		fclose(file);
	} else {
		close(fd);
	}

	return ret;
}

int igt_sysfs_vprintf(int dir, const char *attr, const char *fmt, va_list ap)
{
	char stack[128], *buf = stack;
	va_list tmp;
	int ret, fd, len;

	fd = openat(dir, attr, O_WRONLY);
	if (igt_debug_on(fd < 0))
		return -errno;

	va_copy(tmp, ap);
	ret = vsnprintf(stack, sizeof(stack), fmt, tmp);
	va_end(tmp);
	if (igt_debug_on(ret < 0)) {
		ret = -EINVAL;
		goto end;
	}

	len = ret;

	if (!ret) {
		/*
		 * Make sure to always issue a write() syscall, even if writing
		 * an empty string, otherwise values in sysfs like module
		 * parameters don't really get overwritten.  vsnprintf()
		 * guarantees to return a \0 terminated string, so just add
		 * that char. The return code is still the same as before, to
		 * abstract that from caller.
		 */
		ret = 1;
	} else if (ret > sizeof(stack)) {
		buf = malloc(len + 1);
		if (igt_debug_on(!buf)) {
			ret = -ENOMEM;
			goto end;
		}

		ret = vsnprintf(buf, len + 1, fmt, ap);
		if (igt_debug_on(ret != len)) {
			ret = -EINVAL;
			goto free_buf;
		}
	}

	ret = igt_writen(fd, buf, ret);

	/* Caller shouldn't know about special sysfs handling, just return 0 */
	if (!len && ret == 1)
		ret = 0;

free_buf:
	if (buf != stack)
		free(buf);

end:
	close(fd);

	return ret;
}

/**
 * igt_sysfs_printf:
 * @dir: directory for the device from igt_sysfs_open()
 * @attr: name of the sysfs node to open
 * @fmt: printf format string
 * @...: Additional paramaters to store the scaned input values
 *
 * printf() wrapper for sysfs.
 *
 * Returns:
 * Number of characters written, negative value on error.
 */
int igt_sysfs_printf(int dir, const char *attr, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = igt_sysfs_vprintf(dir, attr, fmt, ap);
	va_end(ap);

	return ret;
}

/**
 * __igt_sysfs_get_u32:
 * @dir: directory corresponding to attribute
 * @attr: name of the sysfs node to read
 * @value: pointer for storing read value
 *
 * Convenience wrapper to read a unsigned 32bit integer from a sysfs file.
 *
 * Returns:
 * True if value successfully read, false otherwise.
 */
bool __igt_sysfs_get_u32(int dir, const char *attr, uint32_t *value)
{
	if (igt_debug_on(igt_sysfs_scanf(dir, attr, "%u", value) != 1))
		return false;

	return true;
}

/**
 * igt_sysfs_get_u32:
 * @dir: directory corresponding to attribute
 * @attr: name of the sysfs node to read
 *
 * Convenience wrapper to read a unsigned 32bit integer from a sysfs file.
 * It asserts on failure.
 *
 * Returns:
 * Read value.
 */
uint32_t igt_sysfs_get_u32(int dir, const char *attr)
{
	uint32_t value;

	igt_assert_f(__igt_sysfs_get_u32(dir, attr, &value),
		     "Failed to read %s attribute (%s)\n", attr, strerror(errno));

	return value;
}

/**
 * __igt_sysfs_set_u32:
 * @dir: directory corresponding to attribute
 * @attr: name of the sysfs node to write
 * @value: value to set
 *
 * Convenience wrapper to write a unsigned 32bit integer to a sysfs file.
 *
 * Returns:
 * True if successfully written, false otherwise.
 */
bool __igt_sysfs_set_u32(int dir, const char *attr, uint32_t value)
{
	return igt_sysfs_printf(dir, attr, "%u", value) > 0;
}

/**
 * igt_sysfs_set_u32:
 * @dir: directory corresponding to attribute
 * @attr: name of the sysfs node to write
 * @value: value to set
 *
 * Convenience wrapper to write a unsigned 32bit integer to a sysfs file.
 * It asserts on failure.
 */
void igt_sysfs_set_u32(int dir, const char *attr, uint32_t value)
{
	igt_assert_f(__igt_sysfs_set_u32(dir, attr, value),
		     "Failed to write %u to %s attribute (%s)\n", value, attr, strerror(errno));
}

/**
 * __igt_sysfs_get_s32:
 * @dir: directory corresponding to attribute
 * @attr: name of the sysfs node to read
 * @value: pointer for storing read value
 *
 * Convenience wrapper to read a signed 32bit integer from a sysfs file.
 *
 * Returns:
 * True if value successfully read, false otherwise.
 */
bool __igt_sysfs_get_s32(int dir, const char *attr, int32_t *value)
{
	if (igt_debug_on(igt_sysfs_scanf(dir, attr, "%d", value) != 1))
		return false;

	return true;
}

/**
 * igt_sysfs_get_s32:
 * @dir: directory corresponding to attribute
 * @attr: name of the sysfs node to read
 *
 * Convenience wrapper to read a signed 32bit integer from a sysfs file.
 * It asserts on failure.
 *
 * Returns:
 * Read value.
 */
int32_t igt_sysfs_get_s32(int dir, const char *attr)
{
	int32_t value;

	igt_assert_f(__igt_sysfs_get_s32(dir, attr, &value),
		     "Failed to read %s attribute (%s)\n", attr, strerror(errno));

	return value;
}

/**
 * __igt_sysfs_set_s32:
 * @dir: directory corresponding to attribute
 * @attr: name of the sysfs node to write
 * @value: value to set
 *
 * Convenience wrapper to write a signed 32bit integer to a sysfs file.
 *
 * Returns:
 * True if successfully written, false otherwise.
 */
bool __igt_sysfs_set_s32(int dir, const char *attr, int32_t value)
{
	return igt_sysfs_printf(dir, attr, "%d", value) > 0;
}

/**
 * igt_sysfs_set_s32:
 * @dir: directory corresponding to attribute
 * @attr: name of the sysfs node to write
 * @value: value to set
 *
 * Convenience wrapper to write a signed 32bit integer to a sysfs file.
 * It asserts on failure.
 */
void igt_sysfs_set_s32(int dir, const char *attr, int32_t value)
{
	igt_assert_f(__igt_sysfs_set_s32(dir, attr, value),
		     "Failed to write %d to %s attribute (%s)\n", value, attr, strerror(errno));
}

/**
 * __igt_sysfs_get_u64:
 * @dir: directory corresponding to attribute
 * @attr: name of the sysfs node to read
 * @value: pointer for storing read value
 *
 * Convenience wrapper to read a unsigned 64bit integer from a sysfs file.
 *
 * Returns:
 * True if value successfully read, false otherwise.
 */
bool __igt_sysfs_get_u64(int dir, const char *attr, uint64_t *value)
{
	if (igt_debug_on(igt_sysfs_scanf(dir, attr, "%"PRIu64, value) != 1))
		return false;

	return true;
}

/**
 * igt_sysfs_get_u64:
 * @dir: directory corresponding to attribute
 * @attr: name of the sysfs node to read
 *
 * Convenience wrapper to read a unsigned 64bit integer from a sysfs file.
 * It asserts on failure.
 *
 * Returns:
 * Read value.
 */
uint64_t igt_sysfs_get_u64(int dir, const char *attr)
{
	uint64_t value;

	igt_assert_f(__igt_sysfs_get_u64(dir, attr, &value),
		     "Failed to read %s attribute (%s)\n", attr, strerror(errno));

	return value;
}

/**
 * __igt_sysfs_set_u64:
 * @dir: directory corresponding to attribute
 * @attr: name of the sysfs node to write
 * @value: value to set
 *
 * Convenience wrapper to write a unsigned 64bit integer to a sysfs file.
 *
 * Returns:
 * True if successfully written, false otherwise.
 */
bool __igt_sysfs_set_u64(int dir, const char *attr, uint64_t value)
{
	return igt_sysfs_printf(dir, attr, "%"PRIu64, value) > 0;
}

/**
 * igt_sysfs_set_u64:
 * @dir: directory corresponding to attribute
 * @attr: name of the sysfs node to write
 * @value: value to set
 *
 * Convenience wrapper to write a unsigned 64bit integer to a sysfs file.
 * It asserts on failure.
 */
void igt_sysfs_set_u64(int dir, const char *attr, uint64_t value)
{
	igt_assert_f(__igt_sysfs_set_u64(dir, attr, value),
		     "Failed to write  %"PRIu64" to %s attribute (%s)\n",
		     value, attr, strerror(errno));
}

/**
 * __igt_sysfs_get_boolean:
 * @dir: directory corresponding to attribute
 * @attr: name of the sysfs node to read
 * @value: pointer for storing read value
 *
 * Convenience wrapper to read a boolean sysfs file.
 *
 * Returns:
 * True if value successfully read, false otherwise.
 */
bool __igt_sysfs_get_boolean(int dir, const char *attr, bool *value)
{
	char *buf;
	int ret, read_value;

	buf = igt_sysfs_get(dir, attr);
	if (igt_debug_on_f(!buf, "Failed to read %s attribute (%s)\n", attr, strerror(errno)))
		return false;

	ret = sscanf(buf, "%d", &read_value);
	if (((ret == 1) && (read_value == 1)) || ((ret == 0) && !strcasecmp(buf, "Y"))) {
		*value = true;
	} else if (((ret == 1) && (read_value == 0)) || ((ret == 0) && !strcasecmp(buf, "N"))) {
		*value = false;
	} else {
		igt_debug("Value read from %s attribute (%s) is not as expected (0|1|N|Y|n|y)\n",
			  attr, buf);
		free(buf);
		return false;
	}

	free(buf);
	return true;
}

/**
 * igt_sysfs_get_boolean:
 * @dir: directory corresponding to attribute
 * @attr: name of the sysfs node to read
 *
 * Convenience wrapper to read a boolean sysfs file.
 * It asserts on failure.
 *
 * Returns:
 * Read value.
 */
bool igt_sysfs_get_boolean(int dir, const char *attr)
{
	bool value;

	igt_assert(__igt_sysfs_get_boolean(dir, attr, &value));

	return value;
}

/**
 * __igt_sysfs_set_boolean:
 * @dir: directory corresponding to attribute
 * @attr: name of the sysfs node to write
 * @value: value to set
 *
 * Convenience wrapper to write a boolean sysfs file.
 *
 * Returns:
 * True if successfully written, false otherwise.
 */
bool __igt_sysfs_set_boolean(int dir, const char *attr, bool value)
{
	return igt_sysfs_printf(dir, attr, "%d", value) == 1;
}

/**
 * igt_sysfs_set_boolean:
 * @dir: directory corresponding to attribute
 * @attr: name of the sysfs node to write
 * @value: value to set
 *
 * Convenience wrapper to write a boolean sysfs file.
 * It asserts on failure.
 */
void igt_sysfs_set_boolean(int dir, const char *attr, bool value)
{
	igt_assert_f(__igt_sysfs_set_boolean(dir, attr, value),
		     "Failed to write %u to %s attribute (%s)\n", value, attr, strerror(errno));
}

static void bind_con(const char *name, bool enable)
{
	const char *path = "/sys/class/vtconsole";
	DIR *dir;
	struct dirent *de;

	dir = opendir(path);
	if (!dir)
		return;

	while ((de = readdir(dir))) {
		char buf[PATH_MAX];
		int fd, len;

		if (strncmp(de->d_name, "vtcon", 5))
			continue;

		sprintf(buf, "%s/%s/name", path, de->d_name);
		fd = open(buf, O_RDONLY);
		if (fd < 0)
			continue;

		buf[sizeof(buf) - 1] = '\0';
		len = read(fd, buf, sizeof(buf) - 1);
		close(fd);
		if (len >= 0)
			buf[len] = '\0';

		if (!strstr(buf, name))
			continue;

		sprintf(buf, "%s/%s/bind", path, de->d_name);
		fd = open(buf, O_WRONLY);
		if (fd != -1) {
			igt_ignore_warn(write(fd, enable ? "1\n" : "0\n", 2));
			close(fd);
		}
		break;
	}
	closedir(dir);
}

/**
 * bind_fbcon:
 * @enable: boolean value
 *
 * This functions enables/disables the text console running on top of the
 * framebuffer device.
 */
void bind_fbcon(bool enable)
{
	/*
	 * The vtcon bind interface seems somewhat broken. Possibly
	 * depending on the order the console drivers have been
	 * registered you either have to unbind the old driver,
	 * or bind the new driver. Let's do both.
	 */
	bind_con("dummy device", !enable);
	bind_con("frame buffer device", enable);
}

static int fbcon_cursor_blink_fd = -1;
static char fbcon_cursor_blink_prev_value[2];

static void fbcon_cursor_blink_restore(int sig)
{
	write(fbcon_cursor_blink_fd, fbcon_cursor_blink_prev_value,
	      strlen(fbcon_cursor_blink_prev_value) + 1);
	close(fbcon_cursor_blink_fd);
}

/**
 * fbcon_blink_enable:
 * @enable: if true enables the fbcon cursor blinking otherwise disables it
 *
 * Enables or disables the cursor blinking in fbcon, it also restores the
 * previous blinking state when exiting test.
 *
 */
void fbcon_blink_enable(bool enable)
{
	const char *cur_blink_path = "/sys/class/graphics/fbcon/cursor_blink";
	int fd, r;
	char buffer[2];

	fd = open(cur_blink_path, O_RDWR);
	igt_require(fd >= 0);

	/* Restore original value on exit */
	if (fbcon_cursor_blink_fd == -1) {
		r = read(fd, fbcon_cursor_blink_prev_value,
			 sizeof(fbcon_cursor_blink_prev_value));
		if (r > 0) {
			fbcon_cursor_blink_fd = dup(fd);
			igt_assert(fbcon_cursor_blink_fd >= 0);
			igt_install_exit_handler(fbcon_cursor_blink_restore);
		}
	}

	r = snprintf(buffer, sizeof(buffer), enable ? "1" : "0");
	write(fd, buffer, r + 1);
	close(fd);
}

static bool rw_attr_equal_within_epsilon(uint64_t x, uint64_t ref, double tol)
{
	return (x <= (1.0 + tol) * ref) && (x >= (1.0 - tol) * ref);
}

/* Sweep the range of values for an attribute to identify matching reads/writes */
static int rw_attr_sweep(igt_sysfs_rw_attr_t *rw)
{
	uint64_t get = 0, set = rw->start;
	int num_points = 0;
	bool ret;

	igt_debug("'%s': sweeping range of values\n", rw->attr);
	while (set < UINT64_MAX / 2) {
		ret = __igt_sysfs_set_u64(rw->dir, rw->attr, set);
		__igt_sysfs_get_u64(rw->dir, rw->attr, &get);
		igt_debug("'%s': ret %d set %"PRIu64" get %"PRIu64"\n", rw->attr, ret, set, get);
		if (ret && rw_attr_equal_within_epsilon(get, set, rw->tol)) {
			igt_debug("'%s': matches\n", rw->attr);
			num_points++;
		}
		set *= 2;
	}
	igt_debug("'%s': done sweeping\n", rw->attr);

	return num_points ? 0 : -ENOENT;
}

/**
 * igt_sysfs_rw_attr_verify:
 * @rw: 'struct igt_sysfs_rw_attr' describing a rw sysfs attr
 *
 * This function attempts to verify writable sysfs attributes, that is the
 * attribute is first written to and then read back and it is verified that
 * the read value matches the written value to a tolerance. However, when
 * we try to do this we run into the issue that a sysfs attribute might
 * have a behavior where the read value is different from the written value
 * for any reason. For example, attributes such as power, voltage,
 * frequency and time typically have a linear region outside which they are
 * clamped (the values saturate). Therefore for such attributes read values
 * match the written value only in the linear region and when writing we
 * don't know if we are writing to the linear or to the clamped region.
 *
 * Therefore the verification implemented here takes the approach of
 * sweeping across the range of possible values of the attribute (this is
 * done using 'doubling' rather than linearly) and seeing where there are
 * matches. There should be at least one match (to a tolerance) for the
 * verification to have succeeded.
 */
void igt_sysfs_rw_attr_verify(igt_sysfs_rw_attr_t *rw)
{
	uint64_t prev = 0, get = 0;
	struct stat st;
	int ret;

	igt_assert(!fstatat(rw->dir, rw->attr, &st, 0));
	igt_assert(st.st_mode & 0222); /* writable */
	igt_assert(rw->start);	/* cannot be 0 */

	__igt_sysfs_get_u64(rw->dir, rw->attr, &prev);
	igt_debug("'%s': prev %"PRIu64"\n", rw->attr, prev);

	ret = rw_attr_sweep(rw);

	/*
	 * Restore previous value: we don't assert before this point so
	 * that we can restore the attr before asserting
	 */
	igt_sysfs_set_u64(rw->dir, rw->attr, prev);
	__igt_sysfs_get_u64(rw->dir, rw->attr, &get);
	igt_assert_eq(get, prev);
	igt_assert(!ret);
}

/**
 * xe_get_engine_class:
 * @name: de_d_name that we get from igt_sysfs_engine.
 *
 * It returns engine class corresponding to the engine dir from igt_sysfs_engines.
 *
 */
static uint16_t xe_get_engine_class(char *name)
{
	uint16_t class;

	if (strcmp(name, "rcs") == 0) {
		class = DRM_XE_ENGINE_CLASS_RENDER;
	} else if (strcmp(name, "bcs") == 0) {
		class = DRM_XE_ENGINE_CLASS_COPY;
	} else if (strcmp(name, "vcs") == 0) {
		class = DRM_XE_ENGINE_CLASS_VIDEO_DECODE;
	} else if (strcmp(name, "vecs") == 0) {
		class = DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE;
	} else if (strcmp(name, "ccs") == 0) {
		class = DRM_XE_ENGINE_CLASS_COMPUTE;
	}

	return class;
}

/**
 * igt_sysfs_get_engine_list:
 * @engines: fd of the directory engine
 *
 * Iterates over sysfs/engines and returns an array of
 * opened engines.  The user will be in charge of closing
 * the opened engines.
 *
 * The returned array will always be terminated by a -1.
 */
int *igt_sysfs_get_engine_list(int engines)
{
	struct dirent *de;
	DIR *dir;
	const int array_max = 16;
	int *ret = calloc(array_max, sizeof(int));
	int size = 0;

	igt_assert(ret);

	lseek(engines, 0, SEEK_SET);

	dir = fdopendir(engines);
	if (!dir)
		close(engines);

	while ((de = readdir(dir))) {
		if (*de->d_name == '.')
			continue;
		igt_assert_lt(size, array_max);
		ret[size] = openat(engines, de->d_name, O_RDONLY);
		if (ret[size] < 0) {
			ret[size] = 0;
			continue;
		}
		size += 1;
	}

	igt_assert_lt(size, array_max);
	ret[size] = -1;

	return ret;
}

/**
 * igt_sysfs_free_engine_list:
 * @list: list of opened engines
 * @size: number of engines in list
 *
 * Helper for cleaning up after igt_sysfs_get_engine_list.
 * Closes all engines in list before freeing the list.
 */
void igt_sysfs_free_engine_list(int *list)
{
	int i = 0;

	while (list[i] != -1)
		close(list[i++]);
	free(list);
}

/**
 * igt_sysfs_engines:
 * @xe: fd of the device
 * @engines: fd of the directory engine
 * @property: property array
 * @test: Dynamic engine test
 *
 * It iterates over sysfs/engines and runs a dynamic engine test.
 *
 */
void igt_sysfs_engines(int xe, int engines, int gt, bool all, const char **property,
		       void (*test)(int, int, const char **, uint16_t, int))
{
	struct dirent *de;
	DIR *dir;
	uint16_t class;

	lseek(engines, 0, SEEK_SET);

	dir = fdopendir(engines);
	if (!dir)
		close(engines);

	while ((de = readdir(dir))) {
		int engine_fd;

		if (*de->d_name == '.')
			continue;

		engine_fd = openat(engines, de->d_name, O_RDONLY);
		if (engine_fd < 0)
			continue;

		igt_dynamic(de->d_name) {
			if (property) {
				struct stat st;

				igt_require(fstatat(engine_fd, property[0], &st, 0) == 0);
				igt_require(fstatat(engine_fd, property[1], &st, 0) == 0);
				igt_require(fstatat(engine_fd, property[2], &st, 0) == 0);
			}
			errno = 0;

			if (all) {
				class = xe_get_engine_class(de->d_name);
				test(xe, engine_fd, property, class, gt);
			} else {
				test(xe, engine_fd, property, 0, 0);
			}
		}
		close(engine_fd);
	}
}

/**
 * xe_sysfs_tile_path:
 * @xe_device: fd of the device
 * @tile: tile number
 * @path: buffer to fill with the sysfs tile path to the device
 * @pathlen: length of @path buffer
 *
 * Returns:
 * The directory path, or NULL on failure.
 */
char *xe_sysfs_tile_path(int xe_device, int tile, char *path, int pathlen)
{
	struct stat st;

	if (xe_device < 0)
		return NULL;

	if (igt_debug_on(fstat(xe_device, &st)) || igt_debug_on(!S_ISCHR(st.st_mode)))
		return NULL;

	snprintf(path, pathlen, "/sys/dev/char/%d:%d/device/tile%d",
		 major(st.st_rdev), minor(st.st_rdev), tile);

	if (!access(path, F_OK))
		return path;
	return NULL;
}

/**
 * xe_sysfs_tile_open:
 * @xe_device: fd of the device
 * @tile: tile number
 *
 * This opens the sysfs tile directory corresponding to device and tile for use
 *
 * Returns:
 * The directory fd, or -1 on failure.
 */
int xe_sysfs_tile_open(int xe_device, int tile)
{
	char path[96];

	if (!xe_sysfs_tile_path(xe_device, tile, path, sizeof(path)))
		return -1;

	return open(path, O_RDONLY);
}

/**
 * xe_sysfs_get_num_tiles:
 * @xe_device: fd of the device
 *
 * Reads number of tile sysfs entries.
 * Asserts for at least one tile entry.
 * (see xe_sysfs_tile_path).
 *
 * Returns: Number of tiles.
 */
int xe_sysfs_get_num_tiles(int xe_device)
{
	int num_tiles = 0;
	char path[96];

	while (xe_sysfs_tile_path(xe_device, num_tiles, path, sizeof(path)))
		++num_tiles;

	igt_assert_f(num_tiles > 0, "No GT sysfs entry is found.");

	return num_tiles;
}

/**
 * xe_sysfs_engine_class_get_property
 * @xe_device: fd of the device
 * @gt: gt number
 * @class: engine class
 * @property: property of engine class to retrieve
 * @value: pointer for storing read value
 *
 * Convenience wrapper to get value of given property for given engine class on given gt.
 *
 * Returns: true on success, false on failure.
 */
bool xe_sysfs_engine_class_get_property(int xe_device, int gt, uint16_t class, const char *property,
					uint32_t *value)
{
	int engines_fd;

	engines_fd = xe_sysfs_engine_open(xe_device, gt, class);

	if (engines_fd == -1) {
		igt_debug("Failed to open %s on gt%d.\n", xe_engine_class_short_string(class), gt);

		return false;
	}

	if (!__igt_sysfs_get_u32(engines_fd, property, value)) {
		igt_debug("Failed to read %s property of %s on gt%d.\n", property,
			  xe_engine_class_short_string(class), gt);
		close(engines_fd);

		return false;
	}

	close(engines_fd);

	return true;
}

/**
 * xe_sysfs_engine_class_set_property
 * @xe_device: fd of the device
 * @gt: gt number
 * @class: engine class
 * @property: property of engine class to be modified
 * @new_value: value to be set
 * @old_value: pointer for storing old value, can be NULL
 *
 * Convenience wrapper to set given property for given engine class on given gt to given value.
 *
 * Returns: true on success, false on failure.
 */
bool xe_sysfs_engine_class_set_property(int xe_device, int gt, uint16_t class, const char *property,
					uint32_t new_value, uint32_t *old_value)
{
	int engines_fd;

	engines_fd = xe_sysfs_engine_open(xe_device, gt, class);

	if (engines_fd == -1) {
		igt_debug("Failed to open %s on gt%d.\n", xe_engine_class_short_string(class), gt);

		return false;
	}

	if (old_value && !__igt_sysfs_get_u32(engines_fd, property, old_value)) {
		igt_debug("Failed to read %s property of %s on gt%d.\n", property,
			  xe_engine_class_short_string(class), gt);
		close(engines_fd);

		return false;
	}

	if (!__igt_sysfs_set_u32(engines_fd, property, new_value)) {
		igt_debug("Failed to write %s property of %s on gt%d.\n", property,
			  xe_engine_class_short_string(class), gt);
		close(engines_fd);

		return false;
	}

	close(engines_fd);

	return true;
}
