/*
 * Copyright © 2016 Intel Corporation
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
 */

/**
 * TEST: kms ccs
 * Category: Display
 * Description: Test render compression (RC), in which the main surface is
 *              complemented by a color control surface (CCS) that the display
 *              uses to interpret the compressed data.
 * Driver requirement: i915, xe
 * Functionality: ccs, tiling
 * Mega feature: E2E Compression
 * Test category: functionality test
 */

#include "igt.h"

#include "i915/gem_create.h"
#include "intel_pat.h"
#include "intel_blt.h"
#include "intel_mocs.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"

/**
 * SUBTEST: %s-%s
 * Description: Test %arg[1] with given %arg[2] modifier
 *
 * arg[1]:
 *
 * @bad-aux-stride:            Bad AUX stride
 * @ccs-on-another-bo:         CCS with different BO
 * @missing-ccs-buffer:        Missing CCS buffer
 *
 * arg[2]:
 *
 * @y-tiled-ccs:               Y tiled ccs
 * @y-tiled-gen12-mc-ccs:      Y tiled gen12 mc ccs
 * @y-tiled-gen12-rc-ccs:      Y tiled gen12 rc ccs
 * @y-tiled-gen12-rc-ccs-cc:   Y tiled gen12 rc ccs cc
 * @yf-tiled-ccs:              YF tiled ccs
 * @4-tiled-mtl-mc-ccs:        4 tiled mtl mc ccs
 * @4-tiled-mtl-rc-ccs:        4 tiled mtl rc ccs
 * @4-tiled-mtl-rc-ccs-cc:     4 tiled mtl rc ccs cc
 */

/**
 * SUBTEST: %s-%s
 * Description: Test %arg[1] with %arg[2] modifier
 *
 * arg[1]:
 *
 * @bad-pixel-format:            Bad pixel format
 *
 * arg[2]:
 *
 * @4-tiled-dg2-mc-ccs:        4 tiled mc ccs
 * @4-tiled-dg2-rc-ccs:        4 tiled dg2 rc ccs
 * @4-tiled-dg2-rc-ccs-cc:     4 tiled dg2 rc ccs cc
 * @4-tiled-mtl-mc-ccs:        4 tiled mtl mc ccs
 * @4-tiled-mtl-rc-ccs:        4 tiled mtl rc ccs
 * @4-tiled-mtl-rc-ccs-cc:     4 tiled mtl rc ccs cc
 * @y-tiled-ccs:               Y tiled ccs
 * @y-tiled-gen12-mc-ccs:      Y tiled gen12 mc ccs
 * @y-tiled-gen12-rc-ccs:      Y tiled gen12 rc ccs
 * @y-tiled-gen12-rc-ccs-cc:   Y tiled gen12 rc ccs cc
 * @yf-tiled-ccs:              YF tiled ccs
 */

/**
 * SUBTEST: %s-%s
 * Description: Test %arg[1] with %arg[2] modifier
 *
 * arg[1]:
 *
 * @crc-primary-basic:           Primary plane CRC compatibility
 * @crc-sprite-planes-basic:     Sprite plane CRC compatability
 * @random-ccs-data:             Random CCS data
 *
 * arg[2]:
 *
 * @4-tiled-bmg-ccs:           4 tiled xe2 pat controlled ccs
 * @4-tiled-lnl-ccs:           4 tiled xe2 pat controlled ccs
 * @4-tiled-dg2-mc-ccs:        4 tiled mc ccs
 * @4-tiled-dg2-rc-ccs:        4 tiled dg2 rc ccs
 * @4-tiled-dg2-rc-ccs-cc:     4 tiled dg2 rc ccs cc
 * @4-tiled-mtl-mc-ccs:        4 tiled mtl mc ccs
 * @4-tiled-mtl-rc-ccs:        4 tiled mtl rc ccs
 * @4-tiled-mtl-rc-ccs-cc:     4 tiled mtl rc ccs cc
 * @y-tiled-ccs:               Y tiled ccs
 * @y-tiled-gen12-mc-ccs:      Y tiled gen12 mc ccs
 * @y-tiled-gen12-rc-ccs:      Y tiled gen12 rc ccs
 * @y-tiled-gen12-rc-ccs-cc:   Y tiled gen12 rc ccs cc
 * @yf-tiled-ccs:              YF tiled ccs
 */

/**
 * SUBTEST: %s-%s
 * Description: Test %arg[1] with %arg[2] modifier
 * Functionality: ccs, rotation, tiling
 *
 * arg[1]:
 *
 * @bad-rotation-90:             90 degree rotation
 * @crc-primary-rotation-180:    180 degree rotation
 *
 * arg[2]:
 *
 * @4-tiled-bmg-ccs:           4 tiled xe2 pat controlled ccs
 * @4-tiled-lnl-ccs:           4 tiled xe2 pat controlled ccs
 * @4-tiled-dg2-mc-ccs:        4 tiled mc ccs
 * @4-tiled-dg2-rc-ccs:        4 tiled dg2 rc ccs
 * @4-tiled-dg2-rc-ccs-cc:     4 tiled dg2 rc ccs cc
 * @4-tiled-mtl-mc-ccs:        4 tiled mtl mc ccs
 * @4-tiled-mtl-rc-ccs:        4 tiled mtl rc ccs
 * @4-tiled-mtl-rc-ccs-cc:     4 tiled mtl rc ccs cc
 * @y-tiled-ccs:               Y tiled ccs
 * @y-tiled-gen12-mc-ccs:      Y tiled gen12 mc ccs
 * @y-tiled-gen12-rc-ccs:      Y tiled gen12 rc ccs
 * @y-tiled-gen12-rc-ccs-cc:   Y tiled gen12 rc ccs cc
 * @yf-tiled-ccs:              YF tiled ccs
 */

