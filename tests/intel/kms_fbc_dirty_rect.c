/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

/**
 * TEST: kms dirty fbc
 * Category: Display
 * Description: Test DIRTYFB ioctl functionality with FBC enabled.
 * Driver requirement: xe
 * Functionality: dirtyfb, fbc
 * Mega feature: General Display Features
 * Test category: functionality test
 */

#include <sys/types.h>

#include "igt.h"

#include "igt_sysfs.h"
#include "i915/gem.h"
#include "i915/gem_create.h"
#include "i915/intel_drrs.h"
#include "igt_psr.h"

#include "i915/intel_fbc.h"
#include "intel_mocs.h"
#include "intel_pat.h"

#include "xe/xe_query.h"

/**
 *
 * SUBTEST: fbc-dirty-rectangle-out-visible-area
 * Description: Sanity test to verify FBC DR by sending multiple damaged areas with non psr modes
 *
 * SUBTEST: fbc-dirty-rectangle-dirtyfb-tests
 * Description: Sanity test to verify FBC DR by sending multiple damaged areas with non psr modes
 *
 * SUBTEST: fbc-dirty-rectangle-different-formats
 * Description: Sanity test to verify FBC DR by sending multiple
 *              damaged areas with different formats.
 *
 */

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#define SQUARE_SIZE 100
#define SQUARE_OFFSET 100
#define SQUARE_OFFSET_2 600

typedef struct {
	int drm_fd;
	int debugfs_fd;
	igt_display_t display;
	drmModeModeInfo *mode;
	igt_output_t *output;
	igt_pipe_crc_t *pipe_crc;
	enum pipe pipe;
	u32 format;

	igt_crc_t ref_crc;

	enum {
		FEATURE_NONE  = 0,
		FEATURE_PSR   = 1,
		FEATURE_FBC   = 2,
		FEATURE_DRRS  = 4,
		FEATURE_COUNT = 8,
		FEATURE_DEFAULT = 8,
	} feature;
} data_t;

static void set_damage_clip(struct drm_mode_rect *damage, int x1, int y1, int x2, int y2)
{
	damage->x1 = x1;
	damage->y1 = y1;
	damage->x2 = x2;
	damage->y2 = y2;
}

static void set_damage_clip_w(struct drm_mode_rect *damage, int x1, int y1, int width, int height)
{
	set_damage_clip(damage, x1, y1, x1 + width, y1 + height);
}

static void dirty_rect_draw_white_rects(data_t *data, struct igt_fb *fb,
					int nrects, struct drm_mode_rect *rect)
{
	cairo_t *cr;

	if (!nrects || !rect)
		return;

	cr = igt_get_cairo_ctx(data->drm_fd, fb);

	for (int i = 0; i < nrects; i++) {
		igt_paint_color_alpha(cr, rect[i].x1, rect[i].y1,
				      rect[i].x2 - rect[i].x1,
				      rect[i].y2 - rect[i].y1,
				      1.0, 1.0, 1.0, 1.0);
	}

	igt_put_cairo_ctx(cr);
}


static void
set_damage_area(igt_plane_t *plane,  struct drm_mode_rect *rects,
		size_t length)
{
	igt_plane_replace_prop_blob(plane, IGT_PLANE_FB_DAMAGE_CLIPS, rects, length);
}

static void
set_fb_and_collect_crc(data_t *data, igt_plane_t *plane, struct igt_fb *fb,
		       igt_pipe_crc_t *pipe_crc, igt_crc_t *crc)
{
	igt_plane_set_fb(plane, fb);
	igt_display_commit2(&data->display, COMMIT_ATOMIC);

	if (!data->pipe_crc) {
		data->pipe_crc = igt_pipe_crc_new(data->drm_fd, data->pipe,
						  IGT_PIPE_CRC_SOURCE_AUTO);
	}

	igt_pipe_crc_start(data->pipe_crc);
	igt_pipe_crc_get_current(data->drm_fd, data->pipe_crc, crc);
	igt_pipe_crc_stop(data->pipe_crc);
	igt_assert_f(intel_fbc_is_enabled(data->drm_fd, data->pipe,
					  IGT_LOG_INFO),
					  "FBC is not enabled\n");
}

