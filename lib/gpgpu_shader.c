// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 *
 * Author: Dominik Grzegorzek <dominik.grzegorzek@intel.com>
 */

#include <i915_drm.h>

#include "igt_map.h"
#include "ioctl_wrappers.h"
#include "gpgpu_shader.h"
#include "gpu_cmds.h"

struct label_entry {
	uint32_t id;
	uint32_t offset;
};

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

uint32_t
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

	return tpls->size;
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
	struct gen8_interface_descriptor_data *idd;
	uint32_t interface_descriptor, sip_offset;
	uint64_t engine;

	intel_bb_add_intel_buf(ibb, target, true);

	intel_bb_ptr_set(ibb, BATCH_STATE_SPLIT);

	interface_descriptor = gen8_fill_interface_descriptor(ibb, target,
							      shdr->instr,
							      4 * shdr->size);
	idd = intel_bb_ptr_get(ibb, interface_descriptor);
	idd->desc2.illegal_opcode_exception_enable = shdr->illegal_opcode_exception_enable;

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
	idd.desc2.illegal_opcode_exception_enable = shdr->illegal_opcode_exception_enable;

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
	shdr->labels = igt_map_create(igt_map_hash_32, igt_map_equal_32);
	igt_assert(shdr->code);
	return shdr;
}

static void free_func(struct igt_map_entry *entry)
{
       free(entry->data);
}

/**
 * gpgpu_shader_destroy:
 * @shdr: pointer to shader struct created with 'gpgpu_shader_create'
 *
 * Frees resources of gpgpu_shader struct.
 */
void gpgpu_shader_destroy(struct gpgpu_shader *shdr)
{
	igt_map_destroy(shdr->labels, free_func);
	free(shdr->code);
	free(shdr);
}

/**
 * gpgpu_shader_dump:
 * @shdr: shader to be printed
 *
 * Print shader instructions from @shdr in hex.
 */
void gpgpu_shader_dump(struct gpgpu_shader *shdr)
{
	for (int i = 0; i < shdr->size / 4; i++)
		igt_info("0x%08x 0x%08x 0x%08x 0x%08x\n",
			 shdr->instr[i][0], shdr->instr[i][1],
			 shdr->instr[i][2], shdr->instr[i][3]);
}

/**
 * gpgpu_shader__breakpoint_on:
 * @shdr: shader to create breakpoint in
 * @cmd_no: index of the instruction to break on
 *
 * Insert a breakpoint on the @cmd_no'th instruction within @shdr.
 */
void gpgpu_shader__breakpoint_on(struct gpgpu_shader *shdr, uint32_t cmd_no)
{
	igt_assert(cmd_no < shdr->size / 4);
	shdr->instr[cmd_no][0] |= 1<<30;
}

/**
 * gpgpu_shader__breakpoint:
 * @shdr: shader to create breakpoint in
 *
 * Insert a breakpoint on the last instruction in @shdr.
 */
void gpgpu_shader__breakpoint(struct gpgpu_shader *shdr)
{
	gpgpu_shader__breakpoint_on(shdr, gpgpu_shader_last_instr(shdr));
}

/**
 * gpgpu_shader__wait:
 * @shdr: shader to be modified
 *
 * Append wait instruction to @shader. This instruction raises attention
 * and stops execution.
 */
void gpgpu_shader__wait(struct gpgpu_shader *shdr)
{
	emit_iga64_code(shdr, sync_host, "	\n\
(W)	sync.host	        null		\n\
	");
}

/**
 * gpgpu_shader__nop:
 * @shdr: shader to be modified
 *
 * Append a no-op instruction to @shdr.
 */
void gpgpu_shader__nop(struct gpgpu_shader *shdr)
{
	emit_iga64_code(shdr, nop, "	\n\
(W)	nop				\n\
	");
}

/**
 * gpgpu_shader__eot:
 * @shdr: shader to be modified
 *
 * Append end of thread instruction to @shdr.
 */
