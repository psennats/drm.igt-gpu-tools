/* SPDX-License-Identifier: MIT
 * Copyright © 2025 Intel Corporation
 */

#ifndef IGT_DIR_H
#define IGT_DIR_H

#include "igt_list.h"

/**
 * Callback function type for processing files
 * The callback is blocking, meaning traversal waits for it to return
 * before proceeding to the next file
 * @filename: Path to the file
 * @callback_data: Optional pointer to user-defined data passed to the callback
 *
 * Returns:
 * 0 on success, a negative error code on failure.
 */
typedef int (*igt_dir_file_callback)(const char *filename,
				     void *callback_data);

/**
 * igt_dir_file_list_t: List of files with a relative path
 * @relative_path: path to a file, relative to the root directory
 * @match: a boolean used to filter the list of files. When match=true the
 *	   file is processed, otherwise it is skipped
 * @link: list head for linking files in the list
 */
typedef struct {
	char *relative_path;
	bool match;
	struct igt_list_head link;
} igt_dir_file_list_t;

/**
 * igt_dir_t: Main struct for igt_dir
 * @dirfd: file descriptor of the root directory
 * @root_path: string of the root path, for example:
 *	       /sys/kernel/debug/dri/0000:00:02.0/
 * @file_list_head: head of the list of files
 * @callback: Callback function for file operations. If NULL, defaults
 *	      to reading and discarding file contents
 */
typedef struct {
	int dirfd;
	char *root_path;
	struct igt_list_head file_list_head;
	igt_dir_file_callback callback;
} igt_dir_t;

int igt_dir_get_fd_path(int fd, char *path, size_t path_len);
int igt_dir_callback_read_discard(const char *filename,
				  void *callback_data);
igt_dir_t *igt_dir_create(int dirfd);
int igt_dir_scan_dirfd(igt_dir_t *config, int scan_maxdepth);
int igt_dir_process_files(igt_dir_t *config,
			  igt_dir_file_callback callback,
			  void *callback_data);
void igt_dir_destroy(igt_dir_t *config);
int igt_dir_process_files_simple(int dirfd);
#endif /* IGT_DIR_H */
