// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

/**
 * TEST: kms histogram
 * Category: Display
 * Description: Test to verify histogram features.
 * Functionality: histogram
 * Mega feature: Display
 * Test category: functionality test
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "igt.h"
#include "igt_vec.h"
#ifdef HAVE_LIBGHE
#include "ghe.h"
#endif

#define GLOBAL_HIST_DISABLE		0
#define GLOBAL_HIST_ENABLE		1
#define GLOBAL_HIST_DELAY		2
#define FLIP_COUNT			20

/**
 * SUBTEST: global-basic
 * Description: Test to enable histogram, flip monochrome fbs, wait for
 *		histogram event and then read the histogram data
 *
 * SUBTEST: global-color
 * Description: Test to enable histogram, flip color fbs, wait for
 *		histogram event and then read the histogram data
 *
 * SUBTEST: algo-basic
 * Description: Test to enable histogram, flip monochrome fbs, wait for
 *		histogram event and then read the histogram data and enhance pixels by
 *		multiplying by a pixel factor using algo
 *
 * SUBTEST: algo-color
 * Description: Test to enable histogram, flip color fbs, wait for histogram event
 *		and then read the histogram data and enhance pixels by multiplying
 *		by a pixel factor using algo
 */

IGT_TEST_DESCRIPTION("This test will verify the display histogram.");

typedef struct data {
	igt_display_t display;
	int drm_fd;
	igt_fb_t fb[5];
} data_t;

typedef void (*test_t)(data_t*, enum pipe, igt_output_t*, drmModePropertyBlobRes*);

static void enable_and_verify_global_histogram(data_t *data, enum pipe pipe)
{
	uint32_t global_hist_value;

	/* Enable global_hist */
	igt_pipe_set_prop_value(&data->display, pipe, IGT_CRTC_HISTOGRAM, GLOBAL_HIST_ENABLE);
	igt_display_commit2(&data->display, COMMIT_ATOMIC);

	/* Verify if global_hist is enabled */
	global_hist_value = igt_pipe_obj_get_prop(&data->display.pipes[pipe], IGT_CRTC_HISTOGRAM);
	igt_assert_f(global_hist_value == GLOBAL_HIST_ENABLE, "Failed to enable global_hist\n");
}

static void disable_and_verify_global_histogram(data_t *data, enum pipe pipe)
{
	uint32_t global_hist_value;

	/* Disable global_hist */
	igt_pipe_set_prop_value(&data->display, pipe, IGT_CRTC_HISTOGRAM, GLOBAL_HIST_DISABLE);
	igt_display_commit2(&data->display, COMMIT_ATOMIC);

	/* Verify if global_hist is disabled */
	global_hist_value = igt_pipe_obj_get_prop(&data->display.pipes[pipe], IGT_CRTC_HISTOGRAM);
	igt_assert_f(global_hist_value == GLOBAL_HIST_DISABLE, "Failed to disable global_hist\n");
}

static void cleanup_pipe(data_t *data, enum pipe pipe, igt_output_t *output)
{
	igt_plane_t *plane;

	disable_and_verify_global_histogram(data, pipe);

	for_each_plane_on_pipe(&data->display, pipe, plane)
		igt_plane_set_fb(plane, NULL);
	igt_output_set_pipe(output, PIPE_NONE);
	igt_display_commit2(&data->display, COMMIT_ATOMIC);
	igt_remove_fb(data->display.drm_fd, &data->fb[0]);
	igt_remove_fb(data->display.drm_fd, &data->fb[1]);
	igt_remove_fb(data->display.drm_fd, &data->fb[2]);
	igt_remove_fb(data->display.drm_fd, &data->fb[3]);
	igt_remove_fb(data->display.drm_fd, &data->fb[4]);
}

static drmModePropertyBlobRes *get_global_histogram_data(data_t *data, enum pipe pipe)
{
	uint64_t blob_id;

	blob_id = igt_pipe_obj_get_prop(&data->display.pipes[pipe],
					IGT_CRTC_GLOBAL_HISTOGRAM);
	if (blob_id == 0)
		return NULL;

	return drmModeGetPropertyBlob(data->drm_fd, blob_id);
}

