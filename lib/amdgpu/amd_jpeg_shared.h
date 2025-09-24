/* SPDX-License-Identifier: MIT
 * Copyright 2025 Advanced Micro Devices, Inc.
 */

#ifndef AMD_JPEG_SHARED_H
#define AMD_JPEG_SHARED_H

#include "amd_mmd_shared.h"

/* jpeg registers */
#define mmUVD_JPEG_CNTL				0x0200
#define mmUVD_JPEG_RB_BASE			0x0201
#define mmUVD_JPEG_RB_WPTR			0x0202
#define mmUVD_JPEG_RB_RPTR			0x0203
#define mmUVD_JPEG_RB_SIZE			0x0204
#define mmUVD_JPEG_TIER_CNTL2			0x021a
#define mmUVD_JPEG_UV_TILING_CTRL		0x021c
#define mmUVD_JPEG_TILING_CTRL			0x021e
#define mmUVD_JPEG_OUTBUF_RPTR			0x0220
#define mmUVD_JPEG_OUTBUF_WPTR			0x0221
#define mmUVD_JPEG_PITCH			0x0222
#define mmUVD_JPEG_INT_EN			0x0229
#define mmUVD_JPEG_UV_PITCH			0x022b
#define mmUVD_JPEG_INDEX			0x023e
#define mmUVD_JPEG_DATA				0x023f
#define mmUVD_LMI_JPEG_WRITE_64BIT_BAR_HIGH	0x0438
#define mmUVD_LMI_JPEG_WRITE_64BIT_BAR_LOW	0x0439
#define mmUVD_LMI_JPEG_READ_64BIT_BAR_HIGH	0x045a
#define mmUVD_LMI_JPEG_READ_64BIT_BAR_LOW	0x045b
#define mmUVD_CTX_INDEX				0x0528
#define mmUVD_CTX_DATA				0x0529
#define mmUVD_SOFT_RESET			0x05a0

#define vcnipUVD_JPEG_DEC_SOFT_RST		0x402f
#define vcnipUVD_JRBC_IB_COND_RD_TIMER		0x408e
#define vcnipUVD_JRBC_IB_REF_DATA		0x408f
#define vcnipUVD_LMI_JPEG_READ_64BIT_BAR_HIGH	0x40e1
#define vcnipUVD_LMI_JPEG_READ_64BIT_BAR_LOW	0x40e0
#define vcnipUVD_JPEG_RB_BASE			0x4001
#define vcnipUVD_JPEG_RB_SIZE			0x4004
#define vcnipUVD_JPEG_RB_WPTR			0x4002
#define vcnipUVD_JPEG_PITCH			0x401f
#define vcnipUVD_JPEG_UV_PITCH			0x4020
#define vcnipJPEG_DEC_ADDR_MODE			0x4027
#define vcnipJPEG_DEC_Y_GFX10_TILING_SURFACE	0x4024
#define vcnipJPEG_DEC_UV_GFX10_TILING_SURFACE	0x4025
#define vcnipUVD_LMI_JPEG_WRITE_64BIT_BAR_HIGH	0x40e3
#define vcnipUVD_LMI_JPEG_WRITE_64BIT_BAR_LOW	0x40e2
#define vcnipUVD_JPEG_INDEX			0x402c
#define vcnipUVD_JPEG_DATA			0x402d
#define vcnipUVD_JPEG_TIER_CNTL2		0x400f
#define vcnipUVD_JPEG_OUTBUF_RPTR		0x401e
#define vcnipUVD_JPEG_OUTBUF_CNTL		0x401c
#define vcnipUVD_JPEG_INT_EN			0x400a
#define vcnipUVD_JPEG_CNTL			0x4000
#define vcnipUVD_JPEG_RB_RPTR			0x4003
#define vcnipUVD_JPEG_OUTBUF_WPTR		0x401d

