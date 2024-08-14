// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2024 Intel Corporation. All rights reserved.
 */

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "igt_hook.h"

/**
 * SECTION:igt_hook
 * @short_description: Support for running a hook script on test execution
 * @title: Hook support
 *
 * IGT provides support for running a hook script when executing tests. This
 * support is provided to users via CLI option `--hook` available in test
 * binaries. Users should use `--help-hook` for detailed usaged description of
 * the feature.
 *
 * The sole user of the exposed API is `igt_core`, which calls @igt_hook_create()
 * when initializing a test case, then calls @igt_hook_event_notify() for each
 * event that occurs during that test's execution and finally calls
 * @igt_hook_free() to clean up at the end.
 */

#define TEST_NAME_INITIAL_SIZE 16

typedef uint16_t evt_mask_t;

struct igt_hook_descriptor {
	evt_mask_t evt_mask;
	char *cmd;
};

struct igt_hook {
	struct igt_hook_descriptor *descriptors;
	char *test_name;
	size_t test_name_size;
	char *subtest_name;
	size_t subtest_name_size;
	char *dyn_subtest_name;
	size_t dyn_subtest_name_size;
	char *test_fullname;
};

enum igt_hook_error {
	IGT_HOOK_EVT_EMPTY_NAME = 1,
	IGT_HOOK_EVT_NO_MATCH,
};

static_assert(IGT_HOOK_NUM_EVENTS <= sizeof(evt_mask_t) * CHAR_BIT,
	      "Number of event types does not fit event type mask");

static const char *igt_hook_evt_type_to_name(enum igt_hook_evt_type evt_type)
{
	switch (evt_type) {
	case IGT_HOOK_PRE_TEST:
		return "pre-test";
	case IGT_HOOK_PRE_SUBTEST:
		return "pre-subtest";
	case IGT_HOOK_PRE_DYN_SUBTEST:
		return "pre-dyn-subtest";
	case IGT_HOOK_POST_DYN_SUBTEST:
		return "post-dyn-subtest";
	case IGT_HOOK_POST_SUBTEST:
		return "post-subtest";
	case IGT_HOOK_POST_TEST:
		return "post-test";
	case IGT_HOOK_NUM_EVENTS:
		break;
	/* No "default:" case, to force a warning from -Wswitch in case we miss
	 * any new event type. */
	}
	return "?";
}

static int igt_hook_parse_hook_str(const char *hook_str, evt_mask_t *evt_mask, const char **cmd)
{
	const char *s;

	if (!strchr(hook_str, ':')) {
		*evt_mask = ~0;
		*cmd = hook_str;
		return 0;
	}

	s = hook_str;
	*evt_mask = 0;

	while (1) {
		const char *evt_name;
		bool has_match;
		bool is_star;
		enum igt_hook_evt_type evt_type;

		evt_name = s;

		while (*s != ':' && *s != ',')
			s++;

		if (evt_name == s)
			return IGT_HOOK_EVT_EMPTY_NAME;

		has_match = false;
		is_star = *evt_name == '*' && evt_name + 1 == s;

		for (evt_type = IGT_HOOK_PRE_TEST; evt_type < IGT_HOOK_NUM_EVENTS; evt_type++) {
			if (!is_star) {
				const char *this_event_name = igt_hook_evt_type_to_name(evt_type);
				size_t len = s - evt_name;

				if (len != strlen(this_event_name))
					continue;

				if (strncmp(evt_name, this_event_name, len))
					continue;
			}

			*evt_mask |= 1 << evt_type;
			has_match = true;

			if (!is_star)
				break;
		}

		if (!has_match)
			return IGT_HOOK_EVT_NO_MATCH;

		if (*s++ == ':')
			break;
	}

	*cmd = s;

	return 0;
}

static size_t igt_hook_calc_test_fullname_size(struct igt_hook *igt_hook)
{
	/* The maximum size of test_fullname will be the maximum length of
	 * "igt@<test_name>@<subtest_name>@<dyn_subtest_name>" plus 1 for the
	 * null byte. */
	return igt_hook->test_name_size + igt_hook->subtest_name_size + igt_hook->dyn_subtest_name_size + 4;
}

