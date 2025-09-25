/*
 * Copyright © 2006 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "igt_aux.h"
#include "igt_halffloat.h"
#include "intel_chipset.h"
#include "intel_io.h"
#include "drmtest.h"

/* kernel types for intel_vbt_defs.h */
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#define __packed __attribute__ ((packed))

#define _INTEL_BIOS_PRIVATE
#include "intel_vbt_defs.h"

/* additional macros for parsing */
#define DEVICE_TYPE_DP_DVI		0x68d6
#define DEVICE_TYPE_DVI			0x68d2
#define DEVICE_TYPE_MIPI		0x7cc2

struct bdb_legacy_child_devices {
	uint8_t child_dev_size;
	uint8_t devices[0]; /* presumably 7 * 33 */
} __attribute__ ((packed));

#define YESNO(val) ((val) ? "yes" : "no")

/* This is not for mapping to memory layout. */
struct bdb_block {
	uint8_t id;
	uint32_t size;
	uint8_t data[];
};

struct context {
	const struct vbt_header *vbt;
	const struct bdb_header *bdb;
	int size;

	uint32_t devid;
	int panel_type, panel_type2;
	int sdvo_panel_type;
	bool dump_all_panel_types;
	bool hexdump;
};

struct edid {
	uint8_t header[8];
	struct bdb_edid_pnp_id pnpid;
	/* ... */
} __packed;

static void hex_dump(const void *data, uint32_t size)
{
	int i;
	const uint8_t *p = data;

	for (i = 0; i < size; i++) {
		if (i % 16 == 0)
			printf("\t%04x: ", i);
		printf("%02x", p[i]);
		if (i % 16 == 15) {
			if (i + 1 < size)
				printf("\n");
		} else if (i % 8 == 7) {
			printf("  ");
		} else {
			printf(" ");
		}
	}
	printf("\n\n");
}

static bool dump_panel(const struct context *context, int panel_type)
{
	return panel_type == context->panel_type ||
		panel_type == context->panel_type2 ||
		context->dump_all_panel_types;
}

static const char *panel_str(const struct context *context, int panel_type)
{
	if (panel_type == context->panel_type &&
	    panel_type == context->panel_type2)
		return " (LFP1)(LFP2)";

	if (panel_type == context->panel_type)
		return " (LFP1)";

	if (panel_type == context->panel_type2)
		return " (LFP2)";

	return "";
}

static const char *sdvo_panel_str(const struct context *context, int sdvo_panel_type)
{
	if (sdvo_panel_type == context->sdvo_panel_type)
		return " (*)";

	return "";
}

/* Get BDB block size given a pointer to Block ID. */
static uint32_t _get_blocksize(const uint8_t *block_base)
{
	/* The MIPI Sequence Block v3+ has a separate size field. */
	if (*block_base == BDB_MIPI_SEQUENCE && *(block_base + 3) >= 3)
		return *((const uint32_t *)(block_base + 4));
	else
		return *((const uint16_t *)(block_base + 1));
}

/* Get BDB block size give a pointer to data after Block ID and Block Size. */
static u32 get_blocksize(const void *block_data)
{
	return _get_blocksize(block_data - 3);
}

static const void *find_raw_section(const struct context *context, int section_id)
{
	const struct bdb_header *bdb = context->bdb;
	int length = context->size;
	const uint8_t *base = (const uint8_t *)bdb;
	int index = 0;
	uint32_t total, current_size;
	unsigned char current_id;

	/* skip to first section */
	index += bdb->header_size;
	total = bdb->bdb_size;
	if (total > length)
		total = length;

	/* walk the sections looking for section_id */
	while (index + 3 < total) {
		current_id = *(base + index);
		current_size = _get_blocksize(base + index);
		index += 3;

		if (index + current_size > total)
			return NULL;

		if (current_id == section_id)
			return base + index;

		index += current_size;
	}

	return NULL;
}

/*
 * Offset from the start of BDB to the start of the
 * block data (just past the block header).
 */
static u32 raw_block_offset(const struct context *context, enum bdb_block_id section_id)
{
	const void *block;

	block = find_raw_section(context, section_id);
	if (!block)
		return 0;

	return block - (const void *)context->bdb;
}

static const void *block_data(const struct bdb_block *block)
{
	return block->data + 3;
}

static struct bdb_block *find_section(const struct context *context, int section_id);

static size_t lfp_data_min_size(const struct context *context)
{
	const struct bdb_lfp_data_ptrs *ptrs;
	struct bdb_block *ptrs_block;
	size_t size;

	ptrs_block = find_section(context, BDB_LFP_DATA_PTRS);
	if (!ptrs_block)
		return 0;

	ptrs = block_data(ptrs_block);

	size = sizeof(struct bdb_lfp_data);
	if (ptrs->panel_name.table_size)
		size = max(size, ptrs->panel_name.offset +
			   sizeof(struct bdb_lfp_data_tail));

	free(ptrs_block);

	return size;
}

static int make_lfp_data_ptr(struct lfp_data_ptr_table *table,
			     int table_size, int total_size)
{
	if (total_size < table_size)
		return total_size;

	table->table_size = table_size;
	table->offset = total_size - table_size;

	return total_size - table_size;
}

static void next_lfp_data_ptr(struct lfp_data_ptr_table *next,
			      const struct lfp_data_ptr_table *prev,
			      int size)
{
	next->table_size = prev->table_size;
	next->offset = prev->offset + size;
}

static void *generate_lfp_data_ptrs(const struct context *context)
{
	int size, table_size, block_size, offset, fp_timing_size;
	const void *block;
	struct bdb_lfp_data_ptrs *ptrs;
	void *ptrs_block;

	/*
	 * The hardcoded fp_timing_size is only valid for
	 * modernish VBTs. All older VBTs definitely should
	 * include block 41 and thus we don't need to
	 * generate one.
	 */
	if (context->bdb->version < 155)
		return NULL;

	fp_timing_size = 38;

	block = find_raw_section(context, BDB_LFP_DATA);
	if (!block)
		return NULL;

	block_size = get_blocksize(block);

	size = block_size;

	size = fp_timing_size + sizeof(struct bdb_edid_dtd) +
		sizeof(struct bdb_edid_pnp_id);
	if (size * 16 > block_size)
		return NULL;

	ptrs_block = calloc(1, sizeof(*ptrs) + 3);
	if (!ptrs_block)
		return NULL;

	*(uint8_t *)(ptrs_block + 0) = BDB_LFP_DATA_PTRS;
	*(uint16_t *)(ptrs_block + 1) = sizeof(*ptrs);
	ptrs = ptrs_block + 3;

	table_size = sizeof(struct bdb_edid_pnp_id);
	size = make_lfp_data_ptr(&ptrs->ptr[0].panel_pnp_id, table_size, size);

	table_size = sizeof(struct bdb_edid_dtd);
	size = make_lfp_data_ptr(&ptrs->ptr[0].dvo_timing, table_size, size);

	table_size = fp_timing_size;
	size = make_lfp_data_ptr(&ptrs->ptr[0].fp_timing, table_size, size);

	if (ptrs->ptr[0].fp_timing.table_size)
		ptrs->num_entries++;
	if (ptrs->ptr[0].dvo_timing.table_size)
		ptrs->num_entries++;
	if (ptrs->ptr[0].panel_pnp_id.table_size)
		ptrs->num_entries++;

	if (size != 0 || ptrs->num_entries != 3)
		return NULL;

	size = fp_timing_size + sizeof(struct bdb_edid_dtd) +
		sizeof(struct bdb_edid_pnp_id);
	for (int i = 1; i < 16; i++) {
		next_lfp_data_ptr(&ptrs->ptr[i].fp_timing, &ptrs->ptr[i-1].fp_timing, size);
		next_lfp_data_ptr(&ptrs->ptr[i].dvo_timing, &ptrs->ptr[i-1].dvo_timing, size);
		next_lfp_data_ptr(&ptrs->ptr[i].panel_pnp_id, &ptrs->ptr[i-1].panel_pnp_id, size);
	}

	table_size = sizeof(struct bdb_edid_product_name);

	if (16 * (size + table_size) <= block_size) {
		ptrs->panel_name.table_size = table_size;
		ptrs->panel_name.offset = size * 16;
	}

	offset = block - (const void *)context->bdb;
	for (int i = 0; i < 16; i++) {
		ptrs->ptr[i].fp_timing.offset += offset;
		ptrs->ptr[i].dvo_timing.offset += offset;
		ptrs->ptr[i].panel_pnp_id.offset += offset;
	}

	if (ptrs->panel_name.offset)
		ptrs->panel_name.offset += offset;

	return ptrs_block;
}

static size_t block_min_size(const struct context *context, int section_id)
{
	switch (section_id) {
	case BDB_GENERAL_FEATURES:
		return sizeof(struct bdb_general_features);
	case BDB_GENERAL_DEFINITIONS:
		return sizeof(struct bdb_general_definitions);
	case BDB_DISPLAY_TOGGLE:
		return sizeof(struct bdb_display_toggle);
	case BDB_MODE_SUPPORT_LIST:
		return sizeof(struct bdb_mode_support_list);
	case BDB_GENERIC_MODE_TABLE:
		return max(sizeof(struct bdb_generic_mode_table_alm),
			   sizeof(struct bdb_generic_mode_table_mgm));
	case BDB_EXT_MMIO_REGS:
	case BDB_SWF_IO:
	case BDB_SWF_MMIO:
		return sizeof(struct bdb_reg_table);
	case BDB_PSR: /* nee BDB_DOT_CLOCK_OVERRIDE_ALM */
		return max(sizeof(struct bdb_psr),
			   sizeof(struct bdb_dot_clock_override_alm));
	case BDB_MODE_REMOVAL_TABLE:
		return sizeof(struct bdb_mode_removal);
	case BDB_CHILD_DEVICE_TABLE:
		return sizeof(struct bdb_legacy_child_devices);
	case BDB_DRIVER_FEATURES:
		return sizeof(struct bdb_driver_features);
	case BDB_DRIVER_PERSISTENCE:
		return sizeof(struct bdb_driver_persistence);
	case BDB_DOT_CLOCK_OVERRIDE:
		return sizeof(struct bdb_dot_clock_override);
	case BDB_DISPLAY_SELECT_OLD:
		return sizeof(struct bdb_display_select_old);
	case BDB_DRIVER_ROTATION:
		return sizeof(struct bdb_driver_rotation);
	case BDB_DISPLAY_REMOVE_OLD:
		return sizeof(struct bdb_display_remove_old);
	case BDB_OEM_CUSTOM:
		return sizeof(struct bdb_oem_custom);
	case BDB_EFP_LIST:
		return sizeof(struct bdb_efp_list);
	case BDB_SDVO_LVDS_OPTIONS:
		return sizeof(struct bdb_sdvo_lvds_options);
	case BDB_SDVO_LVDS_DTD:
		return sizeof(struct bdb_sdvo_lvds_dtd);
	case BDB_SDVO_LVDS_PNP_ID:
		return sizeof(struct bdb_sdvo_lvds_pnp_id);
	case BDB_SDVO_LVDS_PPS:
		return sizeof(struct bdb_sdvo_lvds_pps);
	case BDB_TV_OPTIONS:
		return sizeof(struct bdb_tv_options);
	case BDB_EDP:
		return sizeof(struct bdb_edp);
	case BDB_EFP_DTD:
		return sizeof(struct bdb_efp_dtd);
	case BDB_DISPLAY_SELECT_IVB:
		return sizeof(struct bdb_display_select_ivb);
	case BDB_DISPLAY_REMOVE_IVB:
		return sizeof(struct bdb_display_remove_ivb);
	case BDB_DISPLAY_SELECT_HSW:
		return sizeof(struct bdb_display_select_hsw);
	case BDB_DISPLAY_REMOVE_HSW:
		return sizeof(struct bdb_display_remove_hsw);
	case BDB_LFP_OPTIONS:
		return sizeof(struct bdb_lfp_options);
	case BDB_LFP_DATA_PTRS:
		return sizeof(struct bdb_lfp_data_ptrs);
	case BDB_LFP_DATA:
		return lfp_data_min_size(context);
	case BDB_LFP_BACKLIGHT:
		return sizeof(struct bdb_lfp_backlight);
	case BDB_LFP_POWER:
		return sizeof(struct bdb_lfp_power);
	case BDB_EDP_BFI:
		return sizeof(struct bdb_edp_bfi);
	case BDB_CHROMATICITY:
		return sizeof(struct bdb_chromaticity);
	case BDB_FIXED_SET_MODE:
		return sizeof(struct bdb_fixed_set_mode);
	case BDB_MIPI_CONFIG:
		return sizeof(struct bdb_mipi_config);
	case BDB_MIPI_SEQUENCE:
		return sizeof(struct bdb_mipi_sequence);
	case BDB_RGB_PALETTE:
		return sizeof(struct bdb_rgb_palette);
	case BDB_COMPRESSION_PARAMETERS:
		return sizeof(struct bdb_compression_parameters);
	case BDB_VSWING_PREEMPH:
		return sizeof(struct bdb_vswing_preemph);
	case BDB_GENERIC_DTD:
		return sizeof(struct bdb_generic_dtd);
	case BDB_PRD_TABLE:
		return max(sizeof(struct bdb_prd_table_old),
			   sizeof(struct bdb_prd_table_new));
	default:
		return 0;
	}
}

static bool validate_lfp_data_ptrs(const struct context *context,
				   const struct bdb_lfp_data_ptrs *ptrs)
{
	int fp_timing_size, dvo_timing_size, panel_pnp_id_size, panel_name_size;
	int data_block_size, lfp_data_size;
	const void *block;
	int i;

	block = find_raw_section(context, BDB_LFP_DATA);
	if (!block)
		return false;

	data_block_size = get_blocksize(block);
	if (data_block_size == 0)
		return false;

	/* always 3 indicating the presence of fp_timing+dvo_timing+panel_pnp_id */
	if (ptrs->num_entries != 3)
		return false;

	fp_timing_size = ptrs->ptr[0].fp_timing.table_size;
	dvo_timing_size = ptrs->ptr[0].dvo_timing.table_size;
	panel_pnp_id_size = ptrs->ptr[0].panel_pnp_id.table_size;
	panel_name_size = ptrs->panel_name.table_size;

	/* fp_timing has variable size */
	if (fp_timing_size < 32 ||
	    dvo_timing_size != sizeof(struct bdb_edid_dtd) ||
	    panel_pnp_id_size != sizeof(struct bdb_edid_pnp_id))
		return false;

	/* panel_name is not present in old VBTs */
	if (panel_name_size != 0 &&
	    panel_name_size != sizeof(struct bdb_edid_product_name))
		return false;

	lfp_data_size = ptrs->ptr[1].fp_timing.offset - ptrs->ptr[0].fp_timing.offset;
	if (16 * lfp_data_size > data_block_size)
		return false;

	/* make sure the table entries have uniform size */
	for (i = 1; i < 16; i++) {
		if (ptrs->ptr[i].fp_timing.table_size != fp_timing_size ||
		    ptrs->ptr[i].dvo_timing.table_size != dvo_timing_size ||
		    ptrs->ptr[i].panel_pnp_id.table_size != panel_pnp_id_size)
			return false;

		if (ptrs->ptr[i].fp_timing.offset - ptrs->ptr[i-1].fp_timing.offset != lfp_data_size ||
		    ptrs->ptr[i].dvo_timing.offset - ptrs->ptr[i-1].dvo_timing.offset != lfp_data_size ||
		    ptrs->ptr[i].panel_pnp_id.offset - ptrs->ptr[i-1].panel_pnp_id.offset != lfp_data_size)
			return false;
	}

	/*
	 * Except for vlv/chv machines all real VBTs seem to have 6
	 * unaccounted bytes in the fp_timing table. And it doesn't
	 * appear to be a really intentional hole as the fp_timing
	 * 0xffff terminator is always within those 6 missing bytes.
	 */
	if (fp_timing_size + 6 + dvo_timing_size + panel_pnp_id_size == lfp_data_size)
		fp_timing_size += 6;

	if (fp_timing_size + dvo_timing_size + panel_pnp_id_size != lfp_data_size)
		return false;

	if (ptrs->ptr[0].fp_timing.offset + fp_timing_size != ptrs->ptr[0].dvo_timing.offset ||
	    ptrs->ptr[0].dvo_timing.offset + dvo_timing_size != ptrs->ptr[0].panel_pnp_id.offset ||
	    ptrs->ptr[0].panel_pnp_id.offset + panel_pnp_id_size != lfp_data_size)
		return false;

	/* make sure the tables fit inside the data block */
	for (i = 0; i < 16; i++) {
		if (ptrs->ptr[i].fp_timing.offset + fp_timing_size > data_block_size ||
		    ptrs->ptr[i].dvo_timing.offset + dvo_timing_size > data_block_size ||
		    ptrs->ptr[i].panel_pnp_id.offset + panel_pnp_id_size > data_block_size)
			return false;
	}

	if (ptrs->panel_name.offset + 16 * panel_name_size > data_block_size)
		return false;

	/* make sure fp_timing terminators are present at expected locations */
	for (i = 0; i < 16; i++) {
		const u16 *t = block + ptrs->ptr[i].fp_timing.offset + fp_timing_size - 2;

		if (*t != 0xffff)
			return false;
	}

	return true;
}

/* make the data table offsets relative to the data block */
static bool fixup_lfp_data_ptrs(const struct context *context,
				void *ptrs_block)
{
	struct bdb_lfp_data_ptrs *ptrs = ptrs_block;
	u32 offset;
	int i;

	offset = raw_block_offset(context, BDB_LFP_DATA);

	for (i = 0; i < 16; i++) {
		if (ptrs->ptr[i].fp_timing.offset < offset ||
		    ptrs->ptr[i].dvo_timing.offset < offset ||
		    ptrs->ptr[i].panel_pnp_id.offset < offset)
			return false;

		ptrs->ptr[i].fp_timing.offset -= offset;
		ptrs->ptr[i].dvo_timing.offset -= offset;
		ptrs->ptr[i].panel_pnp_id.offset -= offset;
	}

	if (ptrs->panel_name.table_size) {
		if (ptrs->panel_name.offset < offset)
			return false;

		ptrs->panel_name.offset -= offset;
	}

	return validate_lfp_data_ptrs(context, ptrs);
}

static struct bdb_block *find_section(const struct context *context, int section_id)
{
	size_t min_size = block_min_size(context, section_id);
	struct bdb_block *block;
	void *temp_block = NULL;
	const void *data;
	size_t size;

	data = find_raw_section(context, section_id);
	if (!data && section_id == BDB_LFP_DATA_PTRS) {
		fprintf(stderr, "Generating LFP data table pointers\n");
		temp_block = generate_lfp_data_ptrs(context);
		if (temp_block)
			data = temp_block + 3;
	}
	if (!data)
		return NULL;

	size = get_blocksize(data);

	/*
	 * Version number and new block size are considered
	 * part of the header for MIPI sequenece block v3+.
	 */
	if (section_id == BDB_MIPI_SEQUENCE && *(const u8*)data >= 3)
		size += 5;

	/* expect to have the full definition for each block with modern VBTs */
	if (min_size && size > min_size &&
	    section_id != BDB_CHILD_DEVICE_TABLE &&
	    section_id != BDB_SDVO_LVDS_OPTIONS &&
	    section_id != BDB_GENERAL_DEFINITIONS &&
	    context->bdb->version >= 155)
		fprintf(stderr, "Block %d min size %zu less than block size %zu\n",
			section_id, min_size, size);

	block = calloc(1, sizeof(*block) + 3 + max(size, min_size));
	if (!block) {
		free(temp_block);
		return NULL;
	}

	block->id = section_id;
	block->size = size;
	memcpy(block->data, data - 3, 3 + size);

	free(temp_block);