#define vcnipUVD_JPEG_DEC_SOFT_RST_1             0x4051
#define vcnipUVD_JPEG_PITCH_1                    0x4043
#define vcnipUVD_JPEG_UV_PITCH_1                 0x4044
#define vcnipJPEG_DEC_ADDR_MODE_1                0x404B
#define vcnipUVD_JPEG_TIER_CNTL2_1               0x400E
#define vcnipUVD_JPEG_OUTBUF_CNTL_1              0x4040
#define vcnipUVD_JPEG_OUTBUF_WPTR_1              0x4041
#define vcnipUVD_JPEG_OUTBUF_RPTR_1              0x4042
#define vcnipUVD_JPEG_LUMA_BASE0_0               0x41C0
#define vcnipUVD_JPEG_CHROMA_BASE0_0             0x41C1
#define vcnipJPEG_DEC_Y_GFX10_TILING_SURFACE_1   0x4048
#define vcnipJPEG_DEC_UV_GFX10_TILING_SURFACE_1  0x4049
#define vcnipUVD_LMI_JPEG_WRITE_64BIT_BAR_HIGH_1 0x40B5
#define vcnipUVD_LMI_JPEG_WRITE_64BIT_BAR_LOW_1  0x40B4
#define vcnipUVD_LMI_JPEG_READ_64BIT_BAR_HIGH_1  0x40B3
#define vcnipUVD_LMI_JPEG_READ_64BIT_BAR_LOW_1   0x40B2

uint32_t jpeg_dec_soft_rst;
uint32_t jrbc_ib_cond_rd_timer;
uint32_t jrbc_ib_ref_data;
uint32_t lmi_jpeg_read_64bit_bar_high;
uint32_t lmi_jpeg_read_64bit_bar_low;
uint32_t jpeg_rb_base;
uint32_t jpeg_rb_size;
uint32_t jpeg_rb_wptr;
uint32_t jpeg_pitch;
uint32_t jpeg_uv_pitch;
uint32_t dec_addr_mode;
uint32_t dec_y_gfx10_tiling_surface;
uint32_t dec_uv_gfx10_tiling_surface;
uint32_t lmi_jpeg_write_64bit_bar_high;
uint32_t lmi_jpeg_write_64bit_bar_low;
uint32_t jpeg_tier_cntl2;
uint32_t jpeg_outbuf_rptr;
uint32_t jpeg_outbuf_cntl;
uint32_t jpeg_int_en;
uint32_t jpeg_cntl;
uint32_t jpeg_rb_rptr;
uint32_t jpeg_outbuf_wptr;
uint32_t jpeg_luma_base0_0;
uint32_t jpeg_chroma_base0_0;

#define RDECODE_PKT_REG_J(x)		((unsigned int)(x)&0x3FFFF)
#define RDECODE_PKT_RES_J(x)		(((unsigned int)(x)&0x3F) << 18)
#define RDECODE_PKT_COND_J(x)		(((unsigned int)(x)&0xF) << 24)
#define RDECODE_PKT_TYPE_J(x)		(((unsigned int)(x)&0xF) << 28)
#define RDECODE_PKTJ(reg, cond, type)	(RDECODE_PKT_REG_J(reg) | \
					 RDECODE_PKT_RES_J(0) | \
					 RDECODE_PKT_COND_J(cond) | \
					 RDECODE_PKT_TYPE_J(type))

#define UVD_BASE_INST0_SEG1		0x00007E00
#define SOC15_REG_ADDR(reg)		(UVD_BASE_INST0_SEG1 + reg)

#define COND0				0
#define COND1				1
#define COND3				3
#define TYPE0				0
#define TYPE1				1
#define TYPE3				3
#define JPEG_DEC_DT_PITCH       0x100
#define WIDTH                   64
#define JPEG_DEC_BSD_SIZE       0x200
#define JPEG_DEC_LUMA_OFFSET    0
#define JPEG_DEC_CHROMA_OFFSET  0x4000
#define JPEG_DEC_SUM            262144
#define MAX_RESOURCES           16

bool
is_jpeg_tests_enable(amdgpu_device_handle device_handle,
		struct mmd_shared_context *context);

void
set_reg_jpeg(struct mmd_context *context, uint32_t reg, uint32_t cond,
		uint32_t type, uint32_t val, uint32_t *idx);

/* send a bitstream buffer command */
void
send_cmd_bitstream_direct(struct mmd_context *context, uint64_t addr,
		uint32_t *idx);

/* send a target buffer command */
void
send_cmd_target_direct(struct mmd_context *context, uint64_t addr,
		uint32_t *idx);
#endif // AMD_JPEG_SHARED_H