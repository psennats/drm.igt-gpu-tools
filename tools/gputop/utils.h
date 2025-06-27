/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef COMMON_GPUTOP_H
#define COMMON_GPUTOP_H

#include <glib.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "igt_device_scan.h"

#define ANSI_HEADER "\033[7m"
#define ANSI_RESET "\033[0m"

#define PERCLIENT_ENGINE_WIDTH 8

/**
 * struct gputop_driver
 *
 * @device_present: It is set if at least a single device of the
 * respective driver is found.
 * @len: Number of total device discovered of the respective
 * driver.
 * @instances: pointer to the array of discovered instances
 * of the devices of the same driver.
 */
struct gputop_driver {
	bool device_present;
	int len;
	void *instances;
};

/**
 * struct device_operations - Structure to hold function
 * pointers for device specific operations for each individual driver.
 * @gputop_init: Function to initialize GPUTOP object
 * @init_engines: Function to initialize engines for the respective driver.
 * @pmu_init: Function to initialize the PMU (Performance Monitoring Unit).
 * @pmu_sample: Function to sample PMU data.
 * @print_engines: Function to print engine business.
 * @clean_up: Function to release resources.
 */
struct device_operations {
	void (*gputop_init)(void *ptr, int index,
			    struct igt_device_card *card);
	void *(*init_engines)(const void *obj, int index);
	int (*pmu_init)(const void *obj, int index);
	void (*pmu_sample)(const void *obj, int index);
	int (*print_engines)(const void *obj, int index, int lines,
			     int w, int h);
	void (*clean_up)(void *obj, int len);
};

void print_percentage_bar(double percent, int max_len);
int print_engines_footer(int lines, int con_w, int con_h);
void n_spaces(const unsigned int n);

#endif  /* COMMON_GPUTOP_H */