	if (section_id == BDB_LFP_DATA_PTRS &&
	    !fixup_lfp_data_ptrs(context, 3 + block->data)) {
		fprintf(stderr, "VBT has malformed LFP data table pointers\n");
		free(block);
		return NULL;
	}

	return block;
}

static unsigned int panel_bits(unsigned int value, int panel_type, int num_bits)
{
	return (value >> (panel_type * num_bits)) & (BIT(num_bits) - 1);
}

static bool panel_bool(unsigned int value, int panel_type)
{
	return panel_bits(value, panel_type, 1);
}

static const char *_to_str(const char * const strings[],
			   int num_strings, int value)
{
	if (value >= num_strings || value < 0 || strings[value] == NULL)
		return "<unknown>";

	return strings[value];
}

#define to_str(strings, value) _to_str((strings), ARRAY_SIZE(strings), (value))

static int decode_ssc_freq(struct context *context, bool alternate)
{
	switch (intel_gen(context->devid)) {
	case 2:
		return alternate ? 66 : 48;
	case 3:
	case 4:
		return alternate ? 100 : 96;
	default:
		return alternate ? 100 : 120;
	}
}

static const char * const panel_fitting_str[] = {
	[0] = "disabled",
	[1] = "text only",
	[2] = "graphics only",
	[3] = "text & graphics",
};

static void dump_general_features(struct context *context,
				  const struct bdb_block *block)
{
	const struct bdb_general_features *features = block_data(block);

	printf("\tPanel fitting: %s (0x%x)\n",
	       to_str(panel_fitting_str, features->panel_fitting), features->panel_fitting);
	printf("\tFlexaim: %s\n", YESNO(features->flexaim));
	printf("\tMessage: %s\n", YESNO(features->msg_enable));
	printf("\tClear screen: %d\n", features->clear_screen);
	printf("\tDVO color flip required: %s\n", YESNO(features->color_flip));

	printf("\tExternal VBT: %s\n", YESNO(features->download_ext_vbt));
	printf("\tLVDS SSC Enable: %s\n", YESNO(features->enable_ssc));
	printf("\tLVDS SSC frequency: %d MHz (0x%x)\n",
	       decode_ssc_freq(context, features->ssc_freq),
	       features->ssc_freq);
	printf("\tLFP on override: %s\n",
	       YESNO(features->enable_lfp_on_override));
	printf("\tDisable SSC on clone: %s\n",
	       YESNO(features->disable_ssc_ddt));
	printf("\tUnderscan support for VGA timings: %s\n",
	       YESNO(features->underscan_vga_timings));
	if (context->bdb->version >= 183)
		printf("\tDynamic CD clock: %s\n", YESNO(features->display_clock_mode));
	printf("\tHotplug support in VBIOS: %s\n",
	       YESNO(features->vbios_hotplug_support));

	printf("\tDisable smooth vision: %s\n",
	       YESNO(features->disable_smooth_vision));
	printf("\tSingle DVI for CRT/DVI: %s\n", YESNO(features->single_dvi));
	if (context->bdb->version >= 181)
		printf("\tEnable 180 degree rotation: %s\n", YESNO(features->rotate_180));
	printf("\tInverted FDI Rx polarity: %s\n", YESNO(features->fdi_rx_polarity_inverted));
	if (context->bdb->version >= 160) {
		printf("\tExtended VBIOS mode: %s\n", YESNO(features->vbios_extended_mode));
		printf("\tCopy iLFP DTD to SDVO LVDS DTD: %s\n", YESNO(features->copy_ilfp_dtd_to_sdvo_lvds_dtd));
		printf("\tBest fit panel timing algorithm: %s\n", YESNO(features->panel_best_fit_timing));
		printf("\tIgnore strap state: %s\n", YESNO(features->ignore_strap_state));
	}

	printf("\tLegacy monitor detect: %s\n",
	       YESNO(features->legacy_monitor_detect));

	printf("\tIntegrated CRT: %s\n", YESNO(features->int_crt_support));
	printf("\tIntegrated TV: %s\n", YESNO(features->int_tv_support));
	printf("\tIntegrated EFP: %s\n", YESNO(features->int_efp_support));
	printf("\tDP SSC enable: %s\n", YESNO(features->dp_ssc_enable));
	printf("\tDP SSC frequency: %d MHz (0x%x)\n",
	       decode_ssc_freq(context, features->dp_ssc_freq),
	       features->dp_ssc_freq);
	printf("\tDP SSC dongle supported: %s\n", YESNO(features->dp_ssc_dongle_supported));
}

static const char * const inverter_type_str[] = {
	[0] = "none/external",
	[1] = "I2C",
	[2] = "PWM",
};

static const char * const i2c_speed_str[] = {
	[0] = "100 kHz",
	[1] = "50 kHz",
	[2] = "400 kHz",
	[3] = "1 MHz",
};

static void dump_backlight_info(struct context *context,
				const struct bdb_block *block)
{
	const struct bdb_lfp_backlight *backlight = block_data(block);
	const struct lfp_backlight_data_entry *blc;
	const struct lfp_backlight_control_method *control;
	int i;

	if (sizeof(*blc) != backlight->entry_size) {
		printf("\tBacklight struct sizes don't match (expected %zu, got %u), skipping\n",
		     sizeof(*blc), backlight->entry_size);
		return;
	}

	printf("\tEntry size: %u\n", backlight->entry_size);

	for (i = 0; i < ARRAY_SIZE(backlight->data); i++) {
		if (!dump_panel(context, i))
			continue;

		printf("\tPanel %d%s\n", i, panel_str(context, i));

		blc = &backlight->data[i];

		printf("\t\tInverter type: %s (%u)\n",
		       to_str(inverter_type_str, blc->type), blc->type);
		printf("\t\tActive low: %s\n", YESNO(blc->active_low_pwm));
		printf("\t\tPWM freq: %u\n", blc->pwm_freq_hz);
		printf("\t\tMinimum brightness: %u\n", blc->min_brightness);

		if (blc->type == 1) {
			printf("\t\tI2C pin: 0x%02x\n", blc->i2c_pin);
			printf("\t\tI2C speed: %s (0x%02x)\n",
			       to_str(i2c_speed_str, blc->i2c_speed), blc->i2c_speed);
			printf("\t\tI2C address: 0x%02x\n", blc->i2c_address);
			printf("\t\tI2C command: 0x%02x\n", blc->i2c_command);
		}

		if (context->bdb->version < 162)
			continue;

		printf("\t\tLevel: %u\n", backlight->level[i]);

		if (context->bdb->version < 191)
			continue;

		control = &backlight->backlight_control[i];

		printf("\t\tControl type: %u\n", control->type);
		printf("\t\tController: %u\n", control->controller);

		if (context->bdb->version < 234)
			continue;

		printf("\t\tBrightness level: %u\n",
		       backlight->brightness_level[i].level);
		printf("\t\tBrightness min level: %u\n",
		       backlight->brightness_min_level[i].level);

		if (context->bdb->version < 236)
			continue;

		printf("\t\tBrigthness precision bits: %u\n",
		       backlight->brightness_precision_bits[i]);

		if (context->bdb->version < 239)
			continue;

		printf("\t\tHDR DPCD refresh timeout: %.2f ms\n",
		       backlight->hdr_dpcd_refresh_timeout[i] / 100.0);
	}
}

static const struct {
	unsigned short type;
	const char *name;
} child_device_types[] = {
	{ DEVICE_TYPE_NONE, "none" },
	{ DEVICE_TYPE_CRT, "CRT" },
	{ DEVICE_TYPE_TV, "TV" },
	{ DEVICE_TYPE_EFP, "EFP" },
	{ DEVICE_TYPE_LFP, "LFP" },
	{ DEVICE_TYPE_CRT_DPMS, "CRT" },
	{ DEVICE_TYPE_CRT_DPMS_HOTPLUG, "CRT" },
	{ DEVICE_TYPE_TV_COMPOSITE, "TV composite" },
	{ DEVICE_TYPE_TV_MACROVISION, "TV" },
	{ DEVICE_TYPE_TV_RF_COMPOSITE, "TV" },
	{ DEVICE_TYPE_TV_SVIDEO_COMPOSITE, "TV S-Video" },
	{ DEVICE_TYPE_TV_SCART, "TV SCART" },
	{ DEVICE_TYPE_TV_CODEC_HOTPLUG_PWR, "TV" },
	{ DEVICE_TYPE_EFP_HOTPLUG_PWR, "EFP" },
	{ DEVICE_TYPE_EFP_DVI_HOTPLUG_PWR, "DVI" },
	{ DEVICE_TYPE_EFP_DVI_I, "DVI-I" },
	{ DEVICE_TYPE_EFP_DVI_D_DUAL, "DL-DVI-D" },
	{ DEVICE_TYPE_EFP_DVI_D_HDCP, "DVI-D" },
	{ DEVICE_TYPE_OPENLDI_HOTPLUG_PWR, "OpenLDI" },
	{ DEVICE_TYPE_OPENLDI_DUALPIX, "OpenLDI" },
	{ DEVICE_TYPE_LFP_PANELLINK, "PanelLink" },
	{ DEVICE_TYPE_LFP_CMOS_PWR, "CMOS LFP" },
	{ DEVICE_TYPE_LFP_LVDS_PWR, "LVDS" },
	{ DEVICE_TYPE_LFP_LVDS_DUAL, "LVDS" },
	{ DEVICE_TYPE_LFP_LVDS_DUAL_HDCP, "LVDS" },
	{ DEVICE_TYPE_INT_LFP, "LFP" },
	{ DEVICE_TYPE_INT_TV, "TV" },
	{ DEVICE_TYPE_DP, "DisplayPort" },
	{ DEVICE_TYPE_DP_DUAL_MODE, "DisplayPort/HDMI/DVI" },
	{ DEVICE_TYPE_DP_DVI, "DisplayPort/DVI" },
	{ DEVICE_TYPE_HDMI, "HDMI/DVI" },
	{ DEVICE_TYPE_DVI, "DVI" },
	{ DEVICE_TYPE_eDP, "eDP" },
	{ DEVICE_TYPE_MIPI, "MIPI" },
};
static const int num_child_device_types =
	sizeof(child_device_types) / sizeof(child_device_types[0]);

static const char *child_device_type(unsigned short type)
{
	int i;

	for (i = 0; i < num_child_device_types; i++)
		if (child_device_types[i].type == type)
			return child_device_types[i].name;

	return "unknown";
}

static const struct {
	unsigned short mask;
	const char *name;
} child_device_type_bits[] = {
	{ DEVICE_TYPE_CLASS_EXTENSION, "Class extension" },
	{ DEVICE_TYPE_POWER_MANAGEMENT, "Power management" },
	{ DEVICE_TYPE_HOTPLUG_SIGNALING, "Hotplug signaling" },
	{ DEVICE_TYPE_INTERNAL_CONNECTOR, "Internal connector" },
	{ DEVICE_TYPE_NOT_HDMI_OUTPUT, "Not HDMI output" },
	{ DEVICE_TYPE_MIPI_OUTPUT, "MIPI output" },
	{ DEVICE_TYPE_COMPOSITE_OUTPUT, "Composite output" },
	{ DEVICE_TYPE_DUAL_CHANNEL, "Dual channel" },
	{ 1 << 7, "Content protection" },
	{ DEVICE_TYPE_HIGH_SPEED_LINK, "High speed link" },
	{ DEVICE_TYPE_LVDS_SIGNALING, "LVDS signaling" },
	{ DEVICE_TYPE_TMDS_DVI_SIGNALING, "TMDS/DVI signaling" },
	{ DEVICE_TYPE_VIDEO_SIGNALING, "Video signaling" },
	{ DEVICE_TYPE_DISPLAYPORT_OUTPUT, "DisplayPort output" },
	{ DEVICE_TYPE_DIGITAL_OUTPUT, "Digital output" },
	{ DEVICE_TYPE_ANALOG_OUTPUT, "Analog output" },
};

static void dump_child_device_type_bits(uint16_t type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(child_device_type_bits); i++) {
		if (child_device_type_bits[i].mask & type)
			printf("\t\t\t%s\n", child_device_type_bits[i].name);
	}
}

static const struct {
	uint16_t handle;
	uint16_t min_ver, max_ver;
	const char *name;
} child_device_handles[] = {
	{ .handle = DEVICE_HANDLE_CRT,  .name = "CRT",  .max_ver = 216, },
	{ .handle = DEVICE_HANDLE_TV,   .name = "TV",   .max_ver = 214, },
	{ .handle = DEVICE_HANDLE_EFP1, .name = "EFP1",                 },
	{ .handle = DEVICE_HANDLE_EFP2, .name = "EFP2",                 },
	{ .handle = DEVICE_HANDLE_EFP3, .name = "EFP3",                 },
	{ .handle = DEVICE_HANDLE_EFP4, .name = "EFP4",                 },
	{ .handle = DEVICE_HANDLE_EFP5, .name = "EFP5", .min_ver = 215, },
	{ .handle = DEVICE_HANDLE_EFP6, .name = "EFP6", .min_ver = 217, },
	{ .handle = DEVICE_HANDLE_EFP7, .name = "EFP7", .min_ver = 217, },
	{ .handle = DEVICE_HANDLE_EFP8, .name = "EFP8", .min_ver = 217, },
	{ .handle = DEVICE_HANDLE_LFP1, .name = "LFP1",                 },
	{ .handle = DEVICE_HANDLE_LFP2, .name = "LFP2",                 },
};
static const int num_child_device_handles =
	sizeof(child_device_handles) / sizeof(child_device_handles[0]);

static const char *child_device_handle(struct context *context,
				       uint16_t handle)
{
	static char buffer[64];
	size_t len = sizeof(buffer);
	char *ptr = buffer;
	bool first = true;

	if (handle == 0)
		return "none";

	for (int i = 0; i < num_child_device_handles; i++) {
		int r;

		if (!(child_device_handles[i].handle & handle))
			continue;

		if (child_device_handles[i].min_ver &&
		    context->bdb->version < child_device_handles[i].min_ver)
			continue;

		if (child_device_handles[i].max_ver &&
		    context->bdb->version > child_device_handles[i].max_ver)
			continue;

		handle &= ~child_device_handles[i].handle;

		r = snprintf(ptr, len, "%s%s", first ? "" : ",",
			     child_device_handles[i].name);
		if (r < 0 || r >= len)
			break;

		first = false;
		ptr += r;
		len -= r;
	}

	if (handle)
		snprintf(ptr, len, "%sunknown(0x%x)",
			 first ? "" : ",", handle);

	return buffer;
}

static const char *dvo_port_names[] = {
	[DVO_PORT_HDMIA] = "HDMI-A",
	[DVO_PORT_HDMIB] = "HDMI-B",
	[DVO_PORT_HDMIC] = "HDMI-C",
	[DVO_PORT_HDMID] = "HDMI-D",
	[DVO_PORT_HDMIE] = "HDMI-E",
	[DVO_PORT_HDMIF] = "HDMI-F",
	[DVO_PORT_HDMIG] = "HDMI-G",
	[DVO_PORT_HDMIH] = "HDMI-H",
	[DVO_PORT_HDMII] = "HDMI-I",
	[DVO_PORT_LVDS] = "LVDS",
	[DVO_PORT_TV] = "TV",
	[DVO_PORT_CRT] = "CRT",
	[DVO_PORT_DPB] = "DP-B",
	[DVO_PORT_DPC] = "DP-C",
	[DVO_PORT_DPD] = "DP-D",
	[DVO_PORT_DPA] = "DP-A",
	[DVO_PORT_DPE] = "DP-E",
	[DVO_PORT_DPF] = "DP-F",
	[DVO_PORT_DPG] = "DP-G",
	[DVO_PORT_DPH] = "DP-H",
	[DVO_PORT_DPI] = "DP-I",
	[DVO_PORT_MIPIA] = "MIPI-A",
	[DVO_PORT_MIPIB] = "MIPI-B",
	[DVO_PORT_MIPIC] = "MIPI-C",
	[DVO_PORT_MIPID] = "MIPI-D",
};

static const char *dvo_port(uint8_t type)
{
	if (type < ARRAY_SIZE(dvo_port_names) && dvo_port_names[type])
		return dvo_port_names[type];
	else
		return "unknown";
}

static const char *aux_ch_names[] = {
	[0] = "none",
	[DP_AUX_A >> 4] = "AUX-A",
	[DP_AUX_B >> 4] = "AUX-B",
	[DP_AUX_C >> 4] = "AUX-C",
	[DP_AUX_D >> 4] = "AUX-D",
	[DP_AUX_E >> 4] = "AUX-E",
	[DP_AUX_F >> 4] = "AUX-F",
	[DP_AUX_G >> 4] = "AUX-G",
	[DP_AUX_H >> 4] = "AUX-H",
	[DP_AUX_I >> 4] = "AUX-I",
};

static const char *aux_ch(uint8_t aux_ch)
{
	aux_ch >>= 4;

	if (aux_ch < ARRAY_SIZE(aux_ch_names) && aux_ch_names[aux_ch])
		return aux_ch_names[aux_ch];
	else
		return "unknown";
}

static const char * const mipi_bridge_type_str[] = {
	[1] = "ASUS",
	[2] = "Toshiba",
	[3] = "Renesas",
};

static void dump_hmdi_max_data_rate(uint8_t hdmi_max_data_rate)
{
	static const uint16_t max_data_rate[] = {
		[HDMI_MAX_DATA_RATE_PLATFORM] = 0,
		[HDMI_MAX_DATA_RATE_297] = 297,
		[HDMI_MAX_DATA_RATE_165] = 165,
		[HDMI_MAX_DATA_RATE_594] = 594,
		[HDMI_MAX_DATA_RATE_340] = 340,
		[HDMI_MAX_DATA_RATE_300] = 300,
	};

	if (hdmi_max_data_rate >= ARRAY_SIZE(max_data_rate))
		printf("\t\tHDMI max data rate: <unknown> (0x%02x)\n",
		       hdmi_max_data_rate);
	else if (hdmi_max_data_rate == HDMI_MAX_DATA_RATE_PLATFORM)
		printf("\t\tHDMI max data rate: <platform max> (0x%02x)\n",
		       hdmi_max_data_rate);
	else
		printf("\t\tHDMI max data rate: %d MHz (0x%02x)\n",
		       max_data_rate[hdmi_max_data_rate],
		       hdmi_max_data_rate);
}

static int parse_dp_max_link_rate_216(uint8_t dp_max_link_rate)
{
	static const uint16_t max_link_rate[] = {
		[BDB_216_VBT_DP_MAX_LINK_RATE_HBR3] = 810,
		[BDB_216_VBT_DP_MAX_LINK_RATE_HBR2] = 540,
		[BDB_216_VBT_DP_MAX_LINK_RATE_HBR] = 270,
		[BDB_216_VBT_DP_MAX_LINK_RATE_LBR] = 162,
	};

	return max_link_rate[dp_max_link_rate & 0x3];
}

static int parse_dp_max_link_rate_230(uint8_t dp_max_link_rate)
{
	static const uint16_t max_link_rate[] = {
		[BDB_230_VBT_DP_MAX_LINK_RATE_DEF] = 0,
		[BDB_230_VBT_DP_MAX_LINK_RATE_LBR] = 162,
		[BDB_230_VBT_DP_MAX_LINK_RATE_HBR] = 270,
		[BDB_230_VBT_DP_MAX_LINK_RATE_HBR2] = 540,
		[BDB_230_VBT_DP_MAX_LINK_RATE_HBR3] = 810,
		[BDB_230_VBT_DP_MAX_LINK_RATE_UHBR10] = 1000,
		[BDB_230_VBT_DP_MAX_LINK_RATE_UHBR13P5] = 1350,
		[BDB_230_VBT_DP_MAX_LINK_RATE_UHBR20] = 2000,
	};

	return max_link_rate[dp_max_link_rate];
}