#define SDR_PLANE_BASE	3

IGT_TEST_DESCRIPTION("Test render compression (RC), in which the main surface "
		     "is complemented by a color control surface (CCS) that "
		     "the display uses to interpret the compressed data.");

enum test_flags {
	TEST_CRC			= 1 << 1,
	TEST_ROTATE_180			= 1 << 2,
	TEST_BAD_PIXEL_FORMAT		= 1 << 3,
	TEST_BAD_ROTATION_90		= 1 << 4,
	TEST_NO_AUX_BUFFER		= 1 << 5,
	TEST_BAD_CCS_HANDLE		= 1 << 6,
	TEST_BAD_AUX_STRIDE		= 1 << 7,
	TEST_RANDOM			= 1 << 8,
	TEST_ALL_PLANES			= 1 << 9,
};

#define TEST_BAD_CCS_PLANE	(TEST_NO_AUX_BUFFER | TEST_BAD_CCS_HANDLE | \
				 TEST_BAD_AUX_STRIDE)
#define TEST_FAIL_ON_ADDFB2	(TEST_BAD_PIXEL_FORMAT | TEST_BAD_CCS_PLANE)

enum test_fb_flags {
	FB_COMPRESSED			= 1 << 0,
	FB_HAS_PLANE			= 1 << 1,
	FB_MISALIGN_AUX_STRIDE		= 1 << 2,
	FB_SMALL_AUX_STRIDE		= 1 << 3,
	FB_ZERO_AUX_STRIDE		= 1 << 4,
	FB_RANDOM			= 1 << 5,
};

typedef struct {
	int drm_fd;
	igt_display_t display;
	igt_output_t *output;
	enum pipe pipe;
	enum test_flags flags;
	igt_plane_t *plane;
	igt_pipe_crc_t *pipe_crc;
	uint32_t format;
	uint64_t ccs_modifier;
	unsigned int seed;
	bool user_seed;
} data_t;

static const struct {
	double r;
	double g;
	double b;
} colors[2] = {
	{1.0, 0.0, 0.0},
	{0.0, 1.0, 0.0}
};

static const uint32_t formats[] = {
	DRM_FORMAT_XYUV8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_NV12,
	DRM_FORMAT_P012,
	DRM_FORMAT_P016,
};

static const struct {
	uint64_t modifier;
	const char *str;
} ccs_modifiers[] = {
	{I915_FORMAT_MOD_Y_TILED_CCS, "y-tiled-ccs"},
	{I915_FORMAT_MOD_Yf_TILED_CCS, "yf-tiled-ccs"},
	{I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS, "y-tiled-gen12-rc-ccs"},
	{I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS_CC, "y-tiled-gen12-rc-ccs-cc"},
	{I915_FORMAT_MOD_Y_TILED_GEN12_MC_CCS, "y-tiled-gen12-mc-ccs"},
	{I915_FORMAT_MOD_4_TILED_DG2_RC_CCS, "4-tiled-dg2-rc-ccs"},
	{I915_FORMAT_MOD_4_TILED_DG2_MC_CCS, "4-tiled-dg2-mc-ccs"},
	{I915_FORMAT_MOD_4_TILED_DG2_RC_CCS_CC, "4-tiled-dg2-rc-ccs-cc"},
	{I915_FORMAT_MOD_4_TILED_MTL_RC_CCS, "4-tiled-mtl-rc-ccs"},
	{I915_FORMAT_MOD_4_TILED_MTL_MC_CCS, "4-tiled-mtl-mc-ccs"},
	{I915_FORMAT_MOD_4_TILED_MTL_RC_CCS_CC, "4-tiled-mtl-rc-ccs-cc"},
	{I915_FORMAT_MOD_4_TILED_BMG_CCS, "4-tiled-bmg-ccs"},
	{I915_FORMAT_MOD_4_TILED_LNL_CCS, "4-tiled-lnl-ccs"},
};

static bool check_ccs_planes;

static const struct {
	const enum test_flags	flags;
	const char		*testname;
	const char		*description;
} tests[] = {
	{TEST_BAD_PIXEL_FORMAT, "bad-pixel-format", "Test bad pixel format with given CCS modifier"},
	{TEST_BAD_ROTATION_90, "bad-rotation-90", "Test 90 degree rotation with given CCS modifier"},
	{TEST_CRC, "crc-primary-basic", "Test primary plane CRC compatibility with given CCS modifier"},
	{TEST_CRC | TEST_ROTATE_180, "crc-primary-rotation-180", "Test 180 degree rotation with given CCS modifier"},
	{TEST_RANDOM, "random-ccs-data", "Test random CCS data"},
	{TEST_NO_AUX_BUFFER, "missing-ccs-buffer", "Test missing CCS buffer with given CCS modifier"},
	{TEST_BAD_CCS_HANDLE, "ccs-on-another-bo", "Test CCS with different BO with given modifier"},
	{TEST_BAD_AUX_STRIDE, "bad-aux-stride", "Test with bad AUX stride with given CCS modifier"},
	{TEST_CRC | TEST_ALL_PLANES, "crc-sprite-planes-basic", "Test sprite plane CRC compatibility with given CCS modifier"},
};

/*
 * Limit maximum used sprite plane width so this test will not mistakenly
 * fail on hardware limitations which are not interesting to this test.
 * On this test too wide sprite plane may fail during creation with dmesg
 * comment saying:
 * "Requested display configuration exceeds system watermark limitations"
 */
