// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2024 Intel Corporation. All rights reserved.
 */

#ifndef IGT_HOOK_H
#define IGT_HOOK_H

#include <stddef.h>
#include <stdio.h>

/**
 * igt_hook:
 *
 * Opaque struct to hold data related to hook support.
 */
struct igt_hook;

/**
 * igt_hook_evt_type:
 * @IGT_HOOK_PRE_TEST: Occurs before a test case (executable) starts the
 * test code.
 * @IGT_HOOK_PRE_SUBTEST: Occurs before the execution of a subtest.
 * @IGT_HOOK_PRE_DYN_SUBTEST: Occurs before the execution of a dynamic subtest.
 * @IGT_HOOK_POST_DYN_SUBTEST: Occurs after the execution of a dynamic subtest.
 * @IGT_HOOK_POST_SUBTEST: Occurs after the execution of a subtest..
 * @IGT_HOOK_POST_TEST: Occurs after a test case (executable) is finished with
 * the test code.
 * @IGT_HOOK_NUM_EVENTS: This is not really an event and represents the number
 * of possible events tracked by igt_hook.
 *
 * Events tracked by igt_hook. Those events occur at specific points during the
 * execution of a test.
 */
enum igt_hook_evt_type {
	IGT_HOOK_PRE_TEST,
	IGT_HOOK_PRE_SUBTEST,
	IGT_HOOK_PRE_DYN_SUBTEST,
	IGT_HOOK_POST_DYN_SUBTEST,
	IGT_HOOK_POST_SUBTEST,
	IGT_HOOK_POST_TEST,
	IGT_HOOK_NUM_EVENTS /* This must always be the last one. */
};

/**
 * igt_hook_evt:
 * @evt_type: Type of event.
 * @target_name: A string pointing to the name of the test, subtest or dynamic
 * subtest, depending on @evt_type.
 * @result: A string containing the result of the test, subtest or dynamic
 * subtest. This is only applicable for the `IGT_HOOK_POST_\*' event types;
 * other types must initialize this to #NULL.
 *
 * An event tracked by igt_hook, which is done with @@igt_hook_event_notify().
 * This must be zero initialized and fields relevant to the event type must be
 * set before passing its reference to @igt_hook_event_notify().
 */
struct igt_hook_evt {
	enum igt_hook_evt_type evt_type;
	const char *target_name;
	const char *result;
};

int igt_hook_create(const char **hook_strs, size_t n, struct igt_hook **igt_hook_ptr);
void igt_hook_free(struct igt_hook *igt_hook);
void igt_hook_event_notify(struct igt_hook *igt_hook, struct igt_hook_evt *evt);
const char *igt_hook_error_str(int error);
void igt_hook_print_help(FILE *f, const char *option_name);

#endif /* IGT_HOOK_H */
