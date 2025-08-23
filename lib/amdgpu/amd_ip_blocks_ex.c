// SPDX-License-Identifier: MIT
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 */

#include <stdint.h>
#include "amd_ip_blocks.h"
#include "amd_PM4.h"
#include "amdgpu_asic_addr.h"

static void gfx_program_compute_default(
	const struct amdgpu_ip_funcs *f,
	struct amdgpu_cmd_base *base,
	uint64_t code_addr,
	uint64_t user_data0_addr,
	uint32_t rsrc1_dw,
	uint32_t rsrc2_or_tmp,
	uint32_t thr_x, uint32_t thr_y, uint32_t thr_z)
{
	base->emit(base, PACKET3(PKT3_CONTEXT_CONTROL, 1));
	base->emit(base, 0x80000000);
	base->emit(base, 0x80000000);

	base->emit(base, PACKET3(PKT3_CLEAR_STATE, 0));
	base->emit(base, 0x80000000);

	base->emit(base, PACKET3(PKT3_SET_SH_REG, 2));
	base->emit(base, f->get_reg_offset(COMPUTE_PGM_LO));
	base->emit(base, (uint32_t)(code_addr >> 8));
	base->emit(base, (uint32_t)(code_addr >> 40));

	base->emit(base, PACKET3(PKT3_SET_SH_REG, 2));
	base->emit(base, f->get_reg_offset(COMPUTE_PGM_RSRC1));
	base->emit(base, rsrc1_dw);
	base->emit(base, rsrc2_or_tmp);

	base->emit(base, PACKET3(PKT3_SET_SH_REG, 1));
	base->emit(base, f->get_reg_offset(COMPUTE_TMPRING_SIZE));
	base->emit(base, 0x00000100);

	base->emit(base, PACKET3(PKT3_SET_SH_REG, 2));
	base->emit(base, f->get_reg_offset(COMPUTE_USER_DATA_0));
	base->emit(base, (uint32_t)user_data0_addr);
	base->emit(base, (uint32_t)(user_data0_addr >> 32));

	base->emit(base, PACKET3(PKT3_SET_SH_REG, 1));
	base->emit(base, f->get_reg_offset(COMPUTE_RESOURCE_LIMITS));
	base->emit(base, 0);

	base->emit(base, PACKET3(PKT3_SET_SH_REG, 3));
	base->emit(base, f->get_reg_offset(COMPUTE_NUM_THREAD_X));
	base->emit(base, thr_x);
	base->emit(base, thr_y);
	base->emit(base, thr_z);
}

static void gfx_dispatch_direct_default(
	const struct amdgpu_ip_funcs *f,
	struct amdgpu_cmd_base *base,
	uint32_t gx, uint32_t gy, uint32_t gz,
	uint32_t flags)
{
	base->emit(base, PACKET3(PACKET3_DISPATCH_DIRECT, 3));
	base->emit(base, gx);
	base->emit(base, gy);
	base->emit(base, gz);
	base->emit(base, flags);
	base->emit_aligned(base, 7, GFX_COMPUTE_NOP);
}

static void gfx_write_confirm_default(
	const struct amdgpu_ip_funcs *f,
	struct amdgpu_cmd_base *base,
	uint64_t dst, uint32_t val)
{
	base->emit(base, PACKET3(PACKET3_WRITE_DATA, 3));
	base->emit(base, WRITE_DATA_DST_SEL(5) | WR_CONFIRM);
	base->emit(base, (uint32_t)dst);
	base->emit(base, (uint32_t)(dst >> 32));
	base->emit(base, val);
	base->emit_aligned(base, 7, GFX_COMPUTE_NOP);
}

static void gfx_dispatch_direct_gfx9(
	const struct amdgpu_ip_funcs *f,
	struct amdgpu_cmd_base *base,
	uint32_t gx, uint32_t gy, uint32_t gz,
	uint32_t flags_unused)
{
	gfx_dispatch_direct_default(f, base, gx, gy, gz, 0x00000000);
}

static void gfx_dispatch_direct_gfx10(
	const struct amdgpu_ip_funcs *f,
	struct amdgpu_cmd_base *base,
	uint32_t gx, uint32_t gy, uint32_t gz,
	uint32_t flags_unused)
{
	gfx_dispatch_direct_default(f, base, gx, gy, gz, 0x00000045);
}