static void dump_dp_max_link_rate(uint16_t version, uint8_t dp_max_link_rate)
{
	int link_rate;

	if (version >= 230)
		link_rate = parse_dp_max_link_rate_230(dp_max_link_rate);
	else
		link_rate = parse_dp_max_link_rate_216(dp_max_link_rate);

	if (link_rate == 0)
		printf("\t\tDP max link rate: <platform max> (0x%02x)\n",
		       dp_max_link_rate);
	else
		printf("\t\tDP max link rate: %g Gbps (0x%02x)\n",
		       link_rate / 100.0f, dp_max_link_rate);
}

static const char * const dp_vswing_str[] = {
	[0] = "0.4V",
	[1] = "0.6V",
	[2] = "0.8V",
	[3] = "1.2V",
};

static const char * const dp_preemph_str[] = {
	[0] = "0dB",
	[1] = "3.5dB",
	[2] = "6dB",
	[3] = "9.5dB",
};

static const char * const hdmi_frl_rate_str[] = {
	[0] = "FRL not supported",
	[1] = "3 GT/s",
	[2] = "6 GT/s",
	[3] = "8 GT/s",
	[4] = "10 GT/s",
	[5] = "12 GT/s",
};

static void dump_child_device(struct context *context,
			      const struct child_device_config *child)
{
	if (!child->device_type)
		return;

	printf("\tChild device info:\n");
	printf("\t\tDevice handle: 0x%04x (%s)\n", child->handle,
	       child_device_handle(context, child->handle));
	printf("\t\tDevice type: 0x%04x (%s)\n", child->device_type,
	       child_device_type(child->device_type));
	dump_child_device_type_bits(child->device_type);

	if (context->bdb->version < 152) {
		printf("\t\tSignature: %.*s\n", (int)sizeof(child->device_id), child->device_id);
	} else {
		printf("\t\tI2C speed: %s (0x%02x)\n",
		       to_str(i2c_speed_str, child->i2c_speed), child->i2c_speed);

		if (context->bdb->version >= 158) {
			printf("\t\tDP onboard redriver:\n");
			printf("\t\t\tpresent: %s\n",
			       YESNO((child->dp_onboard_redriver_present)));
			printf("\t\t\tvswing: %s (0x%x)\n",
			       to_str(dp_vswing_str, child->dp_onboard_redriver_vswing),
			       child->dp_onboard_redriver_vswing);
			printf("\t\t\tpre-emphasis: %s (0x%x)\n",
			       to_str(dp_preemph_str, child->dp_onboard_redriver_preemph),
			       child->dp_onboard_redriver_preemph);

			printf("\t\tDP ondock redriver:\n");
			printf("\t\t\tpresent: %s\n",
			       YESNO((child->dp_ondock_redriver_present)));
			printf("\t\t\tvswing: %s (0x%x)\n",
			       to_str(dp_vswing_str, child->dp_ondock_redriver_vswing),
			       child->dp_ondock_redriver_vswing);
			printf("\t\t\tpre-emphasis: %s (0x%x)\n",
			       to_str(dp_preemph_str, child->dp_ondock_redriver_preemph),
			       child->dp_ondock_redriver_preemph);
		}

		if (context->bdb->version >= 204)
			dump_hmdi_max_data_rate(child->hdmi_max_data_rate);
		if (context->bdb->version >= 169)
			printf("\t\tHDMI level shifter value: 0x%02x\n", child->hdmi_level_shifter_value);

		if (context->bdb->version >= 161)
			printf("\t\tOffset to DTD buffer for edidless CHILD: 0x%02x\n", child->dtd_buf_ptr);

		if (context->bdb->version >= 251)
			printf("\t\tDisable compression for external DP/HDMI: %s\n",
			       YESNO(child->disable_compression_for_ext_disp));
		if (context->bdb->version >= 235)
			printf("\t\tLTTPR Mode: %stransparent\n",
			       child->lttpr_non_transparent ? "non-" : "");
		if (context->bdb->version >= 202)
			printf("\t\tDual pipe ganged eDP: %s\n", YESNO(child->ganged_edp));
		if (context->bdb->version >= 198) {
			printf("\t\tCompression method CPS: %s\n", YESNO(child->compression_method_cps));
			printf("\t\tCompression enable: %s\n", YESNO(child->compression_enable));
		}
		if (context->bdb->version >= 161)
			printf("\t\tEdidless EFP: %s\n", YESNO(child->edidless_efp));

		if (context->bdb->version >= 198)
			printf("\t\tCompression structure index: %d\n", child->compression_structure_index);

		if (context->bdb->version >= 237) {
			printf("\t\tHDMI Max FRL rate valid: %s\n",
			       YESNO(child->hdmi_max_frl_rate_valid));
			printf("\t\tHDMI Max FRL rate: %s (0x%x)\n",
			       to_str(hdmi_frl_rate_str, child->hdmi_max_frl_rate),
			       child->hdmi_max_frl_rate);
		}
	}

	printf("\t\tAIM offset: %d\n", child->addin_offset);
	printf("\t\tDVO Port: %s (0x%02x)\n",
	       dvo_port(child->dvo_port), child->dvo_port);

	printf("\t\tAIM I2C pin: 0x%02x\n", child->i2c_pin);
	printf("\t\tAIM Target address: 0x%02x\n", child->target_addr);
	printf("\t\tDDC pin: 0x%02x\n", child->ddc_pin);
	printf("\t\tEDID buffer ptr: 0x%02x\n", child->edid_ptr);
	printf("\t\tDVO config: 0x%02x\n", child->dvo_cfg);

	if (context->bdb->version < 155) {
		printf("\t\tDVO2 Port: 0x%02x (%s)\n", child->dvo2_port, dvo_port(child->dvo2_port));
		printf("\t\tI2C2 pin: 0x%02x\n", child->i2c2_pin);
		printf("\t\tTarget2 address: 0x%02x\n", child->target2_addr);
		printf("\t\tDDC2 pin: 0x%02x\n", child->ddc2_pin);
	} else {
		if (context->bdb->version >= 244)
			printf("\t\teDP/DP max lane count: X%d\n", child->dp_max_lane_count + 1);
		if (context->bdb->version >= 218)
			printf("\t\tUse VBT vswing/premph table: %s\n", YESNO(child->use_vbt_vswing));
		if (context->bdb->version >= 196) {
			printf("\t\tHPD sense invert: %s\n", YESNO(child->hpd_invert));
			printf("\t\tIboost enable: %s\n", YESNO(child->iboost));
		}
		if (context->bdb->version >= 192)
			printf("\t\tOnboard LSPCON: %s\n", YESNO(child->lspcon));
		if (context->bdb->version >= 184)
			printf("\t\tLane reversal: %s\n", YESNO(child->lane_reversal));
		if (context->bdb->version >= 158)
			printf("\t\tEFP routed through dock: %s\n", YESNO(child->efp_routed));

		if (context->bdb->version >= 158) {
			printf("\t\tTMDS compatible? %s\n", YESNO(child->tmds_support));
			printf("\t\tDP compatible? %s\n", YESNO(child->dp_support));
			printf("\t\tHDMI compatible? %s\n", YESNO(child->hdmi_support));
		}

		printf("\t\tAux channel: %s (0x%02x)\n",
		       aux_ch(child->aux_channel), child->aux_channel);

		printf("\t\tDongle detect: 0x%02x\n", child->dongle_detect);
	}

	printf("\t\tIntegrated encoder instead of SDVO: %s\n", YESNO(child->integrated_encoder));
	printf("\t\tHotplug connect status: 0x%02x\n", child->hpd_status);
	printf("\t\tSDVO stall signal available: %s\n", YESNO(child->sdvo_stall));
	printf("\t\tPipe capabilities: 0x%02x\n", child->pipe_cap);

	printf("\t\tDVO wiring: 0x%02x\n", child->dvo_wiring);

	if (context->bdb->version < 171) {
		printf("\t\tDVO2 wiring: 0x%02x\n", child->dvo2_wiring);
	} else {
		printf("\t\tMIPI bridge type: %02x (%s)\n", child->mipi_bridge_type,
		       to_str(mipi_bridge_type_str, child->mipi_bridge_type));
	}

	printf("\t\tDevice class extension: 0x%02x\n", child->extended_type);

	printf("\t\tDVO function: 0x%02x\n", child->dvo_function);

	if (context->bdb->version >= 209) {
		printf("\t\tDP port trace length: 0x%x\n", child->dp_port_trace_length);
		printf("\t\tThunderbolt port: %s\n", YESNO(child->tbt));
	}
	if (context->bdb->version >= 195)
		printf("\t\tDP USB type C support: %s\n", YESNO(child->dp_usb_type_c));

	if (context->bdb->version >= 195) {
		printf("\t\t2X DP GPIO index: 0x%02x\n", child->dp_gpio_index);
		printf("\t\t2X DP GPIO pin number: 0x%02x\n", child->dp_gpio_pin_num);
	}

	if (context->bdb->version >= 196) {
		printf("\t\tIBoost level for DP/eDP: 0x%02x\n", child->dp_iboost_level);
		printf("\t\tIBoost level for HDMI: 0x%02x\n", child->hdmi_iboost_level);
	}

	if (context->bdb->version >= 216)
		dump_dp_max_link_rate(context->bdb->version,
				      child->dp_max_link_rate);

	if (context->bdb->version >= 256)
		printf("\t\tEFP panel index: %d\n", child->efp_index);
}

static void dump_child_devices(struct context *context, const uint8_t *devices,
			       uint8_t child_dev_num, uint8_t child_dev_size)
{
	struct child_device_config *child;
	int i;

	/*
	 * Use a temp buffer so dump_child_device() doesn't have to worry about
	 * accessing the struct beyond child_dev_size. The tail, if any, remains
	 * initialized to zero.
	 */
	child = calloc(1, sizeof(*child));
	igt_assert(child);

	for (i = 0; i < child_dev_num; i++) {
		memcpy(child, devices + i * child_dev_size,
		       min_t(child_dev_size, sizeof(*child), child_dev_size));

		dump_child_device(context, child);
	}

	free(child);
}

static void dump_general_definitions(struct context *context,
				     const struct bdb_block *block)
{
	const struct bdb_general_definitions *defs = block_data(block);
	int child_dev_num;

	printf("\tCRT DDC GMBUS addr: 0x%02x\n", defs->crt_ddc_gmbus_pin);
	printf("\tUse DPMS on AIM devices: %s\n", YESNO(defs->dpms_aim));
	printf("\tSkip CRT detect at boot: %s\n",
	       YESNO(defs->skip_boot_crt_detect));
	printf("\tUse Non ACPI DPMS CRT power states: %s\n",
	       YESNO(defs->dpms_non_acpi));
	printf("\tBoot display type: 0x%02x%02x\n", defs->boot_display[1],
	       defs->boot_display[0]);
	printf("\tChild device size: %d\n", defs->child_dev_size);

	if (!defs->child_dev_size)
		return;

	child_dev_num = (block->size - sizeof(*defs)) / defs->child_dev_size;
	printf("\tChild device count: %d\n", child_dev_num);

	dump_child_devices(context, defs->devices,
			   child_dev_num, defs->child_dev_size);
}

static void dump_display_toggle(struct context *context,
				const struct bdb_block *block)
{
	const struct bdb_display_toggle *t = block_data(block);

	printf("\tFeature bits: 0x%02x\n", t->feature_bits);
	printf("\tNum entries: %d\n", t->num_entries);

	for (int i = 0; i < t->num_entries; i++)
		printf("\tToggle list #%d: %s (0x%04x)\n",
		       i+1, child_device_handle(context, t->list[i]),
		       t->list[i]);
}

static void dump_mode_support_list(struct context *context,
				   const struct bdb_block *block)
{
	const struct bdb_mode_support_list *l =
		block_data(block) + block->size - sizeof(*l);
	const uint8_t *mode_number = block_data(block);

	printf("\tIntel mode numbers:\n");
	for (int i = 0; i < l->mode_list_length; i++)
		printf("\t\t0x%02x\n", mode_number[i]);

	printf("\tMode list length: %d\n", l->mode_list_length);
}

static void dump_generic_mode_table_base(const struct generic_mode_table *t)
{
	printf("\tResolution: %dx%d\n", t->x_res, t->y_res);
	printf("\tColor depths: 0x%02x\n", t->color_depths);
	printf("\tRefresh rates: %d %d %d Hz\n",
	       t->refresh_rate[0], t->refresh_rate[1], t->refresh_rate[2]);
	printf("\tReserved: 0x%02x\n", t->reserved);
	printf("\tText columns: %d\n", t->text_cols);
	printf("\tText rows: %d\n", t->text_rows);
	printf("\tFont height: %d\n", t->font_height);
	printf("\tPage size: 0x%04x\n", t->page_size);
	printf("\tMisc: 0x%02x\n", t->misc);
}

static void dump_generic_mode_timings(const struct generic_mode_timings *t)
{
	printf("\t\tDotclock: %d kHz\n", t->dotclock_khz);
	printf("\t\tHorizontal active: %d\n", t->hdisplay+1);
	printf("\t\tHorizontal total: %d\n", t->htotal+1);
	printf("\t\tHorizontal blank start: %d\n", t->hblank_start+1);
	printf("\t\tHorizontal blank end: %d\n", t->hblank_end+1);
	printf("\t\tHorizontal sync start: %d\n", t->hsync_start+1);
	printf("\t\tHorizontal sync end: %d\n", t->hsync_end+1);
	printf("\t\tVertical active: %d\n", t->vdisplay+1);
	printf("\t\tVertical total: %d\n", t->vtotal+1);
	printf("\t\tVertical blank start: %d\n", t->vblank_start+1);
	printf("\t\tVertical blank end: %d\n", t->vblank_end+1);
	printf("\t\tVertical sync start: %d\n", t->vsync_start+1);
	printf("\t\tVertical sync end: %d\n", t->vsync_end+1);
}

static void dump_generic_mode_table_alm(const struct bdb_block *block)
{
	const struct bdb_generic_mode_table_alm *t = block_data(block);

	dump_generic_mode_table_base(&t->table);

	for (int i = 0; i < ARRAY_SIZE(t->timings); i++) {
		printf("\t#%d timings:\n", i+1);
		dump_generic_mode_timings(&t->timings[i].timings);
		printf("\t\tWatermark for 8 bpp: %d SW\n", t->timings[i].wm_8bpp);
		printf("\t\tBurst length for 8 bpp: %d SW\n", 4*(t->timings[i].burst_8bpp+1));
		printf("\t\tWatermark for 16 bpp: %d SW\n", t->timings[i].wm_16bpp+1);
		printf("\t\tBurst length for 16 bpp: %d SW\n", 4*(t->timings[i].burst_16bpp+1));
		printf("\t\tWatermark for 32 bpp: %d SW\n", t->timings[i].wm_32bpp+1);
		printf("\t\tBurst length for 32 bpp: %d SW\n", 4*(t->timings[i].burst_32bpp+1));
	}
}

static void dump_generic_mode_table_mgm(const struct bdb_block *block)
{
	const struct bdb_generic_mode_table_mgm *t = block_data(block);

	printf("\tMode flag: 0x%04x\n", t->mode_flag);

	dump_generic_mode_table_base(&t->table);

	for (int i = 0; i < ARRAY_SIZE(t->timings); i++) {
		printf("\t#%d timings:\n", i+1);
		dump_generic_mode_timings(&t->timings[i]);
	}
}

static void dump_generic_mode_table(struct context *context,
				    const struct bdb_block *block)
{
	/*
	 * FIXME ALM/105 is showing one layout, MGM/108
	 * another. Not sure there is actual version
	 * based cutoff.
	 */
	if (context->bdb->version >= 106)
		dump_generic_mode_table_mgm(block);
	else
		dump_generic_mode_table_alm(block);
}

static void dump_reg_table(struct context *context,
			   const struct bdb_block *block)
{
	const struct bdb_reg_table *t = block_data(block);
	const void *data = (const void *)t + sizeof(*t);
	const void *end = (const void *)t + block->size - 2;

	printf("\tTable Id: 0x%0x\n", t->table_id);
	printf("\tData access size: 0x%02x\n", t->data_access_size);

	switch (t->data_access_size) {
	case 0xce:
		for (; data < end; data += 2 * 1) {
			const uint8_t *entry = data;

			printf("\t\t0x%02x: 0x%02x\n", entry[0], entry[1]);
		}
		break;
	case 0x02:
		for (; data < end; data += 2 * 4) {
			const uint32_t *entry = data;

			printf("\t\t0x%08x: 0x%08x\n", entry[0], entry[1]);
		}
		break;
	default:
		printf("\t\tUnknown data access size\n");
		return;
	}

	printf("\tTable end marker: 0x%04x\n",
	       *(const uint16_t *)end);
}

static void dump_mode_removal_table(struct context *context,
				    const struct bdb_block *block)
{
	const struct bdb_mode_removal *r = block_data(block);
	int num_entries = (block->size - sizeof(*r) - 2) / r->row_size;

	printf("\tNum entries: %d\n", num_entries);
	printf("\tRow size: %d\n", r->row_size);

	for (int i = 0; i < num_entries; i++) {
		const struct mode_removal_table *mode =
			(const void*)&r->modes[0] + i * r->row_size;

		printf("\tEntry #%d:\n", i + 1);
		printf("\t\tResolution: %dx%d\n", mode->x_res, mode->y_res);
		printf("\t\tBits per pixel: 0x%02x\n", mode->bpp);
		printf("\t\tRefresh rate: 0x%04x\n", mode->refresh_rate);
		printf("\t\tRemoval flags: 0x%02x\n", mode->removal_flags);

		if (r->row_size >= 10)
			printf("\t\tPanel flags: 0x%04x\n", mode->panel_flags);
	}

	printf("\tTerminator: 0x%04x\n",
	       *(const u16*)(block_data(block) + block->size - 2));
}

static void dump_legacy_child_devices(struct context *context,
				      const struct bdb_block *block)
{
	const struct bdb_legacy_child_devices *defs = block_data(block);
	int child_dev_num;

	printf("\tChild device size: %d\n", defs->child_dev_size);

	if (!defs->child_dev_size)
		return;

	child_dev_num = (block->size - sizeof(*defs)) / defs->child_dev_size;
	printf("\tChild device count: %d\n", child_dev_num);

	dump_child_devices(context, defs->devices,
			   child_dev_num, defs->child_dev_size);
}

static const char * const channel_type_str[] = {
	[0] = "automatic",
	[1] = "single",
	[2] = "dual",
	[3] = "reserved",
};

static const char * const dps_type_str[] = {
	[0] = "static DRRS",
	[1] = "D2PO",
	[2] = "seamless DRRS",
	[3] = "reserved",
};

static const char * const blt_type_str[] = {
	[0] = "default",
	[1] = "CCFL",
	[2] = "LED",
	[3] = "reserved",
};

static const char * const pos_type_str[] = {
	[0] = "inside shell",
	[1] = "outside shell",
	[2] = "reserved",
	[3] = "reserved",
};

