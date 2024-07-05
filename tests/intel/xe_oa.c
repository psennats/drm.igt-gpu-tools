// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <poll.h>
#include <math.h>

#include "drm.h"
#include "igt.h"
#include "igt_device.h"
#include "igt_sysfs.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"
#include "xe/xe_oa.h"

/**
 * TEST: perf
 * Description: Test the Xe OA metrics streaming interface
 * Category: Core
 * Mega feature: Performance interface
 * Sub-category: Performance tests
 * Functionality: oa
 * Feature: xe streaming interface, oa
 * Test category: Perf
 */

#define OA_MI_REPORT_PERF_COUNT		((0x28 << 23) | (4 - 2))

#define OAREPORT_REASON_MASK           0x3f
#define OAREPORT_REASON_SHIFT          19
#define OAREPORT_REASON_TIMER          (1<<0)
#define OAREPORT_REASON_INTERNAL       (3<<1)
#define OAREPORT_REASON_CTX_SWITCH     (1<<3)
#define OAREPORT_REASON_GO             (1<<4)
#define OAREPORT_REASON_CLK_RATIO      (1<<5)

#define PIPE_CONTROL_GLOBAL_SNAPSHOT_COUNT_RESET	(1 << 19)
#define PIPE_CONTROL_SYNC_GFDT	  (1 << 17)
#define PIPE_CONTROL_NO_WRITE	   (0 << 14)
#define PIPE_CONTROL_WRITE_IMMEDIATE    (1 << 14)
#define PIPE_CONTROL_WRITE_DEPTH_COUNT  (2 << 14)
#define PIPE_CONTROL_RENDER_TARGET_FLUSH (1 << 12)
#define PIPE_CONTROL_INSTRUCTION_INVALIDATE (1 << 11)
#define PIPE_CONTROL_ISP_DIS	    (1 << 9)
#define PIPE_CONTROL_INTERRUPT_ENABLE   (1 << 8)
/* GT */
#define PIPE_CONTROL_DATA_CACHE_INVALIDATE      (1 << 5)
#define PIPE_CONTROL_PPGTT_WRITE	(0 << 2)
#define PIPE_CONTROL_GLOBAL_GTT_WRITE   (1 << 2)

#define MAX_OA_BUF_SIZE (16 * 1024 * 1024)
#define OA_BUFFER_SIZE MAX_OA_BUF_SIZE

#define RING_FORCE_TO_NONPRIV_ADDRESS_MASK 0x03fffffc
/*
 * Engine specific registers defined as offsets from engine->mmio_base. For
 * these registers, OR bit[0] with 1 so we can add the mmio_base when running
 * engine specific test.
 */
#define MMIO_BASE_OFFSET 0x1

#define OAG_OASTATUS (0xdafc)
#define OAG_PERF_COUNTER_B(idx) (0xDA94 + 4 * (idx))
#define OAG_OATAILPTR (0xdb04)
#define OAG_OATAILPTR_MASK 0xffffffc0
#define OAG_OABUFFER (0xdb08)

#define XE_OA_MAX_SET_PROPERTIES 16

#define ADD_PROPS(_head, _tail, _key, _value)	\
	do { \
		igt_assert((_tail - _head) < (XE_OA_MAX_SET_PROPERTIES * 2)); \
		*_tail++ = DRM_XE_OA_PROPERTY_##_key; \
		*_tail++ = _value; \
	} while (0)

struct accumulator {
#define MAX_RAW_OA_COUNTERS 62
	enum intel_xe_oa_format_name format;

	uint64_t deltas[MAX_RAW_OA_COUNTERS];
};

/* OA unit types */
enum {
	OAG,
	OAR,
	OAM,

	MAX_OA_TYPE,
};

struct oa_format {
	const char *name;
	size_t size;
	int a40_high_off; /* bytes */
	int a40_low_off;
	int n_a40;
	int a64_off;
	int n_a64;
	int a_off;
	int n_a;
	int first_a;
	int first_a40;
	int b_off;
	int n_b;
	int c_off;
	int n_c;
	int oa_type; /* of enum intel_xe_oa_format_name */
	bool report_hdr_64bit;
	int counter_select;
	int counter_size;
	int bc_report;
};

static struct oa_format gen12_oa_formats[XE_OA_FORMAT_MAX] = {
	[XE_OA_FORMAT_A32u40_A4u32_B8_C8] = {
		"A32u40_A4u32_B8_C8", .size = 256,
		.a40_high_off = 160, .a40_low_off = 16, .n_a40 = 32,
		.a_off = 144, .n_a = 4, .first_a = 32,
		.b_off = 192, .n_b = 8,
		.c_off = 224, .n_c = 8, .oa_type = DRM_XE_OA_FMT_TYPE_OAG,
		.counter_select = 5,
	},
};