void gpgpu_shader__eot(struct gpgpu_shader *shdr)
{
	emit_iga64_code(shdr, eot, "						\n\
(W)	mov (8|M0)               r112.0<1>:ud  r0.0<8;8,1>:ud			\n\
#if GEN_VER < 1250								\n\
(W)	send.ts (16|M0)          null r112 null 0x10000000 0x02000010 {EOT,@1}	\n\
#else										\n\
(W)	send.gtwy (8|M0)         null r112 src1_null     0 0x02000000 {EOT}	\n\
#endif										\n\
	");
}

/**
 * gpgpu_shader__label:
 * @shdr: shader to be modified
 * @label_id: id of the label to be created
 *
 * Create a label for the last instruction within @shdr.
 */
void gpgpu_shader__label(struct gpgpu_shader *shdr, int label_id)
{
	struct label_entry *l = malloc(sizeof(*l));

	l->id = label_id;
	l->offset = shdr->size;
	igt_map_insert(shdr->labels, &l->id, l);
}

#define OPCODE(x) (x & 0x7f)
#define OPCODE_JUMP_INDEXED 0x20
static void __patch_indexed_jump(struct gpgpu_shader *shdr, int label_id,
				 uint32_t jump_iga64_size)
{
	struct label_entry *l;
	uint32_t *start, *end, *label;
	int32_t relative;

	l = igt_map_search(shdr->labels, &label_id);
	igt_assert(l);

	igt_assert(jump_iga64_size % 4 == 0);

	label = shdr->code + l->offset;
	end = shdr->code + shdr->size;
	start = end - jump_iga64_size;

	for (; start < end; start += 4)
		if (OPCODE(*start) == OPCODE_JUMP_INDEXED) {
			relative = (label - start) * 4;
			*(start + 3) = relative;
			break;
		}
}

/**
 * gpgpu_shader__jump:
 * @shdr: shader to be modified
 * @label_id: label to jump to
 *
 * Append jump instruction to @shdr. Jump to instruction with label @label_id.
 */
void gpgpu_shader__jump(struct gpgpu_shader *shdr, int label_id)
{
	size_t shader_size;

	shader_size = emit_iga64_code(shdr, jump, "	\n\
L0:							\n\
(W)	jmpi        L0					\n\
	");

	__patch_indexed_jump(shdr, label_id, shader_size);
}

/**
 * gpgpu_shader__jump_neq:
 * @shdr: shader to be modified
 * @label_id: label to jump to
 * @y_offset: offset within target buffer in rows
 * @value: expected value
 *
 * Append jump instruction to @shdr. Jump to instruction with label @label_id
 * when @value is not equal to dword stored at @y_offset within the surface.
 */
void gpgpu_shader__jump_neq(struct gpgpu_shader *shdr, int label_id,
			    uint32_t y_offset, uint32_t value)
{
	uint32_t size;

	size = emit_iga64_code(shdr, jump_dw_neq, "					\n\
L0:											\n\
(W)		mov (16|M0)              r30.0<1>:ud    0x0:ud				\n\
#if GEN_VER < 2000 // Media Block Write							\n\
	// Y offset of the block in rows := thread group id Y				\n\
(W)		mov (1|M0)               r30.1<1>:ud    ARG(0):ud			\n\
	// block width [0,63] representing 1 to 64 bytes, we want dword			\n\
(W)		mov (1|M0)               r30.2<1>:ud    0x3:ud				\n\
	// FFTID := FFTID from R0 header						\n\
(W)		mov (1|M0)               r30.4<1>:ud    r0.5<0;1,0>:ud  		\n\
(W)		send.dc1 (16|M0)         r31     r30      null    0x0	0x2190000	\n\
#else // Typed 2D Block Store								\n\
	// Store X and Y block start (160:191 and 192:223)				\n\
(W)            mov (1|M0)               r30.6<1>:ud    ARG(0):ud			\n\
	// Store X and Y block size (224:231 and 232:239)				\n\
(W)            mov (1|M0)               r30.7<1>:ud    0x3:ud				\n\
(W)            send.tgm (16|M0)         r31     r30    null:0    0x0    0x62100003	\n\
#endif											\n\
	// clear the flag register							\n\
(W)		mov (1|M0)               f0.0<1>:ud    0x0:ud				\n\
(W)		cmp (1|M0)    (ne)f0.0   null<1>:ud     r31.0<0;1,0>:ud   ARG(1):ud	\n\
(W&f0.0)	jmpi                     L0						\n\
	", y_offset, value);

	__patch_indexed_jump(shdr, label_id, size);
}