static void dump_lfp_options(struct context *context,
			     const struct bdb_block *block)
{
	const struct bdb_lfp_options *options = block_data(block);

	printf("\tPanel type: %d\n", options->panel_type);
	if (context->bdb->version >= 212)
		printf("\tPanel type 2: %d\n", options->panel_type2);
	printf("\tLVDS EDID available: %s\n", YESNO(options->lvds_edid));
	printf("\tPixel dither: %s\n", YESNO(options->pixel_dither));
	printf("\tPFIT auto ratio: %s\n", YESNO(options->pfit_ratio_auto));
	printf("\tPFIT enhanced graphics mode: %s\n",
	       YESNO(options->pfit_gfx_mode_enhanced));
	printf("\tPFIT enhanced text mode: %s\n",
	       YESNO(options->pfit_text_mode_enhanced));
	printf("\tPFIT mode: %d\n", options->pfit_mode);

	if (block->size < 14)
		return;

	for (int i = 0; i < 16; i++) {
		unsigned int val;

		if (!dump_panel(context, i))
			continue;

		printf("\tPanel %d%s\n", i, panel_str(context, i));

		val = panel_bits(options->lvds_panel_channel_bits, i, 2);
		printf("\t\tChannel type: %s (0x%x)\n",
		       to_str(channel_type_str, val), val);

		printf("\t\tSSC: %s\n",
		       YESNO(panel_bool(options->ssc_bits, i)));

		val = panel_bool(options->ssc_freq, i);
		printf("\t\tSSC frequency: %d MHz (0x%x)\n",
		       decode_ssc_freq(context, val), val);

		printf("\t\tDisable SSC in dual display twin: %s\n",
		       YESNO(panel_bool(options->ssc_ddt, i)));

		if (block->size < 16)
			continue;

		val = panel_bool(options->panel_color_depth, i);
		printf("\t\tPanel color depth: %d (0x%x)\n",
		       val ? 24 : 18, val);

		if (block->size < 24)
			continue;

		val = panel_bits(options->dps_panel_type_bits, i, 2);
		printf("\t\tDPS type: %s (0x%x)\n",
		       to_str(dps_type_str, val), val);

		val = panel_bits(options->blt_control_type_bits, i, 2);
		printf("\t\tBacklight type: %s (0x%x)\n",
		       to_str(blt_type_str, val), val);

		if (context->bdb->version < 200)
			continue;

		printf("\t\tLCDVCC on during S0 state: %s\n",
		       YESNO(panel_bool(options->lcdvcc_s0_enable, i)));

		if (context->bdb->version < 228)
			continue;

		val = panel_bits((options->rotation), i, 2);
		printf("\t\tPanel rotation: %d degrees (0x%x)\n",
		       val * 90, val);

		if (context->bdb->version < 240)
			continue;

		val = panel_bits((options->position), i, 2);
		printf("\t\tPanel position: %s (0x%x)\n",
		       to_str(pos_type_str, val), val);
	}
}

static void dump_lfp_data_ptrs(struct context *context,
			       const struct bdb_block *block)
{
	const struct bdb_lfp_data_ptrs *ptrs = block_data(block);

	printf("\tNumber of entries: %d\n", ptrs->num_entries);

	for (int i = 0; i < 16; i++) {
		if (!dump_panel(context, i))
			continue;

		printf("\tPanel %d%s\n", i, panel_str(context, i));

		if (ptrs->num_entries >= 1) {
			printf("\t\tFP timing offset: %d\n",
			       ptrs->ptr[i].fp_timing.offset);
			printf("\t\tFP timing table size: %d\n",
			       ptrs->ptr[i].fp_timing.table_size);
		}
		if (ptrs->num_entries >= 2) {
			printf("\t\tDVO timing offset: %d\n",
			       ptrs->ptr[i].dvo_timing.offset);
			printf("\t\tDVO timing table size: %d\n",
			       ptrs->ptr[i].dvo_timing.table_size);
		}
		if (ptrs->num_entries >= 3) {
			printf("\t\tPanel PnP ID offset: %d\n",
			       ptrs->ptr[i].panel_pnp_id.offset);
			printf("\t\tPanel PnP ID table size: %d\n",
			       ptrs->ptr[i].panel_pnp_id.table_size);
		}
	}

	if (ptrs->panel_name.table_size) {
		printf("\tPanel name offset: %d\n",
		       ptrs->panel_name.offset);
		printf("\tPanel name table size: %d\n",
		       ptrs->panel_name.table_size);
	}
}

static void
print_detail_timing_data(const struct bdb_edid_dtd *dvo_timing)
{
	int display, sync_start, sync_end, total;

	display = (dvo_timing->hactive_hi << 8) | dvo_timing->hactive_lo;
	sync_start = display +
		((dvo_timing->hsync_off_hi << 8) | dvo_timing->hsync_off_lo);
	sync_end = sync_start + ((dvo_timing->hsync_pulse_width_hi << 8) |
				 dvo_timing->hsync_pulse_width_lo);
	total = display +
		((dvo_timing->hblank_hi << 8) | dvo_timing->hblank_lo);
	printf("\t\t  hdisplay: %d\n", display);
	printf("\t\t  hsync [%d, %d] %s\n", sync_start, sync_end,
	       dvo_timing->hsync_positive ? "+sync" : "-sync");
	printf("\t\t  htotal: %d\n", total);

	display = (dvo_timing->vactive_hi << 8) | dvo_timing->vactive_lo;
	sync_start = display + ((dvo_timing->vsync_off_hi << 8) |
				dvo_timing->vsync_off_lo);
	sync_end = sync_start + ((dvo_timing->vsync_pulse_width_hi << 8) |
				 dvo_timing->vsync_pulse_width_lo);
	total = display +
		((dvo_timing->vblank_hi << 8) | dvo_timing->vblank_lo);
	printf("\t\t  vdisplay: %d\n", display);
	printf("\t\t  vsync [%d, %d] %s\n", sync_start, sync_end,
	       dvo_timing->vsync_positive ? "+sync" : "-sync");
	printf("\t\t  vtotal: %d\n", total);

	printf("\t\t  clock: %d\n", dvo_timing->clock * 10);
}

static char *decode_pnp_id(u16 mfg_name, char str[4])
{
	mfg_name = ntohs(mfg_name);

	str[0] = '@' + ((mfg_name >> 10) & 0x1f);
	str[1] = '@' + ((mfg_name >> 5) & 0x1f);
	str[2] = '@' + ((mfg_name >> 0) & 0x1f);
	str[3] = '\0';

	return str;
}

static void dump_pnp_id(const struct bdb_edid_pnp_id *pnp_id)
{
	char mfg[4];

	printf("\t\t  Mfg name: %s (0x%x)\n",
	       decode_pnp_id(pnp_id->mfg_name, mfg), pnp_id->mfg_name);
	printf("\t\t  Product code: %u\n", pnp_id->product_code);
	printf("\t\t  Serial: %u\n", pnp_id->serial);
	printf("\t\t  Mfg week: %d\n", pnp_id->mfg_week);
	printf("\t\t  Mfg year: %d\n", 1990 + pnp_id->mfg_year);
}

static void dump_lfp_data(struct context *context,
			  const struct bdb_block *block)
{
	struct bdb_block *ptrs_block;
	const struct bdb_lfp_data_ptrs *ptrs;
	int i;

	ptrs_block = find_section(context, BDB_LFP_DATA_PTRS);
	if (!ptrs_block)
		return;

	ptrs = block_data(ptrs_block);

	for (i = 0; i < 16; i++) {
		const struct fp_timing *fp_timing =
			block_data(block) + ptrs->ptr[i].fp_timing.offset;
		const struct bdb_edid_dtd *dvo_timing =
			block_data(block) + ptrs->ptr[i].dvo_timing.offset;
		const struct bdb_edid_pnp_id *pnp_id =
			block_data(block) + ptrs->ptr[i].panel_pnp_id.offset;
		const struct bdb_lfp_data_tail *tail =
			block_data(block) + ptrs->panel_name.offset;

		if (!dump_panel(context, i))
			continue;

		printf("\tPanel %d%s\n", i, panel_str(context, i));
		printf("\t\tResolution: %dx%d\n",
		       fp_timing->x_res, fp_timing->y_res);
		printf("\t\tFP timing data:\n");
		printf("\t\t  LVDS: 0x%08lx\n",
		       (unsigned long)fp_timing->lvds_reg_val);
		printf("\t\t  PP_ON_DELAYS: 0x%08lx\n",
		       (unsigned long)fp_timing->pp_on_reg_val);
		printf("\t\t  PP_OFF_DELAYS: 0x%08lx\n",
		       (unsigned long)fp_timing->pp_off_reg_val);
		printf("\t\t  PP_DIVISOR: 0x%08lx\n",
		       (unsigned long)fp_timing->pp_cycle_reg_val);
		printf("\t\t  PFIT: 0x%08lx\n",
		       (unsigned long)fp_timing->pfit_reg_val);

		printf("\t\tDVO timing:\n");
		print_detail_timing_data(dvo_timing);

		printf("\t\tPnP ID:\n");
		dump_pnp_id(pnp_id);

		if (!ptrs->panel_name.table_size)
			continue;

		printf("\t\tPanel name: %.*s\n",
		       (int)sizeof(tail->panel_name[0].name), tail->panel_name[i].name);

		if (context->bdb->version < 187)
			continue;

		printf("\t\tScaling enable: %s\n",
		       YESNO(panel_bool(tail->scaling_enable, i)));

		if (context->bdb->version < 188)
			continue;

		printf("\t\tSeamless DRRS min refresh rate: %d\n",
		       tail->seamless_drrs_min_refresh_rate[i]);

		if (context->bdb->version < 208)
			continue;

		printf("\t\tPixel overlap count: %d\n",
		       tail->pixel_overlap_count[i]);

		if (context->bdb->version < 227)
			continue;

		printf("\t\tBlack border:\n");
		printf("\t\t  Top: %d\n", tail->black_border[i].top);
		printf("\t\t  Bottom: %d\n", tail->black_border[i].top);
		printf("\t\t  Left: %d\n", tail->black_border[i].left);
		printf("\t\t  Right: %d\n", tail->black_border[i].right);

		if (context->bdb->version < 231)
			continue;

		printf("\t\tDual LFP port sync enable: %s\n",
		       YESNO(panel_bool(tail->dual_lfp_port_sync_enable, i)));

		if (context->bdb->version < 245)
			continue;

		printf("\t\tGPU dithering for banding artifacts: %s\n",
		       YESNO(panel_bool(tail->gpu_dithering_for_banding_artifacts, i)));
	}

	free(ptrs_block);
}

static const char * const lvds_config_str[] = {
	[BDB_DRIVER_FEATURE_NO_LVDS] = "No LVDS",
	[BDB_DRIVER_FEATURE_INT_LVDS] = "Integrated LVDS",
	[BDB_DRIVER_FEATURE_SDVO_LVDS] = "SDVO LVDS",
	[BDB_DRIVER_FEATURE_INT_SDVO_LVDS] = "Embedded DisplayPort",
};

static const char *default_algorithm(bool algorithm)
{
	return algorithm ? "driver default" : "OS default";
}

static void dump_driver_feature(struct context *context,
				const struct bdb_block *block)
{
	const struct bdb_driver_features *feature = block_data(block);

	printf("\tUse 00000110h ID for Primary LFP: %s\n",
	       YESNO(feature->primary_lfp_id));
	printf("\tEnable Sprite in Clone Mode: %s\n",
	       YESNO(feature->sprite_in_clone));
	printf("\tDriver INT 15h hook: %s\n",
	       YESNO(feature->int15h_hook));
	printf("\tDual View Zoom: %s\n",
	       YESNO(feature->dual_view_zoom));
	printf("\tHot Plug DVO: %s\n",
	       YESNO(feature->hotplug_dvo));
	printf("\tAllow display switching when in Full Screen DOS: %s\n",
	       YESNO(feature->allow_display_switch_dos));
	printf("\tAllow display switching when DVD active: %s\n",
	       YESNO(feature->allow_display_switch_dvd));
	printf("\tBoot Device Algorithm: %s\n",
	       default_algorithm(feature->boot_dev_algorithm));

	printf("\tBoot Mode X: %u\n", feature->boot_mode_x);
	printf("\tBoot Mode Y: %u\n", feature->boot_mode_y);
	printf("\tBoot Mode Bpp: %u\n", feature->boot_mode_bpp);
	printf("\tBoot Mode Refresh: %u\n", feature->boot_mode_refresh);

	printf("\tEnable LFP as primary: %s\n",
	       YESNO(feature->enable_lfp_primary));
	printf("\tSelective Mode Pruning: %s\n",
	       YESNO(feature->selective_mode_pruning));
	printf("\tDual-Frequency Graphics Technology: %s\n",
	       YESNO(feature->dual_frequency));
	printf("\tDefault Render Clock Frequency: %s\n",
	       feature->render_clock_freq ? "low" : "high");
	printf("\tNT 4.0 Dual Display Clone Support: %s\n",
	       YESNO(feature->nt_clone_support));
	printf("\tDefault Power Scheme user interface: %s\n",
	       feature->power_scheme_ui ? "3rd party" : "CUI");
	printf("\tSprite Display Assignment when Overlay is Active in Clone Mode: %s\n",
	       feature->sprite_display_assign ? "primary" : "secondary");
	printf("\tDisplay Maintain Aspect Scaling via CUI: %s\n",
	       YESNO(feature->cui_aspect_scaling));
	printf("\tPreserve Aspect Ratio: %s\n",
	       YESNO(feature->preserve_aspect_ratio));
	printf("\tEnable SDVO device power down: %s\n",
	       YESNO(feature->sdvo_device_power_down));
	printf("\tCRT hotplug: %s\n", YESNO(feature->crt_hotplug));

	printf("\tLVDS config: %s (0x%x)\n",
	       to_str(lvds_config_str, feature->lvds_config), feature->lvds_config);
	printf("\tTV hotplug: %s\n",
	       YESNO(feature->tv_hotplug));

	printf("\tDisplay subsystem enable: %s\n",
	       YESNO(feature->display_subsystem_enable));
	printf("\tEmbedded platform: %s\n",
	       YESNO(feature->embedded_platform));
	printf("\tDefine Display statically: %s\n",
	       YESNO(feature->static_display));

	printf("\tLegacy CRT max X: %d\n", feature->legacy_crt_max_x);
	printf("\tLegacy CRT max Y: %d\n", feature->legacy_crt_max_y);
	printf("\tLegacy CRT max refresh: %d\n",
	       feature->legacy_crt_max_refresh);

	printf("\tInternal source termination for HDMI: %s\n",
	       YESNO(feature->hdmi_termination));
	printf("\tCEA 861-D HDMI support: %s\n",
	       YESNO(feature->cea861d_hdmi_support));
	printf("\tSelf refresh enable: %s\n",
	       YESNO(feature->self_refresh_enable));

	printf("\tCustom VBT number: 0x%x\n", feature->custom_vbt_version);

	printf("\tPC Features field validity: %s\n",
	       YESNO(feature->pc_feature_valid));
	printf("\tHpd Wake: %s\n",
	       YESNO(feature->hpd_wake));
	printf("\tAssertive Display Technology (ADT): %s\n",
	       YESNO(feature->adt_enabled));
	printf("\tDynamic Media Refresh Rate Switching (DMRRS): %s\n",
	       YESNO(feature->dmrrs_enabled));
	printf("\tDynamic Frames Per Second (DFPS): %s\n",
	       YESNO(feature->dfps_enabled));
	printf("\tIntermediate Pixel Storage (IPS): %s\n",
	       YESNO(feature->ips_enabled));
	printf("\tPanel Self Refresh (PSR): %s\n",
	       YESNO(feature->psr_enabled));
	printf("\tTurbo Boost Technology: %s\n",
	       YESNO(feature->tbt_enabled));
	printf("\tGraphics Power Management (GPMT): %s\n",
	       YESNO(feature->gpmt_enabled));
	printf("\tGraphics Render Standby (RS): %s\n",
	       YESNO(feature->grs_enabled));
	printf("\tDynamic Refresh Rate Switching (DRRS): %s\n",
	       YESNO(feature->drrs_enabled));
	printf("\tAutomatic Display Brightness (ADB): %s\n",
	       YESNO(feature->adb_enabled));
	printf("\tDxgkDDI Backlight Control (DxgkDdiBLC): %s\n",
	       YESNO(feature->bltclt_enabled));
	printf("\tDisplay Power Saving Technology (DPST): %s\n",
	       YESNO(feature->dpst_enabled));
	printf("\tSmart 2D Display Technology (S2DDT): %s\n",
	       YESNO(feature->s2ddt_enabled));
	printf("\tRapid Memory Power Management (RMPM): %s\n",
	       YESNO(feature->rmpm_enabled));
}

static void dump_driver_persistence(struct context *context,
				    const struct bdb_block *block)
{
	const struct bdb_driver_persistence *persistence = block_data(block);

	printf("\tDocking persistent algorithm: %s\n",
	       default_algorithm(persistence->docking_persistent_algorithm));
	printf("\tDVO hotplug persistent on mode: %s\n",
	       YESNO(persistence->dvo_hotplug_persistent_on_mode));
	printf("\tEDID persistent on mode: %s\n",
	       YESNO(persistence->edid_persistent_on_mode));
	printf("\tHotkey persistent on mode: %s\n",
	       YESNO(persistence->hotkey_persistent_on_mode));
	printf("\tHotkey persistent on restore pipe: %s\n",
	       YESNO(persistence->hotkey_persistent_on_restore_pipe));
	printf("\tHotkey persistent on refresh rate: %s\n",
	       YESNO(persistence->hotkey_persistent_on_refresh_rate));
	printf("\tHotkey persistent on MDS/Twin: %s\n",
	       YESNO(persistence->hotkey_persistent_on_mds_twin));
	printf("\tPower management persistent algorithm: %s\n",
	       default_algorithm(persistence->power_management_persistent_algorithm));
	printf("\tLid switch persistent algorithm: %s\n",
	       default_algorithm(persistence->lid_switch_persistent_algorithm));
	printf("\tHotkey persisentt algorithm: %s\n",
	       default_algorithm(persistence->hotkey_persistent_algorithm));
	printf("\tPersistent max config: %d\n", persistence->persistent_max_config);
}

static void dump_dot_clock_override_entry_gen2(const struct dot_clock_override_entry_gen2 *t,
					       bool is_lvds)
{
	int ref = 48000;
	int m1 = t->m1 + 2;
	int m2 = t->m2 + 2;
	int m = 5 * m1 + m2;
	int n = t->n + 2;
	int p1, p2, p;

	if (is_lvds) {
		p1 = igt_fls((unsigned int)t->p1);
		p2 = 14;
	} else {
		p1 = t->p1_div_by_2 ? 2 : (t->p1 + 2);
		p2 = t->p2_div_by_4 ? 4 : 2;
	}

	p = p1 * p2;

	printf("\t\t\tDotclock: %d kHz\n", t->dotclock);

	if (!t->dotclock)
		return;

	printf("\t\t\tCalculated dotclock: %d kHz\n",
	       n && p ? DIV_ROUND_CLOSEST(ref * m, n * p) : 0);
	printf("\t\t\tN: %d\n", n);
	printf("\t\t\tM1: %d\n", m1);
	printf("\t\t\tM2: %d\n", m2);
	printf("\t\t\tM: %d\n", m);
	printf("\t\t\tP1: %d\n", p1);
	printf("\t\t\tP2: %d\n", p2);
	printf("\t\t\tP: %d\n", p);
}

static void dump_dot_clock_override_alm(struct context *context,
					const struct bdb_block *block)
{
	const struct bdb_dot_clock_override_alm *b = block_data(block);
	int count = block->size / sizeof(b->t[0]);

	for (int i = 0; i < count; i++) {
		const struct dot_clock_override_entry_gen2 *t = &b->t[i];

		printf("\t\tEntry #%d:\n", i + 1);
		dump_dot_clock_override_entry_gen2(t, false);
	}
}

static void dump_dot_clock_override_entry_gen3(const struct dot_clock_override_entry_gen3 *t)
{
	int ref = 96000;
	int m1 = t->m1 + 2;
	int m2 = t->m2 + 2;
	int m = 5 * m1 + m2;
	int n = t->n + 2;
	int p1 = t->p1;
	int p2 = t->p2;
	int p = p1 * p2;

	printf("\t\t\tDotclock: %d kHz\n", t->dotclock);

	if (!t->dotclock)
		return;

	printf("\t\t\tCalculated dotclock: %d kHz\n",
	       n && p ? DIV_ROUND_CLOSEST(ref * m, n * p) : 0);
	printf("\t\t\tN: %d\n", n);
	printf("\t\t\tM1: %d\n", m1);
	printf("\t\t\tM2: %d\n", m2);
	printf("\t\t\tP1: %d\n", p1);
	printf("\t\t\tP2: %d\n", p2);
}

