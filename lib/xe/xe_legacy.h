/* SPDX-License-Identifier: MIT
 * Copyright © 2025 Intel Corporation
 */

#ifndef XE_LEGACY_H
#define XE_LEGACY_H

#include "linux_scaffold.h"

void
xe_legacy_test_mode(int fd, struct drm_xe_engine_class_instance *eci,
		    int n_exec_queues, int n_execs, unsigned int flags,
		    u64 addr, bool use_capture_mode);

#endif /* XE_LEGACY_H */