static struct oa_format dg2_oa_formats[XE_OA_FORMAT_MAX] = {
	[XE_OAR_FORMAT_A32u40_A4u32_B8_C8] = {
		"A32u40_A4u32_B8_C8", .size = 256,
		.a40_high_off = 160, .a40_low_off = 16, .n_a40 = 32,
		.a_off = 144, .n_a = 4, .first_a = 32,
		.b_off = 192, .n_b = 8,
		.c_off = 224, .n_c = 8, .oa_type = DRM_XE_OA_FMT_TYPE_OAR,
		.counter_select = 5,
	},
	/* This format has A36 and A37 interleaved with high bytes of some A
	 * counters, so we will accumulate only subset of counters.
	 */
	[XE_OA_FORMAT_A24u40_A14u32_B8_C8] = {
		"A24u40_A14u32_B8_C8", .size = 256,
		/* u40: A4 - A23 */
		.a40_high_off = 160, .a40_low_off = 16, .n_a40 = 20, .first_a40 = 4,
		/* u32: A0 - A3 */
		.a_off = 16, .n_a = 4,
		.b_off = 192, .n_b = 8,
		.c_off = 224, .n_c = 8, .oa_type = DRM_XE_OA_FMT_TYPE_OAG,
		.counter_select = 5,
	},
	/* This format has 24 u64 counters ranging from A0 - A35. Until we come
	 * up with a better mechanism to define missing counters, we will use a
	 * subset of counters that are indexed by one-increments - A28 - A35.
	 */
	[XE_OAC_FORMAT_A24u64_B8_C8] = {
		"OAC_A24u64_B8_C8", .size = 320,
		.a64_off = 160, .n_a64 = 8,
		.b_off = 224, .n_b = 8,
		.c_off = 256, .n_c = 8, .oa_type = DRM_XE_OA_FMT_TYPE_OAC,
		.report_hdr_64bit = true,
		.counter_select = 1, },
};

static struct oa_format mtl_oa_formats[XE_OA_FORMAT_MAX] = {
	[XE_OAR_FORMAT_A32u40_A4u32_B8_C8] = {
		"A32u40_A4u32_B8_C8", .size = 256,
		.a40_high_off = 160, .a40_low_off = 16, .n_a40 = 32,
		.a_off = 144, .n_a = 4, .first_a = 32,
		.b_off = 192, .n_b = 8,
		.c_off = 224, .n_c = 8, .oa_type = DRM_XE_OA_FMT_TYPE_OAR,
		.counter_select = 5,
	},
	/* This format has A36 and A37 interleaved with high bytes of some A
	 * counters, so we will accumulate only subset of counters.
	 */
	[XE_OA_FORMAT_A24u40_A14u32_B8_C8] = {
		"A24u40_A14u32_B8_C8", .size = 256,
		/* u40: A4 - A23 */
		.a40_high_off = 160, .a40_low_off = 16, .n_a40 = 20, .first_a40 = 4,
		/* u32: A0 - A3 */
		.a_off = 16, .n_a = 4,
		.b_off = 192, .n_b = 8,
		.c_off = 224, .n_c = 8, .oa_type = DRM_XE_OA_FMT_TYPE_OAG,
		.counter_select = 5,
	},

	/* Treat MPEC countes as A counters for now */
	[XE_OAM_FORMAT_MPEC8u64_B8_C8] = {
		"MPEC8u64_B8_C8", .size = 192,
		.a64_off = 32, .n_a64 = 8,
		.b_off = 96, .n_b = 8,
		.c_off = 128, .n_c = 8, .oa_type = DRM_XE_OA_FMT_TYPE_OAM_MPEC,
		.report_hdr_64bit = true,
		.counter_select = 1,
	},
	[XE_OAM_FORMAT_MPEC8u32_B8_C8] = {
		"MPEC8u32_B8_C8", .size = 128,
		.a_off = 32, .n_a = 8,
		.b_off = 64, .n_b = 8,
		.c_off = 96, .n_c = 8, .oa_type = DRM_XE_OA_FMT_TYPE_OAM_MPEC,
		.report_hdr_64bit = true,
		.counter_select = 2,
	},
	/* This format has 24 u64 counters ranging from A0 - A35. Until we come
	 * up with a better mechanism to define missing counters, we will use a
	 * subset of counters that are indexed by one-increments - A28 - A35.
	 */
	[XE_OAC_FORMAT_A24u64_B8_C8] = {
		"OAC_A24u64_B8_C8", .size = 320,
		.a64_off = 160, .n_a64 = 8,
		.b_off = 224, .n_b = 8,
		.c_off = 256, .n_c = 8, .oa_type = DRM_XE_OA_FMT_TYPE_OAC,
		.report_hdr_64bit = true,
		.counter_select = 1, },
};

