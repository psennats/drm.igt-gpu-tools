// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Google LLC.
 * Copyright © 2023 Collabora, Ltd.
 * Copyright © 2024-2025 Red Hat, Inc.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "igt.h"
#include "igt_vkms.h"

#define VKMS_ROOT_DIR_NAME		"vkms"
#define VKMS_FILE_ENABLED		"enabled"
#define VKMS_FILE_PLANE_TYPE		"type"
#define VKMS_FILE_CRTC_WRITEBACK	"writeback"
#define VKMS_FILE_CONNECTOR_STATUS	"status"

enum vkms_pipeline_item {
	VKMS_PIPELINE_ITEM_PLANE,
	VKMS_PIPELINE_ITEM_CRTC,
	VKMS_PIPELINE_ITEM_ENCODER,
	VKMS_PIPELINE_ITEM_CONNECTOR,
};

/**
 * SECTION:igt_vkms
 * @short_description: Helpers to create and configure VKMS devices
 * @title: VKMS
 * @include: igt_vkms.h
 *
 * Helpers for creating VKMS devices and configuring them dynamically.
 *
 * First, create a VKMS device, next, add pipeline items (planes, CRTCs,
 * encoders, CRTCs and connectors) compose the pipeline by attaching each item
 * using the _attach_ functions and finally, enable the VKMS device.
 */

static const char *mount_vkms_configfs(void)
{
	static char vkms_root_path[PATH_MAX];
	const char *configfs_path;
	int ret;

	configfs_path = igt_configfs_mount();
	igt_assert_f(configfs_path, "Error mounting configfs");

	ret = snprintf(vkms_root_path, sizeof(vkms_root_path), "%s/%s",
		       configfs_path, VKMS_ROOT_DIR_NAME);
	igt_assert(ret >= 0 && ret < sizeof(vkms_root_path));

	return vkms_root_path;
}

static int read_int(const char *path)
{
	FILE *file;
	int value;
	int ret;

	file = fopen(path, "r");
	igt_assert_f(file, "Error opening '%s'\n", path);

	ret = fscanf(file, "%d", &value);
	fclose(file);
	igt_assert_f(ret == 1, "Error reading integer from '%s'\n", path);

	return value;
}

static bool read_bool(const char *path)
{
	int ret;

	ret = read_int(path);

	return !!ret;
}

static void write_int(const char *path, int value)
{
	FILE *file;
	int ret;

	file = fopen(path, "w");
	igt_assert_f(file, "Error opening '%s'\n", path);

	ret = fprintf(file, "%d", value);
	igt_assert_f(ret >= 0, "Error writing to '%s'\n", path);

	fclose(file);
}

static void write_bool(const char *path, bool value)
{
	write_int(path, value ? 1 : 0);
}

static const char *get_pipeline_item_dir_name(enum vkms_pipeline_item item)
{
	switch (item) {
	case VKMS_PIPELINE_ITEM_PLANE:
		return "planes";
	case VKMS_PIPELINE_ITEM_CRTC:
		return "crtcs";
	case VKMS_PIPELINE_ITEM_ENCODER:
		return "encoders";
	case VKMS_PIPELINE_ITEM_CONNECTOR:
		return "connectors";
	}

	igt_assert(!"Cannot be reached: Unknown VKMS pipeline item type");
}

static void get_pipeline_item_path(igt_vkms_t *dev,
				   enum vkms_pipeline_item item,
				   const char *name, char *path, size_t len)
{
	const char *item_dir_name;
	int ret;

	item_dir_name = get_pipeline_item_dir_name(item);
	ret = snprintf(path, len, "%s/%s/%s", dev->path, item_dir_name, name);
	igt_assert(ret >= 0 && ret < len);
}

static void get_pipeline_item_file_path(igt_vkms_t *dev,
					enum vkms_pipeline_item item,
					const char *name, const char *filename,
					char *path, size_t len)
{
	char item_path[PATH_MAX];
	int ret;

	get_pipeline_item_path(dev, item, name, item_path, sizeof(item_path));

	ret = snprintf(path, len, "%s/%s", item_path, filename);
	igt_assert(ret >= 0 && ret < len);
}

static void add_pipeline_item(igt_vkms_t *dev, enum vkms_pipeline_item item,
			      const char *name)
{
	char path[PATH_MAX];
	int ret;

	get_pipeline_item_path(dev, item, name, path, sizeof(path));

	ret = mkdir(path, 0777);
	igt_assert_f(ret == 0,
		     "Unable to mkdir directory '%s'. Got errno=%d (%s)\n",
		     path, errno, strerror(errno));
}