static void read_global_histogram(data_t *data, enum pipe pipe,
				  drmModePropertyBlobRes **hist_blob_ptr)
{
	uint32_t *histogram_ptr;
	drmModePropertyBlobRes *global_hist_blob = NULL;

	igt_set_timeout(GLOBAL_HIST_DELAY, "Waiting to read global histogram blob.\n");
	do {
		global_hist_blob = get_global_histogram_data(data, pipe);
	} while (!global_hist_blob);

	igt_reset_timeout();

	*hist_blob_ptr = global_hist_blob;
	histogram_ptr = (uint32_t *)global_hist_blob->data;
	for (int i = 0; i < global_hist_blob->length / sizeof(*histogram_ptr); i++)
		igt_debug("Histogram[%d] = %d\n", i, *(histogram_ptr++));
}

#ifdef HAVE_LIBGHE
static void set_pixel_factor(igt_pipe_t *pipe, uint32_t *dietfactor, size_t size)
{
	uint32_t i;

	for (i = 0; i < size; i++) {
		/* Displaying IET LUT */
		igt_debug("Pixel Factor[%d] = %d\n", i, *(dietfactor + i));
	}

	igt_pipe_obj_replace_prop_blob(pipe, IGT_CRTC_GLOBAL_HIST_PIXEL_FACTOR,
				       dietfactor, size);
}

static struct globalhist_args *algo_get_pixel_factor(drmModePropertyBlobRes *global_hist_blob,
						     igt_output_t *output)
{
	struct globalhist_args *argsPtr =
		(struct globalhist_args *)malloc(sizeof(struct globalhist_args));

	drmModeModeInfo *mode;

	mode = igt_output_get_mode(output);

	memcpy(argsPtr->histogram, global_hist_blob->data, global_hist_blob->length);
	argsPtr->resolution_x = mode->hdisplay;
	argsPtr->resolution_y = mode->vdisplay;

	igt_debug("Making call to global histogram algorithm.\n");
	histogram_compute_generate_data_bin(argsPtr);

	return argsPtr;
}

static void algo_image_enhancement_factor(data_t *data, enum pipe pipe,
					  igt_output_t *output,
					  drmModePropertyBlobRes *global_hist_blob)
{
	struct globalhist_args *args = algo_get_pixel_factor(global_hist_blob, output);

	igt_assert(args);
	igt_debug("Writing pixel factor blob.\n");

	set_pixel_factor(&data->display.pipes[pipe], args->dietfactor,
			 ARRAY_SIZE(args->dietfactor));
	free(args);

	igt_display_commit2(&data->display, COMMIT_ATOMIC);
}
#endif

static void create_monochrome_fbs(data_t *data, drmModeModeInfo *mode)
{
	/* TODO: Extend the tests for different formats/modifiers. */
	/* These frame buffers used to flip monochrome fbs to get histogram event. */
	igt_assert(igt_create_color_pattern_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
					       DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR,
					       0, 0, 0, &data->fb[0]));

	igt_assert(igt_create_color_pattern_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
					       DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR,
					       1, 1, 1, &data->fb[1]));
}

static void create_color_fbs(data_t *data, drmModeModeInfo *mode)
{
	/* TODO: Extend the tests for different formats/modifiers. */
	/* These frame buffers used to flip color fbs to get histogram event. */
	igt_assert(igt_create_color_pattern_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
					       DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR,
					       0.5, 0, 0.5, &data->fb[0]));

	igt_assert(igt_create_color_pattern_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
					       DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR,
					       1, 0, 0, &data->fb[1]));

	igt_assert(igt_create_color_pattern_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
					       DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR,
					       0, 1, 0, &data->fb[2]));

	igt_assert(igt_create_color_pattern_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
					       DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR,
					       0, 0, 1, &data->fb[3]));

	igt_assert(igt_create_color_pattern_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
					       DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR,
					       1, 0, 1, &data->fb[4]));
}

static void flip_fb(data_t *data, enum pipe pipe, igt_output_t *output, struct igt_fb *fb)
{
	igt_plane_set_fb(igt_output_get_plane_type(output, DRM_PLANE_TYPE_PRIMARY), fb);
	igt_display_commit2(&data->display, COMMIT_ATOMIC);
}