static struct oa_format lnl_oa_formats[XE_OA_FORMAT_MAX] = {
	[XE_OA_FORMAT_PEC64u64] = {
		"PEC64u64", .size = 576,
		.oa_type = DRM_XE_OA_FMT_TYPE_PEC,
		.report_hdr_64bit = true,
		.counter_select = 1,
		.counter_size = 1,
		.bc_report = 0 },
	[XE_OA_FORMAT_PEC64u64_B8_C8] = {
		"PEC64u64_B8_C8", .size = 640,
		.oa_type = DRM_XE_OA_FMT_TYPE_PEC,
		.report_hdr_64bit = true,
		.counter_select = 1,
		.counter_size = 1,
		.bc_report = 1 },
	[XE_OA_FORMAT_PEC64u32] = {
		"PEC64u32", .size = 320,
		.oa_type = DRM_XE_OA_FMT_TYPE_PEC,
		.report_hdr_64bit = true,
		.counter_select = 1,
		.counter_size = 0,
		.bc_report = 0 },
	[XE_OA_FORMAT_PEC32u64_G1] = {
		"PEC32u64_G1", .size = 320,
		.oa_type = DRM_XE_OA_FMT_TYPE_PEC,
		.report_hdr_64bit = true,
		.counter_select = 5,
		.counter_size = 1,
		.bc_report = 0 },
	[XE_OA_FORMAT_PEC32u32_G1] = {
		"PEC32u32_G1", .size = 192,
		.oa_type = DRM_XE_OA_FMT_TYPE_PEC,
		.report_hdr_64bit = true,
		.counter_select = 5,
		.counter_size = 0,
		.bc_report = 0 },
	[XE_OA_FORMAT_PEC32u64_G2] = {
		"PEC32u64_G2", .size = 320,
		.oa_type = DRM_XE_OA_FMT_TYPE_PEC,
		.report_hdr_64bit = true,
		.counter_select = 6,
		.counter_size = 1,
		.bc_report = 0 },
	[XE_OA_FORMAT_PEC32u32_G2] = {
		"PEC32u64_G2", .size = 192,
		.oa_type = DRM_XE_OA_FMT_TYPE_PEC,
		.report_hdr_64bit = true,
		.counter_select = 6,
		.counter_size = 0,
		.bc_report = 0 },
	[XE_OA_FORMAT_PEC36u64_G1_32_G2_4] = {
		"PEC36u64_G1_32_G2_4", .size = 320,
		.oa_type = DRM_XE_OA_FMT_TYPE_PEC,
		.report_hdr_64bit = true,
		.counter_select = 3,
		.counter_size = 1,
		.bc_report = 0 },
	[XE_OA_FORMAT_PEC36u64_G1_4_G2_32] = {
		"PEC36u64_G1_4_G2_32_G2", .size = 320,
		.oa_type = DRM_XE_OA_FMT_TYPE_PEC,
		.report_hdr_64bit = true,
		.counter_select = 4,
		.counter_size = 1,
		.bc_report = 0 },
};

static int drm_fd = -1;
static int sysfs = -1;
static int pm_fd = -1;
static int stream_fd = -1;
static uint32_t devid;

struct drm_xe_engine_class_instance default_hwe;

static struct intel_xe_perf *intel_xe_perf;
static uint64_t oa_exp_1_millisec;
struct intel_mmio_data mmio_data;
static igt_render_copyfunc_t render_copy;

static struct intel_xe_perf_metric_set *metric_set(const struct drm_xe_engine_class_instance *hwe)
{
	const char *test_set_name = NULL;
	struct intel_xe_perf_metric_set *metric_set_iter;
	struct intel_xe_perf_metric_set *test_set = NULL;

	if (hwe->engine_class == DRM_XE_ENGINE_CLASS_RENDER ||
	    hwe->engine_class == DRM_XE_ENGINE_CLASS_COMPUTE)
		test_set_name = "TestOa";
	else if ((hwe->engine_class == DRM_XE_ENGINE_CLASS_VIDEO_DECODE ||
		  hwe->engine_class == DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE) &&
		 HAS_OAM(devid))
		test_set_name = "MediaSet1";
	else
		igt_assert(!"reached");

	igt_list_for_each_entry(metric_set_iter, &intel_xe_perf->metric_sets, link) {
		if (strcmp(metric_set_iter->symbol_name, test_set_name) == 0) {
			test_set = metric_set_iter;
			break;
		}
	}