static const char *get_attach_dir_name(enum vkms_pipeline_item item)
{
	switch (item) {
	case VKMS_PIPELINE_ITEM_PLANE:
		return "possible_crtcs";
	case VKMS_PIPELINE_ITEM_ENCODER:
		return "possible_crtcs";
	case VKMS_PIPELINE_ITEM_CONNECTOR:
		return "possible_encoders";
	default:
		break;
	}

	igt_assert(!"Cannot be reached: Unknown VKMS attach directory name");
}

static void get_attach_dir_path(igt_vkms_t *dev, enum vkms_pipeline_item item,
				const char *name, char *path, size_t len)
{
	const char *item_dir_name;
	const char *attach_dir_name;
	int ret;

	item_dir_name = get_pipeline_item_dir_name(item);
	attach_dir_name = get_attach_dir_name(item);

	ret = snprintf(path, len, "%s/%s/%s/%s", dev->path, item_dir_name, name,
		       attach_dir_name);
	igt_assert(ret >= 0 && ret < len);
}

static bool remove_pipeline_item(igt_vkms_t *dev, enum vkms_pipeline_item item,
				 const char *name)
{
	char path[PATH_MAX];
	int ret;

	get_pipeline_item_path(dev, item, name, path, sizeof(path));

	ret = rmdir(path);
	return ret == 0;
}

static bool attach_pipeline_item(igt_vkms_t *dev,
				 enum vkms_pipeline_item src_item,
				 const char *src_item_name,
				 enum vkms_pipeline_item dst_item,
				 const char *dst_item_name)
{
	char src_attach_path[PATH_MAX];
	char src_path[PATH_MAX];
	char dst_path[PATH_MAX];
	int ret;

	get_attach_dir_path(dev, src_item, src_item_name, src_attach_path,
			    sizeof(src_attach_path));
	ret = snprintf(src_path, sizeof(src_path), "%s/%s", src_attach_path,
		       dst_item_name);
	igt_assert(ret >= 0 && ret < sizeof(src_path));

	get_pipeline_item_path(dev, dst_item, dst_item_name, dst_path,
			       sizeof(dst_path));

	ret = symlink(dst_path, src_path);
	return ret == 0;
}

static bool detach_pipeline_item(igt_vkms_t *dev,
				 enum vkms_pipeline_item src_item,
				 const char *src_item_name,
				 const char *dst_item_name)
{
	char attach_path[PATH_MAX];
	char link_path[PATH_MAX];
	int ret;

	get_attach_dir_path(dev, src_item, src_item_name, attach_path,
			    sizeof(attach_path));

	ret = snprintf(link_path, sizeof(link_path), "%s/%s", attach_path,
		       dst_item_name);
	igt_assert(ret >= 0 && ret < sizeof(link_path));

	ret = unlink(link_path);
	return ret == 0;
}

/**
 * igt_require_vkms_configfs:
 *
 * Require that VKMS supports configfs configuration.
 */
void igt_require_vkms_configfs(void)
{
	const char *vkms_root_path;
	DIR *dir;

	vkms_root_path = mount_vkms_configfs();

	dir = opendir(vkms_root_path);
	igt_require(dir);
	if (dir)
		closedir(dir);
}

/**
 * igt_vkms_get_device_enabled_path:
 * @dev: Device to get the enabled path from
 * @path: Output path
 * @len: Maximum @path length
 *
 * Returns the device "enabled" file path.
 */
void igt_vkms_get_device_enabled_path(igt_vkms_t *dev, char *path, size_t len)
{
	int ret;

	ret = snprintf(path, len, "%s/%s", dev->path, VKMS_FILE_ENABLED);
	igt_assert(ret >= 0 && ret < len);
}

/**
 * igt_vkms_get_plane_path:
 * @dev: Device containing the plane
 * @name: Plane name
 * @path: Output path
 * @len: Maximum @path length
 *
 * Returns the plane path.
 */
void igt_vkms_get_plane_path(igt_vkms_t *dev, const char *name, char *path,
			     size_t len)
{
	get_pipeline_item_path(dev, VKMS_PIPELINE_ITEM_PLANE, name, path, len);
}

