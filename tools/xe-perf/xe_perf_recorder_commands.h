/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef XE_PERF_RECORDER_COMMANDS_H
#define XE_PERF_RECORDER_COMMANDS_H

#include <stdint.h>

#define XE_PERF_RECORD_FIFO_PATH "/tmp/.xe-perf-record"

enum recorder_command {
	RECORDER_COMMAND_DUMP = 1,
	RECORDER_COMMAND_QUIT,
};

struct recorder_command_base {
	uint32_t command;
	uint32_t size; /* size of recorder_command_base + dump in bytes */
};

/*
 The dump after the recorder_command_base header:

struct recorder_command_dump {
	uint8_t path[0];
};
*/

#endif
