/* SPDX-License-Identifier: MIT
 * Copyright © 2025 Intel Corporation
 */

#ifndef RUNNER_KMEMLEAK_H
#define RUNNER_KMEMLEAK_H

#include <stdbool.h>

bool runner_kmemleak_init(const char *unit_test_kmemleak_file);
bool runner_kmemleak(const char *last_test, int resdirfd,
		     bool kmemleak_each, bool sync);

#define KMEMLEAK_RESFILENAME "kmemleak.txt"

#endif /* RUNNER_KMEMLEAK_H */