/**
 * igt_vkms_get_plane_type_path:
 * @dev: Device containing the plane
 * @name: Plane name
 * @path: Output path
 * @len: Maximum @path length
 *
 * Returns the plane "type" file path.
 */
void igt_vkms_get_plane_type_path(igt_vkms_t *dev, const char *name, char *path,
				  size_t len)
{
	get_pipeline_item_file_path(dev, VKMS_PIPELINE_ITEM_PLANE, name,
				    VKMS_FILE_PLANE_TYPE, path, len);
}

/**
 * igt_vkms_get_plane_possible_crtcs_path:
 * @dev: Device containing the plane
 * @name: Plane name
 * @path: Output path
 * @len: Maximum @path length
 *
 * Returns the plane "possible_crtcs" directory path.
 */
void igt_vkms_get_plane_possible_crtcs_path(igt_vkms_t *dev, const char *name,
					    char *path, size_t len)
{
	get_attach_dir_path(dev, VKMS_PIPELINE_ITEM_PLANE, name, path, len);
}

/**
 * igt_vkms_get_crtc_path:
 * @dev: Device containing the CRTC
 * @name: CRTC name
 * @path: Output path
 * @len: Maximum @path length
 *
 * Returns the plane path.
 */
void igt_vkms_get_crtc_path(igt_vkms_t *dev, const char *name, char *path,
			    size_t len)
{
	get_pipeline_item_path(dev, VKMS_PIPELINE_ITEM_CRTC, name, path, len);
}

/**
 * igt_vkms_get_crtc_writeback_path:
 * @dev: Device containing the CRTC
 * @name: CRTC name
 * @path: Output path
 * @len: Maximum @path length
 *
 * Returns the CRTC "writeback" file path.
 */
void igt_vkms_get_crtc_writeback_path(igt_vkms_t *dev, const char *name,
				      char *path, size_t len)
{
	get_pipeline_item_file_path(dev, VKMS_PIPELINE_ITEM_CRTC, name,
				    VKMS_FILE_CRTC_WRITEBACK, path, len);
}

/**
 * igt_vkms_get_encoder_path:
 * @dev: Device containing the encoder
 * @name: Encoder name
 * @path: Output path
 * @len: Maximum @path length
 *
 * Returns the encoder path.
 */
void igt_vkms_get_encoder_path(igt_vkms_t *dev, const char *name, char *path,
			       size_t len)
{
	get_pipeline_item_path(dev, VKMS_PIPELINE_ITEM_ENCODER, name, path, len);
}

/**
 * igt_vkms_get_encoder_possible_crtcs_path:
 * @dev: Device containing the encoder
 * @name: Encoder name
 * @path: Output path
 * @len: Maximum @path length
 *
 * Returns the encoder "possible_crtcs" directory path.
 */
void igt_vkms_get_encoder_possible_crtcs_path(igt_vkms_t *dev, const char *name,
					      char *path, size_t len)
{
	get_attach_dir_path(dev, VKMS_PIPELINE_ITEM_ENCODER, name, path, len);
}

/**
 * igt_vkms_get_connector_path:
 * @dev: Device containing the connector
 * @name: Connector name
 * @path: Output path
 * @len: Maximum @path length
 *
 * Returns the connector path.
 */
void igt_vkms_get_connector_path(igt_vkms_t *dev, const char *name, char *path,
				 size_t len)
{
	get_pipeline_item_path(dev, VKMS_PIPELINE_ITEM_CONNECTOR, name, path, len);
}

/**
 * igt_vkms_get_connector_status_path:
 * @dev: Device containing the connector
 * @name: Connector name
 * @path: Output path
 * @len: Maximum @path length
 *
 * Returns the connector "status" file path.
 */
void igt_vkms_get_connector_status_path(igt_vkms_t *dev, const char *name,
					char *path, size_t len)
{
	get_pipeline_item_file_path(dev, VKMS_PIPELINE_ITEM_CONNECTOR, name,
				    VKMS_FILE_CONNECTOR_STATUS, path, len);
}

/**
 * igt_vkms_get_connector_possible_encoders_path:
 * @dev: Device containing the connector
 * @name: Connector name
 * @path: Output path
 * @len: Maximum @path length
 *
 * Returns the connector "possible_encoders" directory path.
 */
void igt_vkms_get_connector_possible_encoders_path(igt_vkms_t *dev,
						   const char *name, char *path,
						   size_t len)
{
	get_attach_dir_path(dev, VKMS_PIPELINE_ITEM_CONNECTOR, name, path, len);
}

