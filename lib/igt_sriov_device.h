/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2023 Intel Corporation. All rights reserved.
 */

#ifndef __IGT_SRIOV_DEVICE_H__
#define __IGT_SRIOV_DEVICE_H__

#include <stdbool.h>
#include <stddef.h>

/* Library for managing SR-IOV (Single Root I/O Virtualization)
 * devices.
 *
 * SR-IOV is a specification that allows a single PCIe physical
 * device to appear as a physical function (PF) and multiple virtual
 * functions (VFs) to the operating system.
 */

bool igt_sriov_is_pf(int device);
bool igt_sriov_vfs_supported(int pf);
unsigned int igt_sriov_get_total_vfs(int pf);
unsigned int igt_sriov_get_enabled_vfs(int pf);
void igt_sriov_enable_vfs(int pf, unsigned int num_vfs);
void igt_sriov_disable_vfs(int pf);
bool igt_sriov_is_driver_autoprobe_enabled(int pf);
void igt_sriov_enable_driver_autoprobe(int pf);
void igt_sriov_disable_driver_autoprobe(int pf);
int igt_sriov_open_vf_drm_device(int pf, unsigned int vf_num);
bool igt_sriov_is_vf_drm_driver_probed(int pf, unsigned int vf_num);
void igt_sriov_bind_vf_drm_driver(int pf, unsigned int vf_num);
void igt_sriov_unbind_vf_drm_driver(int pf, unsigned int vf_num);
int igt_sriov_device_sysfs_open(int pf, unsigned int vf_num);

/**
 * for_each_sriov_vf - Helper for running code on each VF
 * @__pf_fd: PF device file descriptor
 * @__vf_num: VFs iterator
 *
 * For loop that iterates over all VFs associated with given PF @__pf_fd.
 */
#define for_each_sriov_vf(__pf_fd, __vf_num) \
	for (unsigned int __vf_num = 1, __total_vfs = igt_sriov_get_total_vfs(__pf_fd); \
	     __vf_num <= __total_vfs; \
	     ++__vf_num)
#define for_each_sriov_num_vfs for_each_sriov_vf

/**
 * for_random_sriov_vf - Helper for running code on random VF
 * @__pf_fd: PF device file descriptor
 * @__vf_num: stores random VF
 *
 * Helper allows to run code using random VF number (stored in @__vf_num)
 * picked from the range of all VFs associated with given PF @__pf_fd.
 */
#define for_random_sriov_vf(__pf_fd, __vf_num) \
	for (unsigned int __vf_num = 1 + random() % igt_sriov_get_total_vfs(__pf_fd), __tmp = 0; \
	     __tmp < 1; \
	     ++__tmp)
#define for_random_sriov_num_vfs for_random_sriov_vf

/**
 * for_last_sriov_vf - Helper for running code on last VF
 * @__pf_fd: PF device file descriptor
 * @__vf_num: stores last VF number
 *
 * Helper allows to run code using last VF number (stored in @__vf_num)
 * associated with given PF @__pf_fd.
 */
#define for_last_sriov_vf(__pf_fd, __vf_num) \
	for (unsigned int __vf_num = igt_sriov_get_total_vfs(__pf_fd), __tmp = 0; \
	     __tmp < 1; \
	     ++__tmp)
#define for_max_sriov_num_vfs for_last_sriov_vf

#endif /* __IGT_SRIOV_DEVICE_H__ */
