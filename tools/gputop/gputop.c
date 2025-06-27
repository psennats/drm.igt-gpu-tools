// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2025 Intel Corporation
 */

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "drmtest.h"
#include "igt_core.h"
#include "igt_drm_clients.h"
#include "igt_drm_fdinfo.h"
#include "igt_perf.h"
#include "igt_profiling.h"
#include "xe_gputop.h"
#include "xe/xe_query.h"

/**
 * Supported Drivers
 *
 * Adhere to the following requirements when implementing support for the
 * new driver:
 * @drivers: Update drivers[] with driver string.
 * @sizeof_gputop_obj: Update this function as per new driver support included.
 * @operations: Update the respective operations of the new driver:
 * gputop_init,
 * discover_engines,
 * pmu_init,
 * pmu_sample,
 * print_engines,
 * clean_up
 * @per_driver_contexts: Update per_driver_contexts[] array of type "struct gputop_driver" with the
 * initial values.
 */
static const char * const drivers[] = {
	"xe",
    /* Keep the last one as NULL */
	NULL
};

static size_t sizeof_gputop_obj(int driver_num)
{
	switch (driver_num) {
	case 0:
		return sizeof(struct xe_gputop);
	default:
		fprintf(stderr,
			"Driver number does not exist.\n");
		exit(EXIT_FAILURE);
	}
}

/**
 * Supported operations on driver instances. Update the ops[] array for
 * each individual driver specific function. Maintain the sequence as per
 * drivers[] array.
 */
struct device_operations ops[] = {
	{
		xe_gputop_init,
		xe_populate_engines,
		xe_pmu_init,
		xe_pmu_sample,
		xe_print_engines,
		xe_clean_up
	}
};

/*
 * per_driver_contexts[] array of type struct gputop_driver which keeps track of the devices
 * and related info discovered per driver.
 */
struct gputop_driver per_driver_contexts[] = {
	{false, 0, NULL}
};

enum utilization_type {
	UTILIZATION_TYPE_ENGINE_TIME,
	UTILIZATION_TYPE_TOTAL_CYCLES,
};

static void gputop_clean_up(void)
{
	for (int i = 0; drivers[i]; i++) {
		ops[i].clean_up(per_driver_contexts[i].instances, per_driver_contexts[i].len);
		free(per_driver_contexts[i].instances);
		per_driver_contexts[i].device_present = false;
		per_driver_contexts[i].len = 0;
	}
}

static int find_driver(struct igt_device_card *card)
{
	for (int i = 0; drivers[i]; i++) {
		if (strcmp(drivers[i], card->driver) == 0)
			return i;
	}
	return -1;
}

static int populate_device_instances(const char *filter)
{
	struct igt_device_card *cards = NULL;
	struct igt_device_card *card_inplace = NULL;
	struct gputop_driver *driver_entry =  NULL;
	int driver_no;
	int count, final_count = 0;

	count = igt_device_card_match_all(filter, &cards);
	for (int j = 0; j < count; j++) {
		if (strcmp(cards[j].subsystem, "pci") != 0)
			continue;

		driver_no = find_driver(&cards[j]);
		if (driver_no < 0)
			continue;

		driver_entry = &per_driver_contexts[driver_no];
		if (!driver_entry->device_present)
			driver_entry->device_present = true;
		driver_entry->len++;
		driver_entry->instances = realloc(driver_entry->instances,
						  driver_entry->len * sizeof_gputop_obj(driver_no));
		if (!driver_entry->instances) {
			fprintf(stderr,
				"Device instance realloc failed (%s)\n",
				strerror(errno));
			exit(EXIT_FAILURE);
		}
		card_inplace = (struct igt_device_card *)
				calloc(1, sizeof(struct igt_device_card));
		memcpy(card_inplace, &cards[j], sizeof(struct igt_device_card));
		ops[driver_no].gputop_init(driver_entry->instances, (driver_entry->len - 1),
			card_inplace);
		final_count++;
	}
	if (count)
		free(cards);
	return final_count;
}