/**
 * igt_vkms_device_create:
 * @name: VKMS device name
 *
 * Create a directory in the ConfigFS VKMS root directory, where the entire
 * pipeline will be configured.
 */
igt_vkms_t *igt_vkms_device_create(const char *name)
{
	igt_vkms_t *dev;
	const char *vkms_root_path;
	size_t path_len;
	DIR *dir;
	int ret;

	dev = calloc(1, sizeof(*dev));

	vkms_root_path = mount_vkms_configfs();

	path_len = strlen(vkms_root_path) + strlen(name) + 2;
	dev->path = malloc(path_len);
	ret = snprintf(dev->path, path_len, "%s/%s", vkms_root_path, name);
	igt_assert(ret >= 0 && ret < path_len);

	dir = opendir(dev->path);
	if (dir) {
		igt_debug("Device at path %s already exists\n", dev->path);
		closedir(dir);
	} else {
		ret = mkdir(dev->path, 0777);
		if (ret != 0) {
			free(dev->path);
			free(dev);
			dev = NULL;
		}
	}

	return dev;
}

/**
 * igt_vkms_device_create_from_config:
 * @cfg: Device configuration
 *
 * Create a VKMS device and set all the parameters specified by the
 * configuration.
 */
igt_vkms_t *igt_vkms_device_create_from_config(igt_vkms_config_t *cfg)
{
	igt_vkms_t *dev;
	igt_vkms_plane_config_t *plane;
	igt_vkms_crtc_config_t *crtc;
	igt_vkms_encoder_config_t *encoder;
	igt_vkms_connector_config_t *connector;
	const char *name;
	int n, i;

	igt_debug("Creating device from configuration:\n");
	igt_debug("\t- Device name: %s\n", cfg->device_name);

	dev = igt_vkms_device_create(cfg->device_name);
	if (!dev)
		return NULL;

	for (n = 0; (crtc = &cfg->crtcs[n])->name; n++) {
		igt_debug("\t- CRTC %d:\n", n);
		igt_debug("\t\t- name: %s\n", crtc->name);
		igt_debug("\t\t- writeback: %d\n", crtc->writeback);

		igt_vkms_device_add_crtc(dev, crtc->name);
		igt_vkms_crtc_set_writeback_enabled(dev, crtc->name,
						    crtc->writeback);
	}

	for (n = 0; (plane = &cfg->planes[n])->name; n++) {
		igt_debug("\t- Plane %d:\n", n);
		igt_debug("\t\t- name: %s\n", plane->name);
		igt_debug("\t\t- type: %d\n", plane->type);
		igt_debug("\t\t- possible_crtcs:\n");

		igt_vkms_device_add_plane(dev, plane->name);
		igt_vkms_plane_set_type(dev, plane->name, plane->type);

		for (i = 0; (name = plane->possible_crtcs[i]); i++) {
			igt_debug("\t\t\t- %s\n", name);

			igt_vkms_plane_attach_crtc(dev, plane->name, name);
		}
	}

	for (n = 0; (encoder = &cfg->encoders[n])->name; n++) {
		igt_debug("\t- Encoder %d:\n", n);
		igt_debug("\t\t- name: %s\n", encoder->name);
		igt_debug("\t\t- possible_crtcs:\n");

		igt_vkms_device_add_encoder(dev, encoder->name);

		for (i = 0; (name = encoder->possible_crtcs[i]); i++) {
			igt_debug("\t\t\t- %s\n", name);

			igt_vkms_encoder_attach_crtc(dev, encoder->name, name);
		}
	}

	for (n = 0; (connector = &cfg->connectors[n])->name; n++) {
		if (connector->status == 0)
			connector->status = DRM_MODE_CONNECTED;

		igt_debug("\t- Connector %d:\n", n);
		igt_debug("\t\t- name: %s\n", connector->name);
		igt_debug("\t\t- status: %d\n", connector->status);
		igt_debug("\t\t- possible_encoders:\n");

		igt_vkms_device_add_connector(dev, connector->name);
		igt_vkms_connector_set_status(dev, connector->name,
					      connector->status);

		for (i = 0; (name = connector->possible_encoders[i]); i++) {
			igt_debug("\t\t\t- %s\n", name);

			igt_vkms_connector_attach_encoder(dev, connector->name,
							  name);
		}
	}

	return dev;
}