static void _dump_dot_clock_override(const struct bdb_dot_clock_override *d,
				     int count, bool is_lvds)
{
	printf("\t\tRow size: %d\n", d->row_size);
	printf("\t\tNum rows: %d\n", d->num_rows);

	for (int i = 0; i < count; i++) {
		const struct dot_clock_override_entry_gen2 *t_gen2 =
			(const void *)d->table + i * d->row_size;
		const struct dot_clock_override_entry_gen3 *t_gen3 =
			(const void *)d->table + i * d->row_size;

		printf("\t\tEntry #%d:\n", i + 1);

		switch (d->row_size) {
		case 9:
			dump_dot_clock_override_entry_gen3(t_gen3);
			break;
		case 8:
			dump_dot_clock_override_entry_gen2(t_gen2, is_lvds);
			break;
		default:
			printf("\t\t\tDotclock: %d kHz\n", t_gen3->dotclock);
			break;
		}
	}
}

static void dump_dot_clock_override(struct context *context,
				    const struct bdb_block *block)
{
	const struct bdb_dot_clock_override *d = block_data(block);
	const void *start = d->table;
	const void *end = (const void *)d + block->size;
	int count;

	count = min((int)(end - start) / d->row_size, (int)d->num_rows);
	printf("\tNormal:\n");
	_dump_dot_clock_override(d, count, false);

	d = (const void *)d->table + count * d->row_size;
	if ((const void *)d + sizeof(*d) >= end)
		return;

	start = d->table;

	count = min((int)(end - start) / d->row_size, (int)d->num_rows);
	printf("\tLVDS:\n");
	_dump_dot_clock_override(d, count, true);
}

static void dump_display_select_old(struct context *context,
				    const struct bdb_block *block)
{
	const void *data = block_data(block);
	int offset = 0;

	for (int n = 0; n < 4; n++) {
		const struct toggle_list_table_old *t = data + offset;

		offset += sizeof(*t) + t->num_entries * t->entry_size;

		printf("\tToggle list #%d\n", n+1);

		printf("\t\tNum entries: %d\n", t->num_entries);
		printf("\t\tEntry size: %d\n\n", t->entry_size);

		if (sizeof(t->list[0]) != t->entry_size) {
			printf("\t\tstruct doesn't match (expected %zu, got %u), skipping\n",
			       sizeof(t->list[0]), t->entry_size);
			continue;
		}

		for (int i = 0; i < t->num_entries; i++) {
			printf("\t\tEntry #%d:\n", i + 1);
			printf("\t\t\tDisplay select pipe A: %s (0x%02x)\n",
			       child_device_handle(context, t->list[i].display_select_pipe_a),
			       t->list[i].display_select_pipe_a);
			printf("\t\t\tDisplay select pipe B: %s (0x%02x)\n",
			       child_device_handle(context, t->list[i].display_select_pipe_b),
			       t->list[i].display_select_pipe_b);
			printf("\t\t\tCapabilities: 0x%02x\n",
			       t->list[i].caps);
		}
	}
}

static void dump_display_select_ivb(struct context *context,
				    const struct bdb_block *block)
{
	const void *data = block_data(block);
	int offset = 0;

	for (int n = 0; n < 4; n++) {
		const struct toggle_list_table_ivb *t = data + offset;

		offset += sizeof(*t) + t->num_entries * t->entry_size;

		printf("\tToggle list #%d\n", n+1);

		printf("\t\tNum entries: %d\n", t->num_entries);
		printf("\t\tEntry size: %d\n\n", t->entry_size);

		if (sizeof(t->list[0]) != t->entry_size) {
			printf("\t\tstruct doesn't match (expected %zu, got %u), skipping\n",
			       sizeof(t->list[0]), t->entry_size);
			continue;
		}

		for (int i = 0; i < t->num_entries; i++) {
			printf("\t\tEntry #%d:\n", i + 1);
			printf("\t\t\tDisplay select: %s (0x%02x)\n",
			       child_device_handle(context, t->list[i].display_select),
			       t->list[i].display_select);
		}
	}
}

static void dump_display_select_hsw(struct context *context,
				    const struct bdb_block *block)
{
	const void *data = block_data(block);
	int offset = 0;

	for (int n = 0; n < 4; n++) {
		const struct toggle_list_table_hsw *t = data + offset;

		offset += sizeof(*t) + t->num_entries * t->entry_size;

		printf("\tToggle list #%d\n", n+1);

		printf("\t\tNum entries: %d\n", t->num_entries);
		printf("\t\tEntry size: %d\n\n", t->entry_size);

		if (sizeof(t->list[0]) != t->entry_size) {
			printf("\t\tstruct doesn't match (expected %zu, got %u), skipping\n",
			       sizeof(t->list[0]), t->entry_size);
			continue;
		}

		for (int i = 0; i < t->num_entries; i++) {
			printf("\t\tEntry #%d:\n", i + 1);
			printf("\t\t\tDisplay select: %s (0x%04x)\n",
			       child_device_handle(context, t->list[i].display_select),
			       t->list[i].display_select);
		}
	}
}

static void dump_display_remove_old(struct context *context,
				    const struct bdb_block *block)
{
	const struct bdb_display_remove_old *r = block_data(block);

	printf("\tNum entries: %d\n", r->num_entries);
	printf("\tEntry size: %d\n\n", r->entry_size);

	if (sizeof(r->table[0]) != r->entry_size) {
		printf("\t\tstruct doesn't match (expected %zu, got %u), skipping\n",
		       sizeof(r->table[0]), r->entry_size);
		return;
	}

	for (int i = 0; i < r->num_entries; i++) {
		printf("\tEntry #%d:\n", i + 1);

		printf("\t\t\tDisplay select pipe A: %s (0x%02x)\n",
		       child_device_handle(context, r->table[i].display_select_pipe_a),
		       r->table[i].display_select_pipe_a);
		printf("\t\t\tDisplay select pipe B: %s (0x%02x)\n",
		       child_device_handle(context, r->table[i].display_select_pipe_b),
		       r->table[i].display_select_pipe_b);
	}
}

static void dump_display_remove_ivb(struct context *context,
				    const struct bdb_block *block)
{
	const struct bdb_display_remove_ivb *r = block_data(block);

	printf("\tNum entries: %d\n", r->num_entries);
	printf("\tEntry size: %d\n\n", r->entry_size);

	if (sizeof(r->table[0]) != r->entry_size) {
		printf("\t\tstruct doesn't match (expected %zu, got %u), skipping\n",
		       sizeof(r->table[0]), r->entry_size);
		return;
	}

	for (int i = 0; i < r->num_entries; i++) {
		printf("\tEntry #%d:\n", i + 1);

		printf("\t\t\tDisplay select: %s (0x%02x)\n",
		       child_device_handle(context, r->table[i].display_select),
		       r->table[i].display_select);
	}
}

static void dump_display_remove_hsw(struct context *context,
				    const struct bdb_block *block)
{
	const struct bdb_display_remove_hsw *r = block_data(block);

	printf("\tNum entries: %d\n", r->num_entries);
	printf("\tEntry size: %d\n\n", r->entry_size);

	if (sizeof(r->table[0]) != r->entry_size) {
		printf("\t\tstruct doesn't match (expected %zu, got %u), skipping\n",
		       sizeof(r->table[0]), r->entry_size);
		return;
	}

	for (int i = 0; i < r->num_entries; i++) {
		printf("\tEntry #%d:\n", i + 1);

		printf("\t\t\tDisplay select: %s (0x%04x)\n",
		       child_device_handle(context, r->table[i].display_select),
		       r->table[i].display_select);
	}
}

static void dump_driver_rotation(struct context *context,
				 const struct bdb_block *block)
{
	const struct bdb_driver_rotation *rot = block_data(block);

	printf("\tRotation enable: %s (0x%x)\n", YESNO(rot->rotation_enable),
	       rot->rotation_enable);

	printf("\tRotation flags 1: 0x%02x\n", rot->rotation_flags_1);
	printf("\tRotation flags 2: 0x%04x\n", rot->rotation_flags_2);
	printf("\tRotation flags 3: 0x%08x\n", rot->rotation_flags_3);
	printf("\tRotation flags 4: 0x%08x\n", rot->rotation_flags_4);
}

static void dump_oem_custom(struct context *context,
			    const struct bdb_block *block)
{
	const struct bdb_oem_custom *oem = block_data(block);

	printf("\tNum entries: %d\n", oem->num_entries);
	printf("\tEntry size: %d\n", oem->entry_size);

	for (int i = 0; i < oem->num_entries; i++) {
		const struct oem_mode *m = (const void *)&oem->modes[0] +
			i * oem->entry_size;

		printf("\tEntry #%d:\n", i+1);
		printf("\t\tEnable in GOP: %s\n", YESNO(m->enable_in_gop));
		printf("\t\tEnable in OS: %s\n", YESNO(m->enable_in_os));
		printf("\t\tEnable in VBIOS: %s\n", YESNO(m->enable_in_vbios));
		printf("\t\tResolution: %dx%d\n", m->x_res, m->y_res);
		printf("\t\tDisplay flags: %s (0x%02x)\n",
		       child_device_handle(context, m->display_flags),
		       m->display_flags);
		printf("\t\tColor depth: 0x%02x\n", m->color_depth);
		printf("\t\tRefresh rate: %d\n", m->refresh_rate);

		printf("\t\tDTD:\n");
		print_detail_timing_data(&m->dtd);

		if (oem->entry_size >= 28)
			printf("\t\tDisplay flags 2: %s (0x%04x)\n",
			       child_device_handle(context, m->display_flags_2),
			       m->display_flags_2);
	}
}

static void dump_efp_list(struct context *context,
			  const struct bdb_block *block)
{
	const struct bdb_efp_list *list = block_data(block);

	printf("\tEntry size: %d\n", list->entry_size);
	printf("\tNum entries: %d\n", list->num_entries);

	if (sizeof(list->efp[0]) != list->entry_size) {
		printf("\tEFP struct sizes don't match (expected %zu, got %u), skipping\n",
		       sizeof(list->efp[0]), list->entry_size);
		return;
	}

	for (int i = 0; i < list->num_entries; i++) {
		char mfg[4];

		printf("\tEFP #%d:\n", i + 1);
		printf("\t\tMfg name: %s (0x%x)\n",
		       decode_pnp_id(list->efp[i].mfg_name, mfg),
		       list->efp[i].mfg_name);
		printf("\t\tProduct code: %u\n",
		       list->efp[i].product_code);
	}
}

static const char * const underscan_overscan_str[] = {
	[0] = "Neither",
	[1] = "Underscan/Overscan",
	[2] = "Overscan only",
	[3] = "Underscan only",
};

static void dump_tv_options(struct context *context,
			    const struct bdb_block *block)
{
	const struct bdb_tv_options *tv = block_data(block);

	printf("\tD connector support: %s\n",
	       YESNO(tv->d_connector_support));
	printf("\tAdd modes to avoid overscan issue: %s\n",
	       YESNO(tv->add_modes_to_avoid_overscan_issue));
	printf("\tUndescan/Overscan for HDTV via DVI: %s\n",
	       to_str(underscan_overscan_str, tv->underscan_overscan_hdtv_dvi));
	printf("\tUndescan/Overscan for HDTV via component: %s\n",
	       to_str(underscan_overscan_str, tv->underscan_overscan_hdtv_component));
}

static const char * const edp_bpp_str[] = {
	[EDP_18BPP] = "18 bpp",
	[EDP_24BPP] = "24 bpp",
	[EDP_30BPP] = "30 bpp",
};

static const char * const edp_rate_str[] = {
	[EDP_RATE_1_62] = "1.62Gbps",
	[EDP_RATE_2_7] = "2.7Gbps",
	[EDP_RATE_5_4] = "5.4Gbps",
};

static const char * const edp_preemph_str[] = {
	[0] = "Low power (200 mV)",
	[1] = "Default (400 mV)",
};

static void dump_edp(struct context *context,
		     const struct bdb_block *block)
{
	const struct bdb_edp *edp = block_data(block);
	int bpp, msa;
	int i;

	for (i = 0; i < 16; i++) {
		if (!dump_panel(context, i))
			continue;

		printf("\tPanel %d%s\n", i, panel_str(context, i));

		printf("\t\tPower Sequence: T1-T3 %d T8 %d T9 %d T10 %d T11-T12 %d\n",
		       edp->power_seqs[i].t1_t3,
		       edp->power_seqs[i].t8,
		       edp->power_seqs[i].t9,
		       edp->power_seqs[i].t10,
		       edp->power_seqs[i].t11_t12);

		bpp = panel_bits(edp->color_depth, i, 2);

		printf("\t\tPanel color depth: %s (0x%x)\n",
		       to_str(edp_bpp_str, bpp), bpp);

		msa = panel_bits(edp->sdrrs_msa_timing_delay, i, 2);
		printf("\t\teDP sDRRS MSA Delay: Lane %d\n", msa + 1);

		printf("\t\tFast link params:\n");
		printf("\t\t\trate: %s (0x%x)\n",
		       to_str(edp_rate_str, edp->fast_link_params[i].rate),
		       edp->fast_link_params[i].rate);

		printf("\t\t\tlanes: X%d\n",
		       edp->fast_link_params[i].lanes + 1);
		printf("\t\t\tpre-emphasis: %s (0x%x)\n",
		       to_str(dp_preemph_str, edp->fast_link_params[i].preemphasis),
		       edp->fast_link_params[i].preemphasis);
		printf("\t\t\tvswing: %s (0x%x)\n",
		       to_str(dp_vswing_str, edp->fast_link_params[i].vswing),
		       edp->fast_link_params[i].vswing);

		if (context->bdb->version >= 162)
			printf("\t\tStereo 3D feature: %s\n",
			       YESNO(panel_bool(edp->edp_s3d_feature, i)));

		if (context->bdb->version >= 165)
			printf("\t\tT3 optimization: %s\n",
			       YESNO(panel_bool(edp->edp_t3_optimization, i)));

		if (context->bdb->version >= 173) {
			int val = (edp->edp_vswing_preemph >> (i * 4)) & 0xf;

			printf("\t\tVswing/preemphasis table selection: %s (0x%x)\n",
			       to_str(edp_preemph_str, val), val);
		}

		if (context->bdb->version >= 182)
			printf("\t\tFast link training: %s\n",
			       YESNO(panel_bool(edp->fast_link_training, i)));

		if (context->bdb->version >= 185)
			printf("\t\tDPCD 600h write required: %s\n",
			       YESNO(panel_bool(edp->dpcd_600h_write_required, i)));

		if (context->bdb->version >= 186)
			printf("\t\tPWM delays:\n"
			       "\t\t\tPWM on to backlight enable: %d\n"
			       "\t\t\tBacklight disable to PWM off: %d\n",
			       edp->pwm_delays[i].pwm_on_to_backlight_enable,
			       edp->pwm_delays[i].backlight_disable_to_pwm_off);

		if (context->bdb->version >= 199) {
			printf("\t\tFull link params provided: %s\n",
			       YESNO(panel_bool(edp->full_link_params_provided, i)));

			printf("\t\tFull link params:\n");
			printf("\t\t\tpre-emphasis: %s (0x%x)\n",
			       to_str(dp_preemph_str, edp->full_link_params[i].preemphasis),
			       edp->full_link_params[i].preemphasis);
			printf("\t\t\tvswing: %s (0x%x)\n",
			       to_str(dp_vswing_str, edp->full_link_params[i].vswing),
			       edp->full_link_params[i].vswing);
		}

		if (context->bdb->version >= 224) {
			u16 rate = edp->edp_fast_link_training_rate[i];

			printf("\t\teDP fast link training data rate: %g Gbps (0x%02x)\n",
			       rate / 5000.0f, rate);
		}

		if (context->bdb->version >= 244) {
			u16 rate = edp->edp_max_port_link_rate[i];

			printf("\t\teDP max port link rate: %g Gbps (0x%02x)\n",
			       rate / 5000.0f, rate);
		}

		if (context->bdb->version >= 251)
			printf("\t\teDP DSC disable: %s\n",
			       YESNO(panel_bool(edp->edp_dsc_disable, i)));
	}
}

static void dump_efp_dtd(struct context *context,
			 const struct bdb_block *block)
{
	const struct bdb_efp_dtd *efp = block_data(block);

	for (int n = 0; n < ARRAY_SIZE(efp->dtd); n++) {
		printf("\tEFP DTD #%d:\n", n + 1);
		print_detail_timing_data(&efp->dtd[n]);
	}
}

static void dump_psr(struct context *context,
		     const struct bdb_block *block)
{
	const struct bdb_psr *psr_block = block_data(block);
	int i;
	uint32_t psr2_tp_time;

	psr2_tp_time = psr_block->psr2_tp2_tp3_wakeup_time;
	for (i = 0; i < 16; i++) {
		const struct psr_table *psr = &psr_block->psr_table[i];

		if (!dump_panel(context, i))
			continue;

		printf("\tPanel %d%s\n", i, panel_str(context, i));

		printf("\t\tFull link: %s\n", YESNO(psr->full_link));
		printf("\t\tRequire AUX to wakeup: %s\n", YESNO(psr->require_aux_to_wakeup));

		switch (psr->lines_to_wait) {
		case 0:
		case 1:
			printf("\t\tLines to wait before link standby: %d\n",
			       psr->lines_to_wait);
			break;
		case 2:
		case 3:
			printf("\t\tLines to wait before link standby: %d\n",
			       1 << psr->lines_to_wait);
			break;
		default:
			printf("\t\tLines to wait before link standby: (unknown) (0x%x)\n",
			       psr->lines_to_wait);
			break;
		}

		printf("\t\tIdle frames to for PSR enable: %d\n",
		       psr->idle_frames);

		printf("\t\tTP1 wakeup time: %d usec (0x%x)\n",
		       psr->tp1_wakeup_time * 100,
		       psr->tp1_wakeup_time);

		printf("\t\tTP2/TP3 wakeup time: %d usec (0x%x)\n",
		       psr->tp2_tp3_wakeup_time * 100,
		       psr->tp2_tp3_wakeup_time);

		if (context->bdb->version >= 226) {
			int index;
			static const uint16_t psr2_tp_times[] = {500, 100, 2500, 5};

			index = panel_bits(psr2_tp_time, i, 2);

			printf("\t\tPSR2 TP2/TP3 wakeup time: %d usec (0x%x)\n",
			       psr2_tp_times[index], index);
		}
	}
}