/**
 * gpgpu_shader__loop_begin:
 * @shdr: shader to be modified
 * @label_id: id of the label to be created
 *
 * Begin a counting loop in @shdr. All subsequent instructions will constitute
 * the loop body up until 'gpgpu_shader__loop_end' gets called. The first
 * instruction of the loop will be at label @label_id. The r40 register will be
 * overwritten as it is used as the loop counter.
 */
void gpgpu_shader__loop_begin(struct gpgpu_shader *shdr, int label_id)
{
	emit_iga64_code(shdr, clear_r40, "		\n\
L0:							\n\
(W)	mov (1|M0)               r40:ud    0x0:ud	\n\
	");

	gpgpu_shader__label(shdr, label_id);
}

/**
 * gpgpu_shader__loop_end:
 * @shdr: shader to be modified
 * @label_id: label id passed to 'gpgpu_shader__loop_begin'
 * @iter: iteration count
 *
 * End loop body in @shdr.
 */
void gpgpu_shader__loop_end(struct gpgpu_shader *shdr, int label_id, uint32_t iter)
{
	uint32_t size;

	size = emit_iga64_code(shdr, inc_r40_jump_neq, "				\n\
L0:											\n\
(W)		add (1|M0)              r40:ud          r40.0<0;1,0>:ud 0x1:ud		\n\
(W)		mov (1|M0)              f0.0<1>:ud      0x0:ud				\n\
(W)		cmp (1|M0)    (ne)f0.0   null<1>:ud     r40.0<0;1,0>:ud   ARG(0):ud	\n\
(W&f0.0)	jmpi                     L0						\n\
	", iter);

	__patch_indexed_jump(shdr, label_id, size);
}

/**
 * gpgpu_shader__common_target_write:
 * @shdr: shader to be modified
 * @y_offset: write target offset within target buffer in rows
 * @value: oword to be written
 *
 * Write the oword stored in @value to the target buffer at @y_offset.
 */
void gpgpu_shader__common_target_write(struct gpgpu_shader *shdr,
				       uint32_t y_offset, const uint32_t value[4])
{
	emit_iga64_code(shdr, common_target_write, "				\n\
(W)	mov (16|M0)		r30.0<1>:ud	0x0:ud				\n\
(W)	mov (16|M0)		r31.0<1>:ud	0x0:ud				\n\
(W)	mov (1|M0)		r31.0<1>:ud	ARG(1):ud			\n\
(W)	mov (1|M0)		r31.1<1>:ud	ARG(2):ud			\n\
(W)	mov (1|M0)		r31.2<1>:ud	ARG(3):ud			\n\
(W)	mov (1|M0)		r31.3<1>:ud	ARG(4):ud			\n\
#if GEN_VER < 2000 // Media Block Write						\n\
	// Y offset of the block in rows					\n\
(W)	mov (1|M0)		r30.1<1>:ud	ARG(0):ud			\n\
	// block width [0,63] representing 1 to 64 bytes			\n\
(W)	mov (1|M0)		r30.2<1>:ud	0xf:ud				\n\
	// FFTID := FFTID from R0 header					\n\
(W)	mov (1|M0)		r30.4<1>:ud	r0.5<0;1,0>:ud			\n\
	// written value							\n\
(W)	send.dc1 (16|M0)	null	r30	src1_null  0x0	0x40A8000	\n\
#else	// Typed 2D Block Store							\n\
	// Store X and Y block start (160:191 and 192:223)			\n\
(W)	mov (1|M0)              r30.6<1>:ud     ARG(0):ud			\n\
	// Store X and Y block size (224:231 and 232:239)			\n\
(W)	mov (1|M0)              r30.7<1>:ud     0xf:ud				\n\
(W)	send.tgm (16|M0)        null    r30     null:0  0x0     0x64000007	\n\
#endif										\n\
	", y_offset, value[0], value[1], value[2], value[3]);
}

