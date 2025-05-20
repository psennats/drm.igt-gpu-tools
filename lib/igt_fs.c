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
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "igt_fs.h"

/**
 * SECTION:igt_fs
 * @short_description: Helpers for file operations
 * @title: fs
 * @include: igt_fs.h
 *
 * This library provides helpers for file operations
 */

/**
 * igt_readn:
 * @fd: the file descriptor
 * @buf: buffer where the contents will be stored, allocated by the caller
 * @size: size of the buffer
 *
 * Read from fd into provided buffer until the buffer is full or EOF
 *
 * Returns:
 * -errno on failure or bytes read on success
 */
ssize_t igt_readn(int fd, char *buf, size_t len)
{
	ssize_t ret;
	size_t total = 0;

	do {
		ret = read(fd, buf + total, len - total);
		if (ret < 0)
			ret = -errno;
		if (ret == -EINTR || ret == -EAGAIN)
			continue;
		if (ret <= 0)
			break;
		total += ret;
	} while (total != len);
	return total ?: ret;
}

/**
 * igt_writen:
 * @fd: the file descriptor
 * @buf: the block with the contents to write
 * @len: the length to write
 *
 * This writes @len bytes from @data to the sysfs file.
 *
 * Returns:
 * -errno on failure or bytes written on success
 */
ssize_t igt_writen(int fd, const char *buf, size_t len)
{
	ssize_t ret;
	size_t total = 0;

	do {
		ret = write(fd, buf + total, len - total);
		if (ret < 0)
			ret = -errno;
		if (ret == -EINTR || ret == -EAGAIN)
			continue;
		if (ret <= 0)
			break;
		total += ret;
	} while (total != len);
	return total ?: ret;
}

/**
 * igt_fs_create_dir: creates and opens directory
 * @fd: file descriptor of parent directory
 * @name: name of the directory to create
 * @mode: permissions for the directory
 *
 * creates a directory under parent directory and returns
 * the fd
 *
 * Returns: directory fd on success, -errno otherwise
 */
int igt_fs_create_dir(int fd, const char *name, mode_t mode)
{
	int ret;
	int dirfd;

	ret = mkdirat(fd, name, mode);
	if (ret)
		return -errno;

	dirfd = openat(fd, name, O_DIRECTORY);
	if (dirfd < 0)
		return -errno;

	return dirfd;
}

/**
 * igt_fs_remove_directory: removes directory
 * @fd: fd of parent directory
 * @name: name of directory to remove
 *
 * removes directory under parent directory
 *
 * Returns: 0 on success, -errno otherwise
 */
int igt_fs_remove_dir(int fd, const char *name)
{
	int ret = unlinkat(fd, name, AT_REMOVEDIR);

	if (ret)
		return -errno;

	return 0;
}