static void dump_lfp_power(struct context *context,
			   const struct bdb_block *block)
{
	const struct bdb_lfp_power *lfp_block = block_data(block);
	int i;

	printf("\tALS enable: %s\n",
	       YESNO(lfp_block->features.als_enable));
	printf("\tDisplay LACE support: %s\n",
	       YESNO(lfp_block->features.lace_support));
	printf("\tDefault Display LACE enabled status: %s\n",
	       YESNO(lfp_block->features.lace_enabled_status));
	printf("\tPower conservation preference level: %d\n",
	       lfp_block->features.power_conservation_pref);
	printf("\tDPST support: %s\n",
	       YESNO(lfp_block->features.dpst_support));

	for (i = 0; i < 5; i++) {
		printf("\tALS entry #%d\n", i + 1);
		printf("\t\tALS backlight adjust: %d\n",
		       lfp_block->als[i].backlight_adjust);
		printf("\t\tALS Lux: %d\n",
		       lfp_block->als[i].lux);
	}

	if (context->bdb->version < 210)
		return;

	printf("\tDisplay LACE aggressiveness profile: %d\n",
	       lfp_block->lace_aggressiveness_profile);

	if (context->bdb->version < 228)
		return;

	for (i = 0; i < 16; i++) {
		if (!dump_panel(context, i))
			continue;

		printf("\tPanel %d%s\n", i, panel_str(context, i));

		printf("\t\tDisplay Power Saving Technology (DPST): %s\n",
		       YESNO(panel_bool(lfp_block->dpst, i)));
		printf("\t\tPanel Self Refresh (PSR): %s\n",
		       YESNO(panel_bool(lfp_block->psr, i)));
		printf("\t\tDynamic Refresh Rate Switching (DRRS): %s\n",
		       YESNO(panel_bool(lfp_block->drrs, i)));
		printf("\t\tDisplay LACE support: %s\n",
		       YESNO(panel_bool(lfp_block->lace_support, i)));
		printf("\t\tAssertive Display Technology (ADT): %s\n",
		       YESNO(panel_bool(lfp_block->adt, i)));
		printf("\t\tDynamic Media Refresh Rate Switching (DMRRS): %s\n",
		       YESNO(panel_bool(lfp_block->dmrrs, i)));
		printf("\t\tAutomatic Display Brightness (ADB): %s\n",
		       YESNO(panel_bool(lfp_block->adb, i)));
		printf("\t\tDefault Display LACE enabled: %s\n",
		       YESNO(panel_bool(lfp_block->lace_enabled_status, i)));
		printf("\t\tLACE Aggressiveness: %d\n",
		       lfp_block->aggressiveness[i].lace_aggressiveness);
		printf("\t\tDPST Aggressiveness: %d\n",
		       lfp_block->aggressiveness[i].dpst_aggressiveness);

		if (context->bdb->version < 232)
			continue;

		printf("\t\tEDP 4k/2k HOBL feature: %s\n",
		       YESNO(panel_bool(lfp_block->hobl, i)));

		if (context->bdb->version < 233)
			continue;

		printf("\t\tVariable Refresh Rate (VRR): %s\n",
		       YESNO(panel_bool(lfp_block->vrr_feature_enabled, i)));

		if (context->bdb->version < 247)
			continue;

		printf("\t\tELP: %s\n",
		       YESNO(panel_bool(lfp_block->elp, i)));
		printf("\t\tOPST: %s\n",
		       YESNO(panel_bool(lfp_block->opst, i)));
		printf("\t\tELP Aggressiveness: %d\n",
		       lfp_block->aggressiveness2[i].elp_aggressiveness);
		printf("\t\tOPST Aggrgessiveness: %d\n",
		       lfp_block->aggressiveness2[i].opst_aggressiveness);
	}
}

static void dump_sdvo_lvds_dtd(struct context *context,
			       const struct bdb_block *block)
{
	const struct bdb_sdvo_lvds_dtd *t = block_data(block);

	for (int n = 0; n < ARRAY_SIZE(t->dtd); n++) {
		printf("\tSDVO Panel %d%s\n", n, sdvo_panel_str(context, n));
		print_detail_timing_data(&t->dtd[n]);
	}
}

static void dump_sdvo_lvds_pnp_id(struct context *context,
				  const struct bdb_block *block)
{
	const struct bdb_sdvo_lvds_pnp_id *t = block_data(block);

	for (int n = 0; n < ARRAY_SIZE(t->pnp_id); n++) {
		printf("\tSDVO Panel %d%s\n", n, sdvo_panel_str(context, n));
		dump_pnp_id(&t->pnp_id[n]);
	}
}

static void dump_sdvo_lvds_pps(struct context *context,
			       const struct bdb_block *block)
{
	const struct bdb_sdvo_lvds_pps *t = block_data(block);

	for (int n = 0; n < ARRAY_SIZE(t->pps); n++) {
		printf("\tSDVO Panel %d%s\n", n, sdvo_panel_str(context, n));
		printf("\t\tT0: %d ms\n", t->pps[n].t0);
		printf("\t\tT1: %d ms\n", t->pps[n].t1);
		printf("\t\tT2: %d ms\n", t->pps[n].t2);
		printf("\t\tT3: %d ms\n", t->pps[n].t3);
		printf("\t\tT4: %d ms\n", t->pps[n].t4);
	}
}

static void dump_sdvo_lvds_options(struct context *context,
				   const struct bdb_block *block)
{
	const struct bdb_sdvo_lvds_options *options = block_data(block);

	printf("\tbacklight: %d\n", options->panel_backlight);
	printf("\th40 type: %d\n", options->h40_set_panel_type);
	printf("\ttype: %d\n", options->panel_type);
	printf("\tssc_clk_freq: %d\n", options->ssc_clk_freq);
	printf("\tals_low_trip: %d\n", options->als_low_trip);
	printf("\tals_high_trip: %d\n", options->als_high_trip);
	/*
	u8 sclalarcoeff_tab_row_num;
	u8 sclalarcoeff_tab_row_size;
	u8 coefficient[8];
	*/
	printf("\tmisc[0]: %x\n", options->panel_misc_bits_1);
	printf("\tmisc[1]: %x\n", options->panel_misc_bits_2);
	printf("\tmisc[2]: %x\n", options->panel_misc_bits_3);
	printf("\tmisc[3]: %x\n", options->panel_misc_bits_4);
}

static void dump_edp_bfi(struct context *context,
			 const struct bdb_block *block)
{
	const struct bdb_edp_bfi *b = block_data(block);

	printf("\tBFI strucure size: %d\n", b->bfi_structure_size);

	if (sizeof(b->bfi[0]) != b->bfi_structure_size) {
		printf("\tBFI struct sizes don't match (expected %zu, got %u), skipping\n",
		       sizeof(b->bfi[0]), b->bfi_structure_size);
		return;
	}

	for (int i = 0; i < 16; i++) {
		if (!dump_panel(context, i))
			continue;

		printf("\tPanel %d%s\n", i, panel_str(context, i));

		printf("\t\tEnable brightness control in CUI: %s\n",
		       YESNO(b->bfi[i].enable_brightness_control_in_cui));
		printf("\t\tEnable BFI in driver: %s\n",
		       YESNO(b->bfi[i].enable_bfi_in_driver));
		printf("\t\tBrightness percentage when BFI is disabled: %d\n",
		       b->bfi[i].brightness_percentage_when_bfi_disabled);
	}
}

static float decode_coordinate(int value)
{
	return 1.0f * value / (1 << 10);
}

static float decode_luminance(uint16_t value)
{
	float f;

	igt_half_to_float(&value, &f, 1);

	return f;
}

static float decode_gamma(int value)
{
	return (value + 100) / 100.0f;
}

static void dump_chromaticity(struct context *context,
			      const struct bdb_block *block)
{
	const struct bdb_chromaticity *chromaticity = block_data(block);

	for (int i = 0; i < 16; i++) {
		const struct chromaticity *c = &chromaticity->chromaticity[i];
		const struct luminance_and_gamma *l = &chromaticity->luminance_and_gamma[i];
		int x, y;

		if (!dump_panel(context, i))
			continue;

		printf("\tPanel %d%s\n", i, panel_str(context, i));

		printf("\t\tUse chromaticity values from EDID base block: %s\n",
		       YESNO(c->chromaticity_from_edid_base_block));
		printf("\t\tChromaticity enable: %s\n",
		       YESNO(c->chromaticity_enable));

		x = (c->red_x_hi << 2) | c->red_x_lo;
		y = (c->red_y_hi << 2) | c->red_y_lo;
		printf("\t\tRed X coordinate: %f (0x%03x)\n", decode_coordinate(x), x);
		printf("\t\tRed Y coordinate: %f (0x%03x)\n", decode_coordinate(y), y);

		x = (c->green_x_hi << 2) | c->green_x_lo;
		y = (c->green_y_hi << 2) | c->green_y_lo;
		printf("\t\tGreen X coordinate: %f (0x%03x)\n", decode_coordinate(x), x);
		printf("\t\tGreen Y coordinate: %f (0x%03x)\n", decode_coordinate(y), y);

		x = (c->blue_x_hi << 2) | c->blue_x_lo;
		y = (c->blue_y_hi << 2) | c->blue_y_lo;
		printf("\t\tBlue X coordinate: %f (0x%03x)\n", decode_coordinate(x), x);
		printf("\t\tBlue Y coordinate: %f (0x%03x)\n", decode_coordinate(y), y);

		x = (c->white_x_hi << 2) | c->white_x_lo;
		y = (c->white_y_hi << 2) | c->white_y_lo;
		printf("\t\tWhite X coordinate: %f (0x%03x)\n", decode_coordinate(x), x);
		printf("\t\tWhite Y coordinate: %f (0x%03x)\n", decode_coordinate(y), y);

		if (context->bdb->version < 211)
			continue;

		printf("\t\tGamma enable: %s\n", YESNO(l->gamma_enable));
		printf("\t\tLuminance enable: %s\n", YESNO(l->luminance_enable));

		printf("\t\tMinimum luminance: %f (0x%04x)\n",
		       decode_luminance(l->min_luminance), l->min_luminance);
		printf("\t\tMaximum luminance: %f (0x%04x)\n",
		       decode_luminance(l->max_luminance), l->max_luminance);
		printf("\t\t1%% maximum luminanace: %f (0x%04x)\n",
		       decode_luminance(l->one_percent_max_luminance), l->one_percent_max_luminance);
		if (l->gamma != 0xff)
			printf("\t\tGamma: %f (0x%02x)\n", decode_gamma(l->gamma), l->gamma);
		else
			printf("\t\tGamma: n/a (0x%02x)\n", l->gamma);
	}
}

static void dump_fixed_set_mode(struct context *context,
				const struct bdb_block *block)
{
	const struct bdb_fixed_set_mode *f = block_data(block);

	printf("\tEnable: %s (0x%02x)\n", YESNO(f->enable), f->enable);
	printf("\tX Res: %d\n", f->x_res);
	printf("\tY Res: %d\n", f->y_res);
}

static void dump_mipi_config(struct context *context,
			     const struct bdb_block *block)
{
	const struct bdb_mipi_config *start = block_data(block);

	for (int i = 0; i < ARRAY_SIZE(start->config); i++) {
		const struct mipi_config *config = &start->config[i];
		const struct mipi_pps_data *pps = &start->pps[i];
		const struct edp_pwm_delays *pwm_delays = &start->pwm_delays[i];

		if (!dump_panel(context, i))
			continue;

		printf("\tPanel %d%s\n", i, panel_str(context, i));

		printf("\t\tGeneral Param\n");
		printf("\t\t\t BTA disable: %s\n", config->bta_disable ? "Disabled" : "Enabled");
		printf("\t\t\t Panel Rotation: %d degrees\n", config->rotation * 90);

		printf("\t\t\t Video Mode Color Format: ");
		if (config->videomode_color_format == 0)
			printf("Not supported\n");
		else if (config->videomode_color_format == 1)
			printf("RGB565\n");
		else if (config->videomode_color_format == 2)
			printf("RGB666\n");
		else if (config->videomode_color_format == 3)
			printf("RGB666 Loosely Packed\n");
		else if (config->videomode_color_format == 4)
			printf("RGB888\n");
		printf("\t\t\t PPS GPIO Pins: %s \n",
		       config->pwm_blc ? "Using SOC" : "Using PMIC");
		printf("\t\t\t CABC Support: %s\n",
		       config->cabc_supported ? "supported" : "not supported");
		printf("\t\t\t Mode: %s\n",
		       config->is_cmd_mode ? "COMMAND" : "VIDEO");
		printf("\t\t\t Video transfer mode: %s (0x%x)\n",
		       config->video_transfer_mode == 1 ? "non-burst with sync pulse" :
		       config->video_transfer_mode == 2 ? "non-burst with sync events" :
		       config->video_transfer_mode == 3 ? "burst" : "<unknown>",
		       config->video_transfer_mode);
		printf("\t\t\t Dithering: %s\n",
		       config->enable_dithering ? "done in Display Controller" : "done in Panel Controller");

		printf("\t\tPort Desc\n");
		printf("\t\t\t Pixel overlap: %d\n", config->pixel_overlap);
		printf("\t\t\t Lane Count: %d\n", config->lane_cnt + 1);
		printf("\t\t\t Dual Link Support: ");
		if (config->dual_link == 0)
			printf("not supported\n");
		else if (config->dual_link == 1)
			printf("Front Back mode\n");
		else
			printf("Pixel Alternative Mode\n");

		printf("\t\tDphy Flags\n");
		printf("\t\t\t Clock Stop: %s\n",
		       config->enable_clk_stop ? "ENABLED" : "DISABLED");
		printf("\t\t\t EOT disabled: %s\n\n",
		       config->eot_pkt_disabled ? "EOT not to be sent" : "EOT to be sent");

		printf("\t\tHSTxTimeOut: 0x%x\n", config->hs_tx_timeout);
		printf("\t\tLPRXTimeOut: 0x%x\n", config->lp_rx_timeout);
		printf("\t\tTurnAroundTimeOut: 0x%x\n", config->turn_around_timeout);
		printf("\t\tDeviceResetTimer: 0x%x\n", config->device_reset_timer);
		printf("\t\tMasterinitTimer: 0x%x\n", config->master_init_timer);
		printf("\t\tDBIBandwidthTimer: 0x%x\n", config->dbi_bw_timer);
		printf("\t\tLpByteClkValue: 0x%x\n\n", config->lp_byte_clk_val);

		printf("\t\tDphy Params\n");
		printf("\t\t\tExit to zero Count: 0x%x\n", config->exit_zero_cnt);
		printf("\t\t\tTrail Count: 0x%X\n", config->trail_cnt);
		printf("\t\t\tClk zero count: 0x%x\n", config->clk_zero_cnt);
		printf("\t\t\tPrepare count:0x%x\n\n", config->prepare_cnt);

		printf("\t\tClockLaneSwitchingCount: 0x%x\n", config->clk_lane_switch_cnt);
		printf("\t\tHighToLowSwitchingCount: 0x%x\n\n", config->hl_switch_cnt);

		printf("\t\tTimings based on Dphy spec\n");
		printf("\t\t\tTClkMiss: 0x%x\n", config->tclk_miss);
		printf("\t\t\tTClkPost: 0x%x\n", config->tclk_post);
		printf("\t\t\tTClkPre: 0x%x\n", config->tclk_pre);
		printf("\t\t\tTClkPrepare: 0x%x\n", config->tclk_prepare);
		printf("\t\t\tTClkSettle: 0x%x\n", config->tclk_settle);
		printf("\t\t\tTClkTermEnable: 0x%x\n\n", config->tclk_term_enable);

		printf("\t\tTClkTrail: 0x%x\n", config->tclk_trail);
		printf("\t\tTClkPrepareTClkZero: 0x%x\n", config->tclk_prepare_clkzero);
		printf("\t\tTHSExit: 0x%x\n", config->ths_exit);
		printf("\t\tTHsPrepare: 0x%x\n", config->ths_prepare);
		printf("\t\tTHsPrepareTHsZero: 0x%x\n", config->ths_prepare_hszero);
		printf("\t\tTHSSettle: 0x%x\n", config->ths_settle);
		printf("\t\tTHSSkip: 0x%x\n", config->ths_skip);
		printf("\t\tTHsTrail: 0x%x\n", config->ths_trail);
		printf("\t\tTInit: 0x%x\n", config->tinit);
		printf("\t\tTLPX: 0x%x\n", config->tlpx);

		printf("\t\tMIPI PPS\n");
		printf("\t\t\tPanel power ON delay: %d\n", pps->panel_on_delay);
		printf("\t\t\tPanel power on to Backlight enable delay: %d\n", pps->bl_enable_delay);
		printf("\t\t\tBacklight disable to Panel power OFF delay: %d\n", pps->bl_disable_delay);
		printf("\t\t\tPanel power OFF delay: %d\n", pps->panel_off_delay);
		printf("\t\t\tPanel power cycle delay: %d\n", pps->panel_power_cycle_delay);

		if (context->bdb->version >= 186)
			printf("\t\tMIPI PWM delays:\n"
			       "\t\t\tPWM on to backlight enable: %d\n"
			       "\t\t\tBacklight disable to PWM off: %d\n",
			       pwm_delays->pwm_on_to_backlight_enable,
			       pwm_delays->backlight_disable_to_pwm_off);

		if (context->bdb->version >= 190)
			printf("\t\tMIPI PMIC I2C Bus Number: %d\n",
			       start->pmic_i2c_bus_number[i]);
	}
}

static const uint8_t *mipi_dump_send_packet(const uint8_t *data, uint8_t seq_version)
{
	uint8_t flags, type;
	uint16_t len, i;

	flags = *data++;
	type = *data++;
	len = *((const uint16_t *) data);
	data += 2;

	printf("\t\t\tSend DCS: Port %s, VC %d, %s, Type %02x, Length %u, Data",
	       (flags >> 3) & 1 ? "C" : "A",
	       (flags >> 1) & 3,
	       flags & 1 ? "HS" : "LP",
	       type,
	       len);
	for (i = 0; i < len; i++)
		printf(" %02x", *data++);
	printf("\n");

	return data;
}

static const uint8_t *mipi_dump_delay(const uint8_t *data, uint8_t seq_version)
{
	printf("\t\t\tDelay: %u us\n", *((const uint32_t *)data));

	return data + 4;
}

static const uint8_t *mipi_dump_gpio(const uint8_t *data, uint8_t seq_version)
{
	uint8_t index, number, flags;

	if (seq_version >= 3) {
		index = *data++;
		number = *data++;
		flags = *data++;

		if (seq_version >= 4)
			printf("\t\t\tGPIO index %u, number %u, native %d, set %d (0x%02x)\n",
			       index, number, !(flags & 2), flags & 1, flags);
		else
			printf("\t\t\tGPIO index %u, number %u, set %d (0x%02x)\n",
			       index, number, flags & 1, flags);
	} else {
		index = *data++;
		flags = *data++;

		printf("\t\t\tGPIO index %u, source %d, set %d (0x%02x)\n",
		       index, (flags >> 1) & 3, flags & 1, flags);
	}

	return data;
}

static const uint8_t *mipi_dump_i2c(const uint8_t *data, uint8_t seq_version)
{
	uint8_t flags, index, bus, offset, len, i;
	uint16_t address;

	flags = *data++;
	index = *data++;
	bus = *data++;
	address = *((const uint16_t *) data);
	data += 2;
	offset = *data++;
	len = *data++;

	printf("\t\t\tSend I2C: Flags %02x, Index %02x, Bus %02x, Address %04x, Offset %02x, Length %u, Data",
	       flags, index, bus, address, offset, len);
	for (i = 0; i < len; i++)
		printf(" %02x", *data++);
	printf("\n");

	return data;
}

typedef const uint8_t * (*fn_mipi_elem_dump)(const uint8_t *data, uint8_t seq_version);

static const fn_mipi_elem_dump dump_elem[] = {
	[MIPI_SEQ_ELEM_SEND_PKT] = mipi_dump_send_packet,
	[MIPI_SEQ_ELEM_DELAY] = mipi_dump_delay,
	[MIPI_SEQ_ELEM_GPIO] = mipi_dump_gpio,
	[MIPI_SEQ_ELEM_I2C] = mipi_dump_i2c,
};

static const char * const seq_name[] = {
	[MIPI_SEQ_ASSERT_RESET] = "MIPI_SEQ_ASSERT_RESET",
	[MIPI_SEQ_INIT_OTP] = "MIPI_SEQ_INIT_OTP",
	[MIPI_SEQ_DISPLAY_ON] = "MIPI_SEQ_DISPLAY_ON",
	[MIPI_SEQ_DISPLAY_OFF]  = "MIPI_SEQ_DISPLAY_OFF",
	[MIPI_SEQ_DEASSERT_RESET] = "MIPI_SEQ_DEASSERT_RESET",
	[MIPI_SEQ_BACKLIGHT_ON] = "MIPI_SEQ_BACKLIGHT_ON",
	[MIPI_SEQ_BACKLIGHT_OFF] = "MIPI_SEQ_BACKLIGHT_OFF",
	[MIPI_SEQ_TEAR_ON] = "MIPI_SEQ_TEAR_ON",
	[MIPI_SEQ_TEAR_OFF] = "MIPI_SEQ_TEAR_OFF",
	[MIPI_SEQ_POWER_ON] = "MIPI_SEQ_POWER_ON",
	[MIPI_SEQ_POWER_OFF] = "MIPI_SEQ_POWER_OFF",
};

