// SPDX-License-Identifier: MIT
// Copyright 2025 Advanced Micro Devices, Inc.

#include "amd_vcn_shared.h"

bool
is_vcn_tests_enable(amdgpu_device_handle device_handle, struct mmd_shared_context *context)
{
	struct drm_amdgpu_info_hw_ip info;
	int r;

	r = amdgpu_query_hw_ip_info(device_handle, AMDGPU_HW_IP_VCN_ENC, 0, &info);

	if (r)
		return false;

	context->vcn_ip_version_major = info.hw_ip_version_major;
	context->vcn_ip_version_minor = info.hw_ip_version_minor;
	context->enc_ring = !!info.available_rings;
	/* in vcn 4.0 it re-uses encoding queue as unified queue */
	if (context->vcn_ip_version_major >= 4) {
		context->vcn_unified_ring = true;
		context->vcn_dec_sw_ring = true;
		context->dec_ring = context->enc_ring;
	} else {
		r = amdgpu_query_hw_ip_info(device_handle, AMDGPU_HW_IP_VCN_DEC, 0, &info);
		context->dec_ring = !!info.available_rings;
	}

	if (!(context->dec_ring || context->enc_ring) ||
		(context->family_id < AMDGPU_FAMILY_RV &&
		(context->family_id == AMDGPU_FAMILY_AI &&
		(context->chip_id - context->chip_rev) < 0x32))) { /* Arcturus */
		igt_info("The ASIC does NOT support VCN, vcn test is disabled\n");
		return false;
	}

	if (context->family_id == AMDGPU_FAMILY_AI)
		context->enc_ring  = false;

	if (!context->dec_ring) {
		igt_info("VCN Tests DEC create disable\n");
		igt_info("VCN Tests DEC decode disable\n");
		igt_info("VCN Tests DEC destroy disable\n");
	}

	if (!context->enc_ring) {
		igt_info("VCN Tests ENC create disable\n");
		igt_info("VCN Tests ENC encode disable\n");
		igt_info("VCN Tests ENC destroy disable\n");
	}

	if (context->vcn_ip_version_major == 1) {
		context->vcn_reg_index = 0;
	} else if (context->vcn_ip_version_major == 2 && context->vcn_ip_version_minor == 0) {
		context->vcn_reg_index = 1;
	} else if ((context->vcn_ip_version_major == 2 && context->vcn_ip_version_minor >= 5) ||
			context->vcn_ip_version_major == 3) {
		context->vcn_reg_index = 2;
	}

	/* Skip VCN tests on Radeon RX 7600  (GFX12, chip_id = 0x51, family_id = 152) asic_id= 7550*/
	if (context->family_id == 152 && context->chip_id == 0x51 && context->asic_id == 0x7550 ) {
		igt_info("Skipping VCN tests on RX 7600 (family_id = 152, chip_id = 0x51, asic_id = 0x7550)\n");
		return false;
	}


	return true;
}

void
amdgpu_cs_sq_head(struct vcn_context *v_context, uint32_t *base, int *offset, bool enc)
{
	/* signature */
	*(base + (*offset)++) = 0x00000010;
	*(base + (*offset)++) = 0x30000002;
	v_context->ib_checksum = base + (*offset)++;
	v_context->ib_size_in_dw = base + (*offset)++;

	/* engine info */
	*(base + (*offset)++) = 0x00000010;
	*(base + (*offset)++) = 0x30000001;
	*(base + (*offset)++) = enc ? 2 : 3;
	*(base + (*offset)++) = 0x00000000;
}

void
amdgpu_cs_sq_ib_tail(struct vcn_context *v_context, uint32_t *end)
{
	uint32_t size_in_dw;
	uint32_t checksum = 0;

	/* if the pointers are invalid, no need to process */
	if (v_context->ib_checksum == NULL || v_context->ib_size_in_dw == NULL)
		return;

	size_in_dw = end - v_context->ib_size_in_dw - 1;
	*v_context->ib_size_in_dw = size_in_dw;
	*(v_context->ib_size_in_dw + 4) = size_in_dw * sizeof(uint32_t);

	for (int i = 0; i < size_in_dw; i++)
		checksum += *(v_context->ib_checksum + 2 + i);

	*v_context->ib_checksum = checksum;

	v_context->ib_checksum = NULL;
	v_context->ib_size_in_dw = NULL;
}

