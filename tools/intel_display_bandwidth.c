// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <getopt.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "intel_io.h"
#include "intel_chipset.h"
#include "intel_reg.h"

static bool has_de_power2(uint32_t devid)
{
	/*
	 * TGL has DE_POWER2 but it measures the low priority traffic
	 * on ABOX, not the actual display traffic on ABOX0/ABOX1.
	 */
	if (intel_display_ver(devid) == 12)
		return false;

	return intel_display_ver(devid) >= 6 &&
		!IS_VALLEYVIEW(devid) && !IS_CHERRYVIEW(devid);
}

static bool has_de_power2_abox0_abox1(uint32_t devid)
{
	/*
	 * Despite having ABOX0/ABOX1 TGL lacks the
	 * accompanying DE_POWER2_ABOX* registers.
	 */
	return intel_display_ver(devid) >= 13;
}

static int de_power2_scale(uint32_t devid)
{
	/*
	 * FIXME should perhaps use something like
	 * is_intel_dgfx() but that one wants to open the device :(
	 */
	switch (intel_display_ver(devid)) {
	case 14:
		return IS_BATTLEMAGE(devid) ? 1 : 2;
	case 13:
		return IS_DG2(devid) ? 1 : 2;
	default:
		return 1;
	}
}

static int de_power2_unit(uint32_t devid)
{
	return 64 * de_power2_scale(devid);
}

static float bandwidth(uint32_t devid, int duration,
		       uint32_t pre, uint32_t post)
{
	return (float)(post - pre) * de_power2_unit(devid) / (duration << 20);
}

static void measure_de_power2_abox0_abox1(uint32_t devid, unsigned int sleep_duration)
{
	uint32_t pre_abox0, post_abox0;
	uint32_t pre_abox1, post_abox1;

	pre_abox0 = INREG(DE_POWER2_ABOX0);
	pre_abox1 = INREG(DE_POWER2_ABOX1);

	if (sleep_duration) {
		sleep(sleep_duration);

		post_abox0 = INREG(DE_POWER2_ABOX0);
		post_abox1 = INREG(DE_POWER2_ABOX1);

		printf("DE_POWER2_ABOX0: 0x%08x->0x%08x\n",
		       pre_abox0, post_abox0);
		printf("DE_POWER2_ABOX1: 0x%08x->0x%08x\n",
		       pre_abox1, post_abox1);

		printf("ABOX0 bandwidth: %.2f MiB/s\n",
		       bandwidth(devid, sleep_duration,
				 pre_abox0, post_abox0));
		printf("ABOX1 bandwidth: %.2f MiB/s\n",
		       bandwidth(devid, sleep_duration,
				 pre_abox1, post_abox1));
		printf("Total bandwidth: %.2f MiB/s\n",
		       bandwidth(devid, sleep_duration,
				 pre_abox0 + pre_abox1, post_abox0 + post_abox1));
	} else {
		printf("DE_POWER2_ABOX0: 0x%08x\n", pre_abox0);
		printf("DE_POWER2_ABOX1: 0x%08x\n", pre_abox1);
	}
}

static void measure_de_power2(uint32_t devid, unsigned int sleep_duration)
{
	uint32_t pre, post;

	pre = INREG(DE_POWER2);

	if (sleep_duration) {
		sleep(sleep_duration);

		post = INREG(DE_POWER2);

		printf("DE_POWER2: 0x%08x->0x%08x\n", pre, post);

		printf("Total bandwidth: %.2f MiB/s\n",
		       bandwidth(devid, sleep_duration, pre, post));
	} else {
		printf("DE_POWER2: 0x%08x\n", pre);
	}
}

static void __attribute__((noreturn)) usage(const char *name)
{
	fprintf(stderr, "Usage: %s [options]\n"
		" -s,--sleep <seconds>\n",
		name);
	exit(1);
}

int main(int argc, char *argv[])
{
	struct intel_mmio_data mmio_data;
	unsigned int sleep_duration = 0;
	uint32_t devid;

	for (;;) {
		static const struct option long_options[] = {
			{ .name = "sleep", .has_arg = required_argument, .val = 's', },
			{}
		};
		int opt;

		opt = getopt_long(argc, argv, "s:", long_options, NULL);
		if (opt == -1)
			break;

		switch (opt) {
		case 's':
			sleep_duration = atoi(optarg);
			break;
		default:
			usage(argv[0]);
			break;
		}
	}

	devid = intel_get_pci_device()->device_id;

	if (!has_de_power2(devid)) {
		fprintf(stderr, "Display bandwidth counter not available\n");
		return 2;
	}

	intel_register_access_init(&mmio_data, intel_get_pci_device(), 0);

	if (has_de_power2_abox0_abox1(devid))
		measure_de_power2_abox0_abox1(devid, sleep_duration);
	else
		measure_de_power2(devid, sleep_duration);

	intel_register_access_fini(&mmio_data);

	return 0;
}