/**
 * gpgpu_shader__common_target_write_u32:
 * @shdr: shader to be modified
 * @y_offset: write target offset within target buffer in rows
 * @value: dword to be written
 *
 * Fill oword at @y_offset with dword stored in @value.
 */
void gpgpu_shader__common_target_write_u32(struct gpgpu_shader *shdr,
					   uint32_t y_offset, uint32_t value)
{
	const uint32_t owblock[4] = {
		value, value, value, value
	};
	gpgpu_shader__common_target_write(shdr, y_offset, owblock);
}

/**
 * gpgpu_shader__write_aip:
 * @shdr: shader to be modified
 * @y_offset: write target offset within the surface in rows
 *
 * Write address instruction pointer to row tg_id_y + @y_offset.
 */
void gpgpu_shader__write_aip(struct gpgpu_shader *shdr, uint32_t y_offset)
{
	emit_iga64_code(shdr, media_block_write_aip, "				\n\
	// Payload								\n\
(W)	mov (1|M0)               r5.0<1>:ud    cr0.2:ud				\n\
#if GEN_VER < 2000 // Media Block Write						\n\
	// X offset of the block in bytes := (thread group id X << ARG(0))	\n\
(W)	shl (1|M0)               r4.0<1>:ud    r0.1<0;1,0>:ud    0x2:ud		\n\
	// Y offset of the block in rows := thread group id Y			\n\
(W)	mov (1|M0)               r4.1<1>:ud    r0.6<0;1,0>:ud			\n\
(W)	add (1|M0)               r4.1<1>:ud    r4.1<0;1,0>:ud    ARG(0):ud	\n\
	// block width [0,63] representing 1 to 64 bytes			\n\
(W)	mov (1|M0)               r4.2<1>:ud    0x3:ud				\n\
	// FFTID := FFTID from R0 header					\n\
(W)	mov (1|M0)               r4.4<1>:ud    r0.5<0;1,0>:ud			\n\
(W)	send.dc1 (16|M0)         null     r4   src1_null 0       0x40A8000	\n\
#else // Typed 2D Block Store							\n\
	// Load r2.0-3 with tg id X << ARG(0)					\n\
(W)	shl (1|M0)               r2.0<1>:ud    r0.1<0;1,0>:ud    0x2:ud		\n\
	// Load r2.4-7 with tg id Y + ARG(1):ud					\n\
(W)	mov (1|M0)               r2.1<1>:ud    r0.6<0;1,0>:ud			\n\
(W)	add (1|M0)               r2.1<1>:ud    r2.1<0;1,0>:ud    ARG(0):ud	\n\
	// payload setup							\n\
(W)	mov (16|M0)              r4.0<1>:ud    0x0:ud				\n\
	// Store X and Y block start (160:191 and 192:223)			\n\
(W)	mov (2|M0)               r4.5<1>:ud    r2.0<2;2,1>:ud			\n\
	// Store X and Y block max_size (224:231 and 232:239)			\n\
(W)	mov (1|M0)               r4.7<1>:ud    0x3:ud				\n\
(W)	send.tgm (16|M0)         null     r4   null:0    0    0x64000007	\n\
#endif										\n\
	", y_offset);
}

