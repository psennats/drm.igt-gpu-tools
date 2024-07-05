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

static struct drm_xe_engine_class_instance *oa_unit_engine(int fd, int n)
{
	struct drm_xe_query_oa_units *qoa = xe_oa_units(fd);
	struct drm_xe_engine_class_instance *hwe = NULL;
	struct drm_xe_oa_unit *oau;
	u8 *poau;

	poau = (u8 *)&qoa->oa_units[0];
	for (int i = 0; i < qoa->num_oa_units; i++) {
		oau = (struct drm_xe_oa_unit *)poau;

		if (i == n) {
			hwe = &oau->eci[random() % oau->num_engines];
			break;
		}
		poau += sizeof(*oau) + oau->num_engines * sizeof(oau->eci[0]);
	}

	return hwe;
}

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

static uint64_t
read_report_ticks(const uint32_t *report, enum intel_xe_oa_format_name format)
{

	struct oa_format fmt = get_oa_format(format);

	return fmt.report_hdr_64bit ? *(uint64_t *)&report[6] : report[3];
}

/*
 * t0 is a value sampled before t1. width is number of bits used to represent
 * t0/t1. Normally t1 is greater than t0. In cases where t1 < t0 use this
 * helper. Since the size of t1/t0 is already 64 bits, no special handling is
 * needed for width = 64.
 */
static uint64_t
elapsed_delta(uint64_t t1, uint64_t t0, uint32_t width)
{
	uint32_t max_bits = sizeof(t1) * 8;

	igt_assert(width <= max_bits);

	if (t1 < t0 && width != max_bits)
		return ((1ULL << width) - t0) + t1;

	return t1 - t0;
}

static uint64_t
oa_tick_delta(const uint32_t *report1,
	      const uint32_t *report0,
	      enum intel_xe_oa_format_name format)
{
	return elapsed_delta(read_report_ticks(report1, format),
			     read_report_ticks(report0, format), 32);
}

static void
read_report_clock_ratios(const uint32_t *report,
			      uint32_t *slice_freq_mhz,
			      uint32_t *unslice_freq_mhz)
{
	uint32_t unslice_freq = report[0] & 0x1ff;
	uint32_t slice_freq_low = (report[0] >> 25) & 0x7f;
	uint32_t slice_freq_high = (report[0] >> 9) & 0x3;
	uint32_t slice_freq = slice_freq_low | (slice_freq_high << 7);

	*slice_freq_mhz = (slice_freq * 16666) / 1000;
	*unslice_freq_mhz = (unslice_freq * 16666) / 1000;
}

static uint32_t
report_reason(const uint32_t *report)
{
	return ((report[0] >> OAREPORT_REASON_SHIFT) &
		OAREPORT_REASON_MASK);
}

static const char *
read_report_reason(const uint32_t *report)
{
	uint32_t reason = report_reason(report);

	if (reason & (1<<0))
		return "timer";
	else if (reason & (1<<1))
	      return "internal trigger 1";
	else if (reason & (1<<2))
	      return "internal trigger 2";
	else if (reason & (1<<3))
	      return "context switch";
	else if (reason & (1<<4))
	      return "GO 1->0 transition (enter RC6)";
	else if (reason & (1<<5))
		return "[un]slice clock ratio change";
	else
		return "unknown";
}

static uint64_t
oa_timestamp(const uint32_t *report, enum intel_xe_oa_format_name format)
{
	struct oa_format fmt = get_oa_format(format);

	return fmt.report_hdr_64bit ? *(uint64_t *)&report[2] : report[1];
}

static uint64_t
oa_timestamp_delta(const uint32_t *report1,
		   const uint32_t *report0,
		   enum intel_xe_oa_format_name format)
{
	uint32_t width = intel_graphics_ver(devid) >= IP_VER(12, 55) ? 56 : 32;

