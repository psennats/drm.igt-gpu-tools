/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef XE_GT_H
#define XE_GT_H

#include "lib/igt_gt.h"

#include "xe_query.h"

bool has_xe_gt_reset(int fd);
void xe_force_gt_reset_all(int fd);
igt_hang_t xe_hang_ring(int fd, uint64_t ahnd, uint32_t ctx, int ring,
				unsigned int flags);
void xe_post_hang_ring(int fd, igt_hang_t arg);
int xe_gt_stats_get_count(int fd, int gt, const char *stat);

bool xe_is_gt_in_c6(int fd, int gt);

int xe_gt_fill_engines_by_class(int fd, int gt, int class,
				struct drm_xe_engine_class_instance eci[static XE_MAX_ENGINE_INSTANCE]);
int xe_gt_count_engines_by_class(int fd, int gt, int class);

#endif