static void
update_rect_with_dirtyfb(data_t *data, struct igt_fb *fb1, struct igt_fb *fb2,
			 struct drm_mode_rect *rect)
{
	struct intel_buf *src, *dst;
	struct intel_bb *ibb;
	igt_spin_t *spin;
	int r;
	struct buf_ops *bops;
	igt_render_copyfunc_t rendercopy;

	bops = buf_ops_create(data->drm_fd);
	rendercopy = igt_get_render_copyfunc(intel_get_drm_devid(data->drm_fd));

	src = intel_buf_create_full(bops, fb1->gem_handle, fb1->width,
				    fb1->height,
				    igt_drm_format_to_bpp(fb1->drm_format),
				    0,
				    igt_fb_mod_to_tiling(fb1->modifier),
				    0, fb1->size, 0, system_memory(data->drm_fd),
				    intel_get_pat_idx_uc(data->drm_fd),
				    DEFAULT_MOCS_INDEX);
	dst = intel_buf_create_full(bops, fb2->gem_handle,
				    fb2->width, fb2->height,
				    igt_drm_format_to_bpp(fb2->drm_format),
				    0, igt_fb_mod_to_tiling(fb2->modifier),
				    0, fb2->size, 0, system_memory(data->drm_fd),
				    intel_get_pat_idx_uc(data->drm_fd),
				    DEFAULT_MOCS_INDEX);
	ibb = intel_bb_create(data->drm_fd, PAGE_SIZE);

	spin = igt_spin_new(data->drm_fd, .ahnd = ibb->allocator_handle);
	igt_spin_set_timeout(spin, NSEC_PER_SEC);

	rendercopy(ibb, src, rect->x1, rect->y1, rect->x2 - rect->x1,
		   rect->y2 - rect->y1, dst, rect->x1, rect->y1);

	/* Perfom dirtyfb right after initiating rendercopy/blitter */
	r = drmModeDirtyFB(data->drm_fd, fb2->fb_id, NULL, 0);
	igt_assert(r == 0 || r == -ENOSYS);

	/* Ensure rendercopy/blitter is complete */
	intel_bb_sync(ibb);

	igt_spin_free(data->drm_fd, spin);
	intel_bb_destroy(ibb);
	intel_buf_destroy(src);
	intel_buf_destroy(dst);
}

