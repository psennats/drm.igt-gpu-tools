/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef XE_OA_DATA_H
#define XE_OA_DATA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* For now this enum is the same as i915 intel_perf_record_type/drm_i915_perf_record_type */
enum intel_xe_perf_record_type {
	/* An packet/record of OA data */
	INTEL_XE_PERF_RECORD_TYPE_SAMPLE = 1,

	/* Indicates one or more OA reports were not written by HW */
	INTEL_XE_PERF_RECORD_OA_TYPE_REPORT_LOST,

	/* An error occurred that resulted in all pending OA reports being lost */
	INTEL_XE_PERF_RECORD_OA_TYPE_BUFFER_LOST,

	INTEL_XE_PERF_RECORD_TYPE_VERSION,

	/* intel_xe_perf_record_device_info */
	INTEL_XE_PERF_RECORD_TYPE_DEVICE_INFO,

	/* intel_xe_perf_record_device_topology */
	INTEL_XE_PERF_RECORD_TYPE_DEVICE_TOPOLOGY,

	/* intel_xe_perf_record_timestamp_correlation */
	INTEL_XE_PERF_RECORD_TYPE_TIMESTAMP_CORRELATION,

	INTEL_XE_PERF_RECORD_MAX /* non-ABI */
};

/* This structure cannot ever change. */
struct intel_xe_perf_record_version {
	/* Version of the xe-perf file recording format (effectively
	 * versioning this file).
	 */
	uint32_t version;

#define INTEL_XE_PERF_RECORD_VERSION (1)

	uint32_t pad;
} __attribute__((packed));

struct intel_xe_perf_record_device_info {
	/* Frequency of the timestamps in the records. */
	uint64_t timestamp_frequency;

	/* PCI ID */
	uint32_t device_id;

	/* Stepping */
	uint32_t device_revision;

	/* GT min/max frequencies */
	uint32_t gt_min_frequency;
	uint32_t gt_max_frequency;

	/* Engine */
	uint32_t engine_class;
	uint32_t engine_instance;

	/* enum intel_xe_oa_format_name */
	uint32_t oa_format;

	/* Metric set name */
	char metric_set_name[256];

	/* Configuration identifier */
	char metric_set_uuid[40];

	uint32_t pad;
 } __attribute__((packed));

/* Topology as filled by xe_fill_topology_info (variable length, aligned by
 * the recorder). */
struct intel_xe_perf_record_device_topology {
	struct intel_xe_topology_info topology;
};

/* Timestamp correlation between CPU/GPU. */
struct intel_xe_perf_record_timestamp_correlation {
	/* In CLOCK_MONOTONIC */
	uint64_t cpu_timestamp;

	/* Engine timestamp associated with the OA unit */
	uint64_t gpu_timestamp;
} __attribute__((packed));

#ifdef __cplusplus
};
#endif

#endif /* XE_OA_DATA_H */