static int detach_pipeline_items(const char *path, const struct stat *info,
				 const int typeflag, struct FTW *pathinfo)
{
	/*
	 * Level 4 are the links in the possible_* directories:
	 * vkms/<dev>/<pipeline items>/<pipeline item>/<possible_*>/<links>
	 */
	if (pathinfo->level == 4 && typeflag == FTW_SL) {
		igt_debug("Detaching pipeline item %s\n", path);
		return unlink(path);
	}

	/* Ignore the other files, they are removed by remove_pipeline_items */
	return 0;
}

static int remove_pipeline_items(const char *path, const struct stat *info,
				 const int typeflag, struct FTW *pathinfo)
{
	/* Level 0 is the device root directory: vkms/<dev> */
	if (pathinfo->level == 0) {
		igt_debug("Removing pipeline item %s\n", path);
		return rmdir(path);
	}

	/*
	 * Level 2 directories are the pipeline items:
	 * vkms/<dev>/<pipeline items>/<pipeline item>
	 */
	if (pathinfo->level == 2 && typeflag == FTW_DP) {
		igt_debug("Removing pipeline item %s\n", path);
		return rmdir(path);
	}

	/* Ignore the other files, they are removed by VKMS */
	return 0;
}

static int remove_device_dir(igt_vkms_t *dev)
{
	int ret;

	ret = nftw(dev->path, detach_pipeline_items, 64, FTW_DEPTH | FTW_PHYS);
	if (ret)
		return ret;

	ret = nftw(dev->path, remove_pipeline_items, 64, FTW_DEPTH | FTW_PHYS);
	return ret;
}

/**
 * igt_vkms_device_destroy:
 * @dev: Device to destroy
 *
 * Remove and free the VKMS device.
 */
void igt_vkms_device_destroy(igt_vkms_t *dev)
{
	int ret;

	igt_assert(dev);

	igt_vkms_device_set_enabled(dev, false);

	ret = remove_device_dir(dev);
	igt_assert_f(ret == 0,
		     "Unable to rmdir device directory '%s'. Got errno=%d (%s)\n",
		     dev->path, errno, strerror(errno));

	free(dev->path);
	free(dev);
}

/**
 * igt_vkms_destroy_all_devices:
 *
 * Remove all VKMS devices created via configfs.
 */
void igt_vkms_destroy_all_devices(void)
{
	igt_vkms_t *dev;
	const char *vkms_root_path;
	DIR *dir;
	struct dirent *ent;

	vkms_root_path = mount_vkms_configfs();
	dir = opendir(vkms_root_path);
	igt_assert_f(dir, "VKMS configfs directory not available at '%s'. "
		     "Got errno=%d (%s)\n", vkms_root_path, errno,
		     strerror(errno));

	while ((ent = readdir(dir)) != NULL) {
		if (strcmp(ent->d_name, ".") == 0 ||
		    strcmp(ent->d_name, "..") == 0)
			continue;

		dev = igt_vkms_device_create(ent->d_name);
		igt_vkms_device_destroy(dev);
	}

	closedir(dir);
}

/**
 * igt_vkms_device_is_enabled:
 * @dev: The device to check
 *
 * Indicate whether a VKMS device is enabled or not.
 */
bool igt_vkms_device_is_enabled(igt_vkms_t *dev)
{
	char path[PATH_MAX];

	igt_vkms_get_device_enabled_path(dev, path, sizeof(path));

	return read_bool(path);
}

/**
 * igt_vkms_device_set_enabled:
 * @dev: Device to enable or disable
 *
 * Enable or disable a VKMS device.
 */
void igt_vkms_device_set_enabled(igt_vkms_t *dev, bool enabled)
{
	char path[PATH_MAX];

	igt_vkms_get_device_enabled_path(dev, path, sizeof(path));

	write_bool(path, enabled);
}

/**
 * igt_vkms_device_add_plane:
 * @dev: Device to add the plane to
 * @name: Plane name
 *
 * Add a new plane to the VKMS device.
 */
void igt_vkms_device_add_plane(igt_vkms_t *dev, const char *name)
{
	add_pipeline_item(dev, VKMS_PIPELINE_ITEM_PLANE, name);
}

/**
 * igt_vkms_device_remove_plane:
 * @dev: Device to remove the plane from
 * @name: Plane name
 *
 * Remove an existing plane from the VKMS device.
 */