/**
 * gpgpu_shader__write_dword:
 * @shdr: shader to be modified
 * @value: dword to be written
 * @y_offset: write target offset within the surface in rows
 *
 * Fill dword in (row, column/dword) == (tg_id_y + @y_offset, tg_id_x).
 */
void gpgpu_shader__write_dword(struct gpgpu_shader *shdr, uint32_t value,
			       uint32_t y_offset)
{
	emit_iga64_code(shdr, media_block_write, "				\n\
	// Clear message header							\n\
(W)	mov (16|M0)              r4.0<1>:ud    0x0:ud				\n\
	// Payload								\n\
(W)	mov (1|M0)               r5.0<1>:ud    ARG(3):ud			\n\
(W)	mov (1|M0)               r5.1<1>:ud    ARG(4):ud			\n\
(W)	mov (1|M0)               r5.2<1>:ud    ARG(5):ud			\n\
(W)	mov (1|M0)               r5.3<1>:ud    ARG(6):ud			\n\
#if GEN_VER < 2000 // Media Block Write						\n\
	// X offset of the block in bytes := (thread group id X << ARG(0))	\n\
(W)	shl (1|M0)               r4.0<1>:ud    r0.1<0;1,0>:ud    ARG(0):ud	\n\
	// Y offset of the block in rows := thread group id Y			\n\
(W)	mov (1|M0)               r4.1<1>:ud    r0.6<0;1,0>:ud			\n\
(W)	add (1|M0)               r4.1<1>:ud    r4.1<0;1,0>:ud   ARG(1):ud	\n\
	// block width [0,63] representing 1 to 64 bytes			\n\
(W)	mov (1|M0)               r4.2<1>:ud    ARG(2):ud			\n\
	// FFTID := FFTID from R0 header					\n\
(W)	mov (1|M0)               r4.4<1>:ud    r0.5<0;1,0>:ud			\n\
(W)	send.dc1 (16|M0)         null     r4   src1_null 0    0x40A8000		\n\
#else // Typed 2D Block Store							\n\
	// Load r2.0-3 with tg id X << ARG(0)					\n\
(W)	shl (1|M0)               r2.0<1>:ud    r0.1<0;1,0>:ud    ARG(0):ud	\n\
	// Load r2.4-7 with tg id Y + ARG(1):ud					\n\
(W)	mov (1|M0)               r2.1<1>:ud    r0.6<0;1,0>:ud			\n\
(W)	add (1|M0)               r2.1<1>:ud    r2.1<0;1,0>:ud    ARG(1):ud	\n\
	// Store X and Y block start (160:191 and 192:223)			\n\
(W)	mov (2|M0)               r4.5<1>:ud    r2.0<2;2,1>:ud			\n\
	// Store X and Y block max_size (224:231 and 232:239)			\n\
(W)	mov (1|M0)               r4.7<1>:ud    ARG(2):ud			\n\
(W)	send.tgm (16|M0)         null     r4   null:0    0    0x64000007	\n\
#endif										\n\
	", 2, y_offset, 3, value, value, value, value);
}

/**
 * gpgpu_shader__clear_exception:
 * @shdr: shader to be modified
 * @value: exception bits to be cleared
 *
 * Clear provided bits in exception register: cr0.1 &= ~value.
 */
void gpgpu_shader__clear_exception(struct gpgpu_shader *shdr, uint32_t value)
{
	emit_iga64_code(shdr, clear_exception, "		\n\
(W)	and (1|M0) cr0.1<1>:ud cr0.1<0;1,0>:ud ARG(0):ud	\n\
	", ~value);
}

/**
 * gpgpu_shader__set_exception:
 * @shdr: shader to be modified
 * @value: exception bits to be set
 *
 * Set provided bits in exception register: cr0.1 |= value.
 */
void gpgpu_shader__set_exception(struct gpgpu_shader *shdr, uint32_t value)
{
	emit_iga64_code(shdr, set_exception, "		\n\
(W)	or (1|M0) cr0.1<1>:ud cr0.1<0;1,0>:ud ARG(0):ud	\n\
	", value);
}