static void fbc_dirty_rectangle_dirtyfb(data_t *data)
{
	igt_pipe_crc_t *pipe_crc = data->pipe_crc;
	igt_display_t *display = &data->display;
	igt_output_t *output = data->output;
	igt_plane_t *primary;
	drmModeModeInfo *mode;
	struct igt_fb main_fb, fb2, fb3;
	struct drm_mode_rect full_rect, rect1, rect2;
	igt_crc_t main_crc, fb2_crc, fb3_crc, crc;

	mode = igt_output_get_mode(output);
	igt_output_set_pipe(output, data->pipe);
	primary = igt_output_get_plane_type(output, DRM_PLANE_TYPE_PRIMARY);

	set_damage_clip_w(&full_rect, 0, 0, mode->hdisplay, mode->vdisplay);
	set_damage_clip_w(&rect1, SQUARE_OFFSET, SQUARE_OFFSET, SQUARE_SIZE, SQUARE_SIZE);
	set_damage_clip_w(&rect2, SQUARE_OFFSET_2, SQUARE_OFFSET_2, SQUARE_SIZE, SQUARE_SIZE);

	igt_create_color_fb(data->drm_fd, data->mode->hdisplay, data->mode->vdisplay,
			    data->format, DRM_FORMAT_MOD_LINEAR, 0.0, 0.0, 1.0, &main_fb);

	igt_create_color_fb(data->drm_fd, data->mode->hdisplay, data->mode->vdisplay,
			    data->format, DRM_FORMAT_MOD_LINEAR, 0.0, 0.0, 1.0, &fb2);
	dirty_rect_draw_white_rects(data, &fb2, 1, &rect1);

	igt_create_color_fb(data->drm_fd, data->mode->hdisplay, data->mode->vdisplay,
			    data->format, DRM_FORMAT_MOD_LINEAR, 0.0, 0.0, 1.0, &fb3);
	dirty_rect_draw_white_rects(data, &fb3, 1, &rect2);

	/* 1st screen - Empty blue screen */
	set_damage_area(primary, &full_rect, sizeof(full_rect));
	set_fb_and_collect_crc(data, primary, &main_fb, pipe_crc, &main_crc);

	/* 2nd screen - 1st white rect at 100, 100 - using damage area */
	set_damage_area(primary, &rect1, sizeof(rect1));
	set_fb_and_collect_crc(data, primary, &fb2, pipe_crc, &fb2_crc);

	/* 3rd screen - 2nd white rect at 600, 600 - using damage area.
	 * Now two white rects on screen
	 */
	set_damage_area(primary, &rect2, sizeof(rect2));
	set_fb_and_collect_crc(data, primary, &fb3, pipe_crc, &fb3_crc);

	/* 4th screen - clear the 2nd white rect at 600,600 with dirtyfb.
	 * Copy rect2 area from main_fb to fb3.
	 */
	update_rect_with_dirtyfb(data, &main_fb, &fb3, &rect2);
	/* Now the screen must match 1st screen - with whole blue */
	igt_pipe_crc_collect_crc(data->pipe_crc, &crc);
	igt_assert_crc_equal(&crc, &main_crc);

	/* 5th screen - Copy the first rect at 100,100 with dirtyfb.
	 * Copy rect1 area from fb2 to fb3.
	 */
	update_rect_with_dirtyfb(data, &fb2, &fb3, &rect1);
	/* Now the screen must match 2nd screen - with one rect at 100,100 */
	igt_pipe_crc_collect_crc(data->pipe_crc, &crc);
	igt_assert_crc_equal(&crc, &fb2_crc);

	igt_plane_set_fb(primary, NULL);
	igt_remove_fb(data->drm_fd, &main_fb);
	igt_remove_fb(data->drm_fd, &fb2);
	igt_remove_fb(data->drm_fd, &fb3);
	igt_display_commit2(display, COMMIT_ATOMIC);

	if (data->pipe_crc) {
		igt_pipe_crc_free(data->pipe_crc);
		data->pipe_crc = NULL;
	}
}

/**
 * fbc_dirty_rectangle_outside_visible_region - Test dirty rectangle outside visible region
 * @data: Pointer to the test data structure
 *
 * This test verifies the behavior of the Frame Buffer Compression (FBC) when
 * dirty rectangles are set outside the visible region of the display. It creates
 * a main framebuffer and three additional framebuffers with dirty rectangles
 * positioned horizontally, vertically, and both horizontally and vertically
 * outside the visible region. The test then sets the damage area to these
 * rectangles and collects CRCs to ensure that the content outside the visible
 * region does not affect the main framebuffer's CRC.
 */
static void fbc_dirty_rectangle_outside_visible_region(data_t *data)
{
	igt_pipe_crc_t *pipe_crc = data->pipe_crc;
	igt_display_t *display = &data->display;
	igt_output_t *output = data->output;
	igt_plane_t *primary;
	struct igt_fb main_fb, rect_fb[3];
	struct drm_mode_rect rect[3], full_rect;
	igt_crc_t main_crc, rect_crc[3];

	igt_output_set_pipe(output, data->pipe);
	primary = igt_output_get_plane_type(output, DRM_PLANE_TYPE_PRIMARY);

	set_damage_clip(&full_rect, 0, 0, data->mode->hdisplay + 200, data->mode->vdisplay + 200);
	/* Rect Horizontally outside visible region */
	set_damage_clip_w(&rect[0], data->mode->hdisplay + 10, 100, SQUARE_SIZE, SQUARE_SIZE);
	/* Rect vertically outside visible region */
	set_damage_clip_w(&rect[1], 10, data->mode->vdisplay + 50, SQUARE_SIZE, SQUARE_SIZE);
	/* Rect Horizontally and vertically outside visible region */
	set_damage_clip_w(&rect[2], data->mode->hdisplay + 10, data->mode->vdisplay + 50,
			  SQUARE_SIZE, SQUARE_SIZE);

	igt_create_color_fb(data->drm_fd, data->mode->hdisplay + 200,
			    data->mode->vdisplay + 200, data->format,
			    DRM_FORMAT_MOD_LINEAR, 0.0, 1.0, 0.0, &main_fb);

	for (int i = 0; i < 3; i++) {
		igt_create_color_fb(data->drm_fd, data->mode->hdisplay + 200,
				    data->mode->vdisplay + 200, data->format,
				    DRM_FORMAT_MOD_LINEAR, 0.0, 1.0, 0.0, &rect_fb[i]);
		dirty_rect_draw_white_rects(data, &rect_fb[i], 1, &rect[i]);
	}

	/* Main rect */
	set_damage_area(primary, &full_rect, sizeof(full_rect));
	set_fb_and_collect_crc(data, primary, &main_fb, pipe_crc, &main_crc);

	for (int i = 0; i < 3; i++) {
		set_damage_area(primary, &rect[i], sizeof(rect[i]));
		set_fb_and_collect_crc(data, primary, &rect_fb[i], pipe_crc, &rect_crc[i]);
		igt_assert_crc_equal(&rect_crc[i], &main_crc);
	}

	igt_plane_set_fb(primary, NULL);
	igt_remove_fb(data->drm_fd, &main_fb);
	for (int i = 0; i < 3; i++)
		igt_remove_fb(data->drm_fd, &rect_fb[i]);
	igt_display_commit2(display, COMMIT_ATOMIC);

	if (data->pipe_crc) {
		igt_pipe_crc_free(data->pipe_crc);
		data->pipe_crc = NULL;
	}
}