static void igt_hook_update_test_fullname(struct igt_hook *igt_hook)
{
	int i;
	char *s;
	const char *values[] = {
		igt_hook->test_name,
		igt_hook->subtest_name,
		igt_hook->dyn_subtest_name,
		NULL,
	};

	if (igt_hook->test_name[0] == '\0') {
		igt_hook->test_fullname[0] = '\0';
		return;
	}

	s = stpcpy(igt_hook->test_fullname, "igt");
	for (i = 0; values[i] && values[i][0] != '\0'; i++) {
		*s++ = '@';
		s = stpcpy(s, values[i]);
	}
}

/**
 * igt_hook_create:
 * @hook_str: Array of hook strings.
 * @n: Number of element in @hook_strs.
 * @igt_hook_ptr: Destination of the struct igt_hook pointer.
 *
 * Allocate and initialize an #igt_hook structure.
 *
 * This function parses the hook descriptors in @hook_strs and initializes the
 * struct. The pointer to the allocated structure is stored in @igt_hook_ptr.
 *
 * Each hook descriptor comes from the argument to `--hook` of the test
 * executable being run.
 *
 * If an error happens, the returned error number can be passed to
 * @igt_hook_error_str() to get a human-readable error message.
 *
 * Returns: Zero on success and a non-zero value on error.
 */
int igt_hook_create(const char **hook_strs, size_t n, struct igt_hook **igt_hook_ptr)
{
	int ret;
	size_t cmd_buffer_size;
	char *cmd_buffer;
	struct igt_hook *igt_hook = NULL;

	/* Parse hook descriptors the first time to learn the needed size. */
	cmd_buffer_size = 0;
	for (size_t i = 0; i < n; i++) {
		evt_mask_t evt_mask;
		const char *cmd;

		ret = igt_hook_parse_hook_str(hook_strs[i], &evt_mask, &cmd);
		if (ret)
			goto out;

		cmd_buffer_size += strlen(cmd) + 1;
	}

	igt_hook = calloc(1, (sizeof(*igt_hook) + (n + 1) * sizeof(*igt_hook->descriptors) +
			      cmd_buffer_size));

	/* Now parse hook descriptors a second time and store the result. */
	igt_hook->descriptors = (void *)igt_hook + sizeof(*igt_hook);
	cmd_buffer = (void *)igt_hook->descriptors + (n + 1) * sizeof(*igt_hook->descriptors);
	for (size_t i = 0; i < n; i++) {
		evt_mask_t evt_mask;
		const char *cmd;

		igt_hook_parse_hook_str(hook_strs[i], &evt_mask, &cmd);
		strcpy(cmd_buffer, cmd);
		igt_hook->descriptors[i].evt_mask = evt_mask;
		igt_hook->descriptors[i].cmd = cmd_buffer;
		cmd_buffer += strlen(cmd) + 1;
	}

	igt_hook->test_name = malloc(TEST_NAME_INITIAL_SIZE);
	igt_hook->test_name_size = TEST_NAME_INITIAL_SIZE;
	igt_hook->subtest_name = malloc(TEST_NAME_INITIAL_SIZE);
	igt_hook->subtest_name_size = TEST_NAME_INITIAL_SIZE;
	igt_hook->dyn_subtest_name = malloc(TEST_NAME_INITIAL_SIZE);
	igt_hook->dyn_subtest_name_size = TEST_NAME_INITIAL_SIZE;
	igt_hook->test_fullname = malloc(igt_hook_calc_test_fullname_size(igt_hook));

	igt_hook->test_name[0] = '\0';
	igt_hook->subtest_name[0] = '\0';
	igt_hook->dyn_subtest_name[0] = '\0';
	igt_hook->test_fullname[0] = '\0';

out:
	if (ret)
		igt_hook_free(igt_hook);
	else
		*igt_hook_ptr = igt_hook;

	return ret;
}

/**
 * igt_hook_free:
 * @igt_hook: The igt_hook struct.
 *
 * De-initialize an igt_hook struct returned by @igt_hook_create().
 *
 * This is a no-op if @igt_hook is #NULL.
 */
void igt_hook_free(struct igt_hook *igt_hook)
{
	if (!igt_hook)
		return;

	free(igt_hook->test_name);
	free(igt_hook->subtest_name);
	free(igt_hook->dyn_subtest_name);
	free(igt_hook);
}

