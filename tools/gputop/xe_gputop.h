/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef __XE_GPUTOP_H__
#define __XE_GPUTOP_H__

#include <dirent.h>

#include "igt_device_scan.h"
#include "igt_perf.h"
#include "utils.h"
#include "xe/xe_query.h"

struct xe_pmu_pair {
	uint64_t cur;
	uint64_t prev;
};

struct xe_pmu_counter {
	uint64_t type;
	uint64_t config;
	unsigned int idx;
	struct xe_pmu_pair val;
	int fd;
	bool present;
};

struct xe_engine {
	const char *name;
	char *display_name;
	struct drm_xe_engine_class_instance drm_xe_engine;
	unsigned int num_counters;
	struct xe_pmu_counter engine_active_ticks;
	struct xe_pmu_counter engine_total_ticks;
};

struct xe_pmu_device {
	unsigned int num_engines;
	unsigned int num_counters;
	int fd;
	char *device;
	struct xe_engine engine;
};

struct xe_gputop {
	struct igt_device_card *card;
	struct xe_pmu_device *pmu_device_obj;
};

void xe_gputop_init(void *ptr, int index, struct igt_device_card *card);
void *xe_populate_engines(const void *obj, int index);
int xe_pmu_init(const void *obj, int index);
void xe_pmu_sample(const void *obj, int index);
int xe_print_engines(const void *obj, int index, int lines, int w, int h);
void xe_clean_up(void *obj, int len);

#endif /* __XE_GPUTOP_H__ */
