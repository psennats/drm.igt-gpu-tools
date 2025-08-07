/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Google LLC.
 * Copyright © 2023 Collabora, Ltd.
 * Copyright © 2024-2025 Red Hat, Inc.
 */

#ifndef __IGT_VKMS_H__
#define __IGT_VKMS_H__

#include <stdbool.h>

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

void igt_vkms_get_device_enabled_path(igt_vkms_t *dev, char *path, size_t len);
void igt_vkms_get_plane_path(igt_vkms_t *dev, const char *name, char *path,
			     size_t len);
void igt_vkms_get_plane_type_path(igt_vkms_t *dev, const char *name, char *path,
				  size_t len);
void igt_vkms_get_crtc_path(igt_vkms_t *dev, const char *name, char *path,
			    size_t len);
void igt_vkms_get_crtc_writeback_path(igt_vkms_t *dev, const char *name,
				      char *path, size_t len);
void igt_vkms_get_encoder_path(igt_vkms_t *dev, const char *name, char *path,
			       size_t len);
void igt_vkms_get_connector_path(igt_vkms_t *dev, const char *name, char *path,
				 size_t len);
void igt_vkms_get_connector_status_path(igt_vkms_t *dev, const char *name,
					char *path, size_t len);

igt_vkms_t *igt_vkms_device_create(const char *name);
void igt_vkms_device_destroy(igt_vkms_t *dev);
void igt_vkms_destroy_all_devices(void);

bool igt_vkms_device_is_enabled(igt_vkms_t *dev);
void igt_vkms_device_set_enabled(igt_vkms_t *dev, bool enabled);

void igt_vkms_device_add_plane(igt_vkms_t *dev, const char *name);
int igt_vkms_plane_get_type(igt_vkms_t *dev, const char *name);
void igt_vkms_plane_set_type(igt_vkms_t *dev, const char *name, int type);

void igt_vkms_device_add_crtc(igt_vkms_t *dev, const char *name);
bool igt_vkms_crtc_is_writeback_enabled(igt_vkms_t *dev, const char *name);
void igt_vkms_crtc_set_writeback_enabled(igt_vkms_t *dev, const char *name,
					 bool writeback);

void igt_vkms_device_add_encoder(igt_vkms_t *dev, const char *name);

void igt_vkms_device_add_connector(igt_vkms_t *dev, const char *name);
int igt_vkms_connector_get_status(igt_vkms_t *dev, const char *name);
void igt_vkms_connector_set_status(igt_vkms_t *dev, const char *name,
				   int status);

#endif /* __IGT_VKMS_H__ */
