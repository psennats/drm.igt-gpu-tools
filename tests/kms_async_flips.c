/*
 * Copyright © 2020 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *  Paulo Zanoni <paulo.r.zanoni@intel.com>
 *  Karthik B S <karthik.b.s@intel.com>
 */

/**
 * TEST: kms async flips
 * Category: Display
 * Description: Test asynchronous page flips.
 * Driver requirement: i915, xe
 * Functionality: async_flips, vblank
 * Mega feature: General Display Features
 * Test category: functionality test
 */

#include "igt.h"
#include "igt_aux.h"
#include "igt_psr.h"
#include <sys/ioctl.h>
#include <sys/time.h>
#include <poll.h>

/**
 * SUBTEST: alternate-sync-async-flip
 * Description: Verify the async flip functionality and the fps during async flips
 *              Alternate between sync and async flips
 *
 * SUBTEST: async-flip-with-page-flip-events
 * Description: Verify the async flip functionality and the fps during async flips
 *              Wait for page flip events in between successive asynchronous flips
 *
 * SUBTEST: test-time-stamp
 * Description: Verify the async flip functionality and the fps during async flips
 *              Verify that the async flip timestamp does not coincide with either
 *              previous or next vblank
 *
 * SUBTEST: test-cursor
 * Description: Verify that the DRM_IOCTL_MODE_CURSOR passes after async flip
 * Functionality: async_flips, cursor, vblank
 *
 * SUBTEST: crc
 * Description: Use CRC to verify async flip scans out the correct framebuffer
 * Functionality: async_flips, crc, vblank
 *
 * SUBTEST: invalid-async-flip
 * Description: Negative case to verify if changes in fb are rejected from kernel as expected
 *
 * SUBTEST: alternate-sync-async-flip-atomic
 * Description: Verify the async flip functionality and the fps during async flips using atomic path
 *
 * SUBTEST: async-flip-with-page-flip-events-atomic
 * Description: Wait for page flip events in between successive asynchronous flips using atomic path
 *
 * SUBTEST: test-time-stamp-atomic
 * Description: Verify that the async flip timestamp does not coincide with either previous
 *              or next vblank when async flip is done using atomic path
 *
 * SUBTEST: test-cursor-atomic
 * Description: Verify that the DRM_IOCTL_MODE_CURSOR passes after async flip with atomic commit
 *
 * SUBTEST: invalid-async-flip-atomic
 * Description: Negative case to verify if changes in fb are rejected from kernel
 *              as expected when async flip is done using atomic path
 *
 * SUBTEST: crc-atomic
 * Description: Use CRC to verify async flip scans out the correct framebuffer with atomic commit
 *
 * SUBTEST: async-flip-suspend-resume
 * Description: Verify the async flip functionality with suspend and resume cycle
 */

#define CURSOR_POS 128

/*
 * These constants can be tuned in case we start getting unexpected
 * results in CI.
 */

#define RUN_TIME 2
#define MIN_FLIPS_PER_FRAME 5
#define NUM_FBS 4

IGT_TEST_DESCRIPTION("Test asynchronous page flips.");

typedef struct {
	int drm_fd;
	uint32_t crtc_id;
	uint32_t refresh_rate;
	struct igt_fb bufs[NUM_FBS];
	igt_display_t display;
	igt_output_t *output;
	unsigned long flip_timestamp_us;
	double flip_interval;
	uint64_t modifier;
	igt_plane_t *plane;
	igt_pipe_crc_t *pipe_crc;
	igt_crc_t ref_crc;
	int flip_count;
	int frame_count;
	bool flip_pending;
	enum pipe pipe;
	bool alternate_sync_async;
	bool suspend_resume;
	bool allow_fail;
	struct buf_ops *bops;
	bool atomic_path;
} data_t;

