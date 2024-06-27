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
};

struct iga64_template {
	uint32_t gen_ver;
	uint32_t size;
	const uint32_t *code;
};

#pragma GCC diagnostic ignored "-Wnested-externs"

void
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

void gpgpu_shader__eot(struct gpgpu_shader *shdr);
void gpgpu_shader__write_dword(struct gpgpu_shader *shdr, uint32_t value,
			       uint32_t y_offset);

#endif /* GPGPU_SHADER_H */
