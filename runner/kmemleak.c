// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "kmemleak.h"

/* We can change the path for unit testing, see runner_kmemleak_init() */
static char runner_kmemleak_file[256] = "/sys/kernel/debug/kmemleak";

#define MAX_WRITE_RETRIES 5

/**
 * runner_kmemleak_write: Writes the buffer to the file descriptor retrying when
 * possible.
 * @fd: The file descriptor to write to.
 * @buf: Pointer to the data to write.
 * @count: Total number of bytes to write.
 *
 * Writes the buffer to the file descriptor retrying when possible.
 */
static bool runner_kmemleak_write(int fd, const void *buf, size_t count)
{
	const char *ptr = buf;
	size_t remaining = count;
	ssize_t written;
	int retries = 0;

	while (remaining > 0) {
		written = write(fd, ptr, remaining);
		if (written > 0) {
			ptr += written;
			remaining -= written;
		} else if (written == -1) {
			if (errno == EINTR || errno == EAGAIN) {
				/* Retry for recoverable errors */
				if (++retries > MAX_WRITE_RETRIES) {
					fprintf(stderr, "%s: Exceeded retry limit\n", __func__);
					return false;
				}
				continue;
			} else {
				/* Log unrecoverable error */
				fprintf(stderr, "%s: unrecoverable write error\n", __func__);
				return false;
			}
		} else if (written == 0) {
			if (++retries > MAX_WRITE_RETRIES) {
				fprintf(stderr, "%s: Exceeded retry limit\n", __func__);
				return false;
			}
		}
	}
	return true;
}

/**
 * runner_kmemleak_cmd:
 * @cmd: command to send to kmemleak
 *
 * Send a command to kmemleak.
 *
 * Returns: true if sending the command was successful, false otherwise.
 */
static bool runner_kmemleak_cmd(const char *cmd)
{
	int fd;
	bool res;

	fd = open(runner_kmemleak_file, O_RDWR);
	if (fd < 0)
		return false;

	res = runner_kmemleak_write(fd, cmd, strlen(cmd));
	close(fd);

	return res;
}

/**
 * runner_kmemleak_clear:
 *
 * Trigger an immediate clear on kmemleak.
 *
 * Returns: true if sending the command to clear was successful, false
 * otherwise.
 */
static bool runner_kmemleak_clear(void)
{
	return runner_kmemleak_cmd("clear");
}

/**
 * runner_kmemleak_found_leaks:
 *
 * Check if kmemleak found any leaks by trying to read one byte from the
 * kmemleak file.
 *
 * Returns: true if kmemleak found any leaks, false otherwise.
 */
static bool runner_kmemleak_found_leaks(void)
{
	int fd;
	char buf[1];
	size_t rlen;

	fd = open(runner_kmemleak_file, O_RDONLY);
	if (fd < 0)
		return false;

	rlen = read(fd, buf, 1);

	if (rlen == 1)
		lseek(fd, 0, SEEK_SET);

	close(fd);

	return rlen == 1;
}

/**
 * runner_kmemleak_scan:
 *
 * Trigger an immediate scan on kmemleak.
 *
 * Returns: true if leaks are found. False on failure and when no leaks are
 * found.
 */
static bool runner_kmemleak_scan(void)
{
	if (!runner_kmemleak_cmd("scan"))
		return false;

	/* kmemleak documentation states that "the memory scanning is only
	 * performed when the /sys/kernel/debug/kmemleak file is read." Read
	 * a byte to trigger the scan now.
	 */
	return runner_kmemleak_found_leaks();
}

/**
 * runner_kmemleak_append_to:
 * @last_test: last test name to append to the file
 * @resdirfd: file descriptor of the results directory
 * @kmemleak_each: if true we scan after each test
 * @sync: sync the kmemleak file often
 *
 * Append the kmemleak file to the result file adding a header indicating if
 * the leaks are for all tests or for a single one.
 *
 * Returns: true if appending to the file was successful, false otherwise.
 */
static bool runner_kmemleak_append_to(const char *last_test, int resdirfd,
				      bool kmemleak_each, bool sync)
{
	const char *before = "kmemleaks found before running any test\n\n";
	const char *once = "kmemleaks found after running all tests\n";
	int kmemleakfd, resfilefd;
	char buf[16384];
	size_t rlen;

	kmemleakfd = open(runner_kmemleak_file, O_RDONLY);
	if (kmemleakfd < 0)
		return false;

	/* Seek back to first byte */
	if (lseek(kmemleakfd, 0, SEEK_SET) == (off_t)-1) {
		close(kmemleakfd);
		return false;
	}

	/* Open text file to append */
	resfilefd = openat(resdirfd, KMEMLEAK_RESFILENAME,
			   O_RDWR | O_CREAT | O_APPEND, 0666);
	if (resfilefd < 0) {
		close(kmemleakfd);
		return false;
	}

	/* This is the header added before the content of the kmemleak file */
	if (kmemleak_each) {
		if (!last_test) {
			runner_kmemleak_write(resfilefd, before, strlen(before));
		} else {
			/* Write \n\n last_test \n to buf */
			snprintf(buf, sizeof(buf),
				 "\n\nkmemleaks found after running %s:\n",
				 last_test);

			runner_kmemleak_write(resfilefd, buf, strlen(buf));
			memset(buf, 0, sizeof(buf));
		}
	} else {
		runner_kmemleak_write(resfilefd, once, strlen(once));
	}

	if (sync)
		fsync(resfilefd);

	while ((rlen = read(kmemleakfd, buf, sizeof(buf))) > 0) {
		if (!runner_kmemleak_write(resfilefd, buf, rlen)) {
			close(resfilefd);
			close(kmemleakfd);
			return false;
		}
		if (sync)
			fsync(resfilefd);
	}

	close(resfilefd);
	close(kmemleakfd);

	return true;
}

/**
 * runner_kmemleak_init:
 * @unit_test_kmemleak_file: path to kmemleak file for unit testing
 *
 * Check if kmemleak is enabled in the kernel, if debugfs is mounted and
 * if kmemleak file is present and readable.
 *
 * Returns: true if kmemleak is enabled, false otherwise.
 */
bool runner_kmemleak_init(const char *unit_test_kmemleak_file)
{
	int fd;

	if (unit_test_kmemleak_file)
		snprintf(runner_kmemleak_file,
			 sizeof(runner_kmemleak_file),
			 "%s",
			 unit_test_kmemleak_file);

	fd = open(runner_kmemleak_file, O_RDONLY);
	if (fd < 0)
		return false;

	close(fd);

	return true;
}

/**
 * runner_kmemleak:
 * @last_test: last test name to append to the file
 * @resdirfd: file descriptor of the results directory
 * @kmemleak_each: Are we scanning once or scanning after each test?
 * @sync: sync the kmemleak file often
 *
 * This is the main function that should be called when integrating runner_kmemleak
 * into igt_runner or elsewhere. There are two flows:
 *  - run once: runs only once after all tests are completed
 *  - run for each test: runs after every test
 *
 * Returns: true on success, false otherwise.
 */
bool runner_kmemleak(const char *last_test, int resdirfd, bool kmemleak_each,
		     bool sync)
{
	/* Scan to collect results */
	if (runner_kmemleak_scan())
		if (!runner_kmemleak_append_to(last_test, resdirfd,
					       kmemleak_each, sync))
			return false;

	if (kmemleak_each)
		runner_kmemleak_clear();

	return true;
}