	igt_assert(test_set);

	/*
	 * configuration was loaded in init_sys_info() ->
	 * intel_xe_perf_load_perf_configs(), and test_set->perf_oa_metrics_set
	 * should point to metric id returned by the config add ioctl. 0 is
	 * invalid.
	 */
	igt_assert_neq_u64(test_set->perf_oa_metrics_set, 0);

	igt_debug("engine %d:%d - %s metric set UUID = %s\n",
		  hwe->engine_class,
		  hwe->engine_instance,
		  test_set->symbol_name,
		  test_set->hw_config_guid);

	return test_set;
}
#define default_test_set metric_set(&default_hwe)

static void set_fd_flags(int fd, int flags)
{
	int old = fcntl(fd, F_GETFL, 0);

	igt_assert_lte(0, old);
	igt_assert_eq(0, fcntl(fd, F_SETFL, old | flags));
}

static u32 get_stream_status(int fd)
{
	struct drm_xe_oa_stream_status status;

	do_ioctl(fd, DRM_XE_OBSERVATION_IOCTL_STATUS, &status);

	return status.oa_status;
}

static void
dump_report(const uint32_t *report, uint32_t size, const char *message) {
	uint32_t i;
	igt_debug("%s\n", message);
	for (i = 0; i < size; i += 4) {
		igt_debug("%08x %08x %08x %08x\n",
				report[i],
				report[i + 1],
				report[i + 2],
				report[i + 3]);
	}
}

static struct oa_format
get_oa_format(enum intel_xe_oa_format_name format)
{
	if (IS_DG2(devid))
		return dg2_oa_formats[format];
	else if (IS_METEORLAKE(devid))
		return mtl_oa_formats[format];
	else if (intel_graphics_ver(devid) >= IP_VER(20, 0))
		return lnl_oa_formats[format];
	else
		return gen12_oa_formats[format];
}

static u64 oa_format_fields(u64 name)
{
#define FIELD_PREP_ULL(_mask, _val) \
	(((_val) << (__builtin_ffsll(_mask) - 1)) & (_mask))

	struct oa_format f = get_oa_format(name);

	/* 0 format name is invalid */
	if (!name)
		memset(&f, 0xff, sizeof(f));

	return FIELD_PREP_ULL(DRM_XE_OA_FORMAT_MASK_FMT_TYPE, (u64)f.oa_type) |
		FIELD_PREP_ULL(DRM_XE_OA_FORMAT_MASK_COUNTER_SEL, (u64)f.counter_select) |
		FIELD_PREP_ULL(DRM_XE_OA_FORMAT_MASK_COUNTER_SIZE, (u64)f.counter_size) |
		FIELD_PREP_ULL(DRM_XE_OA_FORMAT_MASK_BC_REPORT, (u64)f.bc_report);
}
#define __ff oa_format_fields

static void
__perf_close(int fd)
{
	close(fd);
	stream_fd = -1;

	if (pm_fd >= 0) {
		close(pm_fd);
		pm_fd = -1;
	}
}

static int
__perf_open(int fd, struct intel_xe_oa_open_prop *param, bool prevent_pm)
{
	int ret;
	int32_t pm_value = 0;

	if (stream_fd >= 0)
		__perf_close(stream_fd);
	if (pm_fd >= 0) {
		close(pm_fd);
		pm_fd = -1;
	}

	ret = intel_xe_perf_ioctl(fd, DRM_XE_OBSERVATION_OP_STREAM_OPEN, param);

	igt_assert(ret >= 0);
	errno = 0;

	if (prevent_pm) {
		pm_fd = open("/dev/cpu_dma_latency", O_RDWR);
		igt_assert(pm_fd >= 0);

		igt_assert_eq(write(pm_fd, &pm_value, sizeof(pm_value)), sizeof(pm_value));
	}

	return ret;
}

static uint64_t
read_u64_file(const char *path)
{
	FILE *f;
	uint64_t val;

	f = fopen(path, "r");
	igt_assert(f);

	igt_assert_eq(fscanf(f, "%"PRIu64, &val), 1);

	fclose(f);

	return val;
}

static void
write_u64_file(const char *path, uint64_t val)
{
	FILE *f;

	f = fopen(path, "w");
	igt_assert(f);

	igt_assert(fprintf(f, "%"PRIu64, val) > 0);

	fclose(f);
}

static uint32_t
report_reason(const uint32_t *report)
{
	return ((report[0] >> OAREPORT_REASON_SHIFT) &
		OAREPORT_REASON_MASK);
}