bool igt_vkms_device_remove_plane(igt_vkms_t *dev, const char *name)
{
	return remove_pipeline_item(dev, VKMS_PIPELINE_ITEM_PLANE, name);
}

/**
 * igt_vkms_plane_get_type:
 * @dev: Device the plane belongs to
 * @name: Plane name
 *
 * Return the plane type.
 */
int igt_vkms_plane_get_type(igt_vkms_t *dev, const char *name)
{
	char path[PATH_MAX];

	igt_vkms_get_plane_type_path(dev, name, path, sizeof(path));

	return read_int(path);
}

/**
 * igt_vkms_plane_set_type:
 * @dev: Device the plane belongs to
 * @name: Plane name
 * @type: DRM_PLANE_TYPE_OVERLAY, DRM_PLANE_TYPE_PRIMARY or
 * DRM_PLANE_TYPE_CURSOR
 *
 * Set a new type for the plane
 */
void igt_vkms_plane_set_type(igt_vkms_t *dev, const char *name, int type)
{
	char path[PATH_MAX];

	if (type != DRM_PLANE_TYPE_OVERLAY &&
	    type != DRM_PLANE_TYPE_PRIMARY &&
	    type != DRM_PLANE_TYPE_CURSOR)
		igt_assert(!"Cannot be reached: Unknown plane type");

	igt_vkms_get_plane_type_path(dev, name, path, sizeof(path));

	write_int(path, type);
}

/**
 * igt_vkms_plane_attach_crtc:
 * @dev: Target device
 * @plane_name: Target plane name
 * @crtc_name: Destination CRTC name
 *
 * Attach a plane to a CRTC. Return true on success and false on error.
 */
bool igt_vkms_plane_attach_crtc(igt_vkms_t *dev, const char *plane_name,
				const char *crtc_name)
{
	return attach_pipeline_item(dev, VKMS_PIPELINE_ITEM_PLANE, plane_name,
				    VKMS_PIPELINE_ITEM_CRTC, crtc_name);
}

/**
 * igt_vkms_plane_detach_crtc:
 * @dev: Target device
 * @plane_name: Target plane name
 * @crtc_name: Destination CRTC name
 *
 * Detach a plane from a CRTC. Return true on success and false on error.
 */
bool igt_vkms_plane_detach_crtc(igt_vkms_t *dev, const char *plane_name,
				const char *crtc_name)
{
	return detach_pipeline_item(dev, VKMS_PIPELINE_ITEM_PLANE, plane_name,
				    crtc_name);
}

/**
 * igt_vkms_device_add_crtc:
 * @dev: Device to add the CRTC to
 * @name: CRTC name
 *
 * Add a new CRTC to the VKMS device.
 */
void igt_vkms_device_add_crtc(igt_vkms_t *dev, const char *name)
{
	add_pipeline_item(dev, VKMS_PIPELINE_ITEM_CRTC, name);
}

/**
 * igt_vkms_device_remove_crtc:
 * @dev: Device to remove the CRTC from
 * @name: CRTC name
 *
 * Remove an existing CRTC from the VKMS device.
 */
bool igt_vkms_device_remove_crtc(igt_vkms_t *dev, const char *name)
{
	return remove_pipeline_item(dev, VKMS_PIPELINE_ITEM_CRTC, name);
}

/**
 * igt_vkms_crtc_is_writeback_enabled:
 * @dev: Device the CRTC belongs to
 * @name: CRTC name
 *
 * Indicate whether a VKMS CRTC writeback connector is enabled or not.
 */
bool igt_vkms_crtc_is_writeback_enabled(igt_vkms_t *dev, const char *name)
{
	char path[PATH_MAX];

	igt_vkms_get_crtc_writeback_path(dev, name, path, sizeof(path));

	return read_bool(path);
}

/**
 * igt_vkms_crtc_set_writeback_enabled:
 * @dev: Device the CRTC belongs to
 * @name: CRTC name
 * @writeback: Enable or disable the writeback connector
 *
 * Set the VKMS CRTC writeback connector is status.
 */
void igt_vkms_crtc_set_writeback_enabled(igt_vkms_t *dev, const char *name,
					 bool writeback)
{
	char path[PATH_MAX];

	igt_vkms_get_crtc_writeback_path(dev, name, path, sizeof(path));

	write_bool(path, writeback);
}

/**
 * igt_vkms_device_add_encoder:
 * @dev: Device to add the encoder to
 * @name: Encoder name
 *
 * Add a new encoder to the VKMS device.
 */