static void flip_handler(int fd_, unsigned int sequence, unsigned int tv_sec,
			 unsigned int tv_usec, void *_data)
{
	data_t *data = _data;
	static double last_ms;
	double cur_ms;
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	cur_ms =  ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;

	if (last_ms)
		data->flip_interval = cur_ms - last_ms;
	else
		data->flip_interval = 0;

	last_ms = cur_ms;

	data->flip_timestamp_us = tv_sec * 1000000l + tv_usec;
}

static void wait_flip_event(data_t *data)
{
	int ret;
	drmEventContext evctx;
	struct pollfd pfd;

	evctx.version = 2;
	evctx.vblank_handler = NULL;
	evctx.page_flip_handler = flip_handler;

	pfd.fd = data->drm_fd;
	pfd.events = POLLIN;
	pfd.revents = 0;

	ret = poll(&pfd, 1, 2000);

	switch (ret) {
	case 0:
		igt_assert_f(0, "Flip Timeout\n");
		break;
	case 1:
		ret = drmHandleEvent(data->drm_fd, &evctx);
		igt_assert_eq(ret, 0);
		break;
	default:
		/* unexpected */
		igt_assert(0);
	}
}

static uint64_t default_modifier(data_t *data)
{
	if (igt_display_has_format_mod(&data->display, DRM_FORMAT_XRGB8888,
				       I915_FORMAT_MOD_X_TILED))
		return I915_FORMAT_MOD_X_TILED;
	else
		return DRM_FORMAT_MOD_LINEAR;
}

static void make_fb(data_t *data, struct igt_fb *fb,
		    uint32_t width, uint32_t height, int index)
{
	int rec_width;
	cairo_t *cr;

	rec_width = width / (NUM_FBS * 2);

	igt_create_color_fb(data->drm_fd, width, height, DRM_FORMAT_XRGB8888,
			    data->modifier, 0.0, 0.0, 0.5, fb);

	cr = igt_get_cairo_ctx(data->drm_fd, fb);
	igt_paint_color_rand(cr, rec_width * 2 + rec_width * index, 0, rec_width, height);
	igt_put_cairo_ctx(cr);
}

static void require_monotonic_timestamp(int fd)
{
	igt_require_f(igt_has_drm_cap(fd, DRM_CAP_TIMESTAMP_MONOTONIC),
		      "Monotonic timestamps not supported\n");
}

static void test_init(data_t *data)
{
	drmModeModeInfo *mode;

	igt_display_reset(&data->display);
	igt_display_commit(&data->display);

	mode = igt_output_get_mode(data->output);

	data->crtc_id = data->display.pipes[data->pipe].crtc_id;
	data->refresh_rate = mode->vrefresh;

	igt_output_set_pipe(data->output, data->pipe);

	data->plane = igt_output_get_plane_type(data->output, DRM_PLANE_TYPE_PRIMARY);
}

static void test_init_fbs(data_t *data)
{
	int i;
	uint32_t width, height;
	static uint32_t prev_output_id;
	static uint64_t prev_modifier;
	drmModeModeInfo *mode;

	mode = igt_output_get_mode(data->output);
	width = mode->hdisplay;
	height = mode->vdisplay;

	if (prev_output_id != data->output->id ||
	    prev_modifier != data->modifier) {
		prev_output_id = data->output->id;
		prev_modifier = data->modifier;

		if (data->bufs[0].fb_id) {
			for (i = 0; i < NUM_FBS; i++)
				igt_remove_fb(data->drm_fd, &data->bufs[i]);
		}

		for (i = 0; i < NUM_FBS; i++)
			make_fb(data, &data->bufs[i], width, height, i);
	}

	igt_plane_set_fb(data->plane, &data->bufs[0]);
	igt_plane_set_size(data->plane, width, height);
}

