/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Google LLC.
 * Copyright © 2023 Collabora, Ltd.
 * Copyright © 2024 Red Hat, Inc.
 * Copyright © 2025 Intel Corporation
 */

#ifndef __IGT_CONFIGFS_H__
#define __IGT_CONFIGFS_H__

const char *igt_configfs_mount(void);
int igt_configfs_open(const char *name);

#endif /* __IGT_CONFIGFS_H__ */
