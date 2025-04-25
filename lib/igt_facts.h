/* SPDX-License-Identifier: MIT
 * Copyright © 2024 Intel Corporation
 */

#ifndef IGT_FACTS_H
#define IGT_FACTS_H

#include <stdbool.h>

#include "igt_list.h"

/* igt_fact:
 * @name: name of the fact
 * @value: value of the fact
 * @last_test: name of the test that triggered the fact
 * @present: bool indicating if fact is present. Used for deleting facts from
 * the list.
 * @link: link to the next fact
 *
 * A fact is a piece of information that can be used to determine the state of
 * the system.
 *
 */
typedef struct {
	char *name;
	char *value;
	char *last_test;
	bool present; /* For mark and sweep */
	struct igt_list_head link;
} igt_fact;

/* igt_facts configuration:
 * @enabled: bool indicating if igt_facts is enabled
 * @disable_udev: bool indicating if udev is disabled
 */
struct igt_facts_config {
	bool enabled;
	bool disable_udev;
};

void igt_facts_lists_init(void);
void igt_facts(const char *last_test);
bool igt_facts_are_all_lists_empty(void);
void igt_facts_test(void); /* For unit testing only */

#endif /* IGT_FACTS_H */