static bool async_flip_needs_extra_frame(data_t *data)
{
	uint32_t devid;

	if (!is_intel_device(data->drm_fd))
		return false;

	devid = intel_get_drm_devid(data->drm_fd);

	/*
	 * On BDW-GLK async address update bit is double buffered
	 * on vblank. So the first async flip will in fact be
	 * performed as a sync flip by the hardware.
	 *
	 * In order to allow the first async flip to change the modifier
	 * on SKL+ (needed by Xorg/modesetting), and to optimize
	 * watermarks/ddb for faster response on ADL+, we convert the
	 * first async flip to a sync flip.
	 */
	return intel_display_ver(devid) >= 9 || IS_BROADWELL(devid);
}

static int perform_flip(data_t *data, int frame, int flags)
{
	int ret;

	if (!data->atomic_path) {
		ret = drmModePageFlip(data->drm_fd, data->crtc_id,
				      data->bufs[frame % NUM_FBS].fb_id, flags, data);
	} else {
		igt_plane_set_fb(data->plane, &data->bufs[frame % NUM_FBS]);
		ret = igt_display_try_commit_atomic(&data->display, flags, data);
	}

	return ret;
}

static void test_async_flip(data_t *data)
{
	int ret, frame;
	long long int fps;
	struct timeval start, end, diff;
	int suspend_time = RUN_TIME / 2;

	igt_display_commit2(&data->display, data->display.is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);

	gettimeofday(&start, NULL);
	frame = 1;
	do {
		int flags = DRM_MODE_PAGE_FLIP_ASYNC | DRM_MODE_PAGE_FLIP_EVENT;

		if (data->alternate_sync_async) {
			flags &= ~DRM_MODE_PAGE_FLIP_ASYNC;

			ret = perform_flip(data, frame, flags);

			igt_assert_eq(ret, 0);

			wait_flip_event(data);

			flags |= DRM_MODE_PAGE_FLIP_ASYNC;

			if (async_flip_needs_extra_frame(data)) {

				ret = perform_flip(data, frame, flags);
				igt_assert_eq(ret, 0);

				wait_flip_event(data);
			}
		}

		ret = perform_flip(data, frame, flags);

		if (frame == 1 && data->allow_fail)
			igt_skip_on(ret == -EINVAL);
		else
			igt_assert_eq(ret, 0);

		wait_flip_event(data);

		gettimeofday(&end, NULL);
		timersub(&end, &start, &diff);

		if (data->alternate_sync_async) {
			igt_assert_f(data->flip_interval < 1000.0 / (data->refresh_rate * MIN_FLIPS_PER_FRAME),
				     "Flip interval not significantly smaller than vblank interval\n"
				     "Flip interval: %lfms, Refresh Rate = %dHz, Threshold = %d\n",
				     data->flip_interval, data->refresh_rate, MIN_FLIPS_PER_FRAME);
		}

		if (data->suspend_resume && diff.tv_sec == suspend_time) {
			data->suspend_resume = false;
			igt_system_suspend_autoresume(SUSPEND_STATE_MEM, SUSPEND_TEST_NONE);
		}
		frame++;
	} while (diff.tv_sec < RUN_TIME);

	if (!data->alternate_sync_async) {
		fps = frame * 1000 / RUN_TIME;
		igt_assert_f((fps / 1000) > (data->refresh_rate * MIN_FLIPS_PER_FRAME),
			     "FPS should be significantly higher than the refresh rate\n");
	}
}

static void wait_for_vblank(data_t *data, unsigned long *vbl_time, unsigned int *seq)
{
	drmVBlank wait_vbl;
	uint32_t pipe_id_flag;
	int pipe;

	memset(&wait_vbl, 0, sizeof(wait_vbl));
	pipe = kmstest_get_pipe_from_crtc_id(data->drm_fd, data->crtc_id);
	pipe_id_flag = kmstest_get_vbl_flag(pipe);

	wait_vbl.request.type = DRM_VBLANK_RELATIVE | pipe_id_flag;
	wait_vbl.request.sequence = 1;

	do_ioctl(data->drm_fd, DRM_IOCTL_WAIT_VBLANK, &wait_vbl);
	*vbl_time = wait_vbl.reply.tval_sec * 1000000 + wait_vbl.reply.tval_usec;
	*seq = wait_vbl.reply.sequence;
}