static int
print_client_header(struct igt_drm_client *c, int lines, int con_w, int con_h,
		    int *engine_w)
{
	int ret, len;

	if (lines++ >= con_h)
		return lines;

	printf(ANSI_HEADER);
	ret = printf("DRM minor %u", c->drm_minor);
	n_spaces(con_w - ret);

	if (lines++ >= con_h)
		return lines;

	putchar('\n');
	if (c->regions->num_regions)
		len = printf("%*s      MEM      RSS ",
			     c->clients->max_pid_len, "PID");
	else
		len = printf("%*s ", c->clients->max_pid_len, "PID");

	if (c->engines->num_engines) {
		unsigned int i;
		int width;

		*engine_w = width =
			(con_w - len - c->clients->max_name_len - 1) /
			c->engines->num_engines;

		for (i = 0; i <= c->engines->max_engine_id; i++) {
			const char *name = c->engines->names[i];
			int name_len = strlen(name);
			int pad = (width - name_len) / 2;
			int spaces = width - pad - name_len;

			if (!name)
				continue;

			if (pad < 0 || spaces < 0)
				continue;

			n_spaces(pad);
			printf("%s", name);
			n_spaces(spaces);
			len += pad + name_len + spaces;
		}
	}

	printf(" %-*s" ANSI_RESET "\n", con_w - len - 1, "NAME");

	return lines;
}

static bool
engines_identical(const struct igt_drm_client *c,
		  const struct igt_drm_client *pc)
{
	unsigned int i;

	if (c->engines->num_engines != pc->engines->num_engines ||
	    c->engines->max_engine_id != pc->engines->max_engine_id)
		return false;

	for (i = 0; i <= c->engines->max_engine_id; i++)
		if (c->engines->capacity[i] != pc->engines->capacity[i] ||
		    !!c->engines->names[i] != !!pc->engines->names[i] ||
		    strcmp(c->engines->names[i], pc->engines->names[i]))
			return false;

	return true;
}

static bool
newheader(const struct igt_drm_client *c, const struct igt_drm_client *pc)
{
	return !pc || c->drm_minor != pc->drm_minor ||
	       /*
		* Below is a a hack for drivers like amdgpu which omit listing
		* unused engines. Simply treat them as separate minors which
		* will ensure the per-engine columns are correctly sized in all
		* cases.
		*/
	       !engines_identical(c, pc);
}

static int
print_size(uint64_t sz)
{
	char units[] = {'B', 'K', 'M', 'G'};
	unsigned int u;

	for (u = 0; u < ARRAY_SIZE(units) - 1; u++) {
		if (sz < 1024)
			break;
		sz /= 1024;
	}

	return printf("%7"PRIu64"%c ", sz, units[u]);
}

static int
print_client(struct igt_drm_client *c, struct igt_drm_client **prevc,
	     double t, int lines, int con_w, int con_h,
	     unsigned int period_us, int *engine_w)
{
	enum utilization_type utilization_type;
	unsigned int i;
	uint64_t sz;
	int len;

	if (c->utilization_mask & IGT_DRM_CLIENT_UTILIZATION_TOTAL_CYCLES &&
	    c->utilization_mask & IGT_DRM_CLIENT_UTILIZATION_CYCLES)
		utilization_type = UTILIZATION_TYPE_TOTAL_CYCLES;
	else if (c->utilization_mask & IGT_DRM_CLIENT_UTILIZATION_ENGINE_TIME)
		utilization_type = UTILIZATION_TYPE_ENGINE_TIME;
	else
		return 0;

	if (c->samples < 2)
		return 0;

	/* Filter out idle clients. */
	switch (utilization_type) {
	case UTILIZATION_TYPE_ENGINE_TIME:
	       if (!c->total_engine_time)
		       return 0;
	       break;
	case UTILIZATION_TYPE_TOTAL_CYCLES:
	       if (!c->total_total_cycles)
		       return 0;
	       break;
	}

	/* Print header when moving to a different DRM card. */
	if (newheader(c, *prevc)) {
		lines = print_client_header(c, lines, con_w, con_h, engine_w);
		if (lines >= con_h)
			return lines;
	}

	*prevc = c;

	len = printf("%*s ", c->clients->max_pid_len, c->pid_str);

	if (c->regions->num_regions) {
		for (sz = 0, i = 0; i <= c->regions->max_region_id; i++)
			sz += c->memory[i].total;
		len += print_size(sz);

		for (sz = 0, i = 0; i <= c->regions->max_region_id; i++)
			sz += c->memory[i].resident;
		len += print_size(sz);
	}

	lines++;

	for (i = 0; c->samples > 1 && i <= c->engines->max_engine_id; i++) {
		double pct;

		if (!c->engines->capacity[i])
			continue;

		switch (utilization_type) {
		case UTILIZATION_TYPE_ENGINE_TIME:
			pct = (double)c->utilization[i].delta_engine_time / period_us / 1e3 * 100 /
				c->engines->capacity[i];
			break;
		case UTILIZATION_TYPE_TOTAL_CYCLES:
			pct = (double)c->utilization[i].delta_cycles / c->utilization[i].delta_total_cycles * 100 /
				c->engines->capacity[i];
			break;
		}

		/*
		 * Guard against fluctuations between our scanning period and
		 * GPU times as exported by the kernel in fdinfo.
		 */
		if (pct > 100.0)
			pct = 100.0;

		print_percentage_bar(pct, *engine_w);
		len += *engine_w;
	}

	printf(" %-*s\n", con_w - len - 1, c->print_name);

	return lines;
}

