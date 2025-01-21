// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "drmtest.h"
#include "igt_power.h"

struct measurement {
	int battery_index;
	const char *rapl_domain;
	const char *drm_device;
	struct power_sample pre, post;
	struct igt_power power;
};

static bool prepare(struct measurement *m)
{
	if (m->battery_index >= 0) {
		int ret;

		ret = igt_power_bat_open(&m->power, m->battery_index);
		if (ret) {
			fprintf(stderr, "Unable to open battery %d (%d)\n",
				m->battery_index, ret);

			return false;
		}
	}

	if (m->rapl_domain) {
		int ret, fd = -1;

		if (m->drm_device) {
			fd = open(m->drm_device, O_RDONLY);
			if (fd < 0) {
				fprintf(stderr, "Unable to open drm device %s (%d)\n",
					m->drm_device, -errno);
				return false;
			}
		}

		ret = igt_power_open(fd, &m->power, m->rapl_domain);
		if (ret) {
			if (m->drm_device)
				fprintf(stderr, "Unable to open hwmon/rapl for %s (%d)\n",
					m->drm_device, ret);
			else
				fprintf(stderr, "Unable to open rapl domain %s (%d)\n",
					m->rapl_domain, ret);
			close(fd);

			return false;
		}

		close(fd);
	}

	return true;
}

static void sample_pre(struct measurement *m)
{
	igt_power_get_energy(&m->power, &m->pre);
}

static void sample_post(struct measurement *m)
{
	igt_power_get_energy(&m->power, &m->post);
}

static void report(struct measurement *m)
{
	if (m->battery_index >= 0)
		printf("battery[%d]: energy %f mJ, power %f mW, time %f s\n",
		       m->battery_index,
		       igt_power_get_mJ(&m->power, &m->pre, &m->post),
		       igt_power_get_mW(&m->power, &m->pre, &m->post),
		       igt_power_get_s(&m->pre, &m->post));
	else
		printf("%s[%s]: energy %f mJ, power %f mW, time %f s\n",
		       m->drm_device ?: "rapl", m->rapl_domain,
		       igt_power_get_mJ(&m->power, &m->pre, &m->post),
		       igt_power_get_mW(&m->power, &m->pre, &m->post),
		       igt_power_get_s(&m->pre, &m->post));

	igt_power_close(&m->power);
}

static void __attribute__((noreturn)) usage(const char *name)
{
	fprintf(stderr,
		"Usage: %s [[-d <device>][-r <domain>][-b <battery>]...][-S <seconds>][-s <seconds>]\n"
		"  -d,--drm <device>\tDRM device (eg. /dev/dri/card0)\n"
		"  -r,--rapl <domain>\trapl domain (cpu,gpu,pkg,ram)\n"
		"  -b,--battery <battery>\tbattery index\n"
		"  -S,--settle <seconds>\tsettling duration\n"
		"  -s,--sleep <seconds>\tmeasurement duration\n",
		name);
	exit(1);
}

int main(int argc, char *argv[])
{
	struct measurement measurements[8];
	int num_measurements = 0;
	int measurement_duration = 0;
	int settle_duration = 0;

	for (;;) {
		static const struct option long_options[] = {
			{ .name = "drm",     .has_arg = required_argument, .val = 'd', },
			{ .name = "rapl",    .has_arg = required_argument, .val = 'r', },
			{ .name = "battery", .has_arg = required_argument, .val = 'b', },
			{ .name = "sleep",   .has_arg = required_argument, .val = 's', },
			{ .name = "settle",  .has_arg = required_argument, .val = 'S', },
			{}
		};
		int opt;

		opt = getopt_long(argc, argv, "d:r:b:s:S:", long_options, NULL);
		if (opt == -1)
			break;

		switch (opt) {
		case 'd':
			if (num_measurements >= ARRAY_SIZE(measurements))
				usage(argv[0]);
			measurements[num_measurements].battery_index = -1;
			measurements[num_measurements].rapl_domain = "gpu";
			measurements[num_measurements].drm_device = optarg;
			num_measurements++;
			break;
		case 'r':
			if (num_measurements >= ARRAY_SIZE(measurements))
				usage(argv[0]);
			measurements[num_measurements].battery_index = -1;
			measurements[num_measurements].rapl_domain = optarg;
			measurements[num_measurements].drm_device = NULL;
			num_measurements++;
			break;
		case 'b':
			if (num_measurements >= ARRAY_SIZE(measurements))
				usage(argv[0]);
			measurements[num_measurements].battery_index = atoi(optarg);
			measurements[num_measurements].rapl_domain = NULL;
			measurements[num_measurements].drm_device = NULL;
			num_measurements++;
			break;
		case 's':
			measurement_duration = atoi(optarg);
			break;
		case 'S':
			settle_duration = atoi(optarg);
			break;
		default:
			usage(argv[0]);
			break;
		}
	}

	if (num_measurements == 0)
		usage(argv[0]);

	for (int i = 0; i < num_measurements; i++) {
		if (!prepare(&measurements[i]))
			usage(argv[0]);
	}

	sleep(settle_duration);

	for (int i = 0; i < num_measurements; i++)
		sample_pre(&measurements[i]);

	sleep(measurement_duration);

	for (int i = 0; i < num_measurements; i++)
		sample_post(&measurements[i]);

	for (int i = 0; i < num_measurements; i++)
		report(&measurements[i]);

	return 0;
}