static void igt_hook_update_test_name_pre_call(struct igt_hook *igt_hook, struct igt_hook_evt *evt)
{
	char **name_ptr;
	size_t *size_ptr;
	size_t len;

	switch (evt->evt_type) {
	case IGT_HOOK_PRE_TEST:
		name_ptr = &igt_hook->test_name;
		size_ptr = &igt_hook->test_name_size;
		break;
	case IGT_HOOK_PRE_SUBTEST:
		name_ptr = &igt_hook->subtest_name;
		size_ptr = &igt_hook->subtest_name_size;
		break;
	case IGT_HOOK_PRE_DYN_SUBTEST:
		name_ptr = &igt_hook->dyn_subtest_name;
		size_ptr = &igt_hook->dyn_subtest_name_size;
		break;
	default:
		return;
	}

	len = strlen(evt->target_name);
	if (len + 1 > *size_ptr) {
		size_t fullname_size;

		*size_ptr *= 2;
		*name_ptr = realloc(*name_ptr, *size_ptr);

		fullname_size = igt_hook_calc_test_fullname_size(igt_hook);
		igt_hook->test_fullname = realloc(igt_hook->test_fullname, fullname_size);
	}

	strcpy(*name_ptr, evt->target_name);
	igt_hook_update_test_fullname(igt_hook);
}

static void igt_hook_update_test_name_post_call(struct igt_hook *igt_hook, struct igt_hook_evt *evt)
{
	switch (evt->evt_type) {
	case IGT_HOOK_POST_TEST:
		igt_hook->test_name[0] = '\0';
		break;
	case IGT_HOOK_POST_SUBTEST:
		igt_hook->subtest_name[0] = '\0';
		break;
	case IGT_HOOK_POST_DYN_SUBTEST:
		igt_hook->dyn_subtest_name[0] = '\0';
		break;
	default:
		return;
	}

	igt_hook_update_test_fullname(igt_hook);
}

static void igt_hook_update_env_vars(struct igt_hook *igt_hook, struct igt_hook_evt *evt)
{
	setenv("IGT_HOOK_EVENT", igt_hook_evt_type_to_name(evt->evt_type), 1);
	setenv("IGT_HOOK_TEST_FULLNAME", igt_hook->test_fullname, 1);
	setenv("IGT_HOOK_TEST", igt_hook->test_name, 1);
	setenv("IGT_HOOK_SUBTEST", igt_hook->subtest_name, 1);
	setenv("IGT_HOOK_DYN_SUBTEST", igt_hook->dyn_subtest_name, 1);
	setenv("IGT_HOOK_RESULT", evt->result ?: "", 1);
}

/**
 * igt_hook_event_notify:
 * @igt_hook: The igt_hook structure.
 * @evt: The event to be pushed.
 *
 * Push a new igt_hook event.
 *
 * The argument to @igt_hook can be #NULL, which is equivalent to a no-op.
 *
 * This function must be used to notify on a new igt_hook event. Calling it will
 * cause execution of the hook script if the event type matches the filters
 * provided during initialization of @igt_hook.
 */
void igt_hook_event_notify(struct igt_hook *igt_hook, struct igt_hook_evt *evt)
{
	evt_mask_t evt_bit;
	bool has_match = false;

	if (!igt_hook)
		return;

	evt_bit = 1 << evt->evt_type;
	igt_hook_update_test_name_pre_call(igt_hook, evt);

	for (size_t i = 0; igt_hook->descriptors[i].cmd; i++) {
		if (evt_bit & igt_hook->descriptors[i].evt_mask) {
			has_match = true;
			break;
		}
	}

	if (has_match) {
		igt_hook_update_env_vars(igt_hook, evt);

		for (size_t i = 0; igt_hook->descriptors[i].cmd; i++)
			if (evt_bit & igt_hook->descriptors[i].evt_mask)
				system(igt_hook->descriptors[i].cmd);
	}

	igt_hook_update_test_name_post_call(igt_hook, evt);
}

/**
 * igt_hook_error_str:
 * @error: Non-zero error number.
 *
 * Return a human-readable string containing a description of an error number
 * generated by one of the `igt_hook_*` functions.
 *
 * The string will be the result of strerror() for errors from the C standard
 * library or a custom description specific to igt_hook.
 */