static int
__client_id_cmp(const struct igt_drm_client *a,
		const struct igt_drm_client *b)
{
	if (a->id > b->id)
		return 1;
	else if (a->id < b->id)
		return -1;
	else
		return 0;
}

static int client_cmp(const void *_a, const void *_b, void *unused)
{
	const struct igt_drm_client *a = _a;
	const struct igt_drm_client *b = _b;
	long val_a, val_b;

	/* DRM cards into consecutive buckets first. */
	val_a = a->drm_minor;
	val_b = b->drm_minor;
	if (val_a > val_b)
		return 1;
	else if (val_b > val_a)
		return -1;

	/*
	 * Within buckets sort by last sampling period aggregated runtime, with
	 * client id as a tie-breaker.
	 */
	val_a = a->agg_delta_engine_time;
	val_b = b->agg_delta_engine_time;
	if (val_a == val_b)
		return __client_id_cmp(a, b);
	else if (val_b > val_a)
		return 1;
	else
		return -1;

}

static void update_console_size(int *w, int *h)
{
	struct winsize ws = {};

	if (ioctl(0, TIOCGWINSZ, &ws) == -1)
		return;

	*w = ws.ws_col;
	*h = ws.ws_row;

	if (*w == 0 && *h == 0) {
		/* Serial console. */
		*w = 80;
		*h = 24;
	}
}

static void clrscr(void)
{
	printf("\033[H\033[J");
}

struct gputop_args {
	long n_iter;
	unsigned long delay_usec;
	char *device;
};

static void help(char *full_path)
{
	const char *short_program_name = strrchr(full_path, '/');

	if (short_program_name)
		short_program_name++;
	else
		short_program_name = full_path;

	printf("Usage:\n"
	       "\t%s [options]\n\n"
	       "Options:\n"
	       "\t-h, --help                show this help\n"
	       "\t-d, --delay =SEC[.TENTHS] iterative delay as SECS [.TENTHS]\n"
	       "\t-n, --iterations =NUMBER  number of executions\n"
	       "\t-D, --device              Device filter\n"
	       , short_program_name);
}

static int parse_args(int argc, char * const argv[], struct gputop_args *args)
{
	static const char cmdopts_s[] = "hn:d:D:";
	static const struct option cmdopts[] = {
	       {"help", no_argument, 0, 'h'},
	       {"delay", required_argument, 0, 'd'},
	       {"iterations", required_argument, 0, 'n'},
	       {"device", required_argument, 0, 'D'},
	       { }
	};

	/* defaults */
	memset(args, 0, sizeof(*args));
	args->n_iter = -1;
	args->delay_usec = 2 * USEC_PER_SEC;
	args->device = NULL;

	for (;;) {
		int c, idx = 0;
		char *end_ptr = NULL;

		c = getopt_long(argc, argv, cmdopts_s, cmdopts, &idx);
		if (c == -1)
			break;

		switch (c) {
		case 'n':
			args->n_iter = strtol(optarg, NULL, 10);
			break;
		case 'd':
			args->delay_usec = strtoul(optarg, &end_ptr, 10) * USEC_PER_SEC;
			if (*end_ptr == '.')
				args->delay_usec += strtoul(end_ptr + 1, &end_ptr, 10) * USEC_PER_DECISEC;

			if (!args->delay_usec) {
				fprintf(stderr, "Invalid delay value: %s\n", optarg);
				return -1;
			}
			break;
		case 'D':
			args->device = optarg;
			break;
		case 'h':
			help(argv[0]);
			return 0;
		default:
			fprintf(stderr, "Unkonwn option '%c'.\n", c);
			return -1;
		}
	}

	return 1;
}

static volatile bool stop_top;

static void sigint_handler(int sig)
{
	(void) sig;
	stop_top = true;
}

