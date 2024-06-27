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

struct gpgpu_shader *gpgpu_shader_create(int fd);
void gpgpu_shader_destroy(struct gpgpu_shader *shdr);

void gpgpu_shader_dump(struct gpgpu_shader *shdr);

void gpgpu_shader_exec(struct intel_bb *ibb,
		       struct intel_buf *target,
		       unsigned int x_dim, unsigned int y_dim,
		       struct gpgpu_shader *shdr,
		       struct gpgpu_shader *sip,
		       uint64_t ring, bool explicit_engine);

#endif /* GPGPU_SHADER_H */
