// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2024 Intel Corporation. All rights reserved.
 */

#include "igt_device.h"

#include "xe/xe_mmio.h"
#include "xe/xe_query.h"

/**
 * xe_mmio_vf_access_init:
 * @pf_fd: xe device file descriptor
 * @vf_id: PCI virtual function number (0 if native or PF itself)
 * @mmio: xe mmio structure for IO operations
 *
 * This initializes the xe mmio structure, and maps the MMIO BAR owned by
 * the specified virtual function associated with @pf_fd.
 */
void xe_mmio_vf_access_init(int pf_fd, int vf_id, struct xe_mmio *mmio)
{
	struct pci_device *pci_dev = __igt_device_get_pci_device(pf_fd, vf_id);

	igt_assert_f(pci_dev, "No PCI device found for VF%u\n", vf_id);

	intel_register_access_init(&mmio->intel_mmio, pci_dev, false);
	mmio->fd = pf_fd;
	mmio->init = true;
}

/**
 * xe_mmio_access_init:
 * @pf_fd: xe device file descriptor
 * @mmio: xe mmio structure for IO operations
 *
 * This initializes the xe mmio structure, and maps MMIO BAR for @pf_fd device.
 */
void xe_mmio_access_init(int pf_fd, struct xe_mmio *mmio)
{
	xe_mmio_vf_access_init(pf_fd, 0, mmio);
}

/**
 * xe_mmio_is_initialized:
 * @mmio: xe mmio structure for IO operations
 *
 * Returns:
 * Non-zero if the xe mmio structure is initialized.
 */
bool xe_mmio_is_initialized(const struct xe_mmio *mmio)
{
	return mmio->init;
}

/**
 * xe_mmio_access_fini:
 * @mmio: xe mmio structure for IO operations
 *
 * Clean up the mmio access helper initialized with
 * xe_mmio_access_init()/xe_mmio_vf_access_init().
 */
void xe_mmio_access_fini(struct xe_mmio *mmio)
{
	intel_register_access_fini(&mmio->intel_mmio);
	mmio->init = false;
}

/**
 * xe_mmio_read32:
 * @mmio: xe mmio structure for IO operations
 * @offset: mmio register offset
 *
 * 32-bit read of the register at @offset.
 *
 * Returns:
 * The value read from the register.
 */
uint32_t xe_mmio_read32(struct xe_mmio *mmio, uint32_t offset)
{
	return ioread32(mmio->intel_mmio.igt_mmio, offset);
}

/**
 * xe_mmio_read64:
 * @mmio: xe mmio structure for IO operations
 * @offset: mmio register offset
 *
 * 64-bit read of the register at @offset.
 *
 * Returns:
 * The value read from the register.
 */
uint64_t xe_mmio_read64(struct xe_mmio *mmio, uint32_t offset)
{
	return ioread64(mmio->intel_mmio.igt_mmio, offset);
}

/**
 * xe_mmio_write32:
 * @mmio: xe mmio structure for IO operations
 * @offset: mmio register offset
 * @val: value to write
 *
 * 32-bit write to the register at @offset.
 */
void xe_mmio_write32(struct xe_mmio *mmio, uint32_t offset, uint32_t val)
{
	return iowrite32(mmio->intel_mmio.igt_mmio, offset, val);
}

/**
 * xe_mmio_write64:
 * @mmio: xe mmio structure for IO operations
 * @offset: mmio register offset
 * @val: value to write
 *
 * 64-bit write to the register at @offset.
 */
void xe_mmio_write64(struct xe_mmio *mmio, uint32_t offset, uint64_t val)
{
	return iowrite64(mmio->intel_mmio.igt_mmio, offset, val);
}

/** xe_mmio_tile_read32:
 * @mmio: xe mmio structure for IO operations
 * @tile: tile id
 * @offset: mmio register offset in the tile
 *
 * 32-bit read of the register at @offset in the specified @tile
 *
 * Returns: The value read from the register.
 */
uint32_t xe_mmio_tile_read32(struct xe_mmio *mmio, uint8_t tile, uint32_t offset)
{
	return xe_mmio_read32(mmio, offset + (TILE_MMIO_SIZE * tile));
}

/** xe_mmio_tile_read64:
 * @mmio: xe mmio structure for IO operations
 * @tile: tile id
 * @offset: mmio register offset in the @tile
 *
 * 64-bit read of the register at @offset in the specified @tile
 *
 * Returns: The value read from the register.
 */
uint64_t xe_mmio_tile_read64(struct xe_mmio *mmio, uint8_t tile, uint32_t offset)
{
	return xe_mmio_read64(mmio, offset + (TILE_MMIO_SIZE * tile));
}

/**
 * xe_mmio_tile_write32:
 * @mmio: xe mmio structure for IO operations
 * @tile: tile id
 * @offset: mmio register offset in the @tile
 * @val: value to write
 *
 * 32-bit write to the register at @offset in the specified @tile
 */
void xe_mmio_tile_write32(struct xe_mmio *mmio, uint8_t tile, uint32_t offset, uint32_t val)
{
	xe_mmio_write32(mmio, offset + (TILE_MMIO_SIZE * tile), val);
}

/**
 * xe_mmio_tile_write64:
 * @mmio: xe mmio structure for IO operations
 * @tile: tile id
 * @offset: mmio register offset in the @tile
 * @val: value to write
 *
 * 64-bit write to the register at @offset in the specified @tile
 */
void xe_mmio_tile_write64(struct xe_mmio *mmio, uint8_t tile, uint32_t offset, uint64_t val)
{
	xe_mmio_write64(mmio, offset + (TILE_MMIO_SIZE * tile), val);
}

/**
 * xe_mmio_ggtt_read:
 * @mmio: xe mmio structure for IO operations
 * @tile: tile id
 * @offset: PTE offset from the beginning of GGTT in @tile
 *
 * Read of GGTT PTE at GGTT @offset in the @tile.
 *
 * Returns:
 * The value read from the register.
 */
xe_ggtt_pte_t xe_mmio_ggtt_read(struct xe_mmio *mmio, uint8_t tile, uint32_t offset)
{
	return xe_mmio_tile_read64(mmio, tile, offset + GGTT_OFFSET_IN_TILE);
}

/**
 * xe_mmio_ggtt_write:
 * @mmio: xe mmio structure for IO operations
 * @tile: tile id
 * @offset: PTE offset from the beginning of GGTT in @tile
 * @pte: PTE value to write
 *
 * Write PTE value at GGTT @offset in the @tile.
 */
void xe_mmio_ggtt_write(struct xe_mmio *mmio, uint8_t tile, uint32_t offset, xe_ggtt_pte_t pte)
{
	return xe_mmio_tile_write64(mmio, tile, offset + GGTT_OFFSET_IN_TILE, pte);
}