#define MAX_SPRITE_PLANE_WIDTH 2000

static void addfb_init(struct igt_fb *fb, struct drm_mode_fb_cmd2 *f)
{
	int i;

	f->width = fb->width;
	f->height = fb->height;
	f->pixel_format = fb->drm_format;
	f->flags = DRM_MODE_FB_MODIFIERS;

	for (i = 0; i < fb->num_planes; i++) {
		f->handles[i] = fb->gem_handle;
		f->modifier[i] = fb->modifier;
		f->pitches[i] = fb->strides[i];
		f->offsets[i] = fb->offsets[i];
	}
}

static void
create_fb_prepare_add(int drm_fd, int width, int height,
		      uint32_t format, uint64_t modifier,
		      igt_fb_t *fb, struct drm_mode_fb_cmd2 *f)
{
	igt_create_bo_for_fb(drm_fd, width, height, format, modifier, fb);
	igt_assert_lt_u32(0, fb->gem_handle);

	addfb_init(fb, f);
}

/*
 * The CCS planes of compressed framebuffers contain non-zero bytes if the
 * engine compressed effectively the framebuffer. The actual encoding of these
 * bytes is not specified, but we know that seeing an all-zero CCS plane means
 * that the engine left the FB uncompressed, which is not what we expect in
 * the test. Look for the first non-zero byte in the given CCS plane to get a
 * minimal assurance that compression took place.
 */
static void check_ccs_plane(int drm_fd, igt_fb_t *fb, int plane)
{
	void *map;
	void *ccs_p;
	size_t ccs_size;
	int i;

	ccs_size = fb->strides[plane] * fb->plane_height[plane];
	igt_assert(ccs_size);

	if (is_i915_device(drm_fd)) {
		gem_set_domain(drm_fd, fb->gem_handle, I915_GEM_DOMAIN_CPU, 0);
		map = gem_mmap__cpu(drm_fd, fb->gem_handle, 0, fb->size, PROT_READ);
	} else {
		map = xe_bo_mmap_ext(drm_fd, fb->gem_handle, fb->size, PROT_READ);
	}
	ccs_size = fb->strides[plane] * fb->plane_height[plane];
	ccs_p = map + fb->offsets[plane];
	for (i = 0; i < ccs_size; i += sizeof(uint32_t))
		if (*(uint32_t *)(ccs_p + i))
			break;

	igt_assert_eq(0, gem_munmap(map, fb->size));

	igt_assert_f(i < ccs_size,
		     "CCS plane %d (for main plane %d) lacks compression meta-data\n",
		     plane, igt_fb_ccs_to_main_plane(fb, plane));
}

static void check_ccs_cc_plane(int drm_fd, igt_fb_t *fb, int plane, const float *cc_color)
{
	union cc {
		float f;
		uint32_t d;
	} *cc_p;
	void *map;
	uint32_t native_color;

	if (is_i915_device(drm_fd)) {
		gem_set_domain(drm_fd, fb->gem_handle, I915_GEM_DOMAIN_CPU, 0);
		map = gem_mmap__cpu(drm_fd, fb->gem_handle, 0, fb->size, PROT_READ);
	} else {
		map = xe_bo_mmap_ext(drm_fd, fb->gem_handle, fb->size, PROT_READ);
	}
	cc_p = map + fb->offsets[plane];

	igt_assert(cc_color[0] == cc_p[0].f &&
		   cc_color[1] == cc_p[1].f &&
		   cc_color[2] == cc_p[2].f &&
		   cc_color[3] == cc_p[3].f);

	native_color = (uint8_t)(cc_color[3] * 0xff) << 24 |
		       (uint8_t)(cc_color[0] * 0xff) << 16 |
		       (uint8_t)(cc_color[1] * 0xff) << 8 |
		       (uint8_t)(cc_color[2] * 0xff);

	igt_assert_eq_u32(native_color, cc_p[4].d);

	igt_assert_eq(0, gem_munmap(map, fb->size));
};

static void check_all_ccs_planes(int drm_fd, igt_fb_t *fb, const float *cc_color, bool check_cc_plane)
{
	int i;

	for (i = 0; i < fb->num_planes; i++) {
		if (igt_fb_is_ccs_plane(fb, i) &&
		    !igt_fb_is_gen12_ccs_cc_plane(fb, i))
			check_ccs_plane(drm_fd, fb, i);
		else if (igt_fb_is_gen12_ccs_cc_plane(fb, i) && check_cc_plane)
			check_ccs_cc_plane(drm_fd, fb, i, cc_color);
	}
}

