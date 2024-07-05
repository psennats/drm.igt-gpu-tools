/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef XE_OA_H
#define XE_OA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "igt_list.h"
#include <xe_drm.h>

#define _DIV_ROUND_UP(a, b)  (((a) + (b) - 1) / (b))

#define INTEL_XE_DEVICE_MAX_SLICES           (8)
#define INTEL_XE_DEVICE_MAX_SUBSLICES        (32)
#define INTEL_XE_DEVICE_MAX_EUS_PER_SUBSLICE (16) /* Maximum on gfx12 */

enum intel_xe_oa_format_name {
	XE_OA_FORMAT_C4_B8 = 1,

	/* Gen8+ */
	XE_OA_FORMAT_A12,
	XE_OA_FORMAT_A12_B8_C8,
	XE_OA_FORMAT_A32u40_A4u32_B8_C8,

	/* DG2 */
	XE_OAR_FORMAT_A32u40_A4u32_B8_C8,
	XE_OA_FORMAT_A24u40_A14u32_B8_C8,

	/* DG2/MTL OAC */
	XE_OAC_FORMAT_A24u64_B8_C8,
	XE_OAC_FORMAT_A22u32_R2u32_B8_C8,

	/* MTL OAM */
	XE_OAM_FORMAT_MPEC8u64_B8_C8,
	XE_OAM_FORMAT_MPEC8u32_B8_C8,

	/* Xe2+ */
	XE_OA_FORMAT_PEC64u64,
	XE_OA_FORMAT_PEC64u64_B8_C8,
	XE_OA_FORMAT_PEC64u32,
	XE_OA_FORMAT_PEC32u64_G1,
	XE_OA_FORMAT_PEC32u32_G1,
	XE_OA_FORMAT_PEC32u64_G2,
	XE_OA_FORMAT_PEC32u32_G2,
	XE_OA_FORMAT_PEC36u64_G1_32_G2_4,
	XE_OA_FORMAT_PEC36u64_G1_4_G2_32,

	XE_OA_FORMAT_MAX,
};

struct intel_xe_perf_devinfo {
	char devname[20];
	char prettyname[100];

	/*
	 * Always false for gputop, we don't have the additional
	 * snapshots of register values, only the OA reports.
	 */
	bool query_mode;

	bool has_dynamic_configs;

	/* The following fields are prepared for equations from the XML files.
	 * Their values are build up from the topology fields.
	 */
	uint32_t devid;
	uint32_t graphics_ver;
	uint32_t revision;
	/**
	 * Bit shifting required to put OA report timestamps into
	 * timestamp_frequency (some HW generations can shift
	 * timestamp values to the right by a number of bits).
	 */
	int32_t  oa_timestamp_shift;
	/**
	 * On some platforms only part of the timestamp bits are valid
	 * (on previous platforms we would get full 32bits, newer
	 * platforms can have fewer). It's important to know when
	 * correlating the full 36bits timestamps to the OA report
	 * timestamps.
	 */
	uint64_t  oa_timestamp_mask;
	/* Frequency of the timestamps in Hz */
	uint64_t timestamp_frequency;
	uint64_t gt_min_freq;
	uint64_t gt_max_freq;

	/* Total number of EUs */
	uint64_t n_eus;
	/* Total number of EUs in a slice */
	uint64_t n_eu_slices;
	/* Total number of subslices/dualsubslices */
	uint64_t n_eu_sub_slices;
	/* Number of subslices/dualsubslices in the first half of the
	 * slices.
	 */
	uint64_t n_eu_sub_slices_half_slices;
	/* Mask of available subslices/dualsubslices */
	uint64_t subslice_mask;
	/* Mask of available slices */
	uint64_t slice_mask;
	/* Number of threads in one EU */
	uint64_t eu_threads_count;

	/**
	 * Maximu number of slices present on this device (can be more than
	 * num_slices if some slices are fused).
	 */
	uint16_t max_slices;