/*
 * fbc_dirty_rectangle_basic
 * @data: data_t
 *
 * This test draws screens as full-screen updates and collects their CRCs
 * as reference values. Screens are then updated using the FBC
 * dirty rect feature and compared with the reference CRCs.
 * Matching CRCs indicate success.
 *
 * Steps to Collect Reference CRCs:
 * 1. Full Blue Screen
 *    - Frame Buffer: main_fb
 *    - CRC: main_fb_crc
 * 2. White Square on Upper Left
 *    - Frame Buffer: rect1_fb
 *    - CRC: rect1_fb_crc
 * 3. Second White Square Below First
 *    - Frame Buffer: rect2_fb
 *    - CRC: rect2_fb_crc
 * 4. Both Rectangles
 *    - Frame Buffer: rect_combined_fb
 *    - CRC: rect_combined_fb_crc
 *
 * Steps to Update Screen with FBC Dirty Rect:
 * 1. Full Blue Screen
 *    - Set rect_combined_fb with Damage Area Update
 *    - CRC should match rect_combined_fb_crc
 * 2. Clear First Rectangle Area
 *    - Use main_fb and damage area as rect1 coordinates
 *    - CRC should match rect2_fb_crc
 * 3. Clear Second Rectangle Area
 *    - Use main_fb and damage area as rect2 coordinates
 *    - CRC should match main_fb_crc
 */
