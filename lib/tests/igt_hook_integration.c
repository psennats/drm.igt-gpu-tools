// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2024 Intel Corporation. All rights reserved.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "igt_core.h"

#include "igt_tests_common.h"

char prog[] = "igt_hook_integration";
char hook_opt[] = "--hook";
char hook_str[128];
char *fake_argv[] = {prog, hook_opt, hook_str};
int fake_argc = sizeof(fake_argv) / sizeof(fake_argv[0]);

#define ENV_ARRAY(evt_name, fullname_suffix, subtest, dyn_subtest, result) \
{ \
	"IGT_HOOK_EVENT=" evt_name, \
	"IGT_HOOK_TEST_FULLNAME=igt@igt_hook_integration" fullname_suffix, \
	"IGT_HOOK_TEST=igt_hook_integration", \
	"IGT_HOOK_SUBTEST=" subtest, \
	"IGT_HOOK_DYN_SUBTEST=" dyn_subtest, \
	"IGT_HOOK_RESULT=" result, \
}

#define TEST_ENV(evt_name, result) \
	ENV_ARRAY(evt_name, "", "", "", result)

#define SUBTEST_ENV(evt_name, subtest, result) \
	ENV_ARRAY(evt_name, "@" subtest, subtest, "", result)

#define DYN_SUBTEST_ENV(evt_name, subtest, dyn_subtest, result) \
	ENV_ARRAY(evt_name, "@" subtest "@" dyn_subtest, subtest, dyn_subtest, result)

const char *pre_test_env[] = TEST_ENV("pre-test", "");
const char *pre_subtest_a_env[] = SUBTEST_ENV("pre-subtest", "a", "");
const char *pre_dyn_subtest_a_success_env[] = DYN_SUBTEST_ENV("pre-dyn-subtest", "a", "success", "");
const char *post_dyn_subtest_a_success_env[] = DYN_SUBTEST_ENV("post-dyn-subtest", "a", "success", "SUCCESS");
const char *pre_dyn_subtest_a_failed_env[] = DYN_SUBTEST_ENV("pre-dyn-subtest", "a", "failed", "");
const char *post_dyn_subtest_a_failed_env[] = DYN_SUBTEST_ENV("post-dyn-subtest", "a", "failed", "FAIL");
const char *pre_dyn_subtest_a_skipped_env[] = DYN_SUBTEST_ENV("pre-dyn-subtest", "a", "skipped", "");
const char *post_dyn_subtest_a_skipped_env[] = DYN_SUBTEST_ENV("post-dyn-subtest", "a", "skipped", "SKIP");
const char *post_subtest_a_env[] = SUBTEST_ENV("post-subtest", "a", "FAIL");
const char *pre_subtest_b_env[] = SUBTEST_ENV("pre-subtest", "b", "");
const char *post_subtest_b_env[] = SUBTEST_ENV("post-subtest", "b", "SUCCESS");
const char *post_test_env[] = TEST_ENV("post-test", "FAIL");

#define num_env_vars (sizeof(pre_test_env) / sizeof(pre_test_env[0]))

__noreturn static void fake_main(void)
{
	igt_subtest_init(fake_argc, fake_argv);

	igt_subtest_with_dynamic("a") {
		igt_dynamic("success") {
			igt_info("...@a@success\n");
		}

		igt_dynamic("failed") {
			igt_assert_f(false, "Fail on purpose\n");
			igt_info("...@a@failed\n");
		}

		igt_dynamic("skipped") {
			igt_require_f(false, "Skip on purpose\n");
			igt_info("...@a@skipped\n");
		}
	}

	igt_subtest("b") {
		igt_info("...@b\n");
	}

	igt_exit();
}

static void test_invalid_hook_str(void)
{
	int status;
	pid_t pid;
	static char err[4096];
	int errfd;

	sprintf(hook_str, "invalid-event:echo hello");

	pid = do_fork_bg_with_pipes(fake_main, NULL, &errfd);

	read_whole_pipe(errfd, err, sizeof(err));

	internal_assert(safe_wait(pid, &status) != -1);
	internal_assert_wexited(status, IGT_EXIT_INVALID);

	internal_assert(strstr(err, "Failed to initialize hook data:"));

	close(errfd);
}

