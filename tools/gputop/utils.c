// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */
#include <assert.h>

#include "utils.h"

static const char * const bars[] = { " ", "▏", "▎", "▍", "▌", "▋", "▊", "▉", "█" };

void n_spaces(const unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; i++)
		putchar(' ');
}

void print_percentage_bar(double percent, int max_len)
{
	int bar_len, i, len = max_len - 1;
	const int w = PERCLIENT_ENGINE_WIDTH;

	len -= printf("|%5.1f%% ", percent);

	/* no space left for bars, do what we can */
	if (len < 0)
		len = 0;

	bar_len = ceil(w * percent * len / 100.0);
	if (bar_len > w * len)
		bar_len = w * len;

	for (i = bar_len; i >= w; i -= w)
		printf("%s", bars[w]);
	if (i)
		printf("%s", bars[i]);

	len -= (bar_len + (w - 1)) / w;
	n_spaces(len);

	putchar('|');
}

int print_engines_footer(int lines, int con_w, int con_h)
{
	if (lines++ < con_h)
		printf("\n");

	return lines;
}