	return elapsed_delta(oa_timestamp(report1, format),
			     oa_timestamp(report0, format), width);
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

static uint64_t
oa_exponent_to_ns(int exponent)
{
       return 1000000000ULL * (2ULL << exponent) / intel_xe_perf->devinfo.timestamp_frequency;
}

static bool
oa_report_ctx_is_valid(uint32_t *report)
{
	return report[0] & (1ul << 16);
}

static uint32_t
oa_report_get_ctx_id(uint32_t *report)
{
	if (!oa_report_ctx_is_valid(report))
		return 0xffffffff;
	return report[2];
}

static void *buf_map(int fd, struct intel_buf *buf, bool write)
{
	void *p;

	if (is_xe_device(fd)) {
		buf->ptr = xe_bo_map(fd, buf->handle, buf->surface[0].size);
		p = buf->ptr;
	} else {
		if (gem_has_llc(fd))
			p = intel_buf_cpu_map(buf, write);
		else
			p = intel_buf_device_map(buf, write);
	}
	return p;
}

static void
scratch_buf_memset(struct intel_buf *buf, int width, int height, uint32_t color)
{
	buf_map(buf_ops_get_fd(buf->bops), buf, true);

	for (int i = 0; i < width * height; i++)
		buf->ptr[i] = color;

	intel_buf_unmap(buf);
}

static void
scratch_buf_init(struct buf_ops *bops,
		 struct intel_buf *buf,
		 int width, int height,
		 uint32_t color)
{
	intel_buf_init(bops, buf, width, height, 32, 0,
		       I915_TILING_NONE, I915_COMPRESSION_NONE);
	scratch_buf_memset(buf, width, height, color);
}

static bool
oa_report_is_periodic(uint32_t oa_exponent, const uint32_t *report)
{
	if (report_reason(report) & OAREPORT_REASON_TIMER)
		return true;

	return false;
}

static uint64_t
read_40bit_a_counter(const uint32_t *report,
			  enum intel_xe_oa_format_name fmt, int a_id)
{
	struct oa_format format = get_oa_format(fmt);
	uint8_t *a40_high = (((uint8_t *)report) + format.a40_high_off);
	uint32_t *a40_low = (uint32_t *)(((uint8_t *)report) +
					 format.a40_low_off);
	uint64_t high = (uint64_t)(a40_high[a_id]) << 32;

	return a40_low[a_id] | high;
}

static uint64_t
xehpsdv_read_64bit_a_counter(const uint32_t *report, enum intel_xe_oa_format_name fmt, int a_id)
{
	struct oa_format format = get_oa_format(fmt);
	uint64_t *a64 = (uint64_t *)(((uint8_t *)report) + format.a64_off);

	return a64[a_id];
}

static uint64_t
get_40bit_a_delta(uint64_t value0, uint64_t value1)
{
	if (value0 > value1)
		return (1ULL << 40) + value1 - value0;
	else
		return value1 - value0;
}

static void
accumulate_uint64(int a_index,
		  const uint32_t *report0,
		  const uint32_t *report1,
		  enum intel_xe_oa_format_name format,
		  uint64_t *delta)
{
	uint64_t value0 = xehpsdv_read_64bit_a_counter(report0, format, a_index),
		 value1 = xehpsdv_read_64bit_a_counter(report1, format, a_index);

	*delta += (value1 - value0);
}

/* The TestOa metric set is designed so */
static void
sanity_check_reports(const uint32_t *oa_report0, const uint32_t *oa_report1,
		     enum intel_xe_oa_format_name fmt)
{
	struct oa_format format = get_oa_format(fmt);
	uint64_t time_delta = timebase_scale(oa_timestamp_delta(oa_report1,
								oa_report0,
								fmt));
	uint64_t clock_delta = oa_tick_delta(oa_report1, oa_report0, fmt);
	uint64_t max_delta;
	uint64_t freq;
	uint32_t *rpt0_b = (uint32_t *)(((uint8_t *)oa_report0) +
					format.b_off);
	uint32_t *rpt1_b = (uint32_t *)(((uint8_t *)oa_report1) +
					format.b_off);
	uint32_t b;
	uint32_t ref;

	igt_debug("report type: %s->%s\n",
		  read_report_reason(oa_report0),
		  read_report_reason(oa_report1));

	freq = time_delta ? (clock_delta * 1000) / time_delta : 0;
	igt_debug("freq = %"PRIu64"\n", freq);

	igt_debug("clock delta = %"PRIu64"\n", clock_delta);

	max_delta = clock_delta * intel_xe_perf->devinfo.n_eus;

