// SPDX-License-Identifier: MIT
// Copyright 2024 Advanced Micro Devices, Inc.

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/types.h>
#include <semaphore.h>
#include <errno.h>
#include <assert.h>

#include "igt.h"
#include "amd_shared_process.h"

static void
sync_point_signal(sem_t *psem, int num_signals)
{
	int i;

	for (i = 0; i < num_signals; i++)
		sem_post(psem);
}

int
shared_mem_destroy(struct shmbuf *shmp, int shm_fd, int unmap, const char *shm_name)
{
	int ret = 0;

	if (shmp && unmap) {
		munmap(shmp, sizeof(struct shmbuf));
		sem_destroy(&shmp->sem_mutex);
		sem_destroy(&shmp->sync_sem_enter);
		sem_destroy(&shmp->sync_sem_exit);
	}
	if (shm_fd > 0)
		close(shm_fd);

	shm_unlink(shm_name);

	return ret;
}

int
shared_mem_create(struct shmbuf **ppbuf, char shm_name[256])
{
	int shm_fd = -1;
	struct shmbuf *shmp = NULL;
	bool unmap = false;

	// Create a shared memory object
	shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
	if (shm_fd == -1)
		goto error;


	// Configure the size of the shared memory object
	if (ftruncate(shm_fd, sizeof(struct shmbuf)) == -1)
		goto error;

	// Map the shared memory object
	shmp = mmap(0, sizeof(struct shmbuf), PROT_WRITE, MAP_SHARED, shm_fd, 0);
	if (shmp == MAP_FAILED)
		goto error;

	unmap = true;
	if (sem_init(&shmp->sem_mutex, 1, 1) == -1) {
		unmap = true;
		goto error;
	}
	if (sem_init(&shmp->sync_sem_enter, 1, 0) == -1)
		goto error;

	if (sem_init(&shmp->sync_sem_exit, 1, 0) == -1)
		goto error;

	shmp->count = 0;
	*ppbuf = shmp;
	return shm_fd;

error:
	shared_mem_destroy(shmp,  shm_fd,  unmap, shm_name);
	return shm_fd;
}

int
shared_mem_open(struct shmbuf **ppbuf)
{
	int shm_fd = -1;
	struct shmbuf *shmp = NULL;

	shmp = mmap(NULL, sizeof(*shmp), PROT_READ | PROT_WRITE, MAP_SHARED,
			SHARED_BROTHER_DESCRIPTOR, 0);
	if (shmp == MAP_FAILED)
		goto error;
	else
		shm_fd = SHARED_BROTHER_DESCRIPTOR;

	*ppbuf = shmp;

	return shm_fd;
error:
	return shm_fd;
}

void
sync_point_enter(struct shmbuf *sh_mem)
{

	sem_wait(&sh_mem->sem_mutex);
	sh_mem->count++;
	sem_post(&sh_mem->sem_mutex);

	if (sh_mem->count == NUM_BROTHER_PROCESSES)
		sync_point_signal(&sh_mem->sync_sem_enter, NUM_BROTHER_PROCESSES);

	sem_wait(&sh_mem->sync_sem_enter);
}

void
sync_point_exit(struct shmbuf *sh_mem)
{
	sem_wait(&sh_mem->sem_mutex);
	sh_mem->count--;
	sem_post(&sh_mem->sem_mutex);

	if (sh_mem->count == 0)
		sync_point_signal(&sh_mem->sync_sem_exit, NUM_BROTHER_PROCESSES);

	sem_wait(&sh_mem->sync_sem_exit);
}

int
get_command_line(char cmdline[2048], int *pargc, char ***pppargv, char **ppath)
{
	ssize_t total_length = 0;
	char *tmpline;
	char **argv = NULL;
	char *path  = NULL;
	int length_cmd[16] = {0};
	int i, argc = 0;
	ssize_t num_read;

	int fd = open("/proc/self/cmdline", O_RDONLY);

	if (fd == -1) {
		igt_info("**** Error opening /proc/self/cmdline");
		return -1;
	}

	num_read = read(fd, cmdline, 2048 - 1);
	close(fd);

	if (num_read == -1) {
		igt_info("*** Error reading /proc/self/cmdline");
		return -1;
	}
	cmdline[num_read] = '\0';

	tmpline = cmdline;
	memset(length_cmd, 0, sizeof(length_cmd));

	/*assumption that last parameter has 2 '\0' at the end*/
	for (i = 0; total_length < num_read - 2; i++) {
		length_cmd[i] = strlen(tmpline);
		total_length += length_cmd[i];
		tmpline += length_cmd[i] + 1;
		argc++;
	}
	*pargc = argc;
	if (argc == 0 || argc > 20) {
		/* not support yet fancy things */
		return -1;
	}
	/* always do 2 extra for additional parameter */
	argv = (char **)malloc(sizeof(argv) * (argc + 2));
	memset(argv, 0, sizeof(argv) * (argc + 2));
	tmpline = cmdline;
	for (i = 0; i < argc; i++) {
		argv[i] = (char *)malloc(sizeof(char) * length_cmd[i] + 1);
		memcpy(argv[i], tmpline, length_cmd[i]);
		argv[i][length_cmd[i]] = 0;
		if (i == 0) {
			path = (char *)malloc(sizeof(char) * length_cmd[0] + 1);
			memcpy(path, tmpline, length_cmd[0]);
			path[length_cmd[0]] = 0;
		}
		argv[i][length_cmd[i]] = 0;
		tmpline += length_cmd[i] + 1;
	}
	*pppargv = argv;
	*ppath = path;

	return 0;
}

int
is_brother_parameter_found(int argc, char **argv, const char *param)
{
	int ret = 0;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(param, argv[i]) == 0) {
			ret = 1;
			break;
		}
	}
	return ret;
}

int
add_brother_parameter(int *pargc, char **argv, const char *param)
{
	int argc = *pargc;
	int len = strlen(param);

	argv[argc] = (char *)malloc(sizeof(char) * len + 1);
	memcpy(argv[argc], param, len);
	argv[argc][len] = 0;
	*pargc = argc + 1;
	return 1;
}

void
free_command_line(int argc, char **argv, char *path)
{
	int i;

	for (i = 0; i <= argc; i++)
		free(argv[i]);

	free(argv);
	free(path);

}

int
is_run_device_parameter_found(int argc, char **argv, const char *param)
{
	int i;
	int res = 0;
	char *p = NULL;

	for (i = 1; i < argc; i++) {
		if (strcmp(param, argv[i]) == 0) {
			/* Get the sum for a specific device as a unique identifier */
			p = argv[i+1];
			while (*p) {
				res += *p;
				p++;
			}
			break;
		}
	}

	return res;
}

int
launch_brother_process(int argc, char **argv, char *path, pid_t *ppid, int shm_fd)
{
	int status;
	posix_spawn_file_actions_t action;
	posix_spawnattr_t attr;

	for (int i = 0; i < argc; i++) {
		if (strstr(argv[i], "list-subtests") != NULL)
			return 0;
	}
	posix_spawn_file_actions_init(&action);
	posix_spawn_file_actions_adddup2(&action, shm_fd, SHARED_BROTHER_DESCRIPTOR);
	posix_spawnattr_init(&attr);
	posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
	status = posix_spawnp(ppid, path, &action,  &attr, argv, NULL);

	posix_spawn_file_actions_destroy(&action);
	posix_spawnattr_destroy(&attr);

	if (status != 0)
		igt_fail(IGT_EXIT_FAILURE);

	return status;
}
