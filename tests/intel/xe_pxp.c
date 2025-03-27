// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024-2025 Intel Corporation
 */

#include "igt.h"
#include "intel_batchbuffer.h"
#include "intel_bufops.h"
#include "intel_mocs.h"
#include "intel_pat.h"
#include "xe_drm.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"

IGT_TEST_DESCRIPTION("Test PXP that manages protected content through arbitrated HW-PXP-session");
/* Note: PXP = "Protected Xe Path" */

/**
 * TEST: Test PXP functionality
 * Category: Content protection
 * Mega feature: PXP
 * Sub-category: PXP tests
 * Functionality: Execution of protected content
 * Test category: functionality test
 */

static int __pxp_bo_create(int fd, uint32_t vm, uint64_t size,
			   uint32_t session_type, uint32_t *handle)
{
	struct drm_xe_ext_set_property ext = {
		.base.next_extension = 0,
		.base.name = DRM_XE_GEM_CREATE_EXTENSION_SET_PROPERTY,
		.property = DRM_XE_GEM_CREATE_SET_PROPERTY_PXP_TYPE,
		.value = session_type,
	};
	int ret = 0;

	if (__xe_bo_create(fd, vm, size, system_memory(fd), 0, &ext, handle)) {
		ret = -errno;
		errno = 0;
	}

	return ret;
}

static uint32_t pxp_bo_create(int fd, uint32_t vm, uint64_t size, uint32_t type)
{
	uint32_t handle;

	igt_assert_eq(__pxp_bo_create(fd, vm, size, type, &handle), 0);

	return handle;
}

static int __create_pxp_rcs_queue(int fd, uint32_t vm,
				  uint32_t session_type,
				  uint32_t *q)
{
	struct drm_xe_engine_class_instance inst = {
		.engine_class = DRM_XE_ENGINE_CLASS_RENDER,
	};
	struct drm_xe_ext_set_property ext = { 0 };
	uint64_t ext_ptr = to_user_pointer(&ext);

	ext.base.next_extension = 0,
	ext.base.name = DRM_XE_EXEC_QUEUE_EXTENSION_SET_PROPERTY,
	ext.property = DRM_XE_EXEC_QUEUE_SET_PROPERTY_PXP_TYPE,
	ext.value = session_type;

	return __xe_exec_queue_create(fd, vm, 1, 1, &inst, ext_ptr, q);
}

static uint32_t create_pxp_rcs_queue(int fd, uint32_t vm)
{
	uint32_t q;
	int err;

	err = __create_pxp_rcs_queue(fd, vm, DRM_XE_PXP_TYPE_HWDRM, &q);
	igt_assert_eq(err, 0);

	return q;
}

static int query_pxp_status(int fd)
{
	struct drm_xe_query_pxp_status *pxp_query;
	struct drm_xe_device_query query = {
		.extensions = 0,
		.query = DRM_XE_DEVICE_QUERY_PXP_STATUS,
		.size = 0,
		.data = 0,
	};
	int ret;

	if (igt_ioctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query))
		return -errno;

	pxp_query = malloc(query.size);
	igt_assert(pxp_query);
	memset(pxp_query, 0, query.size);

	query.data = to_user_pointer(pxp_query);

	if (igt_ioctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query))
		ret = -errno;
	else
		ret = pxp_query->status;

	free(pxp_query);

	return ret;
}

static bool is_pxp_hw_supported(int fd)
{
	int pxp_status;
	int i = 0;

	/* PXP init completes after driver init, so we might have to wait for it */
	while (i++ < 50) {
		pxp_status = query_pxp_status(fd);

		/* -EINVAL means the PXP interface is not available */
		igt_require(pxp_status != -EINVAL);

		/* -ENODEV means PXP not supported or disabled */
		if (pxp_status == -ENODEV)
			return false;

		/* status 1 means pxp is ready */
		if (pxp_status == 1)
			return true;

		/*
		 * 0 means init still in progress, any other remaining state
		 * is an error
		 */
		igt_assert_eq(pxp_status, 0);

		usleep(50*1000);
	}

	igt_assert_f(0, "PXP failed to initialize within the timeout\n");
	return false;
}

/**
 * SUBTEST: pxp-bo-alloc
 * Description: Verify PXP bo allocation works as expected
 */