	/* Gen8+ has some 40bit A counters... */
	for (int j = format.first_a40; j < format.n_a40 + format.first_a40; j++) {
		uint64_t value0 = read_40bit_a_counter(oa_report0, fmt, j);
		uint64_t value1 = read_40bit_a_counter(oa_report1, fmt, j);
		uint64_t delta = get_40bit_a_delta(value0, value1);

		igt_debug("A40_%d: delta = %"PRIu64"\n", j, delta);
		igt_assert_f(delta <= max_delta,
			     "A40_%d: delta = %"PRIu64", max_delta = %"PRIu64"\n",
			     j, delta, max_delta);
	}

	for (int j = 0; j < format.n_a64; j++) {
		uint64_t delta = 0;

		accumulate_uint64(j, oa_report0, oa_report1, fmt, &delta);

		igt_debug("A64_%d: delta = %"PRIu64"\n", format.first_a + j, delta);
		igt_assert_f(delta <= max_delta,
			     "A64_%d: delta = %"PRIu64", max_delta = %"PRIu64"\n",
			     format.first_a + j, delta, max_delta);
	}

	for (int j = 0; j < format.n_a; j++) {
		uint32_t *a0 = (uint32_t *)(((uint8_t *)oa_report0) +
					    format.a_off);
		uint32_t *a1 = (uint32_t *)(((uint8_t *)oa_report1) +
					    format.a_off);
		int a_id = format.first_a + j;
		uint32_t delta = a1[j] - a0[j];

		igt_debug("A%d: delta = %"PRIu32"\n", a_id, delta);
		igt_assert_f(delta <= max_delta,
			     "A%d: delta = %"PRIu32", max_delta = %"PRIu64"\n",
			     a_id, delta, max_delta);
	}

	/* The TestOa metric set defines all B counters to be a
	 * multiple of the gpu clock
	 */
	if (format.n_b && (format.oa_type == DRM_XE_OA_FMT_TYPE_OAG || format.oa_type == DRM_XE_OA_FMT_TYPE_OAR)) {
		if (clock_delta > 0) {
			b = rpt1_b[0] - rpt0_b[0];
			igt_debug("B0: delta = %"PRIu32"\n", b);
			igt_assert_eq(b, 0);

			b = rpt1_b[1] - rpt0_b[1];
			igt_debug("B1: delta = %"PRIu32"\n", b);
			igt_assert_eq(b, clock_delta);

			b = rpt1_b[2] - rpt0_b[2];
			igt_debug("B2: delta = %"PRIu32"\n", b);
			igt_assert_eq(b, clock_delta);

			b = rpt1_b[3] - rpt0_b[3];
			ref = clock_delta / 2;
			igt_debug("B3: delta = %"PRIu32"\n", b);
			igt_assert(b >= ref - 1 && b <= ref + 1);

			b = rpt1_b[4] - rpt0_b[4];
			ref = clock_delta / 3;
			igt_debug("B4: delta = %"PRIu32"\n", b);
			igt_assert(b >= ref - 1 && b <= ref + 1);

			b = rpt1_b[5] - rpt0_b[5];
			ref = clock_delta / 3;
			igt_debug("B5: delta = %"PRIu32"\n", b);
			igt_assert(b >= ref - 1 && b <= ref + 1);

			b = rpt1_b[6] - rpt0_b[6];
			ref = clock_delta / 6;
			igt_debug("B6: delta = %"PRIu32"\n", b);
			igt_assert(b >= ref - 1 && b <= ref + 1);

			b = rpt1_b[7] - rpt0_b[7];
			ref = clock_delta * 2 / 3;
			igt_debug("B7: delta = %"PRIu32"\n", b);
			igt_assert(b >= ref - 1 && b <= ref + 1);
		} else {
			for (int j = 0; j < format.n_b; j++) {
				b = rpt1_b[j] - rpt0_b[j];
				igt_debug("B%i: delta = %"PRIu32"\n", j, b);
				igt_assert_eq(b, 0);
			}
		}
	}

