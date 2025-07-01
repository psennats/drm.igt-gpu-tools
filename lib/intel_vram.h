/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef __INTEL_VRAM_H__
#define __INTEL_VRAM_H__

struct vram_mapping {
	void *addr; /* Pointer to the mapped VRAM */
	size_t size; /* Size of the mapped VRAM region */
};

int intel_vram_bar_size(int pf_fd, unsigned int vf_num, uint64_t *size);
int intel_vram_mmap(int pf_fd, unsigned int vf_num, uint64_t offset, size_t length,
		    int prot, struct vram_mapping *map);
int intel_vram_munmap(struct vram_mapping *m);
uint8_t intel_vram_read8(const struct vram_mapping *m, size_t offset);
void intel_vram_write8(const struct vram_mapping *m, size_t offset, uint8_t value);
uint8_t intel_vram_write_readback8(const struct vram_mapping *m, size_t offset, uint8_t value);

#endif	/* __INTEL_VRAM_H__ */