static void test_timestamp(data_t *data)
{
	int flags = DRM_MODE_PAGE_FLIP_ASYNC | DRM_MODE_PAGE_FLIP_EVENT;
	unsigned long vbl_time, vbl_time1;
	unsigned int seq, seq1;
	int ret;

	igt_display_commit2(&data->display, data->display.is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);

	/*
	 * In older platforms(<= gen10), async address update bit is double buffered.
	 * So flip timestamp can be verified only from the second flip.
	 * The first async flip just enables the async address update.
	 */
	ret = drmModePageFlip(data->drm_fd, data->crtc_id,
			      data->bufs[0].fb_id,
			      flags, data);

	igt_assert_eq(ret, 0);

	wait_flip_event(data);

	wait_for_vblank(data, &vbl_time, &seq);

	ret = drmModePageFlip(data->drm_fd, data->crtc_id,
			      data->bufs[0].fb_id,
			      flags, data);

	igt_assert_eq(ret, 0);

	wait_flip_event(data);

	wait_for_vblank(data, &vbl_time1, &seq1);

	/* TODO: Make changes to do as many flips as possbile between two vblanks */

	igt_assert_f(seq1 == seq + 1,
		     "Vblank sequence is expected to be incremented by one(%d != (%d + 1)\n", seq1, seq);

	igt_info("vbl1_timestamp = %ldus\nflip_timestamp = %ldus\nvbl2_timestamp = %ldus\n",
		 vbl_time, data->flip_timestamp_us, vbl_time1);

	igt_assert_f(vbl_time <= data->flip_timestamp_us && vbl_time1 > data->flip_timestamp_us,
		     "Async flip time stamp is expected to be in between 2 vblank time stamps\n");
}

static void test_cursor(data_t *data)
{
	int flags = DRM_MODE_PAGE_FLIP_ASYNC | DRM_MODE_PAGE_FLIP_EVENT;
	int ret;
	uint64_t width, height;
	struct igt_fb cursor_fb;
	struct drm_mode_cursor cur;

	igt_display_commit2(&data->display, data->display.is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);

	/*
	 * Intel's PSR2 selective fetch adds other planes to state when
	 * necessary, causing the async flip to fail because async flip is not
	 * supported in cursor plane.
	 */
	igt_skip_on_f(i915_psr2_selective_fetch_check(data->drm_fd, NULL),
		      "PSR2 sel fetch causes cursor to be added to primary plane " \
		      "pages flips and async flip is not supported in cursor\n");

	do_or_die(drmGetCap(data->drm_fd, DRM_CAP_CURSOR_WIDTH, &width));
	do_or_die(drmGetCap(data->drm_fd, DRM_CAP_CURSOR_WIDTH, &height));

	igt_create_color_fb(data->drm_fd, width, height, DRM_FORMAT_ARGB8888,
			    DRM_FORMAT_MOD_LINEAR, 1., 1., 1., &cursor_fb);

	cur.flags = DRM_MODE_CURSOR_BO;
	cur.crtc_id = data->crtc_id;
	cur.width = width;
	cur.height = height;
	cur.handle = cursor_fb.gem_handle;

	do_ioctl(data->drm_fd, DRM_IOCTL_MODE_CURSOR, &cur);

	ret = perform_flip(data, 0, flags);

	igt_assert_eq(ret, 0);

	wait_flip_event(data);

	cur.flags = DRM_MODE_CURSOR_MOVE;
	cur.x = CURSOR_POS;
	cur.y = CURSOR_POS;

	do_ioctl(data->drm_fd, DRM_IOCTL_MODE_CURSOR, &cur);

	igt_remove_fb(data->drm_fd, &cursor_fb);
}