static void fbc_dirty_rectangle_basic(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_output_t *output = data->output;
	igt_plane_t *primary;
	struct igt_fb main_fb;
	struct igt_fb rect_1_fb;
	struct igt_fb rect_2_fb;
	struct igt_fb rect_combined_fb;
	struct drm_mode_rect rect1;
	struct drm_mode_rect rect2;
	struct drm_mode_rect rect_combined[2];
	struct drm_mode_rect full_rect;
	igt_crc_t main_fb_crc, rect_1_fb_crc, rect_2_fb_crc, rect_combined_fb_crc, crc;

	igt_output_set_pipe(output, data->pipe);
	primary = igt_output_get_plane_type(output, DRM_PLANE_TYPE_PRIMARY);

	set_damage_clip(&full_rect, 0, 0, data->mode->hdisplay-1, data->mode->vdisplay-1);
	set_damage_clip(&rect1, SQUARE_OFFSET, SQUARE_OFFSET, SQUARE_OFFSET + SQUARE_SIZE,
			SQUARE_OFFSET + SQUARE_SIZE);
	set_damage_clip(&rect2, SQUARE_OFFSET_2, SQUARE_OFFSET_2, SQUARE_OFFSET_2 + SQUARE_SIZE,
			SQUARE_OFFSET_2 + SQUARE_SIZE);
	set_damage_clip(&rect_combined[0], rect1.x1, rect1.y1, rect1.x2, rect1.y2);
	set_damage_clip(&rect_combined[1], rect2.x1, rect2.y1, rect2.x2, rect2.y2);

	igt_create_color_fb(data->drm_fd, data->mode->hdisplay, data->mode->vdisplay, data->format,
			    DRM_FORMAT_MOD_LINEAR, 0.0, 0.0, 1.0, &main_fb);

	igt_create_color_fb(data->drm_fd, data->mode->hdisplay, data->mode->vdisplay, data->format,
			    DRM_FORMAT_MOD_LINEAR, 0.0, 0.0, 1.0, &rect_1_fb);
	dirty_rect_draw_white_rects(data, &rect_1_fb, 1, &rect1);

	igt_create_color_fb(data->drm_fd, data->mode->hdisplay, data->mode->vdisplay, data->format,
			    DRM_FORMAT_MOD_LINEAR, 0.0, 0.0, 1.0, &rect_2_fb);
	dirty_rect_draw_white_rects(data, &rect_2_fb, 1, &rect2);

	igt_create_color_fb(data->drm_fd, data->mode->hdisplay, data->mode->vdisplay, data->format,
			    DRM_FORMAT_MOD_LINEAR, 0.0, 0.0, 1.0, &rect_combined_fb);
	dirty_rect_draw_white_rects(data, &rect_combined_fb, ARRAY_SIZE(rect_combined),
				    rect_combined);

	/* main_fb blank blue screen - get and store crc */
	set_fb_and_collect_crc(data, primary, &main_fb, data->pipe_crc, &main_fb_crc);

	/* Whole blue screen with one white rect and collect crc */
	set_damage_area(primary, &full_rect, sizeof(full_rect));
	set_fb_and_collect_crc(data, primary, &rect_1_fb, data->pipe_crc, &rect_1_fb_crc);

	/* Second white rect and collect crc */
	set_damage_area(primary, &full_rect, sizeof(full_rect));
	set_fb_and_collect_crc(data, primary, &rect_2_fb, data->pipe_crc, &rect_2_fb_crc);

	/* Both rects and collect crc */
	set_damage_area(primary, &full_rect, sizeof(full_rect));
	set_fb_and_collect_crc(data, primary, &rect_combined_fb, data->pipe_crc,
			       &rect_combined_fb_crc);

	/* Put full blank screen back */
	set_damage_area(primary, &full_rect, sizeof(full_rect));
	set_fb_and_collect_crc(data, primary, &main_fb, data->pipe_crc, &crc);
	igt_assert_crc_equal(&crc, &main_fb_crc);

	/* Set combined rect - draw two white rects using damage area */
	set_damage_area(primary, rect_combined, sizeof(rect_combined));
	set_fb_and_collect_crc(data, primary, &rect_combined_fb, data->pipe_crc, &crc);
	igt_assert_crc_equal(&crc, &rect_combined_fb_crc);

	/* Clear first rect using damage area. Only the second rect should be visible here! */
	set_damage_area(primary, &rect1, sizeof(rect1));
	set_fb_and_collect_crc(data, primary, &main_fb, data->pipe_crc, &crc);
	igt_assert_crc_equal(&crc, &rect_2_fb_crc);

	/* Clear the second rect as well. Now back to original blank screen */
	set_damage_area(primary, &rect2, sizeof(rect2));
	set_fb_and_collect_crc(data, primary, &main_fb, data->pipe_crc, &crc);
	igt_assert_crc_equal(&crc, &main_fb_crc);

	igt_plane_set_fb(primary, NULL);
	igt_remove_fb(data->drm_fd, &main_fb);
	igt_remove_fb(data->drm_fd, &rect_1_fb);
	igt_remove_fb(data->drm_fd, &rect_2_fb);
	igt_remove_fb(data->drm_fd, &rect_combined_fb);
	igt_display_commit2(display, COMMIT_ATOMIC);

	if (data->pipe_crc) {
		igt_pipe_crc_free(data->pipe_crc);
		data->pipe_crc = NULL;
	}
}

