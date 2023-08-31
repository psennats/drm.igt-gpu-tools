/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2024 Intel Corporation. All rights reserved.
 */

#ifndef __XE_SRIOV_DEBUGFS_H__
#define __XE_SRIOV_DEBUGFS_H__

int xe_sriov_pf_debugfs_attr_open(int pf, unsigned int vf_num, unsigned int gt_num,
				  const char *attr, int mode);

#endif /* __XE_SRIOV_DEBUGFS_H__ */