static uint64_t
oa_timestamp(const uint32_t *report, enum intel_xe_oa_format_name format)
{
	struct oa_format fmt = get_oa_format(format);

	return fmt.report_hdr_64bit ? *(uint64_t *)&report[2] : report[1];
}

static uint64_t
timebase_scale(uint64_t delta)
{
	return (delta * NSEC_PER_SEC) / intel_xe_perf->devinfo.timestamp_frequency;
}

/* Returns: the largest OA exponent that will still result in a sampling period
 * less than or equal to the given @period.
 */
static int
max_oa_exponent_for_period_lte(uint64_t period)
{
	/* NB: timebase_scale() takes a uint64_t and an exponent of 30
	 * would already represent a period of ~3 minutes so there's
	 * really no need to consider higher exponents.
	 */
	for (int i = 0; i < 30; i++) {
		uint64_t oa_period = timebase_scale(2 << i);

		if (oa_period > period)
			return max(0, i - 1);
	}

	igt_assert(!"reached");
	return -1;
}

static bool
oa_report_is_periodic(uint32_t oa_exponent, const uint32_t *report)
{
	if (report_reason(report) & OAREPORT_REASON_TIMER)
		return true;

	return false;
}

static bool
init_sys_info(void)
{
	igt_assert_neq(devid, 0);

	intel_xe_perf = intel_xe_perf_for_fd(drm_fd, 0);
	igt_require(intel_xe_perf);

	igt_debug("n_eu_slices: %"PRIu64"\n", intel_xe_perf->devinfo.n_eu_slices);
	igt_debug("n_eu_sub_slices: %"PRIu64"\n", intel_xe_perf->devinfo.n_eu_sub_slices);
	igt_debug("n_eus: %"PRIu64"\n", intel_xe_perf->devinfo.n_eus);
	igt_debug("timestamp_frequency = %"PRIu64"\n",
		  intel_xe_perf->devinfo.timestamp_frequency);
	igt_assert_neq(intel_xe_perf->devinfo.timestamp_frequency, 0);

	intel_xe_perf_load_perf_configs(intel_xe_perf, drm_fd);

	oa_exp_1_millisec = max_oa_exponent_for_period_lte(1000000);

	return true;
}

/**
 * SUBTEST: non-system-wide-paranoid
 * Description: CAP_SYS_ADMIN is required to open system wide metrics, unless
 *		sysctl parameter dev.xe.observation_paranoid == 0
 */
static void test_system_wide_paranoid(void)
{
	igt_fork(child, 1) {
		uint64_t properties[] = {
			DRM_XE_OA_PROPERTY_OA_UNIT_ID, 0,

			/* Include OA reports in samples */
			DRM_XE_OA_PROPERTY_SAMPLE_OA, true,

			/* OA unit configuration */
			DRM_XE_OA_PROPERTY_OA_METRIC_SET, default_test_set->perf_oa_metrics_set,
			DRM_XE_OA_PROPERTY_OA_FORMAT, __ff(default_test_set->perf_oa_format),
			DRM_XE_OA_PROPERTY_OA_PERIOD_EXPONENT, oa_exp_1_millisec,
		};
		struct intel_xe_oa_open_prop param = {
			.num_properties = ARRAY_SIZE(properties) / 2,
			.properties_ptr = to_user_pointer(properties),
		};

		write_u64_file("/proc/sys/dev/xe/observation_paranoid", 1);

		igt_drop_root();

		intel_xe_perf_ioctl_err(drm_fd, DRM_XE_OBSERVATION_OP_STREAM_OPEN, &param, EACCES);
	}

	igt_waitchildren();

	igt_fork(child, 1) {
		uint64_t properties[] = {
			DRM_XE_OA_PROPERTY_OA_UNIT_ID, 0,

			/* Include OA reports in samples */
			DRM_XE_OA_PROPERTY_SAMPLE_OA, true,

			/* OA unit configuration */
			DRM_XE_OA_PROPERTY_OA_METRIC_SET, default_test_set->perf_oa_metrics_set,
			DRM_XE_OA_PROPERTY_OA_FORMAT, __ff(default_test_set->perf_oa_format),
			DRM_XE_OA_PROPERTY_OA_PERIOD_EXPONENT, oa_exp_1_millisec,
		};
		struct intel_xe_oa_open_prop param = {
			.num_properties = ARRAY_SIZE(properties) / 2,
			.properties_ptr = to_user_pointer(properties),
		};
		write_u64_file("/proc/sys/dev/xe/observation_paranoid", 0);

		igt_drop_root();

		stream_fd = __perf_open(drm_fd, &param, false);
		__perf_close(stream_fd);
	}

	igt_waitchildren();

	/* leave in paranoid state */
	write_u64_file("/proc/sys/dev/xe/observation_paranoid", 1);
}