static void prepare_test(data_t *data, igt_output_t *output)
{
	igt_require_f(intel_fbc_supported_on_chipset(data->drm_fd, data->pipe),
		      "FBC not supported by the chipset on pipe\n");

	if (psr_sink_support(data->drm_fd, data->debugfs_fd, PSR_MODE_1, NULL) ||
		psr_sink_support(data->drm_fd, data->debugfs_fd, PSR_MODE_2, NULL) ||
		psr_sink_support(data->drm_fd, data->debugfs_fd, PR_MODE, NULL)) {
		igt_info("PSR is supported by the sink. Disabling PSR to test Dirty FBC functionality.\n");
		psr_disable(data->drm_fd, data->debugfs_fd, output);
	}

	if (data->feature & FEATURE_FBC)
		intel_fbc_enable(data->drm_fd);
}

static void fbc_dirty_rectangle_test(data_t *data, void (*test_func)(data_t *))
{
	prepare_test(data, data->output);
	test_func(data);
}

igt_main
{
	data_t data = {0};

	igt_fixture {
		data.drm_fd = drm_open_driver_master(DRIVER_XE);
		igt_require(data.drm_fd >= 0);
		data.debugfs_fd = igt_debugfs_dir(data.drm_fd);
		kmstest_set_vt_graphics_mode();
		igt_require_pipe_crc(data.drm_fd);
		igt_display_require(&data.display, data.drm_fd);
		igt_display_require_output(&data.display);
		igt_require_f(intel_display_ver(intel_get_drm_devid(data.drm_fd)) >= 30,
			      "FBC with dirty region is not supported\n");
	}

	igt_subtest_with_dynamic("fbc-dirty-rectangle-out-visible-area") {
		data.feature = FEATURE_FBC;

		for_each_pipe(&data.display, data.pipe) {
			for_each_valid_output_on_pipe(&data.display, data.pipe, data.output) {
				data.mode = igt_output_get_mode(data.output);
				data.format = DRM_FORMAT_XRGB8888;
				igt_display_reset(&data.display);
				igt_output_set_pipe(data.output, data.pipe);

				if (!intel_pipe_output_combo_valid(&data.display))
					continue;

				igt_dynamic_f("pipe-%s-%s",
					       kmstest_pipe_name(data.pipe),
					       igt_output_name(data.output)) {
					fbc_dirty_rectangle_test(&data,
						fbc_dirty_rectangle_outside_visible_region);
				}
			}
		}
	}

	igt_subtest_with_dynamic("fbc-dirty-rectangle-dirtyfb-tests") {
		data.feature = FEATURE_FBC;

		for_each_pipe(&data.display, data.pipe) {
			for_each_valid_output_on_pipe(&data.display, data.pipe, data.output) {
				data.mode = igt_output_get_mode(data.output);
				data.format = DRM_FORMAT_XRGB8888;
				igt_display_reset(&data.display);
				igt_output_set_pipe(data.output, data.pipe);

				if (!intel_pipe_output_combo_valid(&data.display))
					continue;

				igt_dynamic_f("pipe-%s-%s",
					       kmstest_pipe_name(data.pipe),
					       igt_output_name(data.output)) {
					fbc_dirty_rectangle_test(&data,
							fbc_dirty_rectangle_dirtyfb);
				}
			}
		}
	}

	igt_subtest_with_dynamic("fbc-dirty-rectangle-different-formats") {
		uint32_t formats[] = {DRM_FORMAT_XRGB8888, DRM_FORMAT_ARGB8888, DRM_FORMAT_RGB565};
		int num_formats = ARRAY_SIZE(formats);

		data.feature = FEATURE_FBC;

		for_each_pipe(&data.display, data.pipe) {
			for_each_valid_output_on_pipe(&data.display, data.pipe, data.output) {
				data.mode = igt_output_get_mode(data.output);
				igt_display_reset(&data.display);
				igt_output_set_pipe(data.output, data.pipe);

				if (!intel_pipe_output_combo_valid(&data.display))
					continue;

				for (int i = 0; i < num_formats; i++) {
					igt_dynamic_f("pipe-%s-%s-format-%s",
						       kmstest_pipe_name(data.pipe),
						       igt_output_name(data.output),
						       igt_format_str(formats[i])) {
						data.format = formats[i];
						fbc_dirty_rectangle_test(&data,
							fbc_dirty_rectangle_basic);
					}
				}
			}
		}
	}

	igt_fixture {
		igt_display_fini(&data.display);
		close(data.drm_fd);
	}
}