static bool match_env(FILE *hook_out_stream, const char **expected_env)
{
	int i;
	char hook_env_buf[4096];
	size_t buf_len = 0;
	char *line = NULL;
	size_t line_size;
	bool env_checklist[num_env_vars] = {};
	bool has_unexpected = false;
	bool has_missing = false;

	/* Store env from hook so we can show it in case of errors */
	while (getdelim(&line, &line_size, '\0', hook_out_stream) != -1) {
		internal_assert(buf_len + strlen(line) + 1 <= sizeof(hook_env_buf));
		strcpy(hook_env_buf + buf_len, line);
		buf_len += strlen(line) + 1;

		if (!strcmp(line, "---"))
			break;
	}

	if (!expected_env && !buf_len) {
		/* We have consumed everything and we are done now. */
		return false;
	}


	if (!expected_env) {
		printf("Detected unexpected hook execution\n");
		has_unexpected = true;
		goto out;
	}

	if (!buf_len) {
		printf("Expected more hook execution, but none found\n");
		has_missing = true;
		goto out;
	}


	line = hook_env_buf;
	while (strcmp(line, "---")) {
		for (i = 0; i < num_env_vars; i++) {
			if (!strcmp(line, expected_env[i])) {
				env_checklist[i] = true;
				break;
			}
		}

		if (i == num_env_vars) {
			printf("Unexpected envline from hook: %s\n", line);
			has_unexpected = true;
		}

		line += strlen(line) + 1;
	}

	for (i = 0; i < num_env_vars; i++) {
		if (!env_checklist[i]) {
			has_missing = true;
			printf("Missing expected envline: %s\n", expected_env[i]);
		}
	}

out:
	if (has_unexpected || has_missing) {
		if (expected_env) {
			printf("Expected environment:\n");
			for (i = 0; i < num_env_vars; i++)
				printf("  %s\n", expected_env[i]);
		}

		if (buf_len) {
			printf("Environment from hook:\n");
			line = hook_env_buf;
			while (strcmp(line, "---")) {
				printf("  %s\n", line);
				line += strlen(line) + 1;
			}
		} else {
			printf("No hook execution found\n");
		}
	}

	internal_assert(!has_unexpected);
	internal_assert(!has_missing);

	/* Ready to consume next hook output. */
	return true;
}

static void run_tests_and_match_env(const char *evt_descriptors, const char **expected_envs[])
{
	int i;
	int ret;
	int pipefd[2];
	pid_t pid;
	FILE *f;

	ret = pipe(pipefd);
	internal_assert(ret == 0);

	/* Use grep to filter only env var set by us. This should ensure that
	 * writing to the pipe will not block due to capacity, since we only
	 * read from the pipe after the shell command is done. */
	sprintf(hook_str,
		"%1$s:printenv -0 | grep -z ^IGT_HOOK >&%2$d; printf -- ---\\\\00 >&%2$d",
		evt_descriptors,
		pipefd[1]);

	pid = do_fork_bg_with_pipes(fake_main, NULL, NULL);
	internal_assert(safe_wait(pid, &ret) != -1);
	internal_assert_wexited(ret, IGT_EXIT_FAILURE);

	close(pipefd[1]);
	f = fdopen(pipefd[0], "r");
	internal_assert(f);

	i = 0;
	while (match_env(f, expected_envs[i]))
		i++;

	fclose(f);

}

int main(int argc, char **argv)
{
	{
		printf("Check invalid hook string\n");
		test_invalid_hook_str();
	}

	{
		const char **expected_envs[] = {
			pre_test_env,
			pre_subtest_a_env,
			pre_dyn_subtest_a_success_env,
			post_dyn_subtest_a_success_env,
			pre_dyn_subtest_a_failed_env,
			post_dyn_subtest_a_failed_env,
			pre_dyn_subtest_a_skipped_env,
			post_dyn_subtest_a_skipped_env,
			post_subtest_a_env,
			pre_subtest_b_env,
			post_subtest_b_env,
			post_test_env,
			NULL,
		};

		printf("Check full event tracking\n");
		run_tests_and_match_env("*", expected_envs);
	}

	{
		const char **expected_envs[] = {
			pre_dyn_subtest_a_success_env,
			pre_dyn_subtest_a_failed_env,
			pre_dyn_subtest_a_skipped_env,
			NULL,
		};

		printf("Check single event type tracking\n");
		run_tests_and_match_env("pre-dyn-subtest", expected_envs);
	}

	{
		const char **expected_envs[] = {
			pre_subtest_a_env,
			post_dyn_subtest_a_success_env,
			post_dyn_subtest_a_failed_env,
			post_dyn_subtest_a_skipped_env,
			pre_subtest_b_env,
			NULL,
		};

		printf("Check multiple event types tracking\n");
		run_tests_and_match_env("post-dyn-subtest,pre-subtest", expected_envs);
	}
}