static void test_pxp_bo_alloc(int fd, bool pxp_supported)
{
	uint32_t bo;
	int ret;

	/* BO creation with DRM_XE_PXP_TYPE_NONE must always succeed */
	ret = __pxp_bo_create(fd, 0, 4096, DRM_XE_PXP_TYPE_NONE, &bo);
	igt_assert_eq(ret, 0);
	gem_close(fd, bo);

	/* BO creation with DRM_XE_PXP_TYPE_HWDRM must only succeed if PXP is supported */
	ret = __pxp_bo_create(fd, 0, 4096, DRM_XE_PXP_TYPE_HWDRM, &bo);
	igt_assert_eq(ret, pxp_supported ? 0 : -ENODEV);
	if (!ret)
		gem_close(fd, bo);

	/* BO creation with an invalid type must always fail */
	ret = __pxp_bo_create(fd, 0, 4096, 0xFF, &bo);
	igt_assert_eq(ret, -EINVAL);
}

/**
 * SUBTEST: pxp-queue-alloc
 * Description: Verify PXP exec queue creation works as expected
 */
static void test_pxp_queue_creation(int fd, bool pxp_supported)
{
	uint32_t q;
	uint32_t vm;
	int ret;

	vm = xe_vm_create(fd, 0, 0);

	/* queue creation with DRM_XE_PXP_TYPE_NONE must always succeed */
	ret = __create_pxp_rcs_queue(fd, vm, DRM_XE_PXP_TYPE_NONE, &q);
	igt_assert_eq(ret, 0);
	xe_exec_queue_destroy(fd, q);

	/* queue creation with DRM_XE_PXP_TYPE_HWDRM must only succeed if PXP is supported */
	ret = __create_pxp_rcs_queue(fd, vm, DRM_XE_PXP_TYPE_HWDRM, &q);
	igt_assert_eq(ret, pxp_supported ? 0 : -ENODEV);
	if (!ret)
		xe_exec_queue_destroy(fd, q);

	/* queue creation with an invalid type must always fail */
	ret = __create_pxp_rcs_queue(fd, vm, 0xFF, &q);
	igt_assert_eq(ret, -EINVAL);

	xe_vm_destroy(fd, vm);
}

static void fill_bo_content(int fd, uint32_t bo, uint32_t size, uint8_t initcolor)
{
	uint32_t *ptr;

	ptr = xe_bo_mmap_ext(fd, bo, size, PROT_READ|PROT_WRITE);

	/* read and count all dword matches till size */
	memset(ptr, initcolor, size);

	igt_assert(munmap(ptr, size) == 0);
}

static void __check_bo_color(int fd, uint32_t bo, uint32_t size, uint32_t color, bool should_match)
{
	uint64_t comp;
	uint64_t *ptr;
	int i, num_matches = 0;

	comp = color;
	comp = comp | (comp << 32);

	ptr =  xe_bo_mmap_ext(fd, bo, size, PROT_READ);

	igt_assert_eq(size % sizeof(uint64_t), 0);

	for (i = 0; i < (size / sizeof(uint64_t)); i++)
		if (ptr[i] == comp)
			++num_matches;

	if (should_match)
		igt_assert_eq(num_matches, (size / sizeof(uint64_t)));
	else
		igt_assert_eq(num_matches, 0);
}

static void check_bo_color(int fd, uint32_t bo, uint32_t size, uint8_t color, bool should_match)
{
	uint32_t comp;

	/*
	 * We memset the buffer using a u8 color value. However, this is too
	 * small to ensure the encrypted data does not accidentally match it,
	 * so we scale it up to a bigger size.
	 */
	comp = color;
	comp = comp | (comp << 8) | (comp << 16) | (comp << 24);

	return __check_bo_color(fd, bo, size, comp, should_match);
}

static uint32_t __bo_create_and_fill(int fd, uint32_t vm, bool protected,
				     uint32_t size, uint8_t init_color)
{
	uint32_t bo;

	if (protected)
		bo = pxp_bo_create(fd, vm, size, DRM_XE_PXP_TYPE_HWDRM);
	else
		bo = xe_bo_create(fd, vm, size, system_memory(fd), 0);

	fill_bo_content(fd, bo, size, init_color);

	return bo;
}

