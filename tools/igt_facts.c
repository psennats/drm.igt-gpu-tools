// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include "igt.h"
#include "igt_facts.h"

/**
 * SECTION:igt_facts
 * @short_description: igt_facts
 * @title: igt_facts
 * @include: igt_facts.c
 *
 * # igtfacts
 *
 * Scan for igt-facts and print them on screen. Indicate if no facts are found.
 */
int main(int argc, char *argv[])
{
	igt_facts_lists_init();

	igt_facts("igt_facts");

	if (igt_facts_are_all_lists_empty())
		igt_info("No facts found...\n");
}