static void test_invalid(data_t *data)
{
	int ret, width, height;
	struct igt_fb fb[2];
	drmModeModeInfo *mode;
	int flags;

	igt_display_commit2(&data->display, data->display.is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);

	mode = igt_output_get_mode(data->output);
	width = mode->hdisplay;
	height = mode->vdisplay;

	flags = DRM_MODE_PAGE_FLIP_ASYNC | DRM_MODE_PAGE_FLIP_EVENT;

	igt_create_fb(data->drm_fd, width, height, DRM_FORMAT_XRGB8888,
		      I915_FORMAT_MOD_X_TILED, &fb[0]);
	igt_create_fb(data->drm_fd, width, height, DRM_FORMAT_XRGB8888,
		      I915_FORMAT_MOD_Y_TILED, &fb[1]);

	igt_plane_set_fb(data->plane, &fb[0]);
	igt_display_commit2(&data->display, data->display.is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);

	/* Note: Not using perform_flips here as this subtest passes
	 * the fb_id parameter differently.
	 */
	if (!data->atomic_path) {
		/* first async flip is expected to allow modifier changes */
		ret = drmModePageFlip(data->drm_fd, data->crtc_id, fb[1].fb_id, flags, data);
		igt_assert_eq(ret, 0);

		wait_flip_event(data);

		/* subsequent async flips should reject modifier changes */
		ret = drmModePageFlip(data->drm_fd, data->crtc_id, fb[0].fb_id, flags, data);
		igt_assert(ret == -EINVAL);
	} else {
		igt_plane_set_fb(data->plane, &fb[1]);
		ret = igt_display_try_commit_atomic(&data->display, flags, data);
		igt_assert_eq(ret, 0);

		wait_flip_event(data);

		igt_plane_set_fb(data->plane, &fb[0]);
		ret = igt_display_try_commit_atomic(&data->display, flags, data);
		igt_assert(ret == -EINVAL);
	}

	/* TODO: Add verification for changes in stride, pixel format */

	igt_remove_fb(data->drm_fd, &fb[1]);
	igt_remove_fb(data->drm_fd, &fb[0]);
}

static void queue_vblank(data_t *data)
{
	int pipe = kmstest_get_pipe_from_crtc_id(data->drm_fd, data->crtc_id);
	drmVBlank wait_vbl = {
		.request.type = DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT |
			kmstest_get_vbl_flag(pipe),
		.request.sequence = 1,
		.request.signal = (long)data,
	};

	do_ioctl(data->drm_fd, DRM_IOCTL_WAIT_VBLANK, &wait_vbl);
}

static void vblank_handler_crc(int fd_, unsigned int sequence, unsigned int tv_sec,
			       unsigned int tv_usec, void *_data)
{
	data_t *data = _data;
	igt_crc_t crc;

	data->frame_count++;

	igt_pipe_crc_get_single(data->pipe_crc, &crc);
	igt_assert_crc_equal(&data->ref_crc, &crc);

	/* check again next vblank */
	queue_vblank(data);
}

static void flip_handler_crc(int fd_, unsigned int sequence, unsigned int tv_sec,
			     unsigned int tv_usec, void *_data)
{
	data_t *data = _data;

	data->flip_pending = false;
	data->flip_count++;
}

static void wait_events_crc(data_t *data)
{
	drmEventContext evctx = {
		.version = 2,
		.vblank_handler = vblank_handler_crc,
		.page_flip_handler = flip_handler_crc,
	};

	while (data->flip_pending) {
		struct pollfd pfd = {
			.fd = data->drm_fd,
			.events = POLLIN,
		};
		int ret;

		ret = poll(&pfd, 1, 2000);

		switch (ret) {
		case 0:
			igt_assert_f(0, "Flip Timeout\n");
			break;
		case 1:
			ret = drmHandleEvent(data->drm_fd, &evctx);
			igt_assert_eq(ret, 0);
			break;
		default:
			/* unexpected */
			igt_assert(0);
		}
	}
}