	/**
	 * Maximu number of subslices per slice present on this device (can be more
	 * than the maximum value in the num_subslices[] array if some subslices are
	 * fused).
	 */
	uint16_t max_subslices_per_slice;

	/**
	 * Stride to access subslice_masks[].
	 */
	uint16_t subslice_slice_stride;

	/**
	 * Maximum number of EUs per subslice (can be more than
	 * num_eu_per_subslice if some EUs are fused off).
	 */
	uint16_t max_eu_per_subslice;

	/**
	 * Strides to access eu_masks[].
	 */
	uint16_t eu_slice_stride;
	uint16_t eu_subslice_stride;

	/**
	 * A bit mask of the slices available.
	 */
	uint8_t slice_masks[_DIV_ROUND_UP(INTEL_XE_DEVICE_MAX_SLICES, 8)];

	/**
	 * An array of bit mask of the subslices available, use subslice_slice_stride
	 * to access this array.
	 */
	uint8_t subslice_masks[INTEL_XE_DEVICE_MAX_SLICES *
			       _DIV_ROUND_UP(INTEL_XE_DEVICE_MAX_SUBSLICES, 8)];

	/**
	 * An array of bit mask of EUs available, use eu_slice_stride &
	 * eu_subslice_stride to access this array.
	 */
	uint8_t eu_masks[INTEL_XE_DEVICE_MAX_SLICES *
			 INTEL_XE_DEVICE_MAX_SUBSLICES *
			 _DIV_ROUND_UP(INTEL_XE_DEVICE_MAX_EUS_PER_SUBSLICE, 8)];
};

typedef enum {
	INTEL_XE_PERF_LOGICAL_COUNTER_STORAGE_UINT64,
	INTEL_XE_PERF_LOGICAL_COUNTER_STORAGE_UINT32,
	INTEL_XE_PERF_LOGICAL_COUNTER_STORAGE_DOUBLE,
	INTEL_XE_PERF_LOGICAL_COUNTER_STORAGE_FLOAT,
	INTEL_XE_PERF_LOGICAL_COUNTER_STORAGE_BOOL32,
} intel_xe_perf_logical_counter_storage_t;

typedef enum {
	INTEL_XE_PERF_LOGICAL_COUNTER_TYPE_RAW,
	INTEL_XE_PERF_LOGICAL_COUNTER_TYPE_DURATION_RAW,
	INTEL_XE_PERF_LOGICAL_COUNTER_TYPE_DURATION_NORM,
	INTEL_XE_PERF_LOGICAL_COUNTER_TYPE_EVENT,
	INTEL_XE_PERF_LOGICAL_COUNTER_TYPE_THROUGHPUT,
	INTEL_XE_PERF_LOGICAL_COUNTER_TYPE_TIMESTAMP,
} intel_xe_perf_logical_counter_type_t;

typedef enum {
	/* size */
	INTEL_XE_PERF_LOGICAL_COUNTER_UNIT_BYTES,

	/* frequency */
	INTEL_XE_PERF_LOGICAL_COUNTER_UNIT_HZ,

	/* time */
	INTEL_XE_PERF_LOGICAL_COUNTER_UNIT_NS,
	INTEL_XE_PERF_LOGICAL_COUNTER_UNIT_US,

	/**/
	INTEL_XE_PERF_LOGICAL_COUNTER_UNIT_PIXELS,
	INTEL_XE_PERF_LOGICAL_COUNTER_UNIT_TEXELS,
	INTEL_XE_PERF_LOGICAL_COUNTER_UNIT_THREADS,
	INTEL_XE_PERF_LOGICAL_COUNTER_UNIT_PERCENT,

	/* events */
	INTEL_XE_PERF_LOGICAL_COUNTER_UNIT_MESSAGES,
	INTEL_XE_PERF_LOGICAL_COUNTER_UNIT_NUMBER,
	INTEL_XE_PERF_LOGICAL_COUNTER_UNIT_CYCLES,
	INTEL_XE_PERF_LOGICAL_COUNTER_UNIT_EVENTS,
	INTEL_XE_PERF_LOGICAL_COUNTER_UNIT_UTILIZATION,

	/**/
	INTEL_XE_PERF_LOGICAL_COUNTER_UNIT_EU_SENDS_TO_L3_CACHE_LINES,
	INTEL_XE_PERF_LOGICAL_COUNTER_UNIT_EU_ATOMIC_REQUESTS_TO_L3_CACHE_LINES,
	INTEL_XE_PERF_LOGICAL_COUNTER_UNIT_EU_REQUESTS_TO_L3_CACHE_LINES,
	INTEL_XE_PERF_LOGICAL_COUNTER_UNIT_EU_BYTES_PER_L3_CACHE_LINE,
	INTEL_XE_PERF_LOGICAL_COUNTER_UNIT_GBPS,

	INTEL_XE_PERF_LOGICAL_COUNTER_UNIT_MAX
} intel_xe_perf_logical_counter_unit_t;