static uint32_t pxp_bo_create_and_fill(int fd, uint32_t vm, uint32_t size,
				       uint8_t init_color)
{
	return __bo_create_and_fill(fd, vm, true, size, init_color);
}

static uint32_t regular_bo_create_and_fill(int fd, uint32_t vm, uint32_t size,
					   uint8_t init_color)
{
	return __bo_create_and_fill(fd, vm, false, size, init_color);
}

static struct intel_buf *buf_create(int fd, struct buf_ops *bops, uint32_t handle,
				    int width, int height, int bpp, uint64_t size)
{
	igt_assert(handle);
	igt_assert(size);
	return intel_buf_create_full(bops, handle, width, height, bpp, 0,
				     I915_TILING_NONE, 0, size, 0,
				     system_memory(fd),
				     DEFAULT_PAT_INDEX, DEFAULT_MOCS_INDEX);
}

/* Rendering tests surface attributes */
#define TSTSURF_WIDTH		64
#define TSTSURF_HEIGHT		64
#define TSTSURF_BYTESPP		4
#define TSTSURF_STRIDE		(TSTSURF_WIDTH * TSTSURF_BYTESPP)
#define TSTSURF_SIZE		(TSTSURF_STRIDE * TSTSURF_HEIGHT)
#define TSTSURF_INITCOLOR1  0xAA
#define TSTSURF_FILLCOLOR1  0x55
#define TSTSURF_INITCOLOR2  0x33

static void pxp_rendercopy(int fd, uint32_t q, uint32_t vm, uint32_t copy_size,
			   uint32_t srcbo, bool src_pxp, uint32_t dstbo, bool dst_pxp)
{
	igt_render_copyfunc_t render_copy;
	struct intel_buf *srcbuf, *dstbuf;
	struct buf_ops *bops;
	struct intel_bb *ibb;

	/*
	 * we use the defined width and height below, which only works if the BO
	 * size is TSTSURF_SIZE
	 */
	igt_assert_eq(copy_size, TSTSURF_SIZE);

	render_copy = igt_get_render_copyfunc(fd);
	igt_assert(render_copy);

	bops = buf_ops_create(fd);
	igt_assert(bops);

	ibb = intel_bb_create_with_context(fd, q, vm, NULL, 4096);
	igt_assert(ibb);
	intel_bb_set_pxp(ibb, true, DISPLAY_APPTYPE, DRM_XE_PXP_HWDRM_DEFAULT_SESSION);

	dstbuf = buf_create(fd, bops, dstbo, TSTSURF_WIDTH, TSTSURF_HEIGHT,
			    TSTSURF_BYTESPP * 8, TSTSURF_SIZE);
	intel_buf_set_pxp(dstbuf, dst_pxp);

	srcbuf = buf_create(fd, bops, srcbo, TSTSURF_WIDTH, TSTSURF_HEIGHT,
			    TSTSURF_BYTESPP * 8, TSTSURF_SIZE);
	intel_buf_set_pxp(srcbuf, src_pxp);

	render_copy(ibb, srcbuf, 0, 0, TSTSURF_WIDTH, TSTSURF_HEIGHT, dstbuf, 0, 0);
	intel_bb_sync(ibb);

	intel_buf_destroy(srcbuf);
	intel_buf_destroy(dstbuf);
	intel_bb_destroy(ibb);
	buf_ops_destroy(bops);
}

/**
 * SUBTEST: regular-src-to-pxp-dest-rendercopy
 * Description: copy from a regular BO to a PXP one and verify the encryption
 */
static void test_render_regular_src_to_pxp_dest(int fd)
{
	uint32_t vm, srcbo, dstbo;
	uint32_t q;

	vm = xe_vm_create(fd, 0, 0);

	/*
	 * Perform a protected render operation but only label the dest as
	 * protected. After rendering, the content should be encrypted.
	 */
	q = create_pxp_rcs_queue(fd, vm);

	srcbo = regular_bo_create_and_fill(fd, vm, TSTSURF_SIZE, TSTSURF_FILLCOLOR1);
	dstbo = pxp_bo_create_and_fill(fd, vm, TSTSURF_SIZE, TSTSURF_INITCOLOR1);

	pxp_rendercopy(fd, q, vm, TSTSURF_SIZE, srcbo, false, dstbo, true);

	check_bo_color(fd, dstbo, TSTSURF_SIZE, TSTSURF_FILLCOLOR1, false);

	gem_close(fd, srcbo);
	gem_close(fd, dstbo);
	xe_exec_queue_destroy(fd, q);
	xe_vm_destroy(fd, vm);
}

