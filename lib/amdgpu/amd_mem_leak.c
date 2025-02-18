// SPDX-License-Identifier: MIT
/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 * Copyright 2025 Advanced Micro Devices, Inc.
 */

#include <fcntl.h>
#include "igt.h"
#include "amd_mem_leak.h"


enum mem_leak_cmd {
	CMD_SCAN = 0, /* as index */
	CMD_CLEAR = 1,
	CMD_MAX,
};

/* return non zero fp write successfully or null if failure */
static
FILE *mem_leak_cmd(enum mem_leak_cmd cmd)
{
	const struct mem_leak_cmd_arr {
		const char *str_cmd;
		enum mem_leak_cmd cmd;
	} memleak_arr[] = {
		{"scan",	CMD_SCAN	},
		{"clear",	CMD_CLEAR	},
		{"",		CMD_MAX		},
		{NULL, 0}
	};

	FILE *fp;
	int len;

	fp = fopen("/sys/kernel/debug/kmemleak", "r+");
	if (fp) {
		len = strlen(memleak_arr[cmd].str_cmd);
		if (fwrite(memleak_arr[cmd].str_cmd, 1, len, fp) != len) {
			fclose(fp);
			fp = NULL;
		}
	}

	return fp;
}

/* return True if scan successfully written to kmemleak */
static
bool send_scan_memleak(void)
{
	FILE *fp;

	fp = mem_leak_cmd(CMD_SCAN);
	if (fp != NULL) {
		fclose(fp);
		return true;
	}
	return false;
}

/* return True if clear successfully sent to kmemleak */
static
bool send_clear_memleak(void)
{
	FILE *fp;

	fp = mem_leak_cmd(CMD_CLEAR);
	if (fp != NULL) {
		fclose(fp);
		return true;
	}
	return false;
}

/* return true if kmemleak is enabled and then clear earlier leak records */
bool clear_memleak(bool is_more_than_one)
{
	if (!send_scan_memleak() || !send_clear_memleak())
		return false;

	if (is_more_than_one) {
		if (!send_scan_memleak() || !send_clear_memleak())
			return false;
	}

	return true;
}

/* return true if kmemleak did not pick up any memory leaks */
bool is_no_memleak(void)
{
	FILE *fp;
	const char *buf[1];
	char read_buf[1024];

	fp = mem_leak_cmd(CMD_SCAN);
	if (fp != NULL) {
		/* read back to see if any leak */
		if (fread(buf, 1, 1, fp) == 0) {
			fclose(fp);
			return true;
		}
	}

	/* Dump contents of kmemleak */
	fseek(fp, 0L, SEEK_SET);
	while (fgets(read_buf, sizeof(read_buf) - 1, fp) != NULL)
		igt_info("MEM_LEAK: %s", read_buf);

	fclose(fp);
	return false;
}
