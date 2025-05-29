/* SPDX-License-Identifier: MIT
 * Copyright 2025 Advanced Micro Devices, Inc.
 */

#ifndef AMD_VCN_SHARED_H
#define AMD_VCN_SHARED_H

#include "amd_mmd_shared.h"

#define DECODE_CMD_MSG_BUFFER                              0x00000000
#define DECODE_CMD_DPB_BUFFER                              0x00000001
#define DECODE_CMD_DECODING_TARGET_BUFFER                  0x00000002
#define DECODE_CMD_FEEDBACK_BUFFER                         0x00000003
#define DECODE_CMD_PROB_TBL_BUFFER                         0x00000004
#define DECODE_CMD_SESSION_CONTEXT_BUFFER                  0x00000005
#define DECODE_CMD_BITSTREAM_BUFFER                        0x00000100
#define DECODE_CMD_IT_SCALING_TABLE_BUFFER                 0x00000204
#define DECODE_CMD_CONTEXT_BUFFER                          0x00000206

#define DECODE_IB_PARAM_DECODE_BUFFER                      (0x00000001)

#define DECODE_CMDBUF_FLAGS_MSG_BUFFER                     (0x00000001)
#define DECODE_CMDBUF_FLAGS_DPB_BUFFER                     (0x00000002)
#define DECODE_CMDBUF_FLAGS_BITSTREAM_BUFFER               (0x00000004)
#define DECODE_CMDBUF_FLAGS_DECODING_TARGET_BUFFER         (0x00000008)
#define DECODE_CMDBUF_FLAGS_FEEDBACK_BUFFER                (0x00000010)
#define DECODE_CMDBUF_FLAGS_IT_SCALING_BUFFER              (0x00000200)
#define DECODE_CMDBUF_FLAGS_CONTEXT_BUFFER                 (0x00000800)
#define DECODE_CMDBUF_FLAGS_PROB_TBL_BUFFER                (0x00001000)
#define DECODE_CMDBUF_FLAGS_SESSION_CONTEXT_BUFFER         (0x00100000)

#define H264_NAL_TYPE_NON_IDR_SLICE 1
#define H264_NAL_TYPE_DP_A_SLICE 2
#define H264_NAL_TYPE_DP_B_SLICE 3
#define H264_NAL_TYPE_DP_C_SLICE 0x4
#define H264_NAL_TYPE_IDR_SLICE 0x5
#define H264_NAL_TYPE_SEI 0x6
#define H264_NAL_TYPE_SEQ_PARAM 0x7
#define H264_NAL_TYPE_PIC_PARAM 0x8
#define H264_NAL_TYPE_ACCESS_UNIT 0x9
#define H264_NAL_TYPE_END_OF_SEQ 0xa
#define H264_NAL_TYPE_END_OF_STREAM 0xb
#define H264_NAL_TYPE_FILLER_DATA 0xc
#define H264_NAL_TYPE_SEQ_EXTENSION 0xd

#define H264_START_CODE 0x000001

struct rvcn_decode_buffer {
	unsigned int valid_buf_flag;
	unsigned int msg_buffer_address_hi;
	unsigned int msg_buffer_address_lo;
	unsigned int dpb_buffer_address_hi;
	unsigned int dpb_buffer_address_lo;
	unsigned int target_buffer_address_hi;
	unsigned int target_buffer_address_lo;
	unsigned int session_contex_buffer_address_hi;
	unsigned int session_contex_buffer_address_lo;
	unsigned int bitstream_buffer_address_hi;
	unsigned int bitstream_buffer_address_lo;
	unsigned int context_buffer_address_hi;
	unsigned int context_buffer_address_lo;
	unsigned int feedback_buffer_address_hi;
	unsigned int feedback_buffer_address_lo;
	unsigned int luma_hist_buffer_address_hi;
	unsigned int luma_hist_buffer_address_lo;
	unsigned int prob_tbl_buffer_address_hi;
	unsigned int prob_tbl_buffer_address_lo;
	unsigned int sclr_coeff_buffer_address_hi;
	unsigned int sclr_coeff_buffer_address_lo;
	unsigned int it_sclr_table_buffer_address_hi;
	unsigned int it_sclr_table_buffer_address_lo;
	unsigned int sclr_target_buffer_address_hi;
	unsigned int sclr_target_buffer_address_lo;
	unsigned int cenc_size_info_buffer_address_hi;
	unsigned int cenc_size_info_buffer_address_lo;
	unsigned int mpeg2_pic_param_buffer_address_hi;
	unsigned int mpeg2_pic_param_buffer_address_lo;
	unsigned int mpeg2_mb_control_buffer_address_hi;
	unsigned int mpeg2_mb_control_buffer_address_lo;
	unsigned int mpeg2_idct_coeff_buffer_address_hi;
	unsigned int mpeg2_idct_coeff_buffer_address_lo;
};

struct rvcn_decode_ib_package {
	unsigned int package_size;
	unsigned int package_type;
};

struct amdgpu_vcn_reg {
	uint32_t data0;
	uint32_t data1;
	uint32_t cmd;
	uint32_t nop;
	uint32_t cntl;
};

struct buffer_info {
	uint32_t num_bits_in_buffer;
	const uint8_t *dec_buffer;
	uint8_t dec_data;
	uint32_t dec_buffer_size;
	const uint8_t *end;
};

struct h264_decode {
	uint8_t profile;
	uint8_t level_idc;
	uint8_t nal_ref_idc;
	uint8_t nal_unit_type;
	uint32_t pic_width, pic_height;
	uint32_t slice_type;
};

struct vcn_context {
	struct amdgpu_mmd_bo enc_buf;
	struct amdgpu_mmd_bo cpb_buf;
	struct amdgpu_mmd_bo session_ctx_buf;
	uint32_t enc_task_id;
	uint32_t *ib_checksum;
	uint32_t *ib_size_in_dw;
	uint32_t gWidth, gHeight, gSliceType;
	struct rvcn_decode_buffer *decode_buffer;
};

struct amdgpu_vcn_reg reg[] = {
	{0x81c4, 0x81c5, 0x81c3, 0x81ff, 0x81c6},
	{0x504, 0x505, 0x503, 0x53f, 0x506},
	{0x10, 0x11, 0xf, 0x29, 0x26d},
};

bool
is_vcn_tests_enable(amdgpu_device_handle device_handle, struct mmd_shared_context *context);

void
amdgpu_cs_sq_head(struct vcn_context *v_context, uint32_t *base, int *offset, bool enc);

void
amdgpu_cs_sq_ib_tail(struct vcn_context *v_context, uint32_t *end);

void
vcn_dec_cmd(struct mmd_shared_context *shared_context,
		struct mmd_context *context, struct vcn_context *v_context,
		uint64_t addr, unsigned int cmd, int *idx, enum decoder_error_type err_type);
#endif //AMD_VCN_SHARED_H