// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "igt_core.h"
#include "igt_sriov_device.h"
#include "intel_vram.h"

static int intel_vram_open_bar(int pf_fd, unsigned int vf_num, int mode)
{
	int sysfs, fd;

	sysfs = igt_sriov_device_sysfs_open(pf_fd, vf_num);
	if (sysfs < 0) {
		igt_debug("Failed to open sysfs for VF%d: %s\n", vf_num, strerror(errno));
		return -1;
	}

	fd = openat(sysfs, "resource2", mode);
	if (fd < 0)
		igt_debug("Failed to open resource2 for VF%d: %s\n", vf_num, strerror(errno));

	close(sysfs);

	return fd;
}

/**
 * intel_vram_bar_size - Get the size of the VRAM BAR
 * @pf_fd: PF device file descriptor
 * @vf_num: VF number (1-based), or 0 for the PF
 * @size: pointer to store VRAM BAR size
 *
 * Opens the VRAM BAR file descriptor for the specified device and retrieves
 * its size by using fstat().
 *
 * Return: 0 on success, or negative value on failure.
 */
int intel_vram_bar_size(int pf_fd, unsigned int vf_num, uint64_t *size)
{
	int fd, ret = 0;
	struct stat st;

	if (!size)
		return -EINVAL;

	fd = intel_vram_open_bar(pf_fd, vf_num, O_RDONLY);
	if (fd < 0)
		return fd;

	if (fstat(fd, &st))
		ret = -errno;
	else
		*size = (uint64_t)st.st_size;

	close(fd);

	return ret;
}

/**
 * intel_vram_mmap - Map VRAM BAR region
 * @pf_fd: PF device file descriptor
 * @vf_num: VF number (1-based), or 0 for the PF
 * @offset: Offset (in bytes) within the BAR to begin the mapping
 * @length: Number of bytes to map
 * @prot: Memory protection flags
 * @map: pointer to vram_mapping struct to store address and size of the mapping
 *
 * Maps a PF or VF VRAM BAR into user space using mmap(). On error, it sets the
 * mapping address to NULL and the mapping size to 0.
 *
 * Return: 0 on success, or negative value on failure.
 */
int intel_vram_mmap(int pf_fd, unsigned int vf_num, uint64_t offset, size_t length,
		    int prot, struct vram_mapping *map)
{
	uint64_t bar_size, end;
	int fd, ret = 0;
	void *addr;

	if (!map)
		return -EINVAL;

	map->addr = NULL;
	map->size = 0;

	if (!length)
		return 0;

	ret = intel_vram_bar_size(pf_fd, vf_num, &bar_size);
	if (ret)
		return ret;

	end = offset + length;
	if (end < offset || end > bar_size)
		return -EINVAL;

	fd = intel_vram_open_bar(pf_fd, vf_num, (prot & PROT_WRITE) ? O_RDWR : O_RDONLY);
	if (fd < 0)
		return fd;

	addr = mmap(NULL, length, prot, MAP_SHARED, fd, (off_t)offset);
	if (addr == MAP_FAILED) {
		ret = -errno;
		goto end;
	}

	map->addr = addr;
	map->size = length;
end:
	close(fd);

	return ret;
}

/**
 * intel_vram_munmap - Unmap previously mapped VRAM region
 * @m: Pointer to a vram_mapping struct representing the mapped region
 *
 * Unmaps the user-space memory region previously mapped by intel_vram_mmap().
 *
 * Return: 0 on success, or negative value on failure.
 */
int intel_vram_munmap(struct vram_mapping *m)
{
	int ret;

	ret = munmap(m->addr, m->size);
	if (ret < 0)
		igt_debug("Failed munmap %p: %s\n", m->addr, strerror(errno));

	return ret;
}

/**
 * intel_vram_read8 - Read 8-bit value from a mapped VRAM region
 * @m: Pointer to a vram_mapping struct representing the mapped region
 * @offset: Offset (in bytes) to read from
 *
 * Reads a single 8-bit value from the specified offset in the mapped VRAM.
 *
 * Return: The 8-bit value read from the given offset.
 */
uint8_t intel_vram_read8(const struct vram_mapping *m, size_t offset)
{
	igt_assert(offset < m->size);

	return READ_ONCE(*((uint8_t *)m->addr + offset));
}

/**
 * intel_vram_write8 - Write 8-bit value to a mapped VRAM region
 * @m: Pointer to a vram_mapping struct representing the mapped region
 * @offset: Offset (in bytes) to write to
 * @value: The 8-bit value to write
 *
 * Writes a single 8-bit value to the specified offset in the mapped VRAM.
 */
void intel_vram_write8(const struct vram_mapping *m, size_t offset, uint8_t value)
{
	igt_assert(offset < m->size);

	WRITE_ONCE(*((uint8_t *)m->addr + offset), value);
}

/**
 * intel_vram_write_readback8 - Write and then read back an 8-bit value
 * @m: Pointer to a vram_mapping struct representing the mapped region
 * @offset: Offset (in bytes) to write to and read from
 * @value: The 8-bit value to write
 *
 * Writes an 8-bit value to the specified offset in the mapped VRAM and
 * reads it back to verify if the write was successful.
 *
 * Return: The 8-bit value read back from the given offset.
 */
uint8_t intel_vram_write_readback8(const struct vram_mapping *m, size_t offset, uint8_t value)
{
	intel_vram_write8(m, offset, value);
	return intel_vram_read8(m, offset);
}
