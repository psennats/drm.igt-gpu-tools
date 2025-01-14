/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2024 Intel Corporation. All rights reserved.
 */

#ifndef __XE_SRIOV_DEBUGFS_H__
#define __XE_SRIOV_DEBUGFS_H__

enum xe_sriov_shared_res;
struct xe_sriov_provisioned_range;

int xe_sriov_pf_debugfs_attr_open(int pf, unsigned int vf_num, unsigned int gt_num,
				  const char *attr, int mode);
const char *xe_sriov_debugfs_provisioned_attr_name(enum xe_sriov_shared_res res);
int xe_sriov_pf_debugfs_read_provisioned_ranges(int pf_fd, enum xe_sriov_shared_res res,
						unsigned int gt_id,
						struct xe_sriov_provisioned_range **ranges,
						unsigned int *nr_ranges);
int xe_sriov_pf_debugfs_read_check_ranges(int pf_fd, enum xe_sriov_shared_res res,
					  unsigned int gt_id,
					  struct xe_sriov_provisioned_range **ranges,
					  unsigned int expected_num_vfs);
int __xe_sriov_pf_debugfs_get_u32(int pf, unsigned int vf_num,
				  unsigned int gt_num, const char *attr,
				  uint32_t *value);
int __xe_sriov_pf_debugfs_set_u32(int pf, unsigned int vf_num,
				  unsigned int gt_num, const char *attr,
				  uint32_t value);
int __xe_sriov_pf_debugfs_get_u64(int pf, unsigned int vf_num,
				  unsigned int gt_num, const char *attr,
				  uint64_t *value);
int __xe_sriov_pf_debugfs_set_u64(int pf, unsigned int vf_num,
				  unsigned int gt_num, const char *attr,
				  uint64_t value);
int __xe_sriov_pf_debugfs_get_boolean(int pf, unsigned int vf_num,
				      unsigned int gt_num, const char *attr,
				      bool *value);
int __xe_sriov_pf_debugfs_set_boolean(int pf, unsigned int vf_num,
				      unsigned int gt_num, const char *attr,
				      bool value);

#endif /* __XE_SRIOV_DEBUGFS_H__ */
