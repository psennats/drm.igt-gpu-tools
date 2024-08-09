// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

/**
 * TEST: Tests for GPGPU shader and system routine (SIP) execution
 * Category: Software building block
 * Description: Exercise interaction between GPGPU shader and system routine
 *              (SIP), which should handle exceptions raised on Execution Unit.
 * Functionality: system routine
 * Mega feature: Compute
 * Sub-category: GPGPU tests
 * Test category: functionality test
 */

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>

#include "gpgpu_shader.h"
#include "igt.h"
#include "igt_sysfs.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"

#define WIDTH 64
#define HEIGHT 64

#define COLOR_C4 0xc4

#define SHADER_CANARY 0x01010101

#define NSEC_PER_MSEC (1000 * 1000ull)

static struct intel_buf *
create_fill_buf(int fd, int width, int height, uint8_t color)
{
	struct intel_buf *buf;
	uint8_t *ptr;

	buf = calloc(1, sizeof(*buf));
	igt_assert(buf);

	intel_buf_init(buf_ops_create(fd), buf, width / 4, height, 32, 0,
		       I915_TILING_NONE, 0);

	ptr = xe_bo_map(fd, buf->handle, buf->surface[0].size);
	memset(ptr, color, buf->surface[0].size);
	munmap(ptr, buf->surface[0].size);

	return buf;
}

static struct gpgpu_shader *get_shader(int fd)
{
	static struct gpgpu_shader *shader;

	shader = gpgpu_shader_create(fd);
	gpgpu_shader__write_dword(shader, SHADER_CANARY, 0);
	gpgpu_shader__eot(shader);
	return shader;
}

static uint32_t gpgpu_shader(int fd, struct intel_bb *ibb, unsigned int threads,
			     unsigned int width, unsigned int height)
{
	struct intel_buf *buf = create_fill_buf(fd, width, height, COLOR_C4);
	struct gpgpu_shader *shader = get_shader(fd);

	gpgpu_shader_exec(ibb, buf, 1, threads, shader, NULL, 0, 0);
	gpgpu_shader_destroy(shader);
	return buf->handle;
}

static void check_fill_buf(uint8_t *ptr, const int width, const int x,
			   const int y, const uint8_t color)
{
	const uint8_t val = ptr[y * width + x];

	igt_assert_f(val == color,
		     "Expected 0x%02x, found 0x%02x at (%d,%d)\n",
		     color, val, x, y);
}

static void check_buf(int fd, uint32_t handle, int width, int height,
		      uint8_t poison_c)
{
	unsigned int sz = ALIGN(width * height, 4096);
	int thread_count = 0;
	uint32_t *ptr;
	int i, j;

	ptr = xe_bo_mmap_ext(fd, handle, sz, PROT_READ);

	for (i = 0, j = 0; j < height / 2; ++j) {
		if (ptr[j * width / 4] == SHADER_CANARY) {
			++thread_count;
			i = 4;
		}

		for (; i < width; i++)
			check_fill_buf((uint8_t *)ptr, width, i, j, poison_c);

		i = 0;
	}

	igt_assert(thread_count);

	munmap(ptr, sz);
}

static uint64_t
xe_sysfs_get_job_timeout_ms(int fd, struct drm_xe_engine_class_instance *eci)
{
	int engine_fd = -1;
	uint64_t ret;

	engine_fd = xe_sysfs_engine_open(fd, eci->gt_id, eci->engine_class);
	ret = igt_sysfs_get_u64(engine_fd, "job_timeout_ms");
	close(engine_fd);

	return ret;
}

/**
 * SUBTEST: sanity
 * Description: check basic shader with write operation
 *
 */
static void test_sip(struct drm_xe_engine_class_instance *eci, uint32_t flags)
{
	unsigned int threads = 512;
	unsigned int height = max_t(threads, HEIGHT, threads * 2);
	uint32_t exec_queue_id, handle, vm_id;
	unsigned int width = WIDTH;
	struct timespec ts = { };
	uint64_t timeout;
	struct intel_bb *ibb;
	int fd;

	igt_debug("Using %s\n", xe_engine_class_string(eci->engine_class));

	fd = drm_open_driver(DRIVER_XE);
	xe_device_get(fd);

	vm_id = xe_vm_create(fd, 0, 0);

	/* Get timeout for job, and add 4s to ensure timeout processes in subtest. */
	timeout = xe_sysfs_get_job_timeout_ms(fd, eci) + 4ull * MSEC_PER_SEC;
	timeout *= NSEC_PER_MSEC;
	timeout *= igt_run_in_simulation() ? 10 : 1;

	exec_queue_id = xe_exec_queue_create(fd, vm_id, eci, 0);
	ibb = intel_bb_create_with_context(fd, exec_queue_id, vm_id, NULL, 4096);

	igt_nsec_elapsed(&ts);
	handle = gpgpu_shader(fd, ibb, threads, width, height);

	intel_bb_sync(ibb);
	igt_assert_lt_u64(igt_nsec_elapsed(&ts), timeout);

	check_buf(fd, handle, width, height, COLOR_C4);

	gem_close(fd, handle);
	intel_bb_destroy(ibb);

	xe_exec_queue_destroy(fd, exec_queue_id);
	xe_vm_destroy(fd, vm_id);
	xe_device_put(fd);
	close(fd);
}

#define test_render_and_compute(t, __fd, __eci) \
	igt_subtest_with_dynamic(t) \
		xe_for_each_engine(__fd, __eci) \
			if (__eci->engine_class == DRM_XE_ENGINE_CLASS_RENDER || \
			    __eci->engine_class == DRM_XE_ENGINE_CLASS_COMPUTE) \
				igt_dynamic_f("%s%d", xe_engine_class_string(__eci->engine_class), \
					      __eci->engine_instance)

igt_main
{
	struct drm_xe_engine_class_instance *eci;
	int fd;

	igt_fixture
		fd = drm_open_driver(DRIVER_XE);

	test_render_and_compute("sanity", fd, eci)
		test_sip(eci, 0);

	igt_fixture
		drm_close_driver(fd);
}