void
vcn_dec_cmd(struct mmd_shared_context *shared_context,
		struct mmd_context *context, struct vcn_context *v_context,
		uint64_t addr, unsigned int cmd, int *idx, enum decoder_error_type err_type)
{
	if (shared_context->vcn_dec_sw_ring == false) {
		context->ib_cpu[(*idx)++] = reg[shared_context->vcn_reg_index].data0;
		context->ib_cpu[(*idx)++] = addr;
		context->ib_cpu[(*idx)++] = reg[shared_context->vcn_reg_index].data1;
		context->ib_cpu[(*idx)++] = addr >> 32;
		context->ib_cpu[(*idx)++] = reg[shared_context->vcn_reg_index].cmd;
		context->ib_cpu[(*idx)++] = cmd << 1;
		return;
	}

	/* Support decode software ring message */
	if (!(*idx)) {
		struct rvcn_decode_ib_package *ib_header;

		if (shared_context->vcn_unified_ring)
			amdgpu_cs_sq_head(v_context, context->ib_cpu, idx, false);

		ib_header = (struct rvcn_decode_ib_package *)&context->ib_cpu[*idx];
		if (err_type == INVALID_DECODER_IB_SIZE)
			ib_header->package_size = 0;
		else
			ib_header->package_size = sizeof(struct rvcn_decode_buffer) +
			sizeof(struct rvcn_decode_ib_package);

		(*idx)++;
		ib_header->package_type = (DECODE_IB_PARAM_DECODE_BUFFER);
		(*idx)++;

		v_context->decode_buffer = (struct rvcn_decode_buffer *)&(context->ib_cpu[*idx]);
		*idx += sizeof(struct rvcn_decode_buffer) / 4;
		memset(v_context->decode_buffer, 0, sizeof(struct rvcn_decode_buffer));
	}

	switch (cmd) {
	case DECODE_CMD_MSG_BUFFER:
		v_context->decode_buffer->valid_buf_flag |= DECODE_CMDBUF_FLAGS_MSG_BUFFER;
		v_context->decode_buffer->msg_buffer_address_hi = (addr >> 32);
		v_context->decode_buffer->msg_buffer_address_lo = (addr);
	break;
	case DECODE_CMD_DPB_BUFFER:
		v_context->decode_buffer->valid_buf_flag |= (DECODE_CMDBUF_FLAGS_DPB_BUFFER);
		v_context->decode_buffer->dpb_buffer_address_hi = (addr >> 32);
		v_context->decode_buffer->dpb_buffer_address_lo = (addr);
	break;
	case DECODE_CMD_DECODING_TARGET_BUFFER:
		v_context->decode_buffer->valid_buf_flag |= (DECODE_CMDBUF_FLAGS_DECODING_TARGET_BUFFER);
		v_context->decode_buffer->target_buffer_address_hi = (addr >> 32);
		v_context->decode_buffer->target_buffer_address_lo = (addr);
	break;
	case DECODE_CMD_FEEDBACK_BUFFER:
		v_context->decode_buffer->valid_buf_flag |= (DECODE_CMDBUF_FLAGS_FEEDBACK_BUFFER);
		v_context->decode_buffer->feedback_buffer_address_hi = (addr >> 32);
		v_context->decode_buffer->feedback_buffer_address_lo = (addr);
	break;
	case DECODE_CMD_PROB_TBL_BUFFER:
		v_context->decode_buffer->valid_buf_flag |= (DECODE_CMDBUF_FLAGS_PROB_TBL_BUFFER);
		v_context->decode_buffer->prob_tbl_buffer_address_hi = (addr >> 32);
		v_context->decode_buffer->prob_tbl_buffer_address_lo = (addr);
	break;
	case DECODE_CMD_SESSION_CONTEXT_BUFFER:
		v_context->decode_buffer->valid_buf_flag |= (DECODE_CMDBUF_FLAGS_SESSION_CONTEXT_BUFFER);
		v_context->decode_buffer->session_contex_buffer_address_hi = (addr >> 32);
		v_context->decode_buffer->session_contex_buffer_address_lo = (addr);
	break;
	case DECODE_CMD_BITSTREAM_BUFFER:
		v_context->decode_buffer->valid_buf_flag |= (DECODE_CMDBUF_FLAGS_BITSTREAM_BUFFER);
		v_context->decode_buffer->bitstream_buffer_address_hi = (addr >> 32);
		v_context->decode_buffer->bitstream_buffer_address_lo = (addr);
	break;
	case DECODE_CMD_IT_SCALING_TABLE_BUFFER:
		v_context->decode_buffer->valid_buf_flag |= (DECODE_CMDBUF_FLAGS_IT_SCALING_BUFFER);
		v_context->decode_buffer->it_sclr_table_buffer_address_hi = (addr >> 32);
		v_context->decode_buffer->it_sclr_table_buffer_address_lo = (addr);
	break;
	case DECODE_CMD_CONTEXT_BUFFER:
		v_context->decode_buffer->valid_buf_flag |= (DECODE_CMDBUF_FLAGS_CONTEXT_BUFFER);
		v_context->decode_buffer->context_buffer_address_hi = (addr >> 32);
		v_context->decode_buffer->context_buffer_address_lo = (addr);
	break;
	default:
		igt_info("Not Supported!\n");
	}
}