int main(int argc, char **argv)
{
	struct gputop_args args;
	unsigned int period_us;
	struct igt_profiled_device *profiled_devices = NULL;
	struct igt_drm_clients *clients = NULL;
	int con_w = -1, con_h = -1;
	int ret;
	long n;

	ret = parse_args(argc, argv, &args);
	if (ret < 0)
		return EXIT_FAILURE;
	if (!ret)
		return EXIT_SUCCESS;

	n = args.n_iter;
	period_us = args.delay_usec;

	if (!populate_device_instances(args.device ? args.device
				       : "device:subsystem=pci,card=all")) {
		printf("No device found.\n");
		gputop_clean_up();
		exit(1);
	}

	for (int i = 0; drivers[i]; i++) {
		if (!per_driver_contexts[i].device_present)
			continue;

		for (int j = 0; j < per_driver_contexts[i].len; j++) {
			if (!ops[i].init_engines(per_driver_contexts[i].instances, j)) {
				fprintf(stderr,
					"Failed to initialize engines! (%s)\n",
					strerror(errno));
					gputop_clean_up();
				return EXIT_FAILURE;
			}
			ret = ops[i].pmu_init(per_driver_contexts[i].instances, j);

			if (ret) {
				fprintf(stderr,
					"Failed to initialize PMU! (%s)\n",
					strerror(errno));
				if (errno == EACCES && geteuid())
					fprintf(stderr,
						"\n"
						"When running as a normal user CAP_PERFMON is required to access performance\n"
						"monitoring. See \"man 7 capabilities\", \"man 8 setcap\", or contact your\n"
						"distribution vendor for assistance.\n"
						"\n"
						"More information can be found at 'Perf events and tool security' document:\n"
						"https://www.kernel.org/doc/html/latest/admin-guide/perf-security.html\n");

				igt_devices_free();
				gputop_clean_up();
				return EXIT_FAILURE;
			}
		}
	}

	for (int i = 0; drivers[i]; i++) {
		for (int j = 0;
		     per_driver_contexts[i].device_present && j < per_driver_contexts[i].len;
		     j++)
			ops[i].pmu_sample(per_driver_contexts[i].instances, j);
	}

	clients = igt_drm_clients_init(NULL);
	if (!clients)
		exit(1);

	profiled_devices = igt_devices_profiled();
	if (profiled_devices != NULL) {
		igt_devices_configure_profiling(profiled_devices, true);

		if (signal(SIGINT, sigint_handler) == SIG_ERR) {
			fprintf(stderr, "Failed to install signal handler!\n");
			igt_devices_configure_profiling(profiled_devices, false);
			igt_devices_free_profiling(profiled_devices);
			profiled_devices = NULL;
		}
	}

	igt_drm_clients_scan(clients, NULL, NULL, 0, NULL, 0);

	while ((n != 0) && !stop_top) {
		struct igt_drm_client *c, *prevc = NULL;
		int k, engine_w = 0, lines = 0;

		igt_drm_clients_scan(clients, NULL, NULL, 0, NULL, 0);

		for (int i = 0; drivers[i]; i++) {
			for (int j = 0;
			     per_driver_contexts[i].device_present &&
			     j < per_driver_contexts[i].len;
			     j++)
				ops[i].pmu_sample(per_driver_contexts[i].instances, j);
		}

		igt_drm_clients_sort(clients, client_cmp);

		update_console_size(&con_w, &con_h);
		clrscr();

		for (int i = 0; drivers[i]; i++) {
			for (int j = 0;
			     per_driver_contexts[i].device_present &&
			     j < per_driver_contexts[i].len;
			     j++) {
				lines = ops[i].print_engines(per_driver_contexts[i].instances, j,
							 lines, con_w, con_h);
			}
		}

		if (!clients->num_clients) {
			const char *msg = " (No GPU clients yet. Start workload to see stats)";

			printf(ANSI_HEADER "%-*s" ANSI_RESET "\n",
			       (int)(con_w - strlen(msg) - 1), msg);
		}

		igt_for_each_drm_client(clients, c, k) {
			assert(c->status != IGT_DRM_CLIENT_PROBE);
			if (c->status != IGT_DRM_CLIENT_ALIVE)
				break; /* Active clients are first in the array. */

			lines = print_client(c, &prevc, (double)period_us / 1e6,
					     lines, con_w, con_h, period_us,
					     &engine_w);
			if (lines >= con_h)
				break;
		}

		if (lines++ < con_h)
			printf("\n");

		usleep(period_us);
		if (n > 0)
			n--;

		if (profiled_devices != NULL)
			igt_devices_update_original_profiling_state(profiled_devices);
	}

	igt_drm_clients_free(clients);
	gputop_clean_up();

	if (profiled_devices != NULL) {
		igt_devices_configure_profiling(profiled_devices, false);
		igt_devices_free_profiling(profiled_devices);
	}
	return 0;
}