static int bocmp(int fd, uint32_t bo1, uint32_t bo2, uint32_t size)
{
	uint32_t *ptr1, *ptr2;
	int ret;

	ptr1 = xe_bo_mmap_ext(fd, bo1, size, PROT_READ);
	ptr2 = xe_bo_mmap_ext(fd, bo2, size, PROT_READ);

	ret = memcmp(ptr1, ptr2, size);

	igt_assert_eq(munmap(ptr1, size), 0);
	igt_assert_eq(munmap(ptr2, size), 0);

	return ret;
}

/**
 * SUBTEST: pxp-src-to-pxp-dest-rendercopy
 * Description: copy between 2 PXP BOs and verify the encryption
 */

static void test_render_pxp_protsrc_to_protdest(int fd)
{
	uint32_t vm, srcbo, dstbo, dstbo2;
	uint32_t q;

	vm = xe_vm_create(fd, 0, 0);

	q = create_pxp_rcs_queue(fd, vm);

	/*
	 * Copy from a regular src to a PXP dst to get a buffer with a
	 * valid encryption.
	 */
	srcbo = regular_bo_create_and_fill(fd, vm, TSTSURF_SIZE, TSTSURF_FILLCOLOR1);
	dstbo = pxp_bo_create_and_fill(fd, vm, TSTSURF_SIZE, TSTSURF_INITCOLOR1);

	pxp_rendercopy(fd, q, vm, TSTSURF_SIZE, srcbo, false, dstbo, true);

	check_bo_color(fd, dstbo, TSTSURF_SIZE, TSTSURF_FILLCOLOR1, false);

	/*
	 * Reuse prior dst as the new-src and create dst2 as the new-dest.
	 * After the rendering, we should find no difference in content since
	 * both new-src and new-dest are labelled as encrypted. HW should read
	 * and decrypt new-src, perform the copy and re-encrypt with the same
	 * key when going into new-dest
	 */
	dstbo2 = pxp_bo_create_and_fill(fd, vm, TSTSURF_SIZE, TSTSURF_INITCOLOR2);

	pxp_rendercopy(fd, q, vm, TSTSURF_SIZE, dstbo, true, dstbo2, true);

	igt_assert_eq(bocmp(fd, dstbo, dstbo2, TSTSURF_SIZE), 0);

	gem_close(fd, srcbo);
	gem_close(fd, dstbo);
	gem_close(fd, dstbo2);
	xe_exec_queue_destroy(fd, q);
	xe_vm_destroy(fd, vm);
}

static void require_pxp_render(int fd, bool pxp_supported)
{
	igt_require_f(pxp_supported, "PXP not supported\n");
	igt_require_f(igt_get_render_copyfunc(fd), "No rendercopy found\n");
}

igt_main
{
	int xe_fd = -1;
	bool pxp_supported = true;

	igt_fixture {
		xe_fd = drm_open_driver(DRIVER_XE);
		igt_require(xe_has_engine_class(xe_fd, DRM_XE_ENGINE_CLASS_RENDER));
		pxp_supported = is_pxp_hw_supported(xe_fd);
	}

	igt_subtest_group {
		igt_describe("Verify PXP allocations work as expected");
		igt_subtest("pxp-bo-alloc")
			test_pxp_bo_alloc(xe_fd, pxp_supported);

		igt_subtest("pxp-queue-alloc")
			test_pxp_queue_creation(xe_fd, pxp_supported);
	}

	igt_subtest_group {
		igt_describe("Verify protected render operations:");
		igt_subtest("regular-src-to-pxp-dest-rendercopy") {
			require_pxp_render(xe_fd, pxp_supported);
			test_render_regular_src_to_pxp_dest(xe_fd);
		}
		igt_subtest("pxp-src-to-pxp-dest-rendercopy") {
			require_pxp_render(xe_fd, pxp_supported);
			test_render_pxp_protsrc_to_protdest(xe_fd);
		}
	}

	igt_fixture {
		close(xe_fd);
	}
}
