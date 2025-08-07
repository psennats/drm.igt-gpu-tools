/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Google LLC.
 * Copyright © 2023 Collabora, Ltd.
 * Copyright © 2024-2025 Red Hat, Inc.
 */

#ifndef __IGT_VKMS_H__
#define __IGT_VKMS_H__

#include <stdbool.h>

#define VKMS_MAX_PIPELINE_ITEMS	40

/**
 * igt_vkms_t:
 * @path: VKMS root directory inside configfs mounted directory
 *
 * A struct representing a VKMS device.
 */
typedef struct igt_vkms {
	char *path;
} igt_vkms_t;

typedef struct igt_vkms_crtc_config {
	const char *name;
	bool writeback; /* Default: false */
} igt_vkms_crtc_config_t;

typedef struct igt_vkms_plane_config {
	const char *name;
	int type; /* Default: DRM_PLANE_TYPE_OVERLAY */
	const char *possible_crtcs[VKMS_MAX_PIPELINE_ITEMS];
} igt_vkms_plane_config_t;

typedef struct igt_vkms_encoder_config {
	const char *name;
	const char *possible_crtcs[VKMS_MAX_PIPELINE_ITEMS];
} igt_vkms_encoder_config_t;

typedef struct igt_vkms_connector_config {
	const char *name;
	int status; /* Default: DRM_MODE_CONNECTED */
	const char *possible_encoders[VKMS_MAX_PIPELINE_ITEMS];
} igt_vkms_connector_config_t;

/**
 * igt_vkms_config_t:
 * @device_name: Device name
 * @planes: NULL terminated list of plane configurations
 * @crtcs: NULL terminated list of CRTC configurations
 * @encoders: NULL terminated list of encoders configurations
 * @connectors: NULL terminated list of connector configurations
 *
 * Structure used to create a VKMS device from a static configuration.
 */
typedef struct igt_vkms_config {
	const char *device_name;
	igt_vkms_plane_config_t planes[VKMS_MAX_PIPELINE_ITEMS];
	igt_vkms_crtc_config_t crtcs[VKMS_MAX_PIPELINE_ITEMS];
	igt_vkms_encoder_config_t encoders[VKMS_MAX_PIPELINE_ITEMS];
	igt_vkms_connector_config_t connectors[VKMS_MAX_PIPELINE_ITEMS];
} igt_vkms_config_t;

void igt_require_vkms_configfs(void);

void igt_vkms_get_device_enabled_path(igt_vkms_t *dev, char *path, size_t len);
void igt_vkms_get_plane_path(igt_vkms_t *dev, const char *name, char *path,
			     size_t len);
void igt_vkms_get_plane_type_path(igt_vkms_t *dev, const char *name, char *path,
				  size_t len);
void igt_vkms_get_plane_possible_crtcs_path(igt_vkms_t *dev, const char *name,
					    char *path, size_t len);
void igt_vkms_get_crtc_path(igt_vkms_t *dev, const char *name, char *path,
			    size_t len);
void igt_vkms_get_crtc_writeback_path(igt_vkms_t *dev, const char *name,
				      char *path, size_t len);
void igt_vkms_get_encoder_path(igt_vkms_t *dev, const char *name, char *path,
			       size_t len);
void igt_vkms_get_encoder_possible_crtcs_path(igt_vkms_t *dev, const char *name,
					      char *path, size_t len);
void igt_vkms_get_connector_path(igt_vkms_t *dev, const char *name, char *path,
				 size_t len);
void igt_vkms_get_connector_status_path(igt_vkms_t *dev, const char *name,
					char *path, size_t len);
void igt_vkms_get_connector_possible_encoders_path(igt_vkms_t *dev,
						   const char *name, char *path,
						   size_t len);

igt_vkms_t *igt_vkms_device_create(const char *name);
igt_vkms_t *igt_vkms_device_create_from_config(igt_vkms_config_t *cfg);
void igt_vkms_device_destroy(igt_vkms_t *dev);
void igt_vkms_destroy_all_devices(void);

bool igt_vkms_device_is_enabled(igt_vkms_t *dev);
void igt_vkms_device_set_enabled(igt_vkms_t *dev, bool enabled);

void igt_vkms_device_add_plane(igt_vkms_t *dev, const char *name);
bool igt_vkms_device_remove_plane(igt_vkms_t *dev, const char *name);
int igt_vkms_plane_get_type(igt_vkms_t *dev, const char *name);
void igt_vkms_plane_set_type(igt_vkms_t *dev, const char *name, int type);
bool igt_vkms_plane_attach_crtc(igt_vkms_t *dev, const char *plane_name,
				const char *crtc_name);
bool igt_vkms_plane_detach_crtc(igt_vkms_t *dev, const char *plane_name,
				const char *crtc_name);

void igt_vkms_device_add_crtc(igt_vkms_t *dev, const char *name);
bool igt_vkms_device_remove_crtc(igt_vkms_t *dev, const char *name);
bool igt_vkms_crtc_is_writeback_enabled(igt_vkms_t *dev, const char *name);
void igt_vkms_crtc_set_writeback_enabled(igt_vkms_t *dev, const char *name,
					 bool writeback);

void igt_vkms_device_add_encoder(igt_vkms_t *dev, const char *name);
bool igt_vkms_device_remove_encoder(igt_vkms_t *dev, const char *name);
bool igt_vkms_encoder_attach_crtc(igt_vkms_t *dev, const char *encoder_name,
				  const char *crtc_name);
bool igt_vkms_encoder_detach_crtc(igt_vkms_t *dev, const char *encoder_name,
				  const char *crtc_name);

void igt_vkms_device_add_connector(igt_vkms_t *dev, const char *name);
int igt_vkms_connector_get_status(igt_vkms_t *dev, const char *name);
void igt_vkms_connector_set_status(igt_vkms_t *dev, const char *name,
				   int status);
bool igt_vkms_connector_attach_encoder(igt_vkms_t *dev,
				       const char *connector_name,
				       const char *encoder_name);
bool igt_vkms_connector_detach_encoder(igt_vkms_t *dev,
				       const char *connector_name,
				       const char *encoder_name);

#endif /* __IGT_VKMS_H__ */
