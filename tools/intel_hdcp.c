// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <fcntl.h>
#include <stdio.h>

#include "igt.h"

#define MAX_HDCP_BUF_LEN	5000

typedef struct data {
	int fd;
	igt_display_t display;
	struct igt_fb red, green;
	int height, width;
} data_t;

static const char *get_hdcp_version(int fd, char *connector_name)
{
	char buf[MAX_HDCP_BUF_LEN];
	int ret;

	ret = igt_debugfs_connector_dir(fd, connector_name, O_RDONLY);
	if (ret < 0) {
		fprintf(stderr, "Failed to open connector directory\n");
		return NULL;
	}

	if (is_intel_device(fd))
		igt_debugfs_simple_read(ret, "i915_hdcp_sink_capability", buf, sizeof(buf));
	else
		igt_debugfs_simple_read(ret, "hdcp_sink_capability", buf, sizeof(buf));

	close(ret);
	if (strstr(buf, "HDCP1.4") && strstr(buf, "HDCP2.2"))
		return "HDCP1.4 and HDCP2.2";
	else if (strstr(buf, "HDCP1.4"))
		return "HDCP1.4";
	else if (strstr(buf, "HDCP2.2"))
		return "HDCP2.2";
	else
		return "No HDCP support";
}

static void get_hdcp_info(data_t *data)
{
	char *output_name;
	drmModeRes *res = drmModeGetResources(data->fd);

	if (!res) {
		fprintf(stderr, "Failed to get DRM resources\n");
		return;
	}

	fprintf(stderr, "Connectors:\n");
	fprintf(stderr, "id\tencoder\tstatus\t\ttype\tHDCP\n");
	for (int i = 0; i < res->count_connectors; i++) {
		drmModeConnector *c;

		c = drmModeGetConnectorCurrent(data->fd, res->connectors[i]);

		if (!c)
			continue;

		asprintf(&output_name, "%s-%d",
			 kmstest_connector_type_str(c->connector_type),
			 c->connector_type_id);

		fprintf(stderr, "%d\t%d\t%s\t%s\t%s\n",
			c->connector_id, c->encoder_id,
			kmstest_connector_status_str(c->connection),
			kmstest_connector_type_str(c->connector_type),
			get_hdcp_version(data->fd, output_name));

		drmModeFreeConnector(c);
	}

	drmModeFreeResources(res);
}

static void print_usage(void)
{
	fprintf(stderr, "Usage: intel_hdcp [OPTIONS]\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "-i,	--info		Get HDCP Information\n");
	fprintf(stderr, "-h,	--help		Display this help message\n");
}

static void test_init(data_t *data)
{
	data->fd = __drm_open_driver(DRIVER_ANY);
	if (data->fd < 0) {
		fprintf(stderr, "Failed to open DRM driver\n");
		exit(EXIT_FAILURE);
	}
	igt_display_require(&data->display, data->fd);
	igt_display_require_output(&data->display);
}

int main(int argc, char **argv)
{
	data_t data;
	int option;
	static const char optstr[] = "hi";
	struct option long_opts[] = {
		{"help",	no_argument,	NULL, 'h'},
		{"info",	no_argument,	NULL, 'i'},
		{NULL,		0,		NULL,  0 }
	};

	test_init(&data);

	while ((option = getopt_long(argc, argv, optstr, long_opts, NULL)) != -1) {
		switch (option) {
		case 'i':
			get_hdcp_info(&data);
			break;
		case 'h':
		default:
			print_usage();
			break;
		}
	}
}
