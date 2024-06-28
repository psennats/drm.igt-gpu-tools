/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef XE_OA_DATA_READER_H
#define XE_OA_DATA_READER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Helper to read a xe-perf recording. */

#include <stdbool.h>
#include <stdint.h>

#include "xe_oa_data.h"

struct intel_xe_perf_timeline_item {
	uint64_t ts_start;
	uint64_t ts_end;
	uint64_t cpu_ts_start;
	uint64_t cpu_ts_end;

	/* Offsets into intel_xe_perf_data_reader.records */
	uint32_t record_start;
	uint32_t record_end;

	uint32_t hw_id;

	/* User associated data with a given item on the xe perf
	 * timeline.
	 */
	void *user_data;
};

struct intel_xe_perf_data_reader {
	/* Array of pointers into the mmapped xe perf file. */
	const struct intel_xe_perf_record_header **records;
	uint32_t n_records;
	uint32_t n_allocated_records;

	/**/
	struct intel_xe_perf_timeline_item *timelines;
	uint32_t n_timelines;
	uint32_t n_allocated_timelines;

	/**/
	const struct intel_xe_perf_record_timestamp_correlation **correlations;
	uint32_t n_correlations;
	uint32_t n_allocated_correlations;

	struct {
		uint64_t gpu_ts_begin;
		uint64_t gpu_ts_end;
		uint32_t idx;
	} correlation_chunks[4];
	uint32_t n_correlation_chunks;

	const char *metric_set_uuid;
	const char *metric_set_name;

	struct intel_xe_perf_devinfo devinfo;

	struct intel_xe_perf *perf;
	struct intel_xe_perf_metric_set *metric_set;

	char error_msg[256];

	/**/
	const void *record_info;
	const void *record_topology;

	const uint8_t *mmap_data;
	size_t mmap_size;
};

bool intel_xe_perf_data_reader_init(struct intel_xe_perf_data_reader *reader,
				    int perf_file_fd);
void intel_xe_perf_data_reader_fini(struct intel_xe_perf_data_reader *reader);

#ifdef __cplusplus
};
#endif

#endif /* XE_OA_DATA_READER_H */