/* Hold deltas of raw performance counters. */
struct intel_xe_perf_accumulator {
#define INTEL_XE_PERF_MAX_RAW_OA_COUNTERS 128
	uint64_t deltas[INTEL_XE_PERF_MAX_RAW_OA_COUNTERS];
};

struct intel_xe_perf;
struct intel_xe_perf_metric_set;
struct intel_xe_perf_logical_counter {
	const struct intel_xe_perf_metric_set *metric_set;
	const char *name;
	const char *symbol_name;
	const char *desc;
	const char *group;
	bool (*availability)(const struct intel_xe_perf *perf);
	intel_xe_perf_logical_counter_storage_t storage;
	intel_xe_perf_logical_counter_type_t type;
	intel_xe_perf_logical_counter_unit_t unit;
	union {
		uint64_t (*max_uint64)(const struct intel_xe_perf *perf,
				       const struct intel_xe_perf_metric_set *metric_set,
				       uint64_t *deltas);
		double (*max_float)(const struct intel_xe_perf *perf,
				    const struct intel_xe_perf_metric_set *metric_set,
				    uint64_t *deltas);
	};

	union {
		uint64_t (*read_uint64)(const struct intel_xe_perf *perf,
					const struct intel_xe_perf_metric_set *metric_set,
					uint64_t *deltas);
		double (*read_float)(const struct intel_xe_perf *perf,
				     const struct intel_xe_perf_metric_set *metric_set,
				     uint64_t *deltas);
	};

	struct igt_list_head link; /* list from intel_xe_perf_logical_counter_group.counters */
};

struct intel_xe_perf_register_prog {
	uint32_t reg;
	uint32_t val;
};

struct intel_xe_perf_metric_set {
	const char *name;
	const char *symbol_name;
	const char *hw_config_guid;

	struct intel_xe_perf_logical_counter *counters;
	int n_counters;

	uint64_t perf_oa_metrics_set;
	int perf_oa_format;
	int perf_raw_size;

	/* For indexing into accumulator->deltas[] ... */
	int gpu_time_offset;
	int gpu_clock_offset;
	int a_offset;
	int b_offset;
	int c_offset;
	int perfcnt_offset;
	int pec_offset;

	const struct intel_xe_perf_register_prog *b_counter_regs;
	uint32_t n_b_counter_regs;

	const struct intel_xe_perf_register_prog *mux_regs;
	uint32_t n_mux_regs;

	const struct intel_xe_perf_register_prog *flex_regs;
	uint32_t n_flex_regs;

	struct igt_list_head link;
};

/* A tree structure with group having subgroups and counters. */
struct intel_xe_perf_logical_counter_group {
	char *name;

	struct igt_list_head counters;
	struct igt_list_head groups;

	struct igt_list_head link;  /* link for intel_xe_perf_logical_counter_group.groups */
};

struct intel_xe_perf {
	const char *name;

