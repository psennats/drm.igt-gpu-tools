// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

/**
 * TEST: Test Xe PMU(Performance Monitoring Unit) functionality
 * Category: Metrics
 * Functionality: Power/Perf
 * Mega feature: Performance Monitoring Unit
 * Sub-category: Telemetry
 * Test category: Functional tests
 */

#include <fcntl.h>
#include <limits.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>
#include <string.h>
#include <sys/time.h>

#include "igt.h"
#include "igt_device.h"
#include "igt_perf.h"
#include "igt_sysfs.h"

#include "xe/xe_gt.h"

#define SLEEP_DURATION 2 /* in seconds */
const double tolerance = 0.1;

static int open_pmu(int xe, uint64_t config)
{
	int fd;

	fd = perf_xe_open(xe, config);
	igt_skip_on(fd < 0 && errno == ENODEV);
	igt_assert(fd >= 0);

	return fd;
}

static uint64_t __pmu_read_single(int fd, uint64_t *ts)
{
	uint64_t data[2];

	igt_assert_eq(read(fd, data, sizeof(data)), sizeof(data));
	if (ts)
		*ts = data[1];

	return data[0];
}

static unsigned long read_idle_residency(int fd, int gt)
{
	unsigned long residency = 0;
	int gt_fd;

	gt_fd = xe_sysfs_gt_open(fd, gt);
	igt_assert(gt_fd >= 0);
	igt_assert(igt_sysfs_scanf(gt_fd, "gtidle/idle_residency_ms", "%lu", &residency) == 1);
	close(gt_fd);

	return residency;
}

static uint64_t get_event_config(int xe, unsigned int gt, char *event)
{
	int ret;
	char xe_device[100];
	uint64_t pmu_config;
	uint32_t shift;

	xe_perf_device(xe, xe_device, sizeof(xe_device));
	ret = perf_event_config(xe_device, event, &pmu_config);
	igt_assert(ret >= 0);
	ret = perf_event_format(xe_device, "gt", &shift);
	igt_assert(ret >= 0);
	pmu_config |= (uint64_t)gt << shift;

	return pmu_config;
}

/**
 * SUBTEST: gt-c6-idle
 * Description: Basic residency test to validate idle residency
 *		measured over a time interval is within the tolerance
 */
static void test_gt_c6_idle(int xe, unsigned int gt)
{
	int pmu_fd;
	uint64_t pmu_config;
	char event[100];
	uint64_t ts[2];
	unsigned long slept, start, end;
	uint64_t val;

	/* Get the PMU config for the gt-c6 event */
	sprintf(event, "gt-c6-residency");
	pmu_config = get_event_config(xe, gt, event);

	pmu_fd = open_pmu(xe, pmu_config);

	igt_require_f(igt_wait(xe_gt_is_in_c6(xe, gt), 1000, 10), "GT %d should be in C6\n", gt);

	/* While idle check full RC6. */
	start = read_idle_residency(xe, gt);
	val = __pmu_read_single(pmu_fd, &ts[0]);
	slept = igt_measured_usleep(SLEEP_DURATION * USEC_PER_SEC) / 1000;
	end = read_idle_residency(xe, gt);
	val = __pmu_read_single(pmu_fd, &ts[1]) - val;

	igt_debug("gt%u: slept=%lu, perf=%"PRIu64"\n",
		  gt, slept,  val);

	igt_debug("Start res: %lu, end_res: %lu", start, end);

	assert_within_epsilon(val,
			      (ts[1] - ts[0])/USEC_PER_SEC,
			      tolerance);
	close(pmu_fd);
}

igt_main
{
	int fd, gt;

	igt_fixture {
		fd = drm_open_driver(DRIVER_XE);
		igt_require(!IS_PONTEVECCHIO(xe_dev_id(fd)));
	}

	igt_describe("Validate PMU gt-c6 residency counters when idle");
	igt_subtest("gt-c6-idle")
		xe_for_each_gt(fd, gt)
			test_gt_c6_idle(fd, gt);

	igt_fixture {
		close(fd);
	}
}