static unsigned int clock_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void paint_fb(data_t *data, struct igt_fb *fb,
		     int width, int height,
		     uint32_t color)
{
	if (is_intel_device(data->drm_fd)) {
		igt_draw_rect_fb(data->drm_fd, data->bops, 0, fb,
				 igt_draw_supports_method(data->drm_fd, IGT_DRAW_MMAP_GTT) ?
				 IGT_DRAW_MMAP_GTT : IGT_DRAW_MMAP_WC,
				 0, 0, width, height, color);
	} else {
		cairo_t *cr;

		cr = igt_get_cairo_ctx(data->drm_fd, fb);
		igt_paint_color(cr, 0, 0, width, height,
				((color & 0xff0000) >> 16) / 255.0,
				((color & 0xff00) >> 8) / 255.0,
				((color & 0xff) >> 9) / 255.0);
		igt_put_cairo_ctx(cr);
	}
}

static void test_crc(data_t *data)
{
	unsigned int frame = 0;
	unsigned int start;
	int ret, width, height;
	drmModeModeInfoPtr mode;

	igt_display_commit2(&data->display, data->display.is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);

	/* make things faster by using a smallish mode */
	mode = &data->output->config.connector->modes[0];
	width = mode->hdisplay;
	height = mode->vdisplay;

	data->flip_count = 0;
	data->frame_count = 0;
	data->flip_pending = false;

	paint_fb(data, &data->bufs[frame], width, height, 0xff0000ff);
	paint_fb(data, &data->bufs[!frame], width, height, 0xff0000ff);

	ret = drmModeSetCrtc(data->drm_fd, data->crtc_id, data->bufs[frame].fb_id, 0, 0,
			     &data->output->config.connector->connector_id, 1, mode);
	igt_assert_eq(ret, 0);

	data->pipe_crc = igt_pipe_crc_new(data->drm_fd,
					  kmstest_get_pipe_from_crtc_id(data->drm_fd, data->crtc_id),
					  IGT_PIPE_CRC_SOURCE_AUTO);

	igt_pipe_crc_start(data->pipe_crc);
	igt_pipe_crc_get_single(data->pipe_crc, &data->ref_crc);

	queue_vblank(data);

	start = clock_ms();

	while (clock_ms() - start < 2000) {
		/* fill the next fb with the expected color */
		paint_fb(data, &data->bufs[frame], 1, height, 0xff0000ff);

		data->flip_pending = true;

		ret = perform_flip(data, frame, DRM_MODE_PAGE_FLIP_ASYNC |
				   DRM_MODE_PAGE_FLIP_EVENT);
		igt_assert_eq(ret, 0);

		wait_events_crc(data);

		/* clobber the previous fb which should no longer be scanned out */
		frame = !frame;
		paint_fb(data, &data->bufs[frame], 1, height, rand());
	}

	igt_pipe_crc_stop(data->pipe_crc);
	igt_pipe_crc_free(data->pipe_crc);

	/* make sure we got at a reasonable number of async flips done */
	igt_assert_lt(data->frame_count * 2, data->flip_count);
}

static void run_test(data_t *data, void (*test)(data_t *))
{
	igt_display_t *display = &data->display;

	for_each_pipe_with_valid_output(display, data->pipe, data->output) {
		igt_display_reset(display);

		igt_output_set_pipe(data->output, data->pipe);
		if (!intel_pipe_output_combo_valid(display))
			continue;

		test_init(data);
		data->allow_fail = false;
		data->modifier = default_modifier(data);
		igt_dynamic_f("pipe-%s-%s", kmstest_pipe_name(data->pipe), data->output->name) {
			/*
			 * FIXME: joiner+async flip is busted currently in KMD.
			 * Remove this check once the issues are fixed in KMD.
			 */
			igt_skip_on_f(is_joiner_mode(data->drm_fd, data->output),
				      "Skipping, async flip not supported on joiner mode\n");
			test_init_fbs(data);
			test(data);
		}
	}
}