	struct intel_xe_perf_logical_counter_group *root_group;

	struct igt_list_head metric_sets;

	struct intel_xe_perf_devinfo devinfo;
};

/* This is identical to 'struct drm_i915_query_topology_info' at present */
struct intel_xe_topology_info {
	uint16_t flags;
	uint16_t max_slices;
	uint16_t max_subslices;
	uint16_t max_eus_per_subslice;
	uint16_t subslice_offset;
	uint16_t subslice_stride;
	uint16_t eu_offset;
	uint16_t eu_stride;
	uint8_t data[];
};

struct intel_xe_perf_record_header {
	uint32_t type;
	uint16_t pad;
	uint16_t size;
};

static inline bool
intel_xe_perf_devinfo_slice_available(const struct intel_xe_perf_devinfo *devinfo,
				      int slice)
{
	return (devinfo->slice_masks[slice / 8] & (1U << (slice % 8))) != 0;
}

static inline bool
intel_xe_perf_devinfo_subslice_available(const struct intel_xe_perf_devinfo *devinfo,
					 int slice, int subslice)
{
	return (devinfo->subslice_masks[slice * devinfo->subslice_slice_stride +
					subslice / 8] & (1U << (subslice % 8))) != 0;
}

static inline bool
intel_xe_perf_devinfo_eu_available(const struct intel_xe_perf_devinfo *devinfo,
				   int slice, int subslice, int eu)
{
	unsigned subslice_offset = slice * devinfo->eu_slice_stride +
		subslice * devinfo->eu_subslice_stride;

	return (devinfo->eu_masks[subslice_offset + eu / 8] & (1U << eu % 8)) != 0;
}

struct intel_xe_topology_info *
xe_fill_topology_info(int drm_fd, uint32_t device_id, uint32_t *topology_size);

struct intel_xe_perf *intel_xe_perf_for_fd(int drm_fd, int gt);
struct intel_xe_perf *intel_xe_perf_for_devinfo(uint32_t device_id,
						uint32_t revision,
						uint64_t timestamp_frequency,
						uint64_t gt_min_freq,
						uint64_t gt_max_freq,
						const struct intel_xe_topology_info *topology);
void intel_xe_perf_free(struct intel_xe_perf *perf);

void intel_xe_perf_add_logical_counter(struct intel_xe_perf *perf,
				       struct intel_xe_perf_logical_counter *counter,
				       const char *group);

void intel_xe_perf_add_metric_set(struct intel_xe_perf *perf,
				  struct intel_xe_perf_metric_set *metric_set);

void intel_xe_perf_load_perf_configs(struct intel_xe_perf *perf, int drm_fd);


struct intel_xe_oa_open_prop {
	uint32_t num_properties;
	uint32_t reserved;
	uint64_t properties_ptr;
};

void intel_xe_perf_accumulate_reports(struct intel_xe_perf_accumulator *acc,
				      const struct intel_xe_perf *perf,
				      const struct intel_xe_perf_metric_set *metric_set,
				      const struct intel_xe_perf_record_header *record0,
				      const struct intel_xe_perf_record_header *record1);

uint64_t intel_xe_perf_read_record_timestamp(const struct intel_xe_perf *perf,
					     const struct intel_xe_perf_metric_set *metric_set,
					     const struct intel_xe_perf_record_header *record);

uint64_t intel_xe_perf_read_record_timestamp_raw(const struct intel_xe_perf *perf,
						 const struct intel_xe_perf_metric_set *metric_set,
						 const struct intel_xe_perf_record_header *record);

const char *intel_xe_perf_read_report_reason(const struct intel_xe_perf *perf,
					     const struct intel_xe_perf_record_header *record);

int intel_xe_perf_ioctl(int fd, enum drm_xe_observation_op op, void *arg);
void intel_xe_perf_ioctl_err(int fd, enum drm_xe_observation_op op, void *arg, int err);

#ifdef __cplusplus
};
#endif

#endif /* XE_OA_H */