/**
 * SUBTEST: invalid-oa-metric-set-id
 * Description: Test behavior for invalid metric set id's
 */
static void test_invalid_oa_metric_set_id(void)
{
	uint64_t properties[] = {
		DRM_XE_OA_PROPERTY_OA_UNIT_ID, 0,

		/* Include OA reports in samples */
		DRM_XE_OA_PROPERTY_SAMPLE_OA, true,

		/* OA unit configuration */
		DRM_XE_OA_PROPERTY_OA_FORMAT, __ff(default_test_set->perf_oa_format),
		DRM_XE_OA_PROPERTY_OA_PERIOD_EXPONENT, oa_exp_1_millisec,
		DRM_XE_OA_PROPERTY_OA_METRIC_SET, UINT64_MAX,
	};
	struct intel_xe_oa_open_prop param = {
		.num_properties = ARRAY_SIZE(properties) / 2,
		.properties_ptr = to_user_pointer(properties),
	};

	intel_xe_perf_ioctl_err(drm_fd, DRM_XE_OBSERVATION_OP_STREAM_OPEN, &param, EINVAL);

	properties[ARRAY_SIZE(properties) - 1] = 0; /* ID 0 is also be reserved as invalid */
	intel_xe_perf_ioctl_err(drm_fd, DRM_XE_OBSERVATION_OP_STREAM_OPEN, &param, EINVAL);

	/* Check that we aren't just seeing false positives... */
	properties[ARRAY_SIZE(properties) - 1] = default_test_set->perf_oa_metrics_set;
	stream_fd = __perf_open(drm_fd, &param, false);
	__perf_close(stream_fd);

	/* There's no valid default OA metric set ID... */
	param.num_properties--;
	intel_xe_perf_ioctl_err(drm_fd, DRM_XE_OBSERVATION_OP_STREAM_OPEN, &param, EINVAL);
}

/**
 * SUBTEST: invalid-oa-format-id
 * Description: Test behavior for invalid OA format fields
 */
static void test_invalid_oa_format_id(void)
{
	uint64_t properties[] = {
		DRM_XE_OA_PROPERTY_OA_UNIT_ID, 0,

		/* Include OA reports in samples */
		DRM_XE_OA_PROPERTY_SAMPLE_OA, true,

		/* OA unit configuration */
		DRM_XE_OA_PROPERTY_OA_METRIC_SET, default_test_set->perf_oa_metrics_set,
		DRM_XE_OA_PROPERTY_OA_PERIOD_EXPONENT, oa_exp_1_millisec,
		DRM_XE_OA_PROPERTY_OA_FORMAT, UINT64_MAX, /* No __ff() here */
	};
	struct intel_xe_oa_open_prop param = {
		.num_properties = ARRAY_SIZE(properties) / 2,
		.properties_ptr = to_user_pointer(properties),
	};

	intel_xe_perf_ioctl_err(drm_fd, DRM_XE_OBSERVATION_OP_STREAM_OPEN, &param, EINVAL);

	properties[ARRAY_SIZE(properties) - 1] = __ff(0); /* ID 0 is also be reserved as invalid */
	intel_xe_perf_ioctl_err(drm_fd, DRM_XE_OBSERVATION_OP_STREAM_OPEN, &param, EINVAL);

	/* Check that we aren't just seeing false positives... */
	properties[ARRAY_SIZE(properties) - 1] = __ff(default_test_set->perf_oa_format);
	stream_fd = __perf_open(drm_fd, &param, false);
	__perf_close(stream_fd);
	/* There's no valid default OA format... */
	param.num_properties--;
	intel_xe_perf_ioctl_err(drm_fd, DRM_XE_OBSERVATION_OP_STREAM_OPEN, &param, EINVAL);
}

/**
 * SUBTEST: missing-sample-flags
 * Description: Test behavior for no SAMPLE_OA and no EXEC_QUEUE_ID
 */
static void test_missing_sample_flags(void)
{
	uint64_t properties[] = {
		DRM_XE_OA_PROPERTY_OA_UNIT_ID, 0,

		/* No _PROP_SAMPLE_xyz flags */

		/* OA unit configuration */
		DRM_XE_OA_PROPERTY_OA_METRIC_SET, default_test_set->perf_oa_metrics_set,
		DRM_XE_OA_PROPERTY_OA_PERIOD_EXPONENT, oa_exp_1_millisec,
		DRM_XE_OA_PROPERTY_OA_FORMAT, __ff(default_test_set->perf_oa_format),
	};
	struct intel_xe_oa_open_prop param = {
		.num_properties = ARRAY_SIZE(properties) / 2,
		.properties_ptr = to_user_pointer(properties),
	};

	intel_xe_perf_ioctl_err(drm_fd, DRM_XE_OBSERVATION_OP_STREAM_OPEN, &param, EINVAL);
}