const char *igt_hook_error_str(int error)
{
	if (!error)
		return "No error";

	switch (error) {
	case IGT_HOOK_EVT_EMPTY_NAME:
		return "Empty name in event descriptor";
	case IGT_HOOK_EVT_NO_MATCH:
		return "Event name in event descriptor does not match any event type";
	default:
		return "Unknown error";
	}
}

/**
 * igt_hook_print_help:
 * @f: File pointer where to write the output.
 * @option_name: Name of the CLI option that accepts the hook descriptor.
 *
 * Print a detailed user help text on hook usage.
 */
void igt_hook_print_help(FILE *f, const char *option_name)
{
	fprintf(f, "\
The option %1$s receives as argument a \"hook descriptor\" and allows the\n\
execution of a shell command at different points during execution of tests. Each\n\
such a point is called a \"hook event\".\n\
\n\
Examples:\n\
\n\
  # Prints hook-specic env vars for every event.\n\
  %1$s 'printenv | grep ^IGT_HOOK_'\n\
\n\
  # Equivalent to the above. Useful if command contains ':'.\n\
  %1$s '*:printenv | grep ^IGT_HOOK_'\n\
\n\
  # Adds a line to out.txt containing the result of each test case.\n\
  %1$s 'post-test:echo $IGT_HOOK_TEST_FULLNAME $IGT_HOOK_RESULT >> out.txt'\n\
\n\
The accepted format for a hook descriptor is `[<events>:]<cmd>`, where:\n\
\n\
  - <events> is a comma-separated list of event descriptors, which defines the\n\
    set of events be tracked. If omitted, all events are tracked.\n\
\n\
  - <cmd> is a shell command to be executed on the occurrence each tracked\n\
    event. If the command contains ':', then passing <events> is required,\n\
    otherwise part of the command would be treated as an event descriptor.\n\
\n\
", option_name);

	fprintf(f, "\
An \"event descriptor\" is either the name of an event or the string '*'. The\n\
latter matches all event names. The list of possible event names is provided\n\
below:\n\
\n\
");

	for (enum igt_hook_evt_type et = 0; et < IGT_HOOK_NUM_EVENTS; et++) {
		const char *desc;

		switch (et) {
		case IGT_HOOK_PRE_TEST:
			desc = "Occurs before a test case starts.";
			break;
		case IGT_HOOK_PRE_SUBTEST:
			desc = "Occurs before the execution of a subtest.";
			break;
		case IGT_HOOK_PRE_DYN_SUBTEST:
			desc = "Occurs before the execution of a dynamic subtest.";
			break;
		case IGT_HOOK_POST_DYN_SUBTEST:
			desc = "Occurs after the execution of a dynamic subtest.";
			break;
		case IGT_HOOK_POST_SUBTEST:
			desc = "Occurs after the execution of a subtest.";
			break;
		case IGT_HOOK_POST_TEST:
			desc = "Occurs after a test case has finished.";
			break;
		default:
			desc = "MISSING DESCRIPTION";
		}

		fprintf(f, "  %s\n  %s\n\n", igt_hook_evt_type_to_name(et), desc);
	}

	fprintf(f, "\
For each event matched by <events>, <cmd> is executed as a shell command. The\n\
exit status of the command is ignored. The following environment variables are\n\
available to the command:\n\
\n\
  IGT_HOOK_EVENT\n\
  Name of the current event.\n\
\n\
  IGT_HOOK_TEST_FULLNAME\n\
  Full name of the test in the format `igt@<test>[@<subtest>[@<dyn_subtest>]]`.\n\
\n\
  IGT_HOOK_TEST\n\
  Name of the current test.\n\
\n\
  IGT_HOOK_SUBTEST\n\
  Name of the current subtest. Will be the empty string if not running a\n\
  subtest.\n\
\n\
  IGT_HOOK_DYN_SUBTEST\n\
  Name of the current dynamic subtest. Will be the empty string if not running a\n\
  dynamic subtest.\n\
\n\
  IGT_HOOK_RESULT\n\
  String representing the result of the test/subtest/dynamic subtest. Possible\n\
  values are: SUCCESS, SKIP or FAIL. This is only applicable on \"post-*\"\n\
  events and will be the empty string for other types of events.\n\
\n\
\n\
Note that %s can be passed multiple times. Each descriptor is evaluated in turn\n\
when matching events and running hook commands.\n\
", option_name);
}