	for (int j = 0; j < format.n_c; j++) {
		uint32_t *c0 = (uint32_t *)(((uint8_t *)oa_report0) +
					    format.c_off);
		uint32_t *c1 = (uint32_t *)(((uint8_t *)oa_report1) +
					    format.c_off);
		uint32_t delta = c1[j] - c0[j];

		igt_debug("C%d: delta = %"PRIu32", max_delta=%"PRIu64"\n",
			  j, delta, max_delta);
		igt_assert_f(delta <= max_delta,
			     "C%d: delta = %"PRIu32", max_delta = %"PRIu64"\n",
			     j, delta, max_delta);
	}
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

static void
open_and_read_2_oa_reports(int format_id,
			   int exponent,
			   uint32_t *oa_report0,
			   uint32_t *oa_report1,
			   bool timer_only,
			   const struct drm_xe_engine_class_instance *hwe)
{
	struct intel_xe_perf_metric_set *test_set = metric_set(hwe);
	uint64_t properties[] = {
		DRM_XE_OA_PROPERTY_OA_UNIT_ID, 0,

		/* Include OA reports in samples */
		DRM_XE_OA_PROPERTY_SAMPLE_OA, true,

		/* OA unit configuration */
		DRM_XE_OA_PROPERTY_OA_METRIC_SET, test_set->perf_oa_metrics_set,
		DRM_XE_OA_PROPERTY_OA_FORMAT, __ff(format_id),
		DRM_XE_OA_PROPERTY_OA_PERIOD_EXPONENT, exponent,
		DRM_XE_OA_PROPERTY_OA_ENGINE_INSTANCE, hwe->engine_instance,

	};
	struct intel_xe_oa_open_prop param = {
		.num_properties = ARRAY_SIZE(properties) / 2,
		.properties_ptr = to_user_pointer(properties),
	};

	stream_fd = __perf_open(drm_fd, &param, false);
	set_fd_flags(stream_fd, O_CLOEXEC);

	read_2_oa_reports(format_id, exponent,
			  oa_report0, oa_report1, timer_only);

	__perf_close(stream_fd);
}

static void
print_reports(uint32_t *oa_report0, uint32_t *oa_report1, int fmt)
{
	struct oa_format format = get_oa_format(fmt);
	uint64_t ts0 = oa_timestamp(oa_report0, fmt);
	uint64_t ts1 = oa_timestamp(oa_report1, fmt);

	igt_debug("TIMESTAMP: 1st = %"PRIu64", 2nd = %"PRIu64", delta = %"PRIu64"\n",
		  ts0, ts1, ts1 - ts0);

	{
		uint64_t clock0 = read_report_ticks(oa_report0, fmt);
		uint64_t clock1 = read_report_ticks(oa_report1, fmt);

		igt_debug("CLOCK: 1st = %"PRIu64", 2nd = %"PRIu64", delta = %"PRIu64"\n",
			  clock0, clock1, clock1 - clock0);
	}

	{
		uint32_t slice_freq0, slice_freq1, unslice_freq0, unslice_freq1;
		const char *reason0 = read_report_reason(oa_report0);
		const char *reason1 = read_report_reason(oa_report1);

		igt_debug("CTX ID: 1st = %"PRIu32", 2nd = %"PRIu32"\n",
			  oa_report0[2], oa_report1[2]);

		read_report_clock_ratios(oa_report0,
					 &slice_freq0, &unslice_freq0);
		read_report_clock_ratios(oa_report1,
					 &slice_freq1, &unslice_freq1);

		igt_debug("SLICE CLK: 1st = %umhz, 2nd = %umhz, delta = %d\n",
			  slice_freq0, slice_freq1,
			  ((int)slice_freq1 - (int)slice_freq0));
		igt_debug("UNSLICE CLK: 1st = %umhz, 2nd = %umhz, delta = %d\n",
			  unslice_freq0, unslice_freq1,
			  ((int)unslice_freq1 - (int)unslice_freq0));

		igt_debug("REASONS: 1st = \"%s\", 2nd = \"%s\"\n", reason0, reason1);
	}

	/* Gen8+ has some 40bit A counters... */
	for (int j = 0; j < format.n_a40; j++) {
		uint64_t value0 = read_40bit_a_counter(oa_report0, fmt, j);
		uint64_t value1 = read_40bit_a_counter(oa_report1, fmt, j);
		uint64_t delta = get_40bit_a_delta(value0, value1);

		igt_debug("A%d: 1st = %"PRIu64", 2nd = %"PRIu64", delta = %"PRIu64"\n",
			  j, value0, value1, delta);
	}

	for (int j = 0; j < format.n_a64; j++) {
		uint64_t value0 = xehpsdv_read_64bit_a_counter(oa_report0, fmt, j);
		uint64_t value1 = xehpsdv_read_64bit_a_counter(oa_report1, fmt, j);
		uint64_t delta = value1 - value0;

		igt_debug("A_64%d: 1st = %"PRIu64", 2nd = %"PRIu64", delta = %"PRIu64"\n",
			  format.first_a + j, value0, value1, delta);
	}

	for (int j = 0; j < format.n_a; j++) {
		uint32_t *a0 = (uint32_t *)(((uint8_t *)oa_report0) +
					    format.a_off);
		uint32_t *a1 = (uint32_t *)(((uint8_t *)oa_report1) +
					    format.a_off);
		int a_id = format.first_a + j;
		uint32_t delta = a1[j] - a0[j];

		igt_debug("A%d: 1st = %"PRIu32", 2nd = %"PRIu32", delta = %"PRIu32"\n",
			  a_id, a0[j], a1[j], delta);
	}

	for (int j = 0; j < format.n_b; j++) {
		uint32_t *b0 = (uint32_t *)(((uint8_t *)oa_report0) +
					    format.b_off);
		uint32_t *b1 = (uint32_t *)(((uint8_t *)oa_report1) +
					    format.b_off);
		uint32_t delta = b1[j] - b0[j];

		igt_debug("B%d: 1st = %"PRIu32", 2nd = %"PRIu32", delta = %"PRIu32"\n",
			  j, b0[j], b1[j], delta);
	}

	for (int j = 0; j < format.n_c; j++) {
		uint32_t *c0 = (uint32_t *)(((uint8_t *)oa_report0) +
					    format.c_off);
		uint32_t *c1 = (uint32_t *)(((uint8_t *)oa_report1) +
					    format.c_off);
		uint32_t delta = c1[j] - c0[j];

		igt_debug("C%d: 1st = %"PRIu32", 2nd = %"PRIu32", delta = %"PRIu32"\n",
			  j, c0[j], c1[j], delta);
	}
}

static bool
hwe_supports_oa_type(int oa_type, const struct drm_xe_engine_class_instance *hwe)
{
	switch (oa_type) {
	case DRM_XE_OA_FMT_TYPE_OAM:
	case DRM_XE_OA_FMT_TYPE_OAM_MPEC:
		return hwe->engine_class == DRM_XE_ENGINE_CLASS_VIDEO_DECODE ||
		       hwe->engine_class == DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE;
	case DRM_XE_OA_FMT_TYPE_OAG:
	case DRM_XE_OA_FMT_TYPE_OAR:
		return hwe->engine_class == DRM_XE_ENGINE_CLASS_RENDER;
	case DRM_XE_OA_FMT_TYPE_OAC:
		return hwe->engine_class == DRM_XE_ENGINE_CLASS_COMPUTE;
	case DRM_XE_OA_FMT_TYPE_PEC:
		return hwe->engine_class == DRM_XE_ENGINE_CLASS_RENDER ||
		       hwe->engine_class == DRM_XE_ENGINE_CLASS_COMPUTE;
	default:
		return false;
	}

}

/**
 * SUBTEST: oa-formats
 * Description: Test that supported OA formats work as expected
 */
static void test_oa_formats(const struct drm_xe_engine_class_instance *hwe)
{
	for (int i = 0; i < XE_OA_FORMAT_MAX; i++) {
		struct oa_format format = get_oa_format(i);
		uint32_t oa_report0[format.size / 4];
		uint32_t oa_report1[format.size / 4];

		if (!format.name) /* sparse, indexed by ID */
			continue;

		if (!hwe_supports_oa_type(format.oa_type, hwe))
			continue;

		igt_debug("Checking OA format %s\n", format.name);

		open_and_read_2_oa_reports(i,
					   oa_exp_1_millisec,
					   oa_report0,
					   oa_report1,
					   false, /* timer reports only */
					   hwe);

		print_reports(oa_report0, oa_report1, i);
		sanity_check_reports(oa_report0, oa_report1, i);
	}
}


enum load {
	LOW,
	HIGH
};

#define LOAD_HELPER_PAUSE_USEC 500

static struct load_helper {
	int devid;
	struct buf_ops *bops;
	uint32_t context_id;
	uint32_t vm;
	struct intel_bb *ibb;
	enum load load;
	bool exit;
	struct igt_helper_process igt_proc;
	struct intel_buf src, dst;
} lh = { 0, };

static void load_helper_signal_handler(int sig)
{
	if (sig == SIGUSR2)
		lh.load = lh.load == LOW ? HIGH : LOW;
	else
		lh.exit = true;
}

static void load_helper_set_load(enum load load)
{
	igt_assert(lh.igt_proc.running);

	if (lh.load == load)
		return;

	lh.load = load;
	kill(lh.igt_proc.pid, SIGUSR2);
}

static void load_helper_run(enum load load)
{
	if (!render_copy)
		return;

	/*
	 * FIXME fork helpers won't get cleaned up when started from within a
	 * subtest, so handle the case where it sticks around a bit too long.
	 */
	if (lh.igt_proc.running) {
		load_helper_set_load(load);
		return;
	}

	lh.load = load;

	igt_fork_helper(&lh.igt_proc) {
		signal(SIGUSR1, load_helper_signal_handler);
		signal(SIGUSR2, load_helper_signal_handler);

		while (!lh.exit) {
			render_copy(lh.ibb,
				    &lh.src, 0, 0, 1920, 1080,
				    &lh.dst, 0, 0);

			intel_bb_sync(lh.ibb);

			/* Lower the load by pausing after every submitted
			 * write. */
			if (lh.load == LOW)
				usleep(LOAD_HELPER_PAUSE_USEC);
		}
	}
}

static void load_helper_stop(void)
{
	if (!render_copy)
		return;

	kill(lh.igt_proc.pid, SIGUSR1);
	igt_assert(igt_wait_helper(&lh.igt_proc) == 0);
}

static void load_helper_init(void)
{
	if (!render_copy) {
		igt_info("Running test without render_copy\n");
		return;
	}

	lh.devid = intel_get_drm_devid(drm_fd);
	lh.bops = buf_ops_create(drm_fd);
	lh.vm = xe_vm_create(drm_fd, 0, 0);
	lh.context_id = xe_exec_queue_create(drm_fd, lh.vm, &xe_engine(drm_fd, 0)->instance, 0);
	igt_assert_neq(lh.context_id, 0xffffffff);

	lh.ibb = intel_bb_create_with_context(drm_fd, lh.context_id, lh.vm, NULL, BATCH_SZ);

	scratch_buf_init(lh.bops, &lh.dst, 1920, 1080, 0);
	scratch_buf_init(lh.bops, &lh.src, 1920, 1080, 0);
}

static void load_helper_fini(void)
{
	if (!render_copy)
		return;

	if (lh.igt_proc.running)
		load_helper_stop();

	intel_buf_close(lh.bops, &lh.src);
	intel_buf_close(lh.bops, &lh.dst);
	intel_bb_destroy(lh.ibb);
	xe_exec_queue_destroy(drm_fd, lh.context_id);
	xe_vm_destroy(drm_fd, lh.vm);
	buf_ops_destroy(lh.bops);
}

static bool expected_report_timing_delta(uint32_t delta, uint32_t expected_delta)
{
	return delta <= expected_delta;
}

/**
 * SUBTEST: oa-exponents
 * Description: Test that oa exponent values behave as expected
 */
static void test_oa_exponents(const struct drm_xe_engine_class_instance *hwe)
{
	struct intel_xe_perf_metric_set *test_set = metric_set(hwe);
	uint64_t fmt = test_set->perf_oa_format;

	load_helper_init();
	load_helper_run(HIGH);

	/* It's asking a lot to sample with a 160 nanosecond period and the
	 * test can fail due to buffer overflows if it wasn't possible to
	 * keep up, so we don't start from an exponent of zero...
	 */
	for (int exponent = 5; exponent < 20; exponent++) {
		uint64_t properties[] = {
			DRM_XE_OA_PROPERTY_OA_UNIT_ID, 0,

			/* Include OA reports in samples */
			DRM_XE_OA_PROPERTY_SAMPLE_OA, true,

			/* OA unit configuration */
			DRM_XE_OA_PROPERTY_OA_METRIC_SET, test_set->perf_oa_metrics_set,
			DRM_XE_OA_PROPERTY_OA_FORMAT, __ff(fmt),
			DRM_XE_OA_PROPERTY_OA_PERIOD_EXPONENT, exponent,
			DRM_XE_OA_PROPERTY_OA_ENGINE_INSTANCE, hwe->engine_instance,
		};
		struct intel_xe_oa_open_prop param = {
			.num_properties = ARRAY_SIZE(properties) / 2,
			.properties_ptr = to_user_pointer(properties),
		};
		uint64_t expected_timestamp_delta = 2ULL << exponent;
		size_t format_size = get_oa_format(fmt).size;
		int max_reports = MAX_OA_BUF_SIZE / format_size;
		int buf_size = format_size * max_reports * 1.5;
		uint8_t *buf = calloc(1, buf_size);
		int ret, n_timer_reports = 0;
		uint32_t matches = 0;
#define NUM_TIMER_REPORTS 30
		uint32_t *reports = malloc(NUM_TIMER_REPORTS * format_size);
		uint32_t *timer_reports = reports;

		igt_debug("testing OA exponent %d,"
			  " expected ts delta = %"PRIu64" (%"PRIu64"ns/%.2fus/%.2fms)\n",
			  exponent, expected_timestamp_delta,
			  oa_exponent_to_ns(exponent),
			  oa_exponent_to_ns(exponent) / 1000.0,
			  oa_exponent_to_ns(exponent) / (1000.0 * 1000.0));

		stream_fd = __perf_open(drm_fd, &param, true /* prevent_pm */);

		while (n_timer_reports < NUM_TIMER_REPORTS) {
			u32 oa_status = 0;

			while ((ret = read(stream_fd, buf, buf_size)) < 0 && errno == EINTR)
				;
			if (errno == EIO) {
				oa_status = get_stream_status(stream_fd);
				igt_debug("oa_status %#x\n", oa_status);
				continue;
			}

			/* igt_debug(" > read %i bytes\n", ret); */
			/* We should never have no data. */
			igt_assert(ret > 0);

			for (int offset = 0;
			     offset < ret && n_timer_reports < NUM_TIMER_REPORTS;
			     offset += format_size) {
				uint32_t *report = (void *)(buf + offset);

				if (oa_status & DRM_XE_OASTATUS_BUFFER_OVERFLOW) {
					igt_assert(!"reached");
					break;
				}

				if (oa_status & DRM_XE_OASTATUS_REPORT_LOST)
					igt_debug("report loss\n");

				if (!oa_report_is_periodic(exponent, report))
					continue;

				memcpy(timer_reports, report, format_size);
				n_timer_reports++;
				timer_reports += (format_size / 4);
			}
		}

		__perf_close(stream_fd);

		igt_debug("report%04i ts=%"PRIx64" hw_id=0x%08x\n", 0,
			  oa_timestamp(&reports[0], fmt),
			  oa_report_get_ctx_id(&reports[0]));
		for (int i = 1; i < n_timer_reports; i++) {
			uint64_t delta = oa_timestamp_delta(&reports[i],
							    &reports[i - 1],
							    fmt);

			igt_debug("report%04i ts=%"PRIx64" hw_id=0x%08x delta=%"PRIu64" %s\n", i,
				  oa_timestamp(&reports[i], fmt),
				  oa_report_get_ctx_id(&reports[i]),
				  delta, expected_report_timing_delta(delta,
								      expected_timestamp_delta) ? "" : "******");

			matches += expected_report_timing_delta(delta,expected_timestamp_delta);
		}

		igt_debug("matches=%u/%u\n", matches, n_timer_reports - 1);

		/*
		 * Expect half the reports to match the timing
		 * expectation. The results are quite erratic because
		 * the condition under which the HW reaches
		 * expectations depends on memory controller pressure
		 * etc...
		 */
		igt_assert_lte(n_timer_reports / 2, matches);

		free(reports);
	}

	load_helper_stop();
	load_helper_fini();
}

/**
 * SUBTEST: invalid-oa-exponent
 * Description: Test that invalid exponent values are rejected
 */
/* The OA exponent selects a timestamp counter bit to trigger reports on.
 *
 * With a 64bit timestamp and least significant bit approx == 80ns then the MSB
 * equates to > 40 thousand years and isn't exposed via the xe oa interface.
 *
 * The max exponent exposed is expected to be 31, which is still a fairly
 * ridiculous period (>5min) but is the maximum exponent where it's still
 * possible to use periodic sampling as a means for tracking the overflow of
 * 32bit OA report timestamps.
 */
static void test_invalid_oa_exponent(void)
{
	uint64_t properties[] = {
		/* Include OA reports in samples */
		DRM_XE_OA_PROPERTY_SAMPLE_OA, true,

		/* OA unit configuration */
		DRM_XE_OA_PROPERTY_OA_METRIC_SET, default_test_set->perf_oa_metrics_set,
		DRM_XE_OA_PROPERTY_OA_FORMAT, __ff(default_test_set->perf_oa_format),
		DRM_XE_OA_PROPERTY_OA_PERIOD_EXPONENT, 31, /* maximum exponent expected
						       to be accepted */
	};
	struct intel_xe_oa_open_prop param = {
		.num_properties = ARRAY_SIZE(properties) / 2,
		.properties_ptr = to_user_pointer(properties),
	};

	stream_fd = __perf_open(drm_fd, &param, false);

	__perf_close(stream_fd);

	for (int i = 32; i < 65; i++) {
		properties[7] = i;
		intel_xe_perf_ioctl_err(drm_fd, DRM_XE_OBSERVATION_OP_STREAM_OPEN, &param, EINVAL);
	}
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

static const char *xe_engine_class_name(uint32_t engine_class)
{
	switch (engine_class) {
		case DRM_XE_ENGINE_CLASS_RENDER:
			return "rcs";
		case DRM_XE_ENGINE_CLASS_COPY:
			return "bcs";
		case DRM_XE_ENGINE_CLASS_VIDEO_DECODE:
			return "vcs";
		case DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE:
			return "vecs";
		case DRM_XE_ENGINE_CLASS_COMPUTE:
			return "ccs";
		default:
			igt_warn("Engine class 0x%x unknown\n", engine_class);
			return "unknown";
	}
}

#define __for_one_hwe_in_each_oa_unit(hwe) \
	for (int m = 0; !m || hwe; m++) \
		for_each_if(hwe = oa_unit_engine(drm_fd, m)) \
			igt_dynamic_f("%s-%d", xe_engine_class_name(hwe->engine_class), \
				      hwe->engine_instance)

/* Only OAG (not OAM) is currently supported */
#define __for_one_hwe_in_oag(hwe) \
	if ((hwe = oa_unit_engine(drm_fd, 0))) \
		igt_dynamic_f("%s-%d", xe_engine_class_name(hwe->engine_class), \
			      hwe->engine_instance)

#define __for_one_render_engine_0(hwe) \
	xe_for_each_engine(drm_fd, hwe) \
		if (hwe->engine_class == DRM_XE_ENGINE_CLASS_RENDER) \
			break; \
	for_each_if(hwe->engine_class == DRM_XE_ENGINE_CLASS_RENDER) \
		igt_dynamic_f("rcs-%d", hwe->engine_instance)

#define __for_one_render_engine(hwe)	      \
	for (int m = 0, done = 0; !done; m++) \
		for_each_if(m < xe_number_engines(drm_fd) && \
			    (hwe = &xe_engine(drm_fd, m)->instance) && \
			    hwe->engine_class == DRM_XE_ENGINE_CLASS_RENDER && \
			    (done = 1)) \
			igt_dynamic_f("rcs-%d", hwe->engine_instance)

igt_main
{
	struct drm_xe_engine_class_instance *hwe = NULL;

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

	igt_subtest_with_dynamic("oa-formats")
		__for_one_render_engine(hwe)
			test_oa_formats(hwe);

	igt_subtest("invalid-oa-exponent")
		test_invalid_oa_exponent();

	igt_subtest_with_dynamic("oa-exponents")
		__for_one_hwe_in_oag(hwe)
			test_oa_exponents(hwe);

	igt_fixture {
		/* leave sysctl options in their default state... */
		write_u64_file("/proc/sys/dev/xe/observation_paranoid", 1);

		if (intel_xe_perf)
			intel_xe_perf_free(intel_xe_perf);

		drm_close_driver(drm_fd);
	}
}