static void access_flat_ccs_surface(struct igt_fb *fb, bool verify_compression)
{
	uint64_t bb_size, ccssize = fb->size / CCS_RATIO(fb->fd);
	uint64_t ccs_bo_size = ALIGN(ccssize, xe_get_default_alignment(fb->fd));
	struct blt_ctrl_surf_copy_data surf = {};
	uint32_t sysmem = system_memory(fb->fd);
	uint32_t bb1, ccs, *ccsmap;
	uint16_t cpu_caching = DRM_XE_GEM_CPU_CACHING_WC;
	uint8_t uc_mocs = intel_get_uc_mocs_index(fb->fd);
	uint8_t comp_pat_index = intel_get_pat_idx_wt(fb->fd);
	uint32_t region = (AT_LEAST_GEN(intel_get_drm_devid(fb->fd), 20) &
						xe_has_vram(fb->fd)) ? REGION_LMEM(0) : REGION_SMEM;

	struct drm_xe_engine_class_instance inst = {
		.engine_class = DRM_XE_ENGINE_CLASS_COPY,
	};

	uint64_t surf_ahnd;
	uint32_t vm, exec_queue;
	intel_ctx_t *surf_ctx;

	vm = xe_vm_create(fb->fd, 0, 0);
	exec_queue = xe_exec_queue_create(fb->fd, vm, &inst, 0);
	surf_ctx = intel_ctx_xe(fb->fd, vm, exec_queue, 0, 0, 0);
	surf_ahnd = intel_allocator_open(fb->fd, surf_ctx->vm,
						INTEL_ALLOCATOR_RELOC);

	ccs = xe_bo_create_caching(fb->fd, 0, ccs_bo_size, sysmem, 0, cpu_caching);

	blt_ctrl_surf_copy_init(fb->fd, &surf);
	blt_set_ctrl_surf_object(&surf.src, fb->gem_handle, region, fb->size,
				 uc_mocs, comp_pat_index, BLT_INDIRECT_ACCESS);
	blt_set_ctrl_surf_object(&surf.dst, ccs, sysmem, ccssize, uc_mocs,
				 DEFAULT_PAT_INDEX, DIRECT_ACCESS);

	bb_size = xe_bb_size(fb->fd, SZ_4K);
	bb1 = xe_bo_create(fb->fd, 0, bb_size, sysmem, 0);
	blt_set_batch(&surf.bb, bb1, bb_size, sysmem);
	blt_ctrl_surf_copy(fb->fd, surf_ctx, NULL, surf_ahnd, &surf);
	intel_ctx_xe_sync(surf_ctx, true);

	ccsmap = xe_bo_map(fb->fd, ccs, surf.dst.size);

	if (verify_compression) {
		for (int i = 0; i < surf.dst.size / sizeof(uint32_t); i++) {
			if (ccsmap[i] != 0)
				goto ccs_verified;
		}
		igt_assert_f(false, "framebuffer was not compressed!\n");
	} else {
		for (int i = 0; i < surf.dst.size / sizeof(uint32_t); i++)
			ccsmap[i] = rand();
	}

	blt_set_ctrl_surf_object(&surf.src, ccs, sysmem, ccssize,
				 uc_mocs, DEFAULT_PAT_INDEX, DIRECT_ACCESS);
	blt_set_ctrl_surf_object(&surf.dst, fb->gem_handle, region, fb->size,
				 uc_mocs, comp_pat_index, INDIRECT_ACCESS);
	blt_ctrl_surf_copy(fb->fd, surf_ctx, NULL, surf_ahnd, &surf);
	intel_ctx_xe_sync(surf_ctx, true);

ccs_verified:
	munmap(ccsmap, ccssize);
	gem_close(fb->fd, ccs);
	gem_close(fb->fd, bb1);
	xe_exec_queue_destroy(fb->fd, exec_queue);
	xe_vm_destroy(fb->fd, vm);
	free(surf_ctx);
	put_ahnd(surf_ahnd);
}