static const char *sequence_name(enum mipi_seq seq_id)
{
	if (seq_id < ARRAY_SIZE(seq_name) && seq_name[seq_id])
		return seq_name[seq_id];
	else
		return "(unknown)";
}

static const uint8_t *dump_sequence(const uint8_t *data, uint8_t seq_version)
{
	fn_mipi_elem_dump mipi_elem_dump;

	printf("\t\tSequence %u - %s\n", *data, sequence_name(*data));

	/* Skip Sequence Byte. */
	data++;

	/* Skip Size of Sequence. */
	if (seq_version >= 3)
		data += 4;

	while (1) {
		uint8_t operation_byte = *data++;
		uint8_t operation_size = 0;

		if (operation_byte == MIPI_SEQ_ELEM_END)
			break;

		if (operation_byte < ARRAY_SIZE(dump_elem))
			mipi_elem_dump = dump_elem[operation_byte];
		else
			mipi_elem_dump = NULL;

		/* Size of Operation. */
		if (seq_version >= 3)
			operation_size = *data++;

		if (mipi_elem_dump) {
			const uint8_t *next = data + operation_size;

			data = mipi_elem_dump(data, seq_version);

			if (operation_size && next != data)
				printf("Error: Inconsistent operation size: %d\n",
					operation_size);
		} else if (operation_size) {
			/* We have size, skip. */
			data += operation_size;
		} else {
			/* No size, can't skip without parsing. */
			printf("Error: Unsupported MIPI element %u\n",
			       operation_byte);
			return NULL;
		}
	}

	return data;
}

/* Find the sequence block and size for the given panel. */
static const uint8_t *
find_panel_sequence_block(const struct bdb_mipi_sequence *sequence,
			  uint16_t panel_id, uint32_t total, uint32_t *seq_size)
{
	const uint8_t *data = &sequence->data[0];
	uint8_t current_id;
	uint32_t current_size;
	int header_size = sequence->version >= 3 ? 5 : 3;
	int index = 0;
	int i;

	/* skip new block size */
	if (sequence->version >= 3)
		data += 4;

	for (i = 0; i < MAX_MIPI_CONFIGURATIONS && index < total; i++) {
		if (index + header_size > total) {
			fprintf(stderr, "Invalid sequence block (header)\n");
			return NULL;
		}

		current_id = *(data + index);
		if (sequence->version >= 3)
			current_size = *((const uint32_t *)(data + index + 1));
		else
			current_size = *((const uint16_t *)(data + index + 1));

		index += header_size;

		if (index + current_size > total) {
			fprintf(stderr, "Invalid sequence block\n");
			return NULL;
		}

		if (current_id == panel_id) {
			*seq_size = current_size;
			return data + index;
		}

		index += current_size;
	}

	fprintf(stderr, "Sequence block detected but no valid configuration\n");

	return NULL;
}

static int goto_next_sequence(const uint8_t *data, int index, int total)
{
	uint16_t len;

	/* Skip Sequence Byte. */
	for (index = index + 1; index < total; index += len) {
		uint8_t operation_byte = *(data + index);
		index++;

		switch (operation_byte) {
		case MIPI_SEQ_ELEM_END:
			return index;
		case MIPI_SEQ_ELEM_SEND_PKT:
			if (index + 4 > total)
				return 0;

			len = *((const uint16_t *)(data + index + 2)) + 4;
			break;
		case MIPI_SEQ_ELEM_DELAY:
			len = 4;
			break;
		case MIPI_SEQ_ELEM_GPIO:
			len = 2;
			break;
		case MIPI_SEQ_ELEM_I2C:
			if (index + 7 > total)
				return 0;
			len = *(data + index + 6) + 7;
			break;
		default:
			fprintf(stderr, "Unknown operation byte\n");
			return 0;
		}
	}

	return 0;
}

static int goto_next_sequence_v3(const uint8_t *data, int index, int total)
{
	int seq_end;
	uint16_t len;
	uint32_t size_of_sequence;

	/*
	 * Could skip sequence based on Size of Sequence alone, but also do some
	 * checking on the structure.
	 */
	if (total < 5) {
		fprintf(stderr, "Too small sequence size\n");
		return 0;
	}

	/* Skip Sequence Byte. */
	index++;

	/*
	 * Size of Sequence. Excludes the Sequence Byte and the size itself,
	 * includes MIPI_SEQ_ELEM_END byte, excludes the final MIPI_SEQ_END
	 * byte.
	 */
	size_of_sequence = *((const uint32_t *)(data + index));
	index += 4;

	seq_end = index + size_of_sequence;
	if (seq_end > total) {
		fprintf(stderr, "Invalid sequence size\n");
		return 0;
	}

	for (; index < total; index += len) {
		uint8_t operation_byte = *(data + index);
		index++;

		if (operation_byte == MIPI_SEQ_ELEM_END) {
			if (index != seq_end) {
				fprintf(stderr, "Invalid element structure\n");
				return 0;
			}
			return index;
		}

		len = *(data + index);
		index++;

		/*
		 * FIXME: Would be nice to check elements like for v1/v2 in
		 * goto_next_sequence() above.
		 */
		switch (operation_byte) {
		case MIPI_SEQ_ELEM_SEND_PKT:
		case MIPI_SEQ_ELEM_DELAY:
		case MIPI_SEQ_ELEM_GPIO:
		case MIPI_SEQ_ELEM_I2C:
		case MIPI_SEQ_ELEM_SPI:
		case MIPI_SEQ_ELEM_PMIC:
			break;
		default:
			fprintf(stderr, "Unknown operation byte %u\n",
				operation_byte);
			break;
		}
	}

	return 0;
}

static void dump_mipi_sequence(struct context *context,
			       const struct bdb_block *block)
{
	const struct bdb_mipi_sequence *sequence = block_data(block);

	/* Check if we have sequence block as well */
	if (!sequence) {
		printf("No MIPI Sequence found\n");
		return;
	}

	printf("\tSequence block version v%u\n", sequence->version);

	/* Fail gracefully for forward incompatible sequence block. */
	if (sequence->version >= 4) {
		fprintf(stderr, "Unable to parse MIPI Sequence Block v%u\n",
			sequence->version);
		return;
	}

	for (int i = 0; i < MAX_MIPI_CONFIGURATIONS; i++) {
		const uint8_t *sequence_ptrs[MIPI_SEQ_MAX] = {};
		const uint8_t *data;
		uint32_t seq_size;
		int index = 0;

		if (!dump_panel(context, i))
			continue;

		data = find_panel_sequence_block(sequence, i,
						 block->size, &seq_size);
		if (!data)
			return;

		printf("\tPanel %d%s\n", i, panel_str(context, i));

		/* Parse the sequences. Corresponds to VBT parsing in the kernel. */
		for (;;) {
			uint8_t seq_id = *(data + index);
			if (seq_id == MIPI_SEQ_END)
				break;

			if (seq_id >= MIPI_SEQ_MAX) {
				fprintf(stderr, "Unknown sequence %u\n", seq_id);
				return;
			}

			sequence_ptrs[seq_id] = data + index;

			if (sequence->version >= 3)
				index = goto_next_sequence_v3(data, index, seq_size);
			else
				index = goto_next_sequence(data, index, seq_size);
			if (!index) {
				fprintf(stderr, "Invalid sequence %u\n", seq_id);
				return;
			}

			dump_sequence(sequence_ptrs[seq_id], sequence->version);
		}
	}
}

static void dump_rgb_palette(struct context *context,
			     const struct bdb_block *block)
{
	const struct bdb_rgb_palette *pal = block_data(block);

	printf("\tIs enabled: %s (0x%02x)\n", YESNO(pal->is_enabled), pal->is_enabled);

	printf("\tRed:\n");
	hex_dump(pal->red, sizeof(pal->red));
	printf("\tGreen:\n");
	hex_dump(pal->green, sizeof(pal->green));
	printf("\tBlue:\n");
	hex_dump(pal->blue, sizeof(pal->blue));
}

#define KB(x) ((x) * 1024)

static int dsc_buffer_block_size(u8 buffer_block_size)
{
	switch (buffer_block_size) {
	case VBT_RC_BUFFER_BLOCK_SIZE_1KB:
		return KB(1);
		break;
	case VBT_RC_BUFFER_BLOCK_SIZE_4KB:
		return KB(4);
		break;
	case VBT_RC_BUFFER_BLOCK_SIZE_16KB:
		return KB(16);
		break;
	case VBT_RC_BUFFER_BLOCK_SIZE_64KB:
		return KB(64);
		break;
	default:
		return 0;
	}
}

static int actual_buffer_size(u8 buffer_block_size, u8 rc_buffer_size)
{
	return dsc_buffer_block_size(buffer_block_size) * (rc_buffer_size + 1);
}

static const char *dsc_max_bpp(u8 value)
{
	switch (value) {
	case 0:
		return "6";
	case 1:
		return "8";
	case 2:
		return "10";
	case 3:
		return "12";
	default:
		return "<unknown>";
	}
}

static void dump_compression_parameters(struct context *context,
					const struct bdb_block *block)
{
	const struct bdb_compression_parameters *dsc = block_data(block);
	const struct dsc_compression_parameters_entry *data;
	int i;

	for (i = 0; i < ARRAY_SIZE(dsc->data); i++) {
		/* FIXME: need to handle sizeof(*data) != dsc->entry_size */
		data = &dsc->data[i];

		if (!dump_panel(context, i))
			continue;

		printf("\tDSC block %d%s\n", i, panel_str(context, i));
		printf("\t\tDSC version: %u.%u\n", data->version_major,
		       data->version_minor);
		printf("\t\tActual buffer size: %d\n",
		       actual_buffer_size(data->rc_buffer_block_size,
					  data->rc_buffer_size));
		printf("\t\t\tRC buffer block size: %d (%u)\n",
		       dsc_buffer_block_size(data->rc_buffer_block_size),
		       data->rc_buffer_block_size);
		printf("\t\t\tRC buffer size: %u\n", data->rc_buffer_size);
		printf("\t\tSlices per line: 0x%02x\n", data->slices_per_line);
		printf("\t\tLine buffer depth: %u bits (%u)\n",
		       data->line_buffer_depth + 8, data->line_buffer_depth);
		printf("\t\tBlock prediction enable: %u\n",
		       data->block_prediction_enable);
		printf("\t\tMax bpp: %s bpp (%u)\n", dsc_max_bpp(data->max_bpp),
		       data->max_bpp);
		printf("\t\tSupport 8 bpc: %u\n", data->support_8bpc);
		printf("\t\tSupport 10 bpc: %u\n", data->support_10bpc);
		printf("\t\tSupport 12 bpc: %u\n", data->support_12bpc);
		printf("\t\tSlice height: %u\n", data->slice_height);
	}
}

static const char * const vswing_preemph[10] = {
	"V0-P0",
	"V0-P1",
	"V0-P2",
	"V0-P3",
	"V1-P0",
	"V1-P1",
	"V1-P2",
	"V2-P0",
	"V2-P1",
	"V3-P0",
};

static void dump_vswing_preemphasis(struct context *context,
				    const struct bdb_block *block)
{
	const struct bdb_vswing_preemph *vs = block_data(block);

	printf("\tNumber of vswing tables: %d\n", vs->num_tables);
	printf("\tNumber of columns: %d\n", vs->num_columns);

	for (int n = 0; n < vs->num_tables; n++) {
		printf("\tVswing Table #%d:\n", n+1);

		for (int i = 0; i < 10; i++) {
			printf("\t\t%s: ", vswing_preemph[i]);

			for (int j = 0; j < vs->num_columns; j++)
				printf(" 0x%08x", vs->tables[n * 10 * vs->num_columns + j]);
			printf("\n");
		}
	}
}

static void dump_generic_dtd_entry(const struct generic_dtd_entry *dtd,
				   const char *prefix)
{
	printf("%shdisplay: %d\n", prefix, dtd->hactive);
	printf("%shsync [%d, %d] %s\n", prefix,
	       dtd->hactive + dtd->hfront_porch,
	       dtd->hactive + dtd->hfront_porch + dtd->hsync,
	       dtd->hsync_positive_polarity ? "+sync" : "-sync");
	printf("%shtotal: %d\n", prefix, dtd->hactive + dtd->hblank);

	printf("%svdisplay: %d\n", prefix, dtd->vactive);
	printf("%svsync [%d, %d] %s\n", prefix,
	       dtd->vactive + dtd->vfront_porch,
	       dtd->vactive + dtd->vfront_porch + dtd->vsync,
	       dtd->vsync_positive_polarity ? "+sync" : "-sync");
	printf("%svtotal: %d\n", prefix, dtd->vactive + dtd->vblank);

	printf("%sclock: %d\n", prefix, dtd->pixel_clock * 10);
}

static void dump_generic_dtd(struct context *context,
			     const struct bdb_block *block)
{
	const struct bdb_generic_dtd *gdtd = block_data(block);
	int num_entries;

	if (sizeof(gdtd->dtd[0]) != gdtd->gdtd_size) {
		printf("\tDTD struct sizes don't match (expected %zu, got %u), skipping\n",
		       sizeof(gdtd->dtd[0]), gdtd->gdtd_size);
		return;
	}

	num_entries = (block->size - sizeof(*gdtd)) / gdtd->gdtd_size;

	printf("\tEntry size: %d\n", gdtd->gdtd_size);

	for (int i = 0; i < num_entries; i++) {
		if (i < 16 && !dump_panel(context, i))
			continue;

		printf("\tEntry #%d (%s #%d):%s\n", i+1, i < 16 ? "LFP" : "EFP",
		       i % 16 + 1, i < 16 ? panel_str(context, i) : "");
		dump_generic_dtd_entry(&gdtd->dtd[i], "\t\t");
	}
}

static void dump_prd_table_old(struct context *context,
			       const struct bdb_block *block)
{
	const struct bdb_prd_table_old *prd =
		block_data(block) + block->size - sizeof(*prd);
	const struct prd_entry_old *list = block_data(block);

	for (int i = 0; i < prd->num_entries; i++) {
		printf("\tEntry #%d:\n", i + 1);
		printf("\t\tDisplays attached: %s (0x%x)\n",
		       child_device_handle(context, list[i].displays_attached),
		       list[i].displays_attached);
		printf("\t\tDisplays in pipe A: %s (0x%x)\n",
		       child_device_handle(context, list[i].display_in_pipe_a),
		       list[i].display_in_pipe_a);
		printf("\t\tDisplays in pipe B: %s (0x%x)\n",
		       child_device_handle(context, list[i].display_in_pipe_b),
		       list[i].display_in_pipe_b);
	}

	printf("\tNum entries: %d\n", prd->num_entries);
}

static void dump_prd_table_new(struct context *context,
			       const struct bdb_block *block)
{
	const struct bdb_prd_table_new *prd = block_data(block);
	const struct prd_entry_new *list = prd->list;

	printf("\tNum entries: %d\n", prd->num_entries);

	for (int i = 0; i < prd->num_entries; i++) {
		printf("\tEntry #%d:\n", i + 1);
		printf("\t\tPrimary display: %s (0x%x)\n",
		       child_device_handle(context, list[i].primary_display),
		       list[i].primary_display);
		printf("\t\tSecondary display: %s (0x%x)\n",
		       child_device_handle(context, list[i].secondary_display),
		       list[i].secondary_display);
	}
}

static void dump_prd_table(struct context *context,
			   const struct bdb_block *block)
{
	const struct bdb_prd_table_old *old =
		block_data(block) + block->size - sizeof(*old);
	const struct bdb_prd_table_new *new =
		block_data(block);
	int num_entries_old = (block->size - sizeof(*old)) / sizeof(*old->list);
	int num_entries_new = (block->size - sizeof(*new)) / sizeof(*new->list);

	/*
	 * The cutoff seems to be TGL+ w/ GOP rather than a specific
	 * BDB version number. Just guess based on the actual data.
	 */
	if (num_entries_old == old->num_entries)
		dump_prd_table_old(context, block);
	else if (num_entries_new == new->num_entries)
		dump_prd_table_new(context, block);
}

static int get_panel_type_pnpid(const struct context *context,
				const char *edid_file)
{
	struct bdb_block *ptrs_block, *data_block;
	const struct bdb_lfp_data *data;
	const struct bdb_lfp_data_ptrs *ptrs;
	struct bdb_edid_pnp_id edid_id, edid_id_nodate;
	const struct edid *edid;
	int fd, best = -1;

	fd = open(edid_file, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Unable to open EDID file %s\n", edid_file);
		return -1;
	}

	edid = mmap(NULL, sizeof(*edid), PROT_READ, MAP_SHARED, fd, 0);
	close(fd);
	if (edid == MAP_FAILED) {
		fprintf(stderr, "Unable to read EDID file %s\n", edid_file);
		return -1;
	}
	edid_id = edid->pnpid;
	munmap((void*)edid, sizeof(*edid));

	edid_id_nodate = edid_id;
	edid_id_nodate.mfg_week = 0;
	edid_id_nodate.mfg_year = 0;

	ptrs_block = find_section(context, BDB_LFP_DATA_PTRS);
	if (!ptrs_block)
		return -1;

	data_block = find_section(context, BDB_LFP_DATA);
	if (!data_block)
		return -1;

	ptrs = block_data(ptrs_block);
	data = block_data(data_block);

	for (int i = 0; i < 16; i++) {
		const struct bdb_edid_pnp_id *vbt_id =
			(const void*)data + ptrs->ptr[i].panel_pnp_id.offset;

		/* full match? */
		if (!memcmp(vbt_id, &edid_id, sizeof(*vbt_id)))
			return i;

		/*
		 * Accept a match w/o date if no full match is found,
		 * and the VBT entry does not specify a date.
		 */
		if (best < 0 &&
		    !memcmp(vbt_id, &edid_id_nodate, sizeof(*vbt_id)))
			best = i;
	}

	return best;
}

/* get panel type from lfp options block, or -1 if block not found */
static int get_panel_type(struct context *context, bool is_panel_type2)
{
	struct bdb_block *block;
	const struct bdb_lfp_options *options;
	int panel_type = -1;

	block = find_section(context, BDB_LFP_OPTIONS);
	if (!block)
		return -1;

	options = block_data(block);
	if (!is_panel_type2)
		panel_type = options->panel_type;
	else if (context->bdb->version >= 212)
		panel_type = options->panel_type2;

	free(block);

	return panel_type;
}

/* get SDVO panel type from SDVO options block, or -1 if block not found */
static int get_sdvo_panel_type(struct context *context)
{
	const struct bdb_sdvo_lvds_options *options;
	struct bdb_block *block;
	int panel_type = -1;

	block = find_section(context, BDB_SDVO_LVDS_OPTIONS);
	if (!block)
		return -1;

	options = block_data(block);
	panel_type = options->panel_type;

	free(block);

	return panel_type;
}

static int
get_device_id(unsigned char *bios, int size)
{
    int device;
    int offset = (bios[0x19] << 8) + bios[0x18];

    if (offset + 7 >= size)
	return -1;

    if (bios[offset] != 'P' ||
	bios[offset+1] != 'C' ||
	bios[offset+2] != 'I' ||
	bios[offset+3] != 'R')
	return -1;

    device = (bios[offset+7] << 8) + bios[offset+6];

    return device;
}

struct dumper {
	uint8_t id;
	uint16_t min_bdb_version;
	uint16_t max_bdb_version;
	const char *name;
	void (*dump)(struct context *context,
		     const struct bdb_block *block);
};