static void run_test_with_modifiers(data_t *data, void (*test)(data_t *))
{
	for_each_pipe_with_valid_output(&data->display, data->pipe, data->output) {
		test_init(data);

		for (int i = 0; i < data->plane->format_mod_count; i++) {
			if (data->plane->formats[i] != DRM_FORMAT_XRGB8888)
				continue;

			data->allow_fail = true;
			data->modifier = data->plane->modifiers[i];

			igt_dynamic_f("pipe-%s-%s-%s", kmstest_pipe_name(data->pipe),
				      data->output->name,
				      igt_fb_modifier_name(data->modifier)) {
				      /*
				       * FIXME: joiner+async flip is busted currently in KMD.
				       * Remove this check once the issues are fixed in KMD.
				       */
				      igt_skip_on_f(is_joiner_mode(data->drm_fd,
								   data->output),
						    "Skipping, async flip not supported "
						    "on joiner mode\n");
				      test_init_fbs(data);
				      test(data);
			}
		}
	}
}

static data_t data;

igt_main
{
	int i;

	igt_fixture {
		data.drm_fd = drm_open_driver_master(DRIVER_ANY);
		kmstest_set_vt_graphics_mode();
		igt_display_require(&data.display, data.drm_fd);
		igt_display_require_output(&data.display);

		igt_require_f(igt_has_drm_cap(data.drm_fd, DRM_CAP_ASYNC_PAGE_FLIP),
			      "Async Flip is not supported\n");

		if (is_intel_device(data.drm_fd))
			data.bops = buf_ops_create(data.drm_fd);
	}

	igt_describe("Verify the async flip functionality and the fps during async flips");
	igt_subtest_group {
		igt_fixture
			require_monotonic_timestamp(data.drm_fd);

		igt_describe("Wait for page flip events in between successive asynchronous flips");
		igt_subtest_with_dynamic("async-flip-with-page-flip-events") {
			data.alternate_sync_async = false;
			data.atomic_path = false;
			if (is_intel_device(data.drm_fd))
				run_test_with_modifiers(&data, test_async_flip);
			else
				run_test(&data, test_async_flip);
		}

		igt_describe("Wait for page flip events in between successive "
			     "asynchronous flips using atomic path");
		igt_subtest_with_dynamic("async-flip-with-page-flip-events-atomic") {
			data.alternate_sync_async = false;
			data.atomic_path = true;
			if (is_intel_device(data.drm_fd))
				run_test_with_modifiers(&data, test_async_flip);
			else
				run_test(&data, test_async_flip);
		}

		igt_describe("Alternate between sync and async flips");
		igt_subtest_with_dynamic("alternate-sync-async-flip") {
			data.alternate_sync_async = true;
			data.atomic_path = false;
			run_test(&data, test_async_flip);
		}

		igt_describe("Alternate between sync and async flips using atomic path");
		igt_subtest_with_dynamic("alternate-sync-async-flip-atomic") {
			data.alternate_sync_async = true;
			data.atomic_path = true;
			run_test(&data, test_async_flip);
		}

		igt_describe("Verify that the async flip timestamp does not "
			     "coincide with either previous or next vblank");
		igt_subtest_with_dynamic("test-time-stamp") {
			data.atomic_path = false;
			run_test(&data, test_timestamp);
		}

		igt_describe("Verify that the async flip timestamp does not coincide "
			     "with either previous or next vblank with atomic path");
		igt_subtest_with_dynamic("test-time-stamp-atomic") {
			data.atomic_path = true;
			run_test(&data, test_timestamp);
		}
	}

	igt_describe("Verify that the DRM_IOCTL_MODE_CURSOR passes after async flip");
	igt_subtest_with_dynamic("test-cursor") {
		/*
		 * Intel's PSR2 selective fetch adds other planes to state when
		 * necessary, causing the async flip to fail because async flip is not
		 * supported in cursor plane.
		 */
		igt_skip_on_f(i915_psr2_selective_fetch_check(data.drm_fd, NULL),
			      "PSR2 sel fetch causes cursor to be added to primary plane "
			      "pages flips and async flip is not supported in cursor\n");

		data.atomic_path = false;
		run_test(&data, test_cursor);
	}

	igt_describe("Verify that the DRM_IOCTL_MODE_CURSOR passes after "
		     "async flip with atomic commit");
	igt_subtest_with_dynamic("test-cursor-atomic") {
		/*
		 * Intel's PSR2 selective fetch adds other planes to state when
		 * necessary, causing the async flip to fail because async flip is not
		 * supported in cursor plane.
		 */
		igt_skip_on_f(i915_psr2_selective_fetch_check(data.drm_fd, NULL),
			      "PSR2 sel fetch causes cursor to be added to primary plane "
			      "pages flips and async flip is not supported in cursor\n");
		data.atomic_path = true;
		run_test(&data, test_cursor);
	}

	igt_describe("Negative case to verify if changes in fb are rejected from kernel as expected");
	igt_subtest_with_dynamic("invalid-async-flip") {
		/* TODO: support more vendors */
		igt_require(is_intel_device(data.drm_fd));
		igt_require(igt_display_has_format_mod(&data.display, DRM_FORMAT_XRGB8888,
						       I915_FORMAT_MOD_X_TILED));
		igt_require(igt_display_has_format_mod(&data.display, DRM_FORMAT_XRGB8888,
						       I915_FORMAT_MOD_Y_TILED));

		data.atomic_path = false;
		run_test(&data, test_invalid);
	}

	igt_describe("Negative case to verify if changes in fb are rejected "
		     "from kernel as expected when async flip is done using atomic path");
	igt_subtest_with_dynamic("invalid-async-flip-atomic") {
		data.atomic_path = true;
		/* TODO: support more vendors */
		igt_require(is_intel_device(data.drm_fd));
		igt_require(igt_display_has_format_mod(&data.display, DRM_FORMAT_XRGB8888,
						       I915_FORMAT_MOD_X_TILED));
		igt_require(igt_display_has_format_mod(&data.display, DRM_FORMAT_XRGB8888,
						       I915_FORMAT_MOD_Y_TILED));

		run_test(&data, test_invalid);
	}

	igt_describe("Use CRC to verify async flip scans out the correct framebuffer");
	igt_subtest_with_dynamic("crc") {
		/* Devices without CRC can't run this test */
		igt_require_pipe_crc(data.drm_fd);

		data.atomic_path = false;
		run_test(&data, test_crc);
	}

	igt_describe("Use CRC to verify async flip scans out the correct framebuffer "
		     "with atomic commit");
	igt_subtest_with_dynamic("crc-atomic") {
		/* Devices without CRC can't run this test */
		igt_require_pipe_crc(data.drm_fd);

		data.atomic_path = true;
		run_test(&data, test_crc);
	}

	igt_describe("Verify the async flip functionality after suspend and resume cycle");
	igt_subtest_with_dynamic("async-flip-suspend-resume") {
		data.suspend_resume = true;
		run_test(&data, test_async_flip);
	}

	igt_fixture {
		for (i = 0; i < NUM_FBS; i++)
			igt_remove_fb(data.drm_fd, &data.bufs[i]);

		if (is_intel_device(data.drm_fd))
			buf_ops_destroy(data.bops);
		igt_display_reset(&data.display);
		igt_display_commit(&data.display);
		igt_display_fini(&data.display);
		drm_close_driver(data.drm_fd);
	}
}
