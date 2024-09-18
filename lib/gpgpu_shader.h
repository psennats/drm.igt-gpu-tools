/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef GPGPU_SHADER_H
#define GPGPU_SHADER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

struct intel_bb;
struct intel_buf;

struct gpgpu_shader {
	uint32_t gen_ver;
	uint32_t size;
	uint32_t max_size;
	union {
		uint32_t *code;
		uint32_t (*instr)[4];
	};
	struct igt_map *labels;
	bool illegal_opcode_exception_enable;
};

struct iga64_template {
	uint32_t gen_ver;
	uint32_t size;
	const uint32_t *code;
};

#pragma GCC diagnostic ignored "-Wnested-externs"

uint32_t
__emit_iga64_code(struct gpgpu_shader *shdr, const struct iga64_template *tpls,
		  int argc, uint32_t *argv);

#define emit_iga64_code(__shdr, __name, __txt, __args...) \
({ \
	static const char t[] __attribute__ ((section(".iga64_assembly"), used)) =\
		"iga64_assembly_" #__name ":" __txt "\n"; \
	extern struct iga64_template const iga64_code_ ## __name[]; \
	u32 args[] = { __args }; \
	__emit_iga64_code(__shdr, iga64_code_ ## __name, ARRAY_SIZE(args), args); \
})

struct gpgpu_shader *gpgpu_shader_create(int fd);
void gpgpu_shader_destroy(struct gpgpu_shader *shdr);

void gpgpu_shader_dump(struct gpgpu_shader *shdr);

void gpgpu_shader_exec(struct intel_bb *ibb,
		       struct intel_buf *target,
		       unsigned int x_dim, unsigned int y_dim,
		       struct gpgpu_shader *shdr,
		       struct gpgpu_shader *sip,
		       uint64_t ring, bool explicit_engine);

static inline uint32_t gpgpu_shader_last_instr(struct gpgpu_shader *shdr)
{
	return shdr->size / 4 - 1;
}

void gpgpu_shader__wait(struct gpgpu_shader *shdr);
void gpgpu_shader__breakpoint_on(struct gpgpu_shader *shdr, uint32_t cmd_no);
void gpgpu_shader__breakpoint(struct gpgpu_shader *shdr);
void gpgpu_shader__nop(struct gpgpu_shader *shdr);
void gpgpu_shader__eot(struct gpgpu_shader *shdr);
void gpgpu_shader__common_target_write(struct gpgpu_shader *shdr,
				       uint32_t y_offset, const uint32_t value[4]);
void gpgpu_shader__common_target_write_u32(struct gpgpu_shader *shdr,
				     uint32_t y_offset, uint32_t value);
void gpgpu_shader__clear_exception(struct gpgpu_shader *shdr, uint32_t value);
void gpgpu_shader__set_exception(struct gpgpu_shader *shdr, uint32_t value);
void gpgpu_shader__end_system_routine(struct gpgpu_shader *shdr,
				      bool breakpoint_suppress);
void gpgpu_shader__end_system_routine_step_if_eq(struct gpgpu_shader *shdr,
						 uint32_t dw_offset,
						 uint32_t value);
void gpgpu_shader__write_aip(struct gpgpu_shader *shdr, uint32_t y_offset);
void gpgpu_shader__write_dword(struct gpgpu_shader *shdr, uint32_t value,
			       uint32_t y_offset);
void gpgpu_shader__write_on_exception(struct gpgpu_shader *shdr, uint32_t dw,
			       uint32_t y_offset, uint32_t mask, uint32_t value);
void gpgpu_shader__label(struct gpgpu_shader *shdr, int label_id);
void gpgpu_shader__jump(struct gpgpu_shader *shdr, int label_id);
void gpgpu_shader__jump_neq(struct gpgpu_shader *shdr, int label_id,
			    uint32_t dw_offset, uint32_t value);
void gpgpu_shader__loop_begin(struct gpgpu_shader *shdr, int label_id);
void gpgpu_shader__loop_end(struct gpgpu_shader *shdr, int label_id, uint32_t iter);
#endif /* GPGPU_SHADER_H */