struct dumper dumpers[] = {
	{
		.id = BDB_GENERAL_FEATURES,
		.name = "General features block",
		.dump = dump_general_features,
	},
	{
		.id = BDB_GENERAL_DEFINITIONS,
		.name = "General definitions block",
		.dump = dump_general_definitions,
	},
	{
		.id = BDB_DISPLAY_TOGGLE,
		.name = "Display toggle option block",
		.dump = dump_display_toggle,
	},
	{
		.id = BDB_MODE_SUPPORT_LIST,
		.name = "Mode support list",
		.dump = dump_mode_support_list,
	},
	{
		.id = BDB_GENERIC_MODE_TABLE,
		.name = "Generic mode table",
		.dump = dump_generic_mode_table,
	},
	{
		.id = BDB_EXT_MMIO_REGS,
		.name = "Extended MMIO registers",
		.dump = dump_reg_table,
	},
	{
		.id = BDB_SWF_IO,
		.name = "IO software flag",
		.dump = dump_reg_table,
	},
	{
		.id = BDB_SWF_MMIO,
		.name = "MMIO SWF register table",
		.dump = dump_reg_table,
	},
	{
		.id = BDB_DOT_CLOCK_OVERRIDE_ALM,
		.max_bdb_version = 164,
		.name = "Dot clock override (ALM)",
		.dump = dump_dot_clock_override_alm,
	},
	{
		.id = BDB_PSR,
		.min_bdb_version = 165,
		.name = "PSR block",
		.dump = dump_psr,
	},
	{
		.id = BDB_MODE_REMOVAL_TABLE,
		.name = "Mode removal table",
		.dump = dump_mode_removal_table,
	},
	{
		.id = BDB_CHILD_DEVICE_TABLE,
		.name = "Legacy child devices block",
		.dump = dump_legacy_child_devices,
	},
	{
		.id = BDB_DRIVER_FEATURES,
		.name = "Driver feature data block",
		.dump = dump_driver_feature,
	},
	{
		.id = BDB_DRIVER_PERSISTENCE,
		.name = "Driver persistent algorithm",
		.dump = dump_driver_persistence,
	},
	{
		.id = BDB_EXT_TABLE_PTRS,
		.name = "Ext table pointers, VBIOS only",
	},
	{
		.id = BDB_DOT_CLOCK_OVERRIDE,
		.name = "Dot clock override",
		.dump = dump_dot_clock_override,
	},
	{
		.id = BDB_DISPLAY_SELECT_OLD,
		.name = "Toggle list block (pre-IVB)",
		.dump = dump_display_select_old,
	},
	{
		.id = BDB_SV_TEST_FUNCTIONS,
		.name = "SV test functions",
	},
	{
		.id = BDB_DRIVER_ROTATION,
		.name = "Driver rotation",
		.dump = dump_driver_rotation,
	},
	{
		.id = BDB_DISPLAY_REMOVE_OLD,
		.name = "Display remove (pre-IVB)",
		.dump = dump_display_remove_old,
	},
	{
		.id = BDB_OEM_CUSTOM,
		.name = "OEM customizable modes",
		.dump = dump_oem_custom,
	},
	{
		.id = BDB_EFP_LIST,
		.name = "EFP list",
		.dump = dump_efp_list,
	},
	{
		.id = BDB_SDVO_LVDS_OPTIONS,
		.name = "SDVO LVDS options block",
		.dump = dump_sdvo_lvds_options,
	},
	{
		.id = BDB_SDVO_LVDS_DTD,
		.name = "SDVO LVDS DTD",
		.dump = dump_sdvo_lvds_dtd,
	},
	{
		.id = BDB_SDVO_LVDS_PNP_ID,
		.name = "SDVO LVDS PnP ID",
		.dump = dump_sdvo_lvds_pnp_id
	},
	{
		.id = BDB_SDVO_LVDS_PPS,
		.name = "SDVO LVDS PPS",
		.dump = dump_sdvo_lvds_pps,
	},
	{
		.id = BDB_TV_OPTIONS,
		.name = "TV options",
		.dump = dump_tv_options,
	},
	{
		.id = BDB_EDP,
		.name = "eDP block",
		.dump = dump_edp,
	},
	{
		.id = BDB_EFP_DTD,
		.name = "EFP DTD",
		.dump = dump_efp_dtd,
	},
	{
		.id = BDB_DISPLAY_SELECT_IVB,
		.name = "Display toggle list (IVB)",
		.dump = dump_display_select_ivb,
	},
	{
		.id = BDB_DISPLAY_REMOVE_IVB,
		.name = "Display removal table (IVB)",
		.dump = dump_display_remove_ivb,
	},
	{
		.id = BDB_DISPLAY_SELECT_HSW,
		.name = "Display toggle list (HSW+)",
		.dump = dump_display_select_hsw,
	},
	{
		.id = BDB_DISPLAY_REMOVE_HSW,
		.name = "Display removal table (HSW+)",
		.dump = dump_display_remove_hsw,
	},
	{
		.id = BDB_LFP_OPTIONS,
		.name = "LFP options block",
		.dump = dump_lfp_options,
	},
	{
		.id = BDB_LFP_DATA_PTRS,
		.name = "LFP data table pointers",
		.dump = dump_lfp_data_ptrs,
	},
	{
		.id = BDB_LFP_DATA,
		.name = "LFP data table block",
		.dump = dump_lfp_data,
	},
	{
		.id = BDB_LFP_BACKLIGHT,
		.name = "LFP backlight info block",
		.dump = dump_backlight_info,
	},
	{
		.id = BDB_LFP_POWER,
		.name = "LFP power conservation features block",
		.dump = dump_lfp_power,
	},
	{
		.id = BDB_EDP_BFI,
		.name = "eDP BFI",
		.dump = dump_edp_bfi,
	},
	{
		.id = BDB_CHROMATICITY,
		.name = "Chromaticity for narrow gamut panel",
		.dump = dump_chromaticity,
	},
	{
		.id = BDB_MIPI,
		.name = "MIPI",
	},
	{
		.id = BDB_FIXED_SET_MODE,
		.name = "Fixed set mode",
		.dump = dump_fixed_set_mode,
	},
	{
		.id = BDB_MIPI_CONFIG,
		.name = "MIPI configuration block",
		.dump = dump_mipi_config,
	},
	{
		.id = BDB_MIPI_SEQUENCE,
		.name = "MIPI sequence block",
		.dump = dump_mipi_sequence,
	},
	{
		.id = BDB_RGB_PALETTE,
		.name = "RGB palette",
		.dump = dump_rgb_palette,
	},
	{
		.id = BDB_COMPRESSION_PARAMETERS,
		.name = "Compression parameters block",
		.dump = dump_compression_parameters,
	},
	{
		.id = BDB_VSWING_PREEMPH,
		.name = "Vswing Preemph",
		.dump = dump_vswing_preemphasis,
	},
	{
		.id = BDB_GENERIC_DTD,
		.name = "Generic DTD",
		.dump = dump_generic_dtd,
	},
	{
		.id = BDB_INT15_HOOK,
		.name = "INT15h hook",
	},
	{
		.id = BDB_PRD_TABLE,
		.name = "PRD table",
		.dump = dump_prd_table,
	},
	{
		.id = BDB_SKIP,
		.name = "VBIOS only",
	},
};

static void hex_dump_block(const struct bdb_block *block)
{
	hex_dump(block->data, 3 + block->size);
}

static bool dump_section(struct context *context, int section_id)
{
	struct dumper *dumper = NULL;
	struct bdb_block *block;
	int i;

	block = find_section(context, section_id);
	if (!block)
		return false;

	for (i = 0; i < ARRAY_SIZE(dumpers); i++) {
		if (dumpers[i].min_bdb_version &&
		    context->bdb->version < dumpers[i].min_bdb_version)
			continue;

		if (dumpers[i].max_bdb_version &&
		    context->bdb->version > dumpers[i].max_bdb_version)
			continue;

		if (block->id == dumpers[i].id) {
			dumper = &dumpers[i];
			break;
		}
	}

	printf("BDB block %d (%d bytes, min %zu bytes) - %s%s:\n",
	       block->id, block->size, block_min_size(context, block->id),
	       dumper ? dumper->name : "Unknown",
	       dumper && !dumper->dump ? ", no decoding available" : "");

	if (context->hexdump)
		hex_dump_block(block);
	if (dumper && dumper->dump)
		dumper->dump(context, block);
	printf("\n");

	free(block);

	return true;
}

/* print a description of the VBT of the form <bdb-version>-<vbt-signature> */
static void print_description(struct context *context)
{
	const struct vbt_header *vbt = context->vbt;
	const struct bdb_header *bdb = context->bdb;
	char *desc = strndup((char *)vbt->signature, sizeof(vbt->signature));
	char *p;

	for (p = desc + strlen(desc) - 1; p >= desc && isspace(*p); p--)
		*p = '\0';

	for (p = desc; *p; p++) {
		if (!isalnum(*p))
			*p = '-';
		else
			*p = tolower(*p);
	}

	p = desc;
	if (strncmp(p, "-vbt-", 5) == 0)
		p += 5;

	printf("%d-%s\n", bdb->version, p);

	free (desc);
}

static void dump_headers(struct context *context)
{
	const struct vbt_header *vbt = context->vbt;
	const struct bdb_header *bdb = context->bdb;
	int i, j = 0;

	printf("VBT header:\n");
	if (context->hexdump)
		hex_dump(vbt, vbt->header_size);

	printf("\tVBT signature:\t\t\"%.*s\"\n",
	       (int)sizeof(vbt->signature), vbt->signature);
	printf("\tVBT version:\t\t0x%04x (%d.%d)\n", vbt->version,
	       vbt->version / 100, vbt->version % 100);
	printf("\tVBT header size:\t0x%04x (%u)\n",
	       vbt->header_size, vbt->header_size);
	printf("\tVBT size:\t\t0x%04x (%u)\n", vbt->vbt_size, vbt->vbt_size);
	printf("\tVBT checksum:\t\t0x%02x\n", vbt->vbt_checksum);
	printf("\tBDB offset:\t\t0x%08x (%u)\n", vbt->bdb_offset, vbt->bdb_offset);
	for (i = 0; i < ARRAY_SIZE(vbt->aim_offset); i++)
		printf("\tAIM #%d offset:\t\t0x%08x (%u)\n", i+1, vbt->aim_offset[i], vbt->aim_offset[i]);

	printf("\n");

	printf("BDB header:\n");
	if (context->hexdump)
		hex_dump(bdb, bdb->header_size);

	printf("\tBDB signature:\t\t\"%.*s\"\n",
	       (int)sizeof(bdb->signature), bdb->signature);
	printf("\tBDB version:\t\t%d\n", bdb->version);
	printf("\tBDB header size:\t0x%04x (%u)\n",
	       bdb->header_size, bdb->header_size);
	printf("\tBDB size:\t\t0x%04x (%u)\n", bdb->bdb_size, bdb->bdb_size);
	printf("\n");

	printf("BDB blocks present:");
	for (i = 0; i < 256; i++) {
		if (!find_raw_section(context, i))
			continue;

		if (j++ % 16)
			printf(" %3d", i);
		else
			printf("\n\t%3d", i);
	}
	printf("\n\n");
}

enum opt {
	OPT_UNKNOWN = '?',
	OPT_END = -1,
	OPT_FILE,
	OPT_DEVID,
	OPT_PANEL_TYPE,
	OPT_PANEL_TYPE2,
	OPT_PANEL_EDID,
	OPT_PANEL_EDID2,
	OPT_ALL_PANELS,
	OPT_HEXDUMP,
	OPT_BLOCK,
	OPT_USAGE,
	OPT_HEADER,
	OPT_DESCRIBE,
};

static void usage(const char *toolname)
{
	fprintf(stderr, "usage: %s", toolname);
	fprintf(stderr, " --file=<rom_file>"
			" [--devid=<device_id>]"
			" [--panel-type=<panel_type>]"
			" [--panel-type2=<panel_type>]"
			" [--panel-edid=<edid_file>]"
			" [--panel-edid2=<edid_file>]"
			" [--all-panels]"
			" [--hexdump]"
			" [--block=<block_no>]"
			" [--header]"
			" [--describe]"
			" [--help]\n");
}

int main(int argc, char **argv)
{
	uint8_t *VBIOS;
	int index;
	enum opt opt;
	int fd;
	struct vbt_header *vbt = NULL;
	int vbt_off, bdb_off, i;
	const char *filename = NULL;
	const char *toolname = argv[0];
	struct stat finfo;
	int size;
	struct context context = {
		.panel_type = -1,
		.panel_type2 = -1,
		.sdvo_panel_type = -1,
	};
	const char *panel_edid = NULL, *panel_edid2 = NULL;
	char *endp;
	int block_number = -1;
	bool header_only = false, describe = false;

	static struct option options[] = {
		{ "file",	required_argument,	NULL,	OPT_FILE },
		{ "devid",	required_argument,	NULL,	OPT_DEVID },
		{ "panel-type",	required_argument,	NULL,	OPT_PANEL_TYPE },
		{ "panel-edid",	required_argument,	NULL,	OPT_PANEL_EDID },
		{ "panel-type2",	required_argument,	NULL,	OPT_PANEL_TYPE2 },
		{ "panel-edid2",	required_argument,	NULL,	OPT_PANEL_EDID2 },
		{ "all-panels",	no_argument,		NULL,	OPT_ALL_PANELS },
		{ "hexdump",	no_argument,		NULL,	OPT_HEXDUMP },
		{ "block",	required_argument,	NULL,	OPT_BLOCK },
		{ "header",	no_argument,		NULL,	OPT_HEADER },
		{ "describe",	no_argument,		NULL,	OPT_DESCRIBE },
		{ "help",	no_argument,		NULL,	OPT_USAGE },
		{ 0 }
	};

	for (opt = 0; opt != OPT_END; ) {
		opt = getopt_long(argc, argv, "", options, &index);

		switch (opt) {
		case OPT_FILE:
			filename = optarg;
			break;
		case OPT_DEVID:
			context.devid = strtoul(optarg, &endp, 16);
			if (!context.devid || *endp) {
				fprintf(stderr, "invalid devid '%s'\n", optarg);
				return EXIT_FAILURE;
			}
			break;
		case OPT_PANEL_TYPE:
			context.panel_type = strtoul(optarg, &endp, 0);
			if (*endp || context.panel_type > 15) {
				fprintf(stderr, "invalid panel type '%s'\n",
					optarg);
				return EXIT_FAILURE;
			}
			break;
		case OPT_PANEL_TYPE2:
			context.panel_type2 = strtoul(optarg, &endp, 0);
			if (*endp || context.panel_type2 > 15) {
				fprintf(stderr, "invalid panel type2 '%s'\n",
					optarg);
				return EXIT_FAILURE;
			}
			break;
		case OPT_PANEL_EDID:
			panel_edid = optarg;
			break;
		case OPT_PANEL_EDID2:
			panel_edid2 = optarg;
			break;
		case OPT_ALL_PANELS:
			context.dump_all_panel_types = true;
			break;
		case OPT_HEXDUMP:
			context.hexdump = true;
			break;
		case OPT_BLOCK:
			block_number = strtoul(optarg, &endp, 0);
			if (*endp) {
				fprintf(stderr, "invalid block number '%s'\n",
					optarg);
				return EXIT_FAILURE;
			}
			break;
		case OPT_HEADER:
			header_only = true;
			break;
		case OPT_DESCRIBE:
			describe = true;
			break;
		case OPT_END:
			break;
		case OPT_USAGE: /* fall-through */
		case OPT_UNKNOWN:
			usage(toolname);
			return EXIT_FAILURE;
		}
	}

	argc -= optind;
	argv += optind;

	if (!filename) {
		if (argc == 1) {
			/* for backwards compatibility */
			filename = argv[0];
		} else {
			usage(toolname);
			return EXIT_FAILURE;
		}
	}

	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		fprintf(stderr, "Couldn't open \"%s\": %s\n",
			filename, strerror(errno));
		return EXIT_FAILURE;
	}

	if (stat(filename, &finfo)) {
		fprintf(stderr, "Failed to stat \"%s\": %s\n",
			filename, strerror(errno));
		return EXIT_FAILURE;
	}
	size = finfo.st_size;

	if (size == 0) {
		int len = 0, ret;
		size = 8192;
		VBIOS = malloc (size);
		while ((ret = read(fd, VBIOS + len, size - len))) {
			if (ret < 0) {
				fprintf(stderr, "Failed to read \"%s\": %s\n",
					filename, strerror(errno));
				return EXIT_FAILURE;
			}

			len += ret;
			if (len == size) {
				size *= 2;
				VBIOS = realloc(VBIOS, size);
			}
		}
	} else {
		VBIOS = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
		if (VBIOS == MAP_FAILED) {
			fprintf(stderr, "Failed to map \"%s\": %s\n",
				filename, strerror(errno));
			return EXIT_FAILURE;
		}
	}

	/* Scour memory looking for the VBT signature */
	for (i = 0; i + 4 < size; i++) {
		if (!memcmp(VBIOS + i, "$VBT", 4)) {
			vbt_off = i;
			vbt = (struct vbt_header *)(VBIOS + i);
			break;
		}
	}

	if (!vbt) {
		fprintf(stderr, "VBT signature missing\n");
		return EXIT_FAILURE;
	}

	bdb_off = vbt_off + vbt->bdb_offset;
	if (bdb_off >= size - sizeof(struct bdb_header)) {
		fprintf(stderr, "Invalid VBT found, BDB points beyond end of data block\n");
		return EXIT_FAILURE;
	}

	context.vbt = vbt;
	context.bdb = (const struct bdb_header *)(VBIOS + bdb_off);
	context.size = size;

	if (!context.devid) {
		const char *devid_string = getenv("DEVICE");
		if (devid_string)
			context.devid = strtoul(devid_string, NULL, 16);
	}
	if (!context.devid)
		context.devid = get_device_id(VBIOS, size);
	if (!context.devid)
		fprintf(stderr, "Warning: could not find PCI device ID!\n");

	if (context.panel_type == -1)
		context.panel_type = get_panel_type(&context, false);
	if (context.panel_type == 255 && !panel_edid) {
		fprintf(stderr, "Warning: panel type depends on EDID (use --panel-edid), ignoring\n");
		context.panel_type = -1;
	} else if (context.panel_type == 255) {
		context.panel_type = get_panel_type_pnpid(&context, panel_edid);
	}
	if (context.panel_type == -1) {
		fprintf(stderr, "Warning: panel type not set, using 0\n");
		context.panel_type = 0;
	}

	if (context.panel_type2 == -1)
		context.panel_type2 = get_panel_type(&context, true);
	if (context.panel_type2 == 255 && !panel_edid2) {
		fprintf(stderr, "Warning: panel type2 depends on EDID (use --panel-edid2), ignoring\n");
		context.panel_type2 = -1;
	} else if (context.panel_type2 == 255) {
		context.panel_type2 = get_panel_type_pnpid(&context, panel_edid2);
	}
	if (context.panel_type2 != -1 && context.bdb->version < 212) {
		fprintf(stderr, "Warning: panel type2 not valid for BDB version %d\n",
			context.bdb->version);
		context.panel_type2 = -1;
	}

	if (context.sdvo_panel_type == -1)
		context.sdvo_panel_type = get_sdvo_panel_type(&context);

	if (describe) {
		print_description(&context);
	} else if (header_only) {
		dump_headers(&context);
	} else if (block_number != -1) {
		/* dump specific section only */
		if (!dump_section(&context, block_number)) {
			fprintf(stderr, "Block %d not found\n", block_number);
			return EXIT_FAILURE;
		}
	} else {
		dump_headers(&context);

		/* dump all sections  */
		for (i = 0; i < 256; i++)
			dump_section(&context, i);
	}

	return 0;
}
