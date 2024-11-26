/* SPDX-License-Identifier: MIT
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 */

#ifndef __AMD_SHARED_PROCESS_H__
#define __AMD_SHARED_PROCESS_H__

#include <semaphore.h>

#define SHARED_BROTHER_DESCRIPTOR 3
#define NUM_BROTHER_PROCESSES 2

#define BROTHER	"brother"
#define ONDEVICE	"--device"

enum  process_type {
	PROCESS_UNKNOWN,
	PROCESS_TEST,
	PROCESS_BROTHER,
};

struct shmbuf {
	sem_t sem_mutex;
	sem_t sync_sem_enter;
	sem_t sync_sem_exit;
	int count;
};

int
shared_mem_create(struct shmbuf **ppbuf, char shm_name[256]);

int
shared_mem_open(struct shmbuf **ppbuf);

int
shared_mem_destroy(struct shmbuf *shmp, int shm_fd, int unmap, const char *shm_name);

void
sync_point_enter(struct shmbuf *sh_mem);

void
sync_point_exit(struct shmbuf *sh_mem);

int
get_command_line(char cmdline[2048], int *pargc, char ***pppargv, char **ppath);

int
is_brother_parameter_found(int argc, char **argv, const char *param);

int
add_brother_parameter(int *pargc, char **argv, const char *param);

void
free_command_line(int argc, char **argv, char *path);

int
is_run_device_parameter_found(int argc, char **argv, const char *param);

int
launch_brother_process(int argc, char **argv, char *path, pid_t *ppid, int shm_fd);

#endif
