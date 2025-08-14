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

#ifndef _INTEL_BIOS_H_
#define _INTEL_BIOS_H_

#include <stdint.h>

#define DEVICE_TYPE_DP_DVI		0x68d6
#define DEVICE_TYPE_DVI			0x68d2
#define DEVICE_TYPE_MIPI		0x7cc2

struct bdb_legacy_child_devices {
	uint8_t child_dev_size;
	uint8_t devices[0]; /* presumably 7 * 33 */
} __attribute__ ((packed));

#define BDB_DRIVER_NO_LVDS	0
#define BDB_DRIVER_INT_LVDS	1
#define BDB_DRIVER_SDVO_LVDS	2
#define BDB_DRIVER_EDP		3

#endif /* _INTEL_BIOS_H_ */