static void prepare_pipe(data_t *data, enum pipe pipe, igt_output_t *output, bool color_fb)
{
	int i;
	struct udev_monitor *mon = igt_watch_uevents();
	drmModeModeInfo *mode = igt_output_get_mode(output);
	bool event_detected = false;
	int fb_count = color_fb ? 5 : 2;

	if (color_fb)
		create_color_fbs(data, mode);
	else
		create_monochrome_fbs(data, mode);

	flip_fb(data, pipe, output, &data->fb[0]);
	enable_and_verify_global_histogram(data, pipe);

	igt_flush_uevents(mon);
	for (i = 1; i <= FLIP_COUNT; i++) {
		flip_fb(data, pipe, output, &data->fb[i % fb_count]);

		/* Check for histogram event on every flip and break the loop if detected. */
		if (igt_global_histogram_event_detected(mon, 0)) {
			event_detected = true;
			break;
		}
	}

	igt_cleanup_uevents(mon);
	igt_assert_f(event_detected, "Histogram event not generated.\n");
}

static void run_global_histogram_pipeline(data_t *data, enum pipe pipe, igt_output_t *output,
					  bool color_fb, test_t test_pixel_factor)
{
	drmModePropertyBlobRes *global_hist_blob = NULL;

	prepare_pipe(data, pipe, output, color_fb);

	read_global_histogram(data, pipe, &global_hist_blob);

	if (test_pixel_factor)
		test_pixel_factor(data, pipe, output, global_hist_blob);

	drmModeFreePropertyBlob(global_hist_blob);
	cleanup_pipe(data, pipe, output);
}

static void run_tests_for_global_histogram(data_t *data, bool color_fb,
					   test_t test_pixel_factor)
{
	enum pipe pipe;
	igt_output_t *output;

	for_each_connected_output(&data->display, output) {
		for_each_pipe(&data->display, pipe) {
			if (!igt_pipe_obj_has_prop(&data->display.pipes[pipe], IGT_CRTC_HISTOGRAM))
				continue;

			igt_display_reset(&data->display);

			igt_output_set_pipe(output, pipe);
			if (!intel_pipe_output_combo_valid(&data->display))
				continue;

			igt_dynamic_f("pipe-%s-%s", kmstest_pipe_name(pipe), igt_output_name(output))
				run_global_histogram_pipeline(data, pipe, output, color_fb, test_pixel_factor);
		}
	}
}

static void run_algo_test(data_t *data, bool color_fb)
{
#ifdef HAVE_LIBGHE
	run_tests_for_global_histogram(data, color_fb, algo_image_enhancement_factor);
#else
	igt_skip("Histogram algorithm library not found.\n");
#endif
}

igt_main
{
	data_t data = {};

	igt_fixture {
		data.drm_fd = drm_open_driver_master(DRIVER_ANY);
		kmstest_set_vt_graphics_mode();
		igt_display_require(&data.display, data.drm_fd);
		igt_display_require_output(&data.display);
		igt_require(data.display.is_atomic);
	}

	igt_describe("Test to enable histogram, flip monochrome fbs, wait for histogram "
		     "event and then read the histogram data.");
	igt_subtest_with_dynamic("global-basic")
		run_tests_for_global_histogram(&data, false, NULL);

	igt_describe("Test to enable histogram, flip color fbs, wait for histogram event "
		     "and then read the histogram data.");
	igt_subtest_with_dynamic("global-color")
		run_tests_for_global_histogram(&data, true, NULL);

	igt_describe("Test to enable histogram, flip monochrome fbs, wait for histogram "
		     "event and then read the histogram data and enhance pixels by multiplying "
		     "by a pixel factor using algo.");
	igt_subtest_with_dynamic("algo-basic")
		run_algo_test(&data, false);

	igt_describe("Test to enable histogram, flip color fbs, wait for histogram event "
		     "and then read the histogram data and enhance pixels by multiplying "
		     "by a pixel factor using algo.");
	igt_subtest_with_dynamic("algo-color")
		run_algo_test(&data, true);

	igt_fixture {
		igt_display_fini(&data.display);
		drm_close_driver(data.drm_fd);
	}
}