static void fill_fb_random(int drm_fd, igt_fb_t *fb)
{
	void *map;
	uint8_t *p;
	int i;

	if (is_i915_device(drm_fd)) {
		gem_set_domain(drm_fd, fb->gem_handle, I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
		p = map = gem_mmap__cpu(drm_fd, fb->gem_handle, 0, fb->size, PROT_WRITE);
	} else {
		p = map = xe_bo_mmap_ext(drm_fd, fb->gem_handle, fb->size, PROT_WRITE);
	}

	for (i = 0; i < fb->size; i++)
		p[i] = rand();

	igt_assert_eq(0, gem_munmap(map, fb->size));

	/* randomize also ccs surface on Xe2 */
	if (AT_LEAST_GEN(intel_get_drm_devid(drm_fd), 20))
		access_flat_ccs_surface(fb, false);
}

static void test_bad_ccs_plane(data_t *data, int width, int height, int ccs_plane,
			       enum test_fb_flags fb_flags)
{
	struct igt_fb fb = {};
	struct drm_mode_fb_cmd2 f = {};
	uint32_t bad_ccs_bo = 0;
	int addfb_errno;
	int ret;

	igt_assert(fb_flags & FB_COMPRESSED);
	create_fb_prepare_add(data->drm_fd, width, height,
			      data->format, data->ccs_modifier,
			      &fb, &f);

	/*
	 * The stride of CCS planes on GEN12+ is fixed, so we can check for
	 * an incorrect stride with the same delta as on earlier platforms.
	 */
	if (fb_flags & FB_MISALIGN_AUX_STRIDE) {
		igt_skip_on_f(HAS_FLATCCS(intel_get_drm_devid(data->drm_fd)), "No aux plane on flat ccs.\n");
		igt_skip_on_f(width <= 1024,
			      "FB already has the smallest possible stride\n");
		f.pitches[ccs_plane] -= 64;
	}

	if (fb_flags & FB_SMALL_AUX_STRIDE) {
		igt_skip_on_f(HAS_FLATCCS(intel_get_drm_devid(data->drm_fd)), "No aux plane on flat ccs.\n");
		igt_skip_on_f(width <= 1024,
			      "FB already has the smallest possible stride\n");
		f.pitches[ccs_plane] = ALIGN(f.pitches[ccs_plane] / 2, 128);
	}

	if (fb_flags & FB_ZERO_AUX_STRIDE)
		f.pitches[ccs_plane] = 0;

	/* Put the CCS buffer on a different BO. */
	if (data->flags & TEST_BAD_CCS_HANDLE) {
		bad_ccs_bo = is_i915_device(data->drm_fd) ?
				gem_create(data->drm_fd, fb.size) :
				xe_bo_create(data->drm_fd, 0, fb.size,
					     vram_if_possible(data->drm_fd, 0),
					     DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
		f.handles[ccs_plane] = bad_ccs_bo;
	}

	if (data->flags & TEST_NO_AUX_BUFFER) {
		igt_skip_on_f(HAS_FLATCCS(intel_get_drm_devid(data->drm_fd)), "No aux plane on flat ccs.\n");
		f.handles[ccs_plane] = 0;
		f.modifier[ccs_plane] = 0;
		f.pitches[ccs_plane] = 0;
		f.offsets[ccs_plane] = 0;
	}

	ret = drmIoctl(data->drm_fd, DRM_IOCTL_MODE_ADDFB2, &f);
	addfb_errno = errno;

	if (bad_ccs_bo)
		gem_close(data->drm_fd, bad_ccs_bo);

	igt_assert_eq(ret, -1);
	igt_assert_eq(addfb_errno, EINVAL);

	gem_close(data->drm_fd, fb.gem_handle);
}

static void test_bad_ccs_plane_params(data_t *data, int width, int height,
				      enum test_fb_flags fb_flags)
{
	for (int ccs_plane = 1;
	     ccs_plane <= (igt_format_is_yuv_semiplanar(data->format) ? 2 : 1);
	     ccs_plane++)
		test_bad_ccs_plane(data, width, height, ccs_plane, fb_flags);
}

static void test_bad_pixel_format(data_t *data, int width, int height,
				  enum test_fb_flags fb_flags)
{
	struct igt_fb fb = {};
	struct drm_mode_fb_cmd2 f = {};
	int ret;

	igt_assert(fb_flags & FB_COMPRESSED);
	create_fb_prepare_add(data->drm_fd, width, height,
			      DRM_FORMAT_RGB565, data->ccs_modifier,
			      &fb, &f);

	ret = drmIoctl(data->drm_fd, DRM_IOCTL_MODE_ADDFB2, &f);
	igt_assert_eq(ret, -1);
	igt_assert_eq(errno, EINVAL);

	gem_close(data->drm_fd, fb.gem_handle);
}

static void test_bad_fb_params(data_t *data, int width, int height, enum test_fb_flags fb_flags)
{
	if (data->flags & TEST_BAD_PIXEL_FORMAT)
		test_bad_pixel_format(data, width, height, fb_flags);

	if (data->flags & TEST_BAD_CCS_PLANE)
		test_bad_ccs_plane_params(data, width, height, fb_flags);
}

static void fast_clear_fb(int drm_fd, struct igt_fb *fb, const float *cc_color)
{
	igt_render_clearfunc_t fast_clear = igt_get_render_clearfunc(intel_get_drm_devid(drm_fd));
	struct intel_bb *ibb = intel_bb_create(drm_fd, 4096);
	struct buf_ops *bops = buf_ops_create(drm_fd);
	struct intel_buf *dst = igt_fb_create_intel_buf(drm_fd, bops, fb, "fast clear dst");

	if (is_i915_device(drm_fd))
		gem_set_domain(drm_fd, fb->gem_handle,
			       I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);

	fast_clear(ibb, dst, 0, 0, fb->width, fb->height, cc_color);

	intel_bb_sync(ibb);
	intel_bb_destroy(ibb);
	intel_buf_destroy(dst);
	buf_ops_destroy(bops);
}

static struct blt_copy_object *blt_fb_init(const struct igt_fb *fb,
					   uint32_t plane, uint32_t memregion,
					   uint8_t pat_index)
{
	uint32_t name, handle;
	struct blt_copy_object *blt;
	uint64_t stride;

	blt = malloc(sizeof(*blt));
	igt_assert(blt);

	name = gem_flink(fb->fd, fb->gem_handle);
	handle = gem_open(fb->fd, name);

	stride = fb->strides[plane] / 4;

	blt_set_object(blt, handle, fb->size, memregion,
		       intel_get_uc_mocs_index(fb->fd),
		       pat_index,
		       T_TILE4,
		       COMPRESSION_DISABLED,
		       COMPRESSION_TYPE_3D);

	blt_set_geom(blt, stride, 0, 0, fb->width, fb->plane_height[plane], 0, 0);
	blt->plane_offset = fb->offsets[plane];
	blt->ptr = xe_bo_mmap_ext(fb->fd, handle, fb->size,
				  PROT_READ | PROT_WRITE);
	return blt;
}

static enum blt_color_depth blt_get_bpp(const struct igt_fb *fb)
{
	switch (fb->plane_bpp[0]) {
	case 8:
		return CD_8bit;
	case 16:
		return CD_16bit;
	case 32:
		return CD_32bit;
	case 64:
		return CD_64bit;
	case 96:
		return CD_96bit;
	case 128:
		return CD_128bit;
	default:
		igt_assert(0);
	}
}

static uint32_t blt_compression_format(struct blt_copy_data *blt,
				       const struct igt_fb *fb)
{
	switch (fb->drm_format) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_P010:
	case DRM_FORMAT_P012:
	case DRM_FORMAT_P016:
	case DRM_FORMAT_YUYV:
		return 8;
	case DRM_FORMAT_XYUV8888:
	case DRM_FORMAT_NV12:
		return 9;
	default:
		igt_assert_f(0, "Unknown format\n");
	}
}

static void xe2_ccs_blit(data_t *data, struct igt_fb *fb, struct igt_fb *temp_fb)
{
	uint64_t ahnd = 0;

	struct blt_copy_data blt = {};
	struct blt_copy_object *src, *dst;
	struct blt_block_copy_data_ext ext = {}, *pext = NULL;
	uint32_t mem_region;
	intel_ctx_t *xe_ctx;
	uint32_t vm, exec_queue;
	uint32_t xe_bb;
	uint64_t bb_size = 4096;
	struct igt_fb *dst_fb = fb, *src_fb = temp_fb;

	struct drm_xe_engine_class_instance inst = {
		.engine_class = DRM_XE_ENGINE_CLASS_COPY,
	};

	vm = xe_vm_create(src_fb->fd, 0, 0);

	exec_queue = xe_exec_queue_create(src_fb->fd, vm, &inst, 0);
	xe_ctx = intel_ctx_xe(src_fb->fd, vm, exec_queue, 0, 0, 0);
	mem_region = vram_if_possible(src_fb->fd, 0);

	ahnd = intel_allocator_open_full(src_fb->fd, xe_ctx->vm, 0, 0,
						INTEL_ALLOCATOR_SIMPLE,
						ALLOC_STRATEGY_LOW_TO_HIGH, 0);

	bb_size = ALIGN(bb_size + xe_cs_prefetch_size(src_fb->fd),
			xe_get_default_alignment(src_fb->fd));
	xe_bb = xe_bo_create(src_fb->fd, 0, bb_size,
			     vram_if_possible(dst_fb->fd, 0), 0);

	for (int i = 0; i < dst_fb->num_planes; i++) {
		src = blt_fb_init(src_fb, i, mem_region, intel_get_pat_idx_uc(src_fb->fd));
		dst = blt_fb_init(dst_fb, i, mem_region, intel_get_pat_idx_wt(dst_fb->fd));

		blt_copy_init(src_fb->fd, &blt);
		blt.color_depth = blt_get_bpp(src_fb);
		blt_set_copy_object(&blt.src, src);
		blt_set_copy_object(&blt.dst, dst);

		blt_set_object_ext(&ext.src,
				blt_compression_format(&blt, src_fb),
				src_fb->width, src_fb->height,
				SURFACE_TYPE_2D);

		blt_set_object_ext(&ext.dst,
				blt_compression_format(&blt, dst_fb),
				dst_fb->width, dst_fb->height,
				SURFACE_TYPE_2D);

		pext = &ext;

		blt_set_batch(&blt.bb, xe_bb, bb_size, mem_region);

		blt_block_copy(src_fb->fd, xe_ctx, NULL, ahnd, &blt, pext);

		blt_destroy_object(src_fb->fd, src);
		blt_destroy_object(dst_fb->fd, dst);
	}

	put_ahnd(ahnd);
	gem_close(dst_fb->fd, xe_bb);
	xe_exec_queue_destroy(dst_fb->fd, exec_queue);
	xe_vm_destroy(dst_fb->fd, vm);
	free(xe_ctx);

	access_flat_ccs_surface(fb, true);
}

static void generate_fb(data_t *data, struct igt_fb *fb,
			int width, int height,
			enum test_fb_flags fb_flags)
{
	struct drm_mode_fb_cmd2 f = {0};
	uint64_t modifier;
	cairo_t *cr;
	const float cc_color[4] = {colors[!!data->plane].r,
				   colors[!!data->plane].g,
				   colors[!!data->plane].b,
				   1.0};

	/* Use either compressed or linear to test. However, given the lack of
	 * available bandwidth, we use linear for the primary plane when
	 * testing sprites, since we cannot fit two CCS planes into the
	 * available FIFO configurations.
	 */
	if (fb_flags & FB_COMPRESSED)
		modifier = data->ccs_modifier;
	else
		modifier = DRM_FORMAT_MOD_LINEAR;

	create_fb_prepare_add(data->drm_fd, width, height,
			      data->format, modifier,
			      fb, &f);

	if (data->flags & TEST_RANDOM) {
		srand(data->seed);
		fill_fb_random(data->drm_fd, fb);
	} else {
		bool do_fast_clear = igt_fb_is_gen12_rc_ccs_cc_modifier(data->ccs_modifier);
		bool do_solid_fill = do_fast_clear || data->plane;
		int c = !!data->plane;

		if (do_fast_clear && (fb_flags & FB_COMPRESSED)) {
			fast_clear_fb(data->drm_fd, fb, cc_color);
		} else {
			if (modifier == I915_FORMAT_MOD_4_TILED_BMG_CCS ||
			    modifier == I915_FORMAT_MOD_4_TILED_LNL_CCS) {
				struct igt_fb temp_fb;
				// non compressed temporary pattern image
				if (do_solid_fill)
					igt_create_color_fb(data->drm_fd, width, height,
						fb->drm_format, I915_FORMAT_MOD_4_TILED,
						colors[c].r, colors[c].g, colors[c].b,
						&temp_fb);
				else
					igt_create_pattern_fb(data->drm_fd, width, height,
							fb->drm_format, I915_FORMAT_MOD_4_TILED,
							&temp_fb);

				xe2_ccs_blit(data, fb, &temp_fb);
				igt_remove_fb(data->drm_fd, &temp_fb);
			} else {
				cr = igt_get_cairo_ctx(data->drm_fd, fb);

				if (do_solid_fill)
					igt_paint_color(cr, 0, 0, width, height,
							colors[c].r, colors[c].g, colors[c].b);
				else
					igt_paint_test_pattern(cr, width, height);

				igt_put_cairo_ctx(cr);
			}
		}
	}

	do_ioctl(data->drm_fd, DRM_IOCTL_MODE_ADDFB2, &f);

	if (check_ccs_planes)
		check_all_ccs_planes(data->drm_fd, fb, cc_color, !(data->flags & TEST_RANDOM));

	fb->fb_id = f.fb_id;
}

static igt_plane_t *first_sdr_plane(data_t *data)
{
	return igt_output_get_plane(data->output, SDR_PLANE_BASE);
}

static bool is_sdr_plane(const igt_plane_t *plane)
{
	return plane->index >= SDR_PLANE_BASE;
}

/*
 * Mixing SDR and HDR planes results in a CRC mismatch, so use the first
 * SDR/HDR plane as the main plane matching the SDR/HDR type of the sprite
 * plane under test.
 */
static igt_plane_t *compatible_main_plane(data_t *data)
{
	if (data->plane && is_sdr_plane(data->plane) &&
	    igt_format_is_yuv(data->format))
		return first_sdr_plane(data);

	return igt_output_get_plane_type(data->output, DRM_PLANE_TYPE_PRIMARY);
}

static bool try_config(data_t *data, enum test_fb_flags fb_flags,
		       igt_crc_t *crc)
{
	igt_display_t *display = &data->display;
	igt_plane_t *primary = compatible_main_plane(data);
	drmModeModeInfo *drm_mode = igt_output_get_mode(data->output);
	int fb_width = drm_mode->hdisplay;
	enum igt_commit_style commit;
	struct igt_fb fb = {};
	struct igt_fb fb_sprite = {};
	int ret;

	if (data->display.is_atomic)
		commit = COMMIT_ATOMIC;
	else
		commit = COMMIT_UNIVERSAL;

	if (primary == data->plane)
		return false;

	if (!igt_plane_has_format_mod(primary, data->format,
				      data->ccs_modifier))
		return false;

	if (igt_fb_is_gen12_rc_ccs_cc_modifier(data->ccs_modifier) &&
	    data->format != DRM_FORMAT_XRGB8888 &&
	    data->format != DRM_FORMAT_XRGB2101010)
		return false;

	if ((fb_flags & FB_MISALIGN_AUX_STRIDE) ||
	    (fb_flags & FB_SMALL_AUX_STRIDE))
		fb_width = max(fb_width, 1536);

	fb_width = min(MAX_SPRITE_PLANE_WIDTH, fb_width);

	if (data->flags & TEST_FAIL_ON_ADDFB2) {
		test_bad_fb_params(data, fb_width, drm_mode->vdisplay, fb_flags);
		return true;
	}

	if (data->plane && fb_flags & FB_COMPRESSED) {
		if (!igt_plane_has_format_mod(data->plane, data->format,
					      data->ccs_modifier))
			return false;

		generate_fb(data, &fb, fb_width, drm_mode->vdisplay,
			    (fb_flags & ~FB_COMPRESSED) | FB_HAS_PLANE);
		generate_fb(data, &fb_sprite, 256, 256, fb_flags);
	} else {
		generate_fb(data, &fb, fb_width, drm_mode->vdisplay, fb_flags);
	}

	igt_plane_set_position(primary, 0, 0);
	igt_plane_set_size(primary, drm_mode->hdisplay, drm_mode->vdisplay);
	igt_plane_set_fb(primary, &fb);

	if (data->plane && fb_flags & FB_COMPRESSED) {
		igt_plane_set_position(data->plane, 0, 0);
		igt_plane_set_size(data->plane, 256, 256);
		igt_plane_set_fb(data->plane, &fb_sprite);
	}

	if (data->flags & TEST_ROTATE_180)
		igt_plane_set_rotation(primary, IGT_ROTATION_180);
	if (data->flags & TEST_BAD_ROTATION_90)
		igt_plane_set_rotation(primary, IGT_ROTATION_90);

	ret = igt_display_try_commit2(display, commit);

	if (ret == 0 && !(fb_flags & TEST_BAD_ROTATION_90) && crc)
		igt_pipe_crc_collect_crc(data->pipe_crc, crc);

	igt_debug_wait_for_keypress("ccs");

	if (data->plane && fb_flags & FB_COMPRESSED) {
		igt_plane_set_position(data->plane, 0, 0);
		igt_plane_set_size(data->plane, 0, 0);
		igt_plane_set_fb(data->plane, NULL);
	}

	igt_plane_set_fb(primary, NULL);
	igt_plane_set_rotation(primary, IGT_ROTATION_0);
	igt_display_commit2(display, commit);

	igt_remove_fb(data->drm_fd, &fb_sprite);
	igt_remove_fb(data->drm_fd, &fb);

	igt_assert_eq(ret, data->flags & TEST_BAD_ROTATION_90 ? -EINVAL : 0);

	return true;
}

static int test_ccs(data_t *data)
{	int valid_tests = 0;
	igt_crc_t crc, ref_crc;
	enum test_fb_flags fb_flags = 0;

	if (data->flags & TEST_CRC) {
		data->pipe_crc = igt_pipe_crc_new(data->drm_fd, data->pipe,
						  IGT_PIPE_CRC_SOURCE_AUTO);

		if (try_config(data, fb_flags | FB_COMPRESSED, &ref_crc) &&
		    try_config(data, fb_flags, &crc)) {
			igt_assert_crc_equal(&crc, &ref_crc);
			valid_tests++;
		}

		igt_pipe_crc_free(data->pipe_crc);
		data->pipe_crc = NULL;
	}

	if (data->flags & TEST_RANDOM)
		valid_tests += try_config(data, fb_flags | FB_COMPRESSED | FB_RANDOM, NULL);

	if (data->flags & TEST_BAD_PIXEL_FORMAT ||
	    data->flags & TEST_BAD_ROTATION_90 ||
	    data->flags & TEST_NO_AUX_BUFFER ||
	    data->flags & TEST_BAD_CCS_HANDLE) {
		valid_tests += try_config(data, fb_flags | FB_COMPRESSED, NULL);
	}

	if (data->flags & TEST_BAD_AUX_STRIDE) {
		valid_tests += try_config(data, fb_flags | FB_COMPRESSED | FB_MISALIGN_AUX_STRIDE , NULL);
		valid_tests += try_config(data, fb_flags | FB_COMPRESSED | FB_SMALL_AUX_STRIDE , NULL);
		valid_tests += try_config(data, fb_flags | FB_COMPRESSED | FB_ZERO_AUX_STRIDE , NULL);
	}

	return valid_tests;
}

static bool skip_plane(data_t *data, igt_plane_t *plane)
{
	int index = plane->index;

	if (intel_display_ver(intel_get_drm_devid(data->drm_fd)) < 11)
		return false;

	/*
	 * Test 1 HDR plane, 1 SDR UV plane, 1 SDR Y plane.
	 *
	 * Kernel registers planes in the hardware Z order:
	 * 0,1,2 HDR planes
	 * 3,4 SDR UV planes
	 * 5,6 SDR Y planes
	 */
	return index != 0 && index != 3 && index != 5;
}

static void test_output(data_t *data, const int testnum)
{
	uint16_t dev_id;

	igt_fixture
		dev_id = intel_get_drm_devid(data->drm_fd);

	data->flags = tests[testnum].flags;

	for (int i = 0; i < ARRAY_SIZE(ccs_modifiers); i++) {
		if (((ccs_modifiers[i].modifier == I915_FORMAT_MOD_4_TILED_DG2_RC_CCS ||
		    ccs_modifiers[i].modifier == I915_FORMAT_MOD_4_TILED_DG2_MC_CCS ||
		    ccs_modifiers[i].modifier == I915_FORMAT_MOD_4_TILED_DG2_RC_CCS_CC) &&
		    tests[testnum].flags & TEST_BAD_CCS_PLANE) ||
		    (tests[testnum].flags & TEST_FAIL_ON_ADDFB2 &&
		    (ccs_modifiers[i].modifier == I915_FORMAT_MOD_4_TILED_BMG_CCS ||
		    ccs_modifiers[i].modifier == I915_FORMAT_MOD_4_TILED_LNL_CCS)))
			continue;

		data->ccs_modifier = ccs_modifiers[i].modifier;

		igt_describe(tests[testnum].description);
		igt_subtest_with_dynamic_f("%s-%s", tests[testnum].testname, ccs_modifiers[i].str) {
			if (ccs_modifiers[i].modifier == I915_FORMAT_MOD_4_TILED_BMG_CCS ||
			    ccs_modifiers[i].modifier == I915_FORMAT_MOD_4_TILED_LNL_CCS) {
				igt_require_f(AT_LEAST_GEN(dev_id, 20),
					      "Xe2 platform needed.\n");
			} else {
				igt_require_f(intel_get_device_info(dev_id)->graphics_ver < 20,
					      "Older than Xe2 platform needed.\n");
			}

			for_each_pipe_with_valid_output(&data->display, data->pipe, data->output) {
				igt_display_reset(&data->display);

				igt_output_set_pipe(data->output, data->pipe);
				if (!intel_pipe_output_combo_valid(&data->display))
					continue;

				igt_dynamic_f("pipe-%s-%s", kmstest_pipe_name(data->pipe),
							    data->output->name) {
					int valid_tests = 0;

					if (data->flags == TEST_RANDOM)
						igt_info("Testing with seed %d\n", data->seed);

					if (data->flags & TEST_ALL_PLANES) {
						igt_display_require_output_on_pipe(&data->display, data->pipe);

						for_each_plane_on_pipe(&data->display, data->pipe, data->plane) {
							if (skip_plane(data, data->plane))
								continue;

							for (int j = 0; j < ARRAY_SIZE(formats); j++) {
								data->format = formats[j];
								valid_tests += test_ccs(data);
							}
						}
					} else {
						for (int j = 0; j < ARRAY_SIZE(formats); j++) {
							data->format = formats[j];
							valid_tests += test_ccs(data);
						}
					}
					igt_require_f(valid_tests > 0,
						      "no valid tests for %s on pipe %s\n",
						      ccs_modifiers[i].str,
						      kmstest_pipe_name(data->pipe));
				}
			}
		}
	}

	igt_fixture
		data->plane = NULL;
}

static int opt_handler(int opt, int opt_index, void *opt_data)
{
	data_t *data = opt_data;

	switch (opt) {
	case 'c':
		check_ccs_planes = true;
		break;
	case 's':
		data->user_seed = true;
		data->seed = strtoul(optarg, NULL, 0);
		break;
	default:
		return IGT_OPT_HANDLER_ERROR;
	}

	return IGT_OPT_HANDLER_SUCCESS;
}

static data_t data;

static const char *help_str =
"  -c\t\tCheck the presence of compression meta-data\n"
"  -s <seed>\tSeed for random number generator\n"
;

igt_main_args("cs:", NULL, help_str, opt_handler, &data)
{
	igt_fixture {
		data.drm_fd = drm_open_driver_master(DRIVER_INTEL | DRIVER_XE);

		igt_require(intel_display_ver(intel_get_drm_devid(data.drm_fd)) >= 9);
		kmstest_set_vt_graphics_mode();
		igt_require_pipe_crc(data.drm_fd);

		igt_display_require(&data.display, data.drm_fd);
		igt_display_require_output(&data.display);

		if (!data.user_seed)
			data.seed = time(NULL);
	}

	for (int c = 0; c < ARRAY_SIZE(tests); c++)
		test_output(&data, c);

	igt_fixture {
		igt_display_fini(&data.display);
		drm_close_driver(data.drm_fd);
	}
}
