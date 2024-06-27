/* SPDX-License-Identifier: MIT */
/* Copyright © 2024 Intel Corporation */

/* Header used during pre-process phase of iga64 assembly. */

#ifndef IGA64_MACROS_H
#define IGA64_MACROS_H

/* send instruction for DG2+ requires 0 length in case src1 is null, BSpec: 47443 */
#if GEN_VER < 1271
#define src1_null null
#else
#define src1_null null:0
#endif

#endif