void igt_vkms_device_add_encoder(igt_vkms_t *dev, const char *name)
{
	add_pipeline_item(dev, VKMS_PIPELINE_ITEM_ENCODER, name);
}

/**
 * igt_vkms_device_remove_encoder:
 * @dev: Device to remove the encoder from
 * @name: Encoder name
 *
 * Remove an existing encoder from the VKMS device.
 */
bool igt_vkms_device_remove_encoder(igt_vkms_t *dev, const char *name)
{
	return remove_pipeline_item(dev, VKMS_PIPELINE_ITEM_ENCODER, name);
}

/**
 * igt_vkms_encoder_attach_crtc:
 * @dev: Target device
 * @encoder_name: Target encoder name
 * @crtc_name: Destination CRTC name
 *
 * Attach an encoder to a CRTC. Return true on success and false on error.
 */
bool igt_vkms_encoder_attach_crtc(igt_vkms_t *dev, const char *encoder_name,
				  const char *crtc_name)
{
	return attach_pipeline_item(dev, VKMS_PIPELINE_ITEM_ENCODER,
				    encoder_name, VKMS_PIPELINE_ITEM_CRTC,
				    crtc_name);
}

/**
 * igt_vkms_encoder_detach_crtc:
 * @dev: Target device
 * @encoder_name: Target encoder name
 * @crtc_name: Destination CRTC name
 *
 * Detach an encoder from a CRTC. Return true on success and false on error.
 */
bool igt_vkms_encoder_detach_crtc(igt_vkms_t *dev, const char *encoder_name,
				  const char *crtc_name)
{
	return detach_pipeline_item(dev, VKMS_PIPELINE_ITEM_ENCODER,
				    encoder_name, crtc_name);
}

/**
 * igt_vkms_device_add_connector:
 * @dev: Device to add the connector to
 * @name: Connector name
 *
 * Add a new connector to the VKMS device.
 */
void igt_vkms_device_add_connector(igt_vkms_t *dev, const char *name)
{
	add_pipeline_item(dev, VKMS_PIPELINE_ITEM_CONNECTOR, name);
}

/**
 * igt_vkms_connector_get_status:
 * @dev: Device the connector belongs to
 * @name: Connector name
 *
 * Return the connector status.
 */
int igt_vkms_connector_get_status(igt_vkms_t *dev, const char *name)
{
	char path[PATH_MAX];

	igt_vkms_get_connector_status_path(dev, name, path, sizeof(path));

	return read_int(path);
}

/**
 * igt_vkms_connector_set_status:
 * @dev: Device the connector belongs to
 * @name: Connector name
 * @type: DRM_MODE_CONNECTED, DRM_MODE_DISCONNECTED or
 * DRM_MODE_UNKNOWNCONNECTION
 *
 * Set a new status for the connector
 */
void igt_vkms_connector_set_status(igt_vkms_t *dev, const char *name,
				   int status)
{
	char path[PATH_MAX];

	if (status != DRM_MODE_CONNECTED &&
	    status != DRM_MODE_DISCONNECTED &&
	    status != DRM_MODE_UNKNOWNCONNECTION)
		igt_assert(!"Cannot be reached: Unknown connector status");

	igt_vkms_get_connector_status_path(dev, name, path, sizeof(path));

	write_int(path, status);
}

/**
 * igt_vkms_connector_attach_encoder:
 * @dev: Target device
 * @connector_name: Target connector name
 * @encoder_name: Destination encoder name
 *
 * Attach a connector to an encoder. Return true on success and false on error.
 */
bool igt_vkms_connector_attach_encoder(igt_vkms_t *dev,
				       const char *connector_name,
				       const char *encoder_name)
{
	return attach_pipeline_item(dev, VKMS_PIPELINE_ITEM_CONNECTOR,
				    connector_name, VKMS_PIPELINE_ITEM_ENCODER,
				    encoder_name);
}

/**
 * igt_vkms_connector_detach_encoder:
 * @dev: Target device
 * @connector_name: Target connector name
 * @encoder_name: Destination encoder name
 *
 * Detach a connector from an encoder. Return true on success and false on
 * error.
 */
bool igt_vkms_connector_detach_encoder(igt_vkms_t *dev,
				       const char *connector_name,
				       const char *encoder_name)
{
	return detach_pipeline_item(dev, VKMS_PIPELINE_ITEM_CONNECTOR,
				    connector_name, encoder_name);
}
