// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024-2025 Intel Corporation
 */

#include "igt.h"
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

	igt_fixture {
		close(xe_fd);
	}
}
