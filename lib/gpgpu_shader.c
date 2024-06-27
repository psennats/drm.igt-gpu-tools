// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 *
 * Author: Dominik Grzegorzek <dominik.grzegorzek@intel.com>
 */

#include <i915_drm.h>

#include "ioctl_wrappers.h"
#include "gpgpu_shader.h"
#include "gpu_cmds.h"

#define IGA64_ARG0 0xc0ded000
#define IGA64_ARG_MASK 0xffffff00

#define SUPPORTED_GEN_VER 1200 /* Support TGL and up */

#define PAGE_SIZE 4096
#define BATCH_STATE_SPLIT 2048
/* VFE STATE params */
#define THREADS (1 << 16) /* max value */
#define GEN8_GPGPU_URB_ENTRIES 1
#define GPGPU_URB_SIZE 0
#define GPGPU_CURBE_SIZE 0
#define GEN7_VFE_STATE_GPGPU_MODE 1

static void gpgpu_shader_extend(struct gpgpu_shader *shdr)
{
	shdr->max_size <<= 1;
	shdr->code = realloc(shdr->code, 4 * shdr->max_size);
	igt_assert(shdr->code);
}

void
__emit_iga64_code(struct gpgpu_shader *shdr, struct iga64_template const *tpls,
		  int argc, uint32_t *argv)
{
	uint32_t *ptr;

	igt_require_f(shdr->gen_ver >= SUPPORTED_GEN_VER,
		      "No available shader templates for platforms older than XeLP\n");

	while (shdr->gen_ver < tpls->gen_ver)
		tpls++;

	while (shdr->max_size < shdr->size + tpls->size)
		gpgpu_shader_extend(shdr);

	ptr = shdr->code + shdr->size;
	memcpy(ptr, tpls->code, 4 * tpls->size);

	/* patch the template */
	for (int n, i = 0; i < tpls->size; ++i) {
		if ((ptr[i] & IGA64_ARG_MASK) != IGA64_ARG0)
			continue;
		n = ptr[i] - IGA64_ARG0;
		igt_assert(n < argc);
		ptr[i] = argv[n];
	}

	shdr->size += tpls->size;
}

static uint32_t fill_sip(struct intel_bb *ibb,
			 const uint32_t sip[][4],
			 const size_t size)
{
	uint32_t *sip_dst;
	uint32_t offset;

	intel_bb_ptr_align(ibb, 16);
	sip_dst = intel_bb_ptr(ibb);
	offset = intel_bb_offset(ibb);

	memcpy(sip_dst, sip, size);

	intel_bb_ptr_add(ibb, size);

	return offset;
}

static void emit_sip(struct intel_bb *ibb, const uint64_t offset)
{
	intel_bb_out(ibb, GEN4_STATE_SIP | (3 - 2));
	intel_bb_out(ibb, lower_32_bits(offset));
	intel_bb_out(ibb, upper_32_bits(offset));
}

static void
__xelp_gpgpu_execfunc(struct intel_bb *ibb,
		      struct intel_buf *target,
		      unsigned int x_dim, unsigned int y_dim,
		      struct gpgpu_shader *shdr,
		      struct gpgpu_shader *sip,
		      uint64_t ring, bool explicit_engine)
{
	uint32_t interface_descriptor, sip_offset;
	uint64_t engine;

	intel_bb_add_intel_buf(ibb, target, true);

	intel_bb_ptr_set(ibb, BATCH_STATE_SPLIT);

	interface_descriptor = gen8_fill_interface_descriptor(ibb, target,
							      shdr->instr,
							      4 * shdr->size);

	if (sip && sip->size)
		sip_offset = fill_sip(ibb, sip->instr, 4 * sip->size);
	else
		sip_offset = 0;

	intel_bb_ptr_set(ibb, 0);

	/* GPGPU pipeline */
	intel_bb_out(ibb, GEN7_PIPELINE_SELECT | GEN9_PIPELINE_SELECTION_MASK |
		     PIPELINE_SELECT_GPGPU);

	gen9_emit_state_base_address(ibb);

	xelp_emit_vfe_state(ibb, THREADS, GEN8_GPGPU_URB_ENTRIES,
			    GPGPU_URB_SIZE, GPGPU_CURBE_SIZE, true);

	gen7_emit_interface_descriptor_load(ibb, interface_descriptor);

	if (sip_offset)
		emit_sip(ibb, sip_offset);

	gen8_emit_gpgpu_walk(ibb, 0, 0, x_dim * 16, y_dim);

	intel_bb_out(ibb, MI_BATCH_BUFFER_END);
	intel_bb_ptr_align(ibb, 32);

	engine = explicit_engine ? ring : I915_EXEC_DEFAULT;
	intel_bb_exec(ibb, intel_bb_offset(ibb),
		      engine | I915_EXEC_NO_RELOC, false);
}