static void gfx_program_compute_gfx11(
	const struct amdgpu_ip_funcs *f,
	struct amdgpu_cmd_base *base,
	uint64_t code, uint64_t udata0,
	uint32_t rsrc1, uint32_t rsrc2,
	uint32_t tx, uint32_t ty, uint32_t tz)
{
	base->emit(base, PACKET3(PKT3_CONTEXT_CONTROL, 1));
	base->emit(base, 0x80000000);
	base->emit(base, 0x80000000);

	base->emit(base, PACKET3(PKT3_CLEAR_STATE, 0));
	base->emit(base, 0x80000000);

	base->emit(base, PACKET3(PKT3_SET_SH_REG, 2));
	base->emit(base, f->get_reg_offset(COMPUTE_PGM_LO));
	base->emit(base, (uint32_t)(code >> 8));
	base->emit(base, (uint32_t)(code >> 40));

	base->emit(base, PACKET3(PKT3_SET_SH_REG, 2));
	base->emit(base, f->get_reg_offset(COMPUTE_PGM_RSRC1));
	base->emit(base, rsrc1);
	base->emit(base, rsrc2);

	base->emit(base, PACKET3(PKT3_SET_SH_REG, 2));
	base->emit(base, f->get_reg_offset(COMPUTE_USER_DATA_0));
	base->emit(base, (uint32_t)udata0);
	base->emit(base, (uint32_t)(udata0 >> 32));

	base->emit(base, PACKET3(PKT3_SET_SH_REG, 1));
	base->emit(base, f->get_reg_offset(COMPUTE_RESOURCE_LIMITS));
	base->emit(base, 0);

	base->emit(base, PACKET3(PKT3_SET_SH_REG, 3));
	base->emit(base, f->get_reg_offset(COMPUTE_NUM_THREAD_X));
	base->emit(base, tx);
	base->emit(base, ty);
	base->emit(base, tz);
}

static void gfx_program_compute_gfx12(
	const struct amdgpu_ip_funcs *f,
	struct amdgpu_cmd_base *base,
	uint64_t code, uint64_t udata0,
	uint32_t rsrc1, uint32_t rsrc2,
	uint32_t tx, uint32_t ty, uint32_t tz)
{
	base->emit(base, PACKET3(PKT3_CONTEXT_CONTROL, 1));
	base->emit(base, 0x80000000);
	base->emit(base, 0x80000000);

	base->emit(base, PACKET3(PKT3_SET_SH_REG, 2));
	base->emit(base, f->get_reg_offset(COMPUTE_PGM_LO));
	base->emit(base, (uint32_t)(code >> 8));
	base->emit(base, (uint32_t)(code >> 40));

	base->emit(base, PACKET3(PKT3_SET_SH_REG, 2));
	base->emit(base, f->get_reg_offset(COMPUTE_PGM_RSRC1));
	base->emit(base, rsrc1);
	base->emit(base, rsrc2);

	base->emit(base, PACKET3(PKT3_SET_SH_REG, 2));
	base->emit(base, f->get_reg_offset(COMPUTE_USER_DATA_0));
	base->emit(base, (uint32_t)udata0);
	base->emit(base, (uint32_t)(udata0 >> 32));

	base->emit(base, PACKET3(PKT3_SET_SH_REG, 1));
	base->emit(base, f->get_reg_offset(COMPUTE_RESOURCE_LIMITS));
	base->emit(base, 0);

	base->emit(base, PACKET3(PKT3_SET_SH_REG, 3));
	base->emit(base, f->get_reg_offset(COMPUTE_NUM_THREAD_X));
	base->emit(base, tx);
	base->emit(base, ty);
	base->emit(base, tz);
}


static void gfx_dispatch_direct_gfx11(
	const struct amdgpu_ip_funcs *f,
	struct amdgpu_cmd_base *base,
	uint32_t gx, uint32_t gy, uint32_t gz,
	uint32_t flags_unused)
{
	gfx_dispatch_direct_default(f, base, gx, gy, gz, 0x00000045);
}

void amd_ip_blocks_ex_init(struct amdgpu_ip_funcs *funcs)
{
	funcs->gfx_program_compute = gfx_program_compute_default;
	funcs->gfx_dispatch_direct = gfx_dispatch_direct_default;
	funcs->gfx_write_confirm = gfx_write_confirm_default;

	switch (funcs->family_id) {
	case AMDGPU_FAMILY_RV:
	case AMDGPU_FAMILY_NV:
	case AMDGPU_FAMILY_VGH:
		funcs->gfx_dispatch_direct = gfx_dispatch_direct_gfx9;
		break;
	case AMDGPU_FAMILY_YC:
	case AMDGPU_FAMILY_GC_10_3_6:
	case AMDGPU_FAMILY_GC_10_3_7:
		funcs->gfx_dispatch_direct = gfx_dispatch_direct_gfx10;
		break;
	case AMDGPU_FAMILY_GC_11_0_0:
	case AMDGPU_FAMILY_GC_11_0_1:
	case AMDGPU_FAMILY_GC_11_5_0:
		funcs->gfx_program_compute = gfx_program_compute_gfx11;
		funcs->gfx_dispatch_direct = gfx_dispatch_direct_gfx11;
		break;
	case AMDGPU_FAMILY_GC_12_0_0:
		funcs->gfx_program_compute =gfx_program_compute_gfx12;
		funcs->gfx_dispatch_direct = gfx_dispatch_direct_gfx11;
		break;
	default:
		break;
	}
}