static void
read_2_oa_reports(int format_id,
		  int exponent,
		  uint32_t *oa_report0,
		  uint32_t *oa_report1,
		  bool timer_only)
{
	size_t format_size = get_oa_format(format_id).size;
	uint32_t exponent_mask = (1 << (exponent + 1)) - 1;

	/* Note: we allocate a large buffer so that each read() iteration
	 * should scrape *all* pending records.
	 *
	 * The largest buffer the OA unit supports is 16MB.
	 *
	 * Being sure we are fetching all buffered reports allows us to
	 * potentially throw away / skip all reports whenever we see
	 * a _REPORT_LOST notification as a way of being sure are
	 * measurements aren't skewed by a lost report.
	 *
	 * Note: that is is useful for some tests but also not something
	 * applications would be expected to resort to. Lost reports are
	 * somewhat unpredictable but typically don't pose a problem - except
	 * to indicate that the OA unit may be over taxed if lots of reports
	 * are being lost.
	 */
	int max_reports = MAX_OA_BUF_SIZE / format_size;
	int buf_size = format_size * max_reports * 1.5;
	uint8_t *buf = malloc(buf_size);
	int n = 0;

	for (int i = 0; i < 1000; i++) {
		u32 oa_status = 0;
		ssize_t len;

		while ((len = read(stream_fd, buf, buf_size)) < 0 && errno == EINTR)
			;
		if (errno == EIO) {
			oa_status = get_stream_status(stream_fd);
			igt_debug("oa_status %#x\n", oa_status);
			continue;
		}

		igt_assert(len > 0);
		igt_debug("read %d bytes\n", (int)len);

		/* Need at least 2 reports */
		if (len < 2 * format_size)
			continue;

		for (size_t offset = 0; offset < len; offset += format_size) {
			const uint32_t *report = (void *)(buf + offset);

			/* Currently the only test that should ever expect to
			 * see a _BUFFER_LOST error is the buffer_fill test,
			 * otherwise something bad has probably happened...
			 */
			igt_assert(!(oa_status & DRM_XE_OASTATUS_BUFFER_OVERFLOW));

			/* At high sampling frequencies the OA HW might not be
			 * able to cope with all write requests and will notify
			 * us that a report was lost. We restart our read of
			 * two sequential reports due to the timeline blip this
			 * implies
			 */
			if (oa_status & DRM_XE_OASTATUS_REPORT_LOST) {
				igt_debug("read restart: OA trigger collision / report lost\n");
				n = 0;

				/* XXX: break, because we don't know where
				 * within the series of already read reports
				 * there could be a blip from the lost report.
				 */
				break;
			}

			dump_report(report, format_size / 4, "oa-formats");

			igt_debug("read report: reason = %x, timestamp = %"PRIx64", exponent mask=%x\n",
				  report[0], oa_timestamp(report, format_id), exponent_mask);

			/* Don't expect zero for timestamps */
			igt_assert_neq_u64(oa_timestamp(report, format_id), 0);

			if (timer_only) {
				if (!oa_report_is_periodic(exponent, report)) {
					igt_debug("skipping non timer report\n");
					continue;
				}
			}

			if (n++ == 0)
				memcpy(oa_report0, report, format_size);
			else {
				memcpy(oa_report1, report, format_size);
				free(buf);
				return;
			}
		}
	}

	free(buf);

	igt_assert(!"reached");
}

static unsigned read_xe_module_ref(void)
{
	FILE *fp = fopen("/proc/modules", "r");
	char *line = NULL;
	size_t line_buf_size = 0;
	int len = 0;
	unsigned ref_count;
	char mod[8];
	int modn = 3;

	igt_assert(fp);

	strcpy(mod, "xe ");
	while ((len = getline(&line, &line_buf_size, fp)) > 0) {
		if (strncmp(line, mod, modn) == 0) {
			unsigned long mem;
			int ret = sscanf(line + 5, "%lu %u", &mem, &ref_count);
			igt_assert(ret == 2);
			goto done;
		}
	}

	igt_assert(!"reached");

done:
	free(line);
	fclose(fp);
	return ref_count;
}

/**
 * SUBTEST: xe-ref-count
 * Description: Check that an open oa stream holds a reference on the xe module
 */