/**
 * gpgpu_shader__write_on_exception:
 * @shdr: shader to be modified
 * @value: dword to be written
 * @y_offset: write target offset within the surface in rows
 * @mask: mask to be applied on exception register
 * @expected: expected value of exception register with @mask applied
 *
 * Check if bits specified by @mask in exception register(cr0.1) are equal
 * to provided ones: cr0.1 & @mask == @expected,
 * if yes fill dword in (row, column/dword) == (tg_id_y + @y_offset, tg_id_x).
 */
void gpgpu_shader__write_on_exception(struct gpgpu_shader *shdr, uint32_t value,
				      uint32_t y_offset, uint32_t mask, uint32_t expected)
{
	emit_iga64_code(shdr, write_on_exception, "					\n\
	// Clear message header								\n\
(W)	mov (16|M0)              r4.0<1>:ud    0x0:ud					\n\
	// Payload									\n\
(W)	mov (1|M0)               r5.0<1>:ud    ARG(3):ud				\n\
#if GEN_VER < 2000 // prepare Media Block Write						\n\
	// X offset of the block in bytes := (thread group id X << ARG(0))		\n\
(W)	shl (1|M0)               r4.0<1>:ud    r0.1<0;1,0>:ud    ARG(0):ud		\n\
	// Y offset of the block in rows := thread group id Y				\n\
(W)	mov (1|M0)               r4.1<1>:ud    r0.6<0;1,0>:ud				\n\
(W)	add (1|M0)               r4.1<1>:ud    r4.1<0;1,0>:ud   ARG(1):ud		\n\
	// block width [0,63] representing 1 to 64 bytes				\n\
(W)	mov (1|M0)               r4.2<1>:ud    ARG(2):ud				\n\
	// FFTID := FFTID from R0 header						\n\
(W)	mov (1|M0)               r4.4<1>:ud    r0.5<0;1,0>:ud				\n\
#else // prepare Typed 2D Block Store							\n\
	// Load r2.0-3 with tg id X << ARG(0)						\n\
(W)	shl (1|M0)               r2.0<1>:ud    r0.1<0;1,0>:ud    ARG(0):ud		\n\
	// Load r2.4-7 with tg id Y + ARG(1):ud						\n\
(W)	mov (1|M0)               r2.1<1>:ud    r0.6<0;1,0>:ud				\n\
(W)	add (1|M0)               r2.1<1>:ud    r2.1<0;1,0>:ud    ARG(1):ud		\n\
	// Store X and Y block start (160:191 and 192:223)				\n\
(W)	mov (2|M0)               r4.5<1>:ud    r2.0<2;2,1>:ud				\n\
	// Store X and Y block max_size (224:231 and 232:239)				\n\
(W)	mov (1|M0)               r4.7<1>:ud    ARG(2):ud				\n\
#endif											\n\
	// Check if masked exception is equal to provided value and write conditionally \n\
(W)      and (1|M0)              r3.0<1>:ud     cr0.1<0;1,0>:ud ARG(4):ud		\n\
(W)      mov (1|M0)              f0.0<1>:ud     0x0:ud					\n\
(W)      cmp (1|M0)     (eq)f0.0 null:ud        r3.0<0;1,0>:ud  ARG(5):ud		\n\
#if GEN_VER < 2000 // Media Block Write							\n\
(W&f0.0) send.dc1 (16|M0)        null     r4   src1_null 0    0x40A8000			\n\
#else // Typed 2D Block Store								\n\
(W&f0.0) send.tgm (16|M0)        null     r4   null:0    0    0x64000007		\n\
#endif											\n\
	", 2, y_offset, 3, value, mask, expected);
}

/**
 * gpgpu_shader__end_system_routine:
 * @shdr: shader to be modified
 * @breakpoint_suppress: breakpoint suppress flag
 *
 * Return from system routine. To prevent infinite jumping to the system
 * routine on a breakpoint, @breakpoint_suppress flag has to be set.
 */