static void
__xehp_gpgpu_execfunc(struct intel_bb *ibb,
		      struct intel_buf *target,
		      unsigned int x_dim, unsigned int y_dim,
		      struct gpgpu_shader *shdr,
		      struct gpgpu_shader *sip,
		      uint64_t ring, bool explicit_engine)
{
	struct xehp_interface_descriptor_data idd;
	uint32_t sip_offset;
	uint64_t engine;

	intel_bb_add_intel_buf(ibb, target, true);

	intel_bb_ptr_set(ibb, BATCH_STATE_SPLIT);

	xehp_fill_interface_descriptor(ibb, target, shdr->instr,
				       4 * shdr->size, &idd);

	if (sip && sip->size)
		sip_offset = fill_sip(ibb, sip->instr, 4 * sip->size);
	else
		sip_offset = 0;

	intel_bb_ptr_set(ibb, 0);

	/* GPGPU pipeline */
	intel_bb_out(ibb, GEN7_PIPELINE_SELECT | GEN9_PIPELINE_SELECTION_MASK |
		     PIPELINE_SELECT_GPGPU);
	xehp_emit_state_base_address(ibb);
	xehp_emit_state_compute_mode(ibb);
	xehp_emit_state_binding_table_pool_alloc(ibb);
	xehp_emit_cfe_state(ibb, THREADS);

	if (sip_offset)
		emit_sip(ibb, sip_offset);

	xehp_emit_compute_walk(ibb, 0, 0, x_dim * 16, y_dim, &idd, 0x0);

	intel_bb_out(ibb, MI_BATCH_BUFFER_END);
	intel_bb_ptr_align(ibb, 32);

	engine = explicit_engine ? ring : I915_EXEC_DEFAULT;
	intel_bb_exec(ibb, intel_bb_offset(ibb),
		      engine | I915_EXEC_NO_RELOC, false);
}

/**
 * gpgpu_shader_exec:
 * @ibb: pointer to initialized intel_bb
 * @target: pointer to initialized intel_buf to be written by shader/sip
 * @x_dim: gpgpu/compute walker thread group width
 * @y_dim: gpgpu/compute walker thread group height
 * @shdr: shader to be executed
 * @sip: sip to be executed, can be NULL
 * @ring: engine index
 * @explicit_engine: whether to use provided engine index
 *
 * Execute provided shader in asynchronous fashion. To wait for completion,
 * caller has to use the provided ibb handle.
 */
void gpgpu_shader_exec(struct intel_bb *ibb,
		       struct intel_buf *target,
		       unsigned int x_dim, unsigned int y_dim,
		       struct gpgpu_shader *shdr,
		       struct gpgpu_shader *sip,
		       uint64_t ring, bool explicit_engine)
{
	igt_require(shdr->gen_ver >= SUPPORTED_GEN_VER);
	igt_assert(ibb->size >= PAGE_SIZE);
	igt_assert(ibb->ptr == ibb->batch);

	if (shdr->gen_ver >= 1250)
		__xehp_gpgpu_execfunc(ibb, target, x_dim, y_dim, shdr, sip,
				      ring, explicit_engine);
	else
		__xelp_gpgpu_execfunc(ibb, target, x_dim, y_dim, shdr, sip,
				      ring, explicit_engine);
}

/**
 * gpgpu_shader_create:
 * @fd: drm fd - i915 or xe
 *
 * Creates empty shader.
 *
 * Returns: pointer to empty shader struct.
 */
struct gpgpu_shader *gpgpu_shader_create(int fd)
{
	struct gpgpu_shader *shdr = calloc(1, sizeof(struct gpgpu_shader));
	const struct intel_device_info *info;

	igt_assert(shdr);
	info = intel_get_device_info(intel_get_drm_devid(fd));
	shdr->gen_ver = 100 * info->graphics_ver + info->graphics_rel;
	shdr->max_size = 16 * 4;
	shdr->code = malloc(4 * shdr->max_size);
	igt_assert(shdr->code);
	return shdr;
}

/**
 * gpgpu_shader_destroy:
 * @shdr: pointer to shader struct created with 'gpgpu_shader_create'
 *
 * Frees resources of gpgpu_shader struct.
 */
void gpgpu_shader_destroy(struct gpgpu_shader *shdr)
{
	free(shdr->code);
	free(shdr);
}
