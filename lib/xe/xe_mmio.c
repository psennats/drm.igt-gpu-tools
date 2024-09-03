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

	intel_mmio_use_pci_bar(&mmio->intel_mmio, pci_dev);

	igt_assert(mmio->intel_mmio.igt_mmio);

	mmio->fd = pf_fd;
	mmio->intel_mmio.safe = false;
	mmio->intel_mmio.pci_device_id = pci_dev->device_id;
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
 * xe_mmio_access_fini:
 * @mmio: xe mmio structure for IO operations
 *
 * Clean up the mmio access helper initialized with
 * xe_mmio_access_init()/xe_mmio_vf_access_init().
 */
void xe_mmio_access_fini(struct xe_mmio *mmio)
{
	mmio->intel_mmio.pci_device_id = 0;
	intel_mmio_unmap_pci_bar(&mmio->intel_mmio);
	igt_pci_system_cleanup();
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

/**
 * xe_mmio_gt_read32:
 * @mmio: xe mmio structure for IO operations
 * @gt: gt id
 * @offset: mmio register offset in tile to which @gt belongs
 *
 * 32-bit read of the register at @offset in tile to which @gt belongs.
 *
 * Returns:
 * The value read from the register.
 */
uint32_t xe_mmio_gt_read32(struct xe_mmio *mmio, int gt, uint32_t offset)
{
	return xe_mmio_read32(mmio, offset + (TILE_MMIO_SIZE * xe_gt_get_tile_id(mmio->fd, gt)));
}

/**
 * xe_mmio_gt_read64:
 * @mmio: xe mmio structure for IO operations
 * @gt: gt id
 * @offset: mmio register offset in tile to which @gt belongs
 *
 * 64-bit read of the register at @offset in tile to which @gt belongs.
 *
 * Returns:
 * The value read from the register.
 */
uint64_t xe_mmio_gt_read64(struct xe_mmio *mmio, int gt, uint32_t offset)
{
	return xe_mmio_read64(mmio, offset + (TILE_MMIO_SIZE * xe_gt_get_tile_id(mmio->fd, gt)));
}

/**
 * xe_mmio_gt_write32:
 * @mmio: xe mmio structure for IO operations
 * @gt: gt id
 * @offset: mmio register offset
 * @val: value to write
 *
 * 32-bit write to the register at @offset in tile to which @gt belongs.
 */
void xe_mmio_gt_write32(struct xe_mmio *mmio, int gt, uint32_t offset, uint32_t val)
{
	return xe_mmio_write32(mmio, offset + (TILE_MMIO_SIZE * xe_gt_get_tile_id(mmio->fd, gt)),
			       val);
}

/**
 * xe_mmio_gt_write64:
 * @mmio: xe mmio structure for IO operations
 * @gt: gt id
 * @offset: mmio register offset
 * @val: value to write
 *
 * 64-bit write to the register at @offset in tile to which @gt belongs.
 */
void xe_mmio_gt_write64(struct xe_mmio *mmio, int gt, uint32_t offset, uint64_t val)
{
	return xe_mmio_write64(mmio, offset + (TILE_MMIO_SIZE * xe_gt_get_tile_id(mmio->fd, gt)),
			       val);
}

/**
 * xe_mmio_ggtt_read:
 * @mmio: xe mmio structure for IO operations
 * @gt: gt id
 * @offset: PTE offset from the beginning of GGTT, in tile to which @gt belongs
 *
 * Read of GGTT PTE at GGTT @offset in tile to which @gt belongs.
 *
 * Returns:
 * The value read from the register.
 */
xe_ggtt_pte_t xe_mmio_ggtt_read(struct xe_mmio *mmio, int gt, uint32_t offset)
{
	return xe_mmio_gt_read64(mmio, gt, offset + GGTT_OFFSET_IN_TILE);
}

/**
 * xe_mmio_ggtt_write:
 * @mmio: xe mmio structure for IO operations
 * @gt: gt id
 * @offset: PTE offset from the beginning of GGTT, in tile to which @gt belongs
 * @pte: PTE value to write
 *
 * Write PTE value at GGTT @offset in tile to which @gt belongs.
 */
void xe_mmio_ggtt_write(struct xe_mmio *mmio, int gt, uint32_t offset, xe_ggtt_pte_t pte)
{
	return xe_mmio_gt_write64(mmio, gt, offset + GGTT_OFFSET_IN_TILE, pte);
}