static void
test_xe_ref_count(void)
{
	uint64_t properties[] = {
		DRM_XE_OA_PROPERTY_OA_UNIT_ID, 0,

		/* Include OA reports in samples */
		DRM_XE_OA_PROPERTY_SAMPLE_OA, true,

		/* OA unit configuration */
		DRM_XE_OA_PROPERTY_OA_METRIC_SET, 0 /* updated below */,
		DRM_XE_OA_PROPERTY_OA_FORMAT, __ff(0), /* update below */
		DRM_XE_OA_PROPERTY_OA_PERIOD_EXPONENT, 0, /* update below */
	};
	struct intel_xe_oa_open_prop param = {
		.num_properties = ARRAY_SIZE(properties) / 2,
		.properties_ptr = to_user_pointer(properties),
	};
	unsigned baseline, ref_count0, ref_count1;
	uint32_t oa_report0[64];
	uint32_t oa_report1[64];

	/* This should be the first test before the first fixture so no drm_fd
	 * should have been opened so far...
	 */
	igt_assert_eq(drm_fd, -1);

	baseline = read_xe_module_ref();
	igt_debug("baseline ref count (drm fd closed) = %u\n", baseline);

	drm_fd = __drm_open_driver(DRIVER_XE);
	if (is_xe_device(drm_fd))
		xe_device_get(drm_fd);
	devid = intel_get_drm_devid(drm_fd);
	sysfs = igt_sysfs_open(drm_fd);

	/* Note: these global variables are only initialized after calling
	 * init_sys_info()...
	 */
	igt_require(init_sys_info());
	properties[5] = default_test_set->perf_oa_metrics_set;
	properties[7] = __ff(default_test_set->perf_oa_format);
	properties[9] = oa_exp_1_millisec;

	ref_count0 = read_xe_module_ref();
	igt_debug("initial ref count with drm_fd open = %u\n", ref_count0);

	stream_fd = __perf_open(drm_fd, &param, false);
        set_fd_flags(stream_fd, O_CLOEXEC);
	ref_count1 = read_xe_module_ref();
	igt_debug("ref count after opening oa stream = %u\n", ref_count1);

	drm_close_driver(drm_fd);
	close(sysfs);
	drm_fd = -1;
	sysfs = -1;
	ref_count0 = read_xe_module_ref();
	igt_debug("ref count after closing drm fd = %u\n", ref_count0);

	read_2_oa_reports(default_test_set->perf_oa_format,
			  oa_exp_1_millisec,
			  oa_report0,
			  oa_report1,
			  false); /* not just timer reports */

	__perf_close(stream_fd);
	ref_count0 = read_xe_module_ref();
	igt_debug("ref count after closing oa stream fd = %u\n", ref_count0);
}

/**
 * SUBTEST: sysctl-defaults
 * Description: Test that observation_paranoid sysctl exists
 */
static void
test_sysctl_defaults(void)
{
	int paranoid = read_u64_file("/proc/sys/dev/xe/observation_paranoid");

	igt_assert_eq(paranoid, 1);
}

igt_main
{
	igt_fixture {
		struct stat sb;

		/*
		 * Prior tests may have unloaded the module or failed while
		 * loading/unloading the module. Load xe here before we
		 * stat the files.
		 */
		drm_load_module(DRIVER_XE);
		srandom(time(NULL));
		igt_require(!stat("/proc/sys/dev/xe/observation_paranoid", &sb));
	}

	igt_subtest("xe-ref-count")
		test_xe_ref_count();

	igt_subtest("sysctl-defaults")
		test_sysctl_defaults();

	igt_fixture {
		/* We expect that the ref count test before these fixtures
		 * should have closed drm_fd...
		 */
		igt_assert_eq(drm_fd, -1);

		drm_fd = drm_open_driver(DRIVER_XE);
		xe_device_get(drm_fd);

		devid = intel_get_drm_devid(drm_fd);
		sysfs = igt_sysfs_open(drm_fd);

		igt_require(init_sys_info());

		write_u64_file("/proc/sys/dev/xe/observation_paranoid", 1);

		render_copy = igt_get_render_copyfunc(devid);

		/* Currently only run on Xe2+ */
		igt_require(intel_graphics_ver(devid) >= IP_VER(20, 0));
	}

	igt_subtest("non-system-wide-paranoid")
		test_system_wide_paranoid();

	igt_subtest("invalid-oa-metric-set-id")
		test_invalid_oa_metric_set_id();

	igt_subtest("invalid-oa-format-id")
		test_invalid_oa_format_id();

	igt_subtest("missing-sample-flags")
		test_missing_sample_flags();

	igt_fixture {
		/* leave sysctl options in their default state... */
		write_u64_file("/proc/sys/dev/xe/observation_paranoid", 1);

		if (intel_xe_perf)
			intel_xe_perf_free(intel_xe_perf);

		drm_close_driver(drm_fd);
	}
}