void gpgpu_shader__end_system_routine(struct gpgpu_shader *shdr,
				      bool breakpoint_suppress)
{
	/*
	 * set breakpoint suppress bit to avoid an endless loop
	 * when sip was invoked by a breakpoint
	 */
	if (breakpoint_suppress)
		emit_iga64_code(shdr, breakpoint_suppress, "			\n\
(W)	or  (1|M0)               cr0.0<1>:ud   cr0.0<0;1,0>:ud   0x8000:ud	\n\
		");

	emit_iga64_code(shdr, end_system_routine, "				\n\
(W)	and (1|M0)               cr0.1<1>:ud   cr0.1<0;1,0>:ud   ARG(0):ud	\n\
	// return to an application						\n\
(W)	and (1|M0)               cr0.0<1>:ud   cr0.0<0;1,0>:ud   0x7FFFFFFD:ud	\n\
	", 0x7fffff | (1 << 26)); /* clear all exceptions, except read only bit */
}

/**
 * gpgpu_shader__end_system_routine_step_if_eq:
 * @shdr: shader to be modified
 * @y_offset: offset within target buffer in rows
 * @value: expected value for single stepping execution
 *
 * Return from system routine. Don't clear breakpoint exception when @value
 * is equal to value stored at @y_offset. This triggers the system routine
 * after the subsequent instruction, resulting in single stepping execution.
 */
void gpgpu_shader__end_system_routine_step_if_eq(struct gpgpu_shader *shdr,
						 uint32_t y_offset,
						 uint32_t value)
{
	emit_iga64_code(shdr, end_system_routine_step_if_eq, "				\n\
(W)		or  (1|M0)               cr0.0<1>:ud   cr0.0<0;1,0>:ud   0x8000:ud	\n\
(W)		and (1|M0)               cr0.1<1>:ud   cr0.1<0;1,0>:ud   ARG(0):ud	\n\
(W)		mov (16|M0)              r30.0<1>:ud    0x0:ud				\n\
#if GEN_VER < 2000 // Media Block Write							\n\
		// Y offset of the block in rows := thread group id Y			\n\
(W)		mov (1|M0)               r30.1<1>:ud    ARG(1):ud			\n\
		// block width [0,63] representing 1 to 64 bytes, we want dword		\n\
(W)		mov (1|M0)               r30.2<1>:ud    0x3:ud				\n\
		// FFTID := FFTID from R0 header					\n\
(W)		mov (1|M0)               r30.4<1>:ud    r0.5<0;1,0>:ud			\n\
(W)		send.dc1 (16|M0)         r31     r30      null    0x0	0x2190000	\n\
#else	// Typed 2D Block Store								\n\
		// Store X and Y block start (160:191 and 192:223)			\n\
(W)		mov (1|M0)               r30.6<1>:ud    ARG(1):ud			\n\
		// Store X and Y block size (224:231 and 232:239)			\n\
(W)		mov (1|M0)               r30.7<1>:ud    0x3:ud				\n\
(W)		send.tgm (16|M0)         r31     r30    null:0    0x0    0x62100003	\n\
#endif											\n\
		// clear the flag register						\n\
(W)		mov (1|M0)               f0.0<1>:ud    0x0:ud				\n\
(W)		cmp (1|M0)    (ne)f0.0   null<1>:ud     r31.0<0;1,0>:ud   ARG(2):ud	\n\
(W&f0.0)	and (1|M0)              cr0.1<1>:ud     cr0.1<0;1,0>:ud   ARG(3):ud	\n\
		// return to an application						\n\
(W)		and (1|M0)               cr0.0<1>:ud   cr0.0<0;1,0>:ud   0x7FFFFFFD:ud	\n\
	", 0x807fffff, /* leave breakpoint exception */
	y_offset, value, 0x7fffff /* clear all exceptions */ );
}
