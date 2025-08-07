/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Google LLC.
 * Copyright © 2023 Collabora, Ltd.
 * Copyright © 2024-2025 Red Hat, Inc.
 */

#ifndef __IGT_VKMS_H__
#define __IGT_VKMS_H__

/**
 * igt_vkms_t:
 * @path: VKMS root directory inside configfs mounted directory
 *
 * A struct representing a VKMS device.
 */
typedef struct igt_vkms {
	char *path;
} igt_vkms_t;

void igt_require_vkms_configfs(void);

igt_vkms_t *igt_vkms_device_create(const char *name);
void igt_vkms_device_destroy(igt_vkms_t *dev);
void igt_vkms_destroy_all_devices(void);

#endif /* __IGT_VKMS_H__ */
