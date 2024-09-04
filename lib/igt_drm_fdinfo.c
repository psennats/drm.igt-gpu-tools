/*
 * Copyright © 2022-2023 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <assert.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "drmtest.h"

#include "igt_drm_fdinfo.h"

static size_t read_fdinfo(char *buf, const size_t sz, int at, const char *name)
{
	size_t count;
	int fd;

	fd = openat(at, name, O_RDONLY);
	if (fd < 0)
		return 0;

	count = read(fd, buf, sz - 1);
	if (count > 0)
		buf[count - 1] = 0;
	close(fd);

	return count > 0 ? count : 0;
}

static const char *ignore_space(const char *s)
{
	for (; *s && isspace(*s); s++)
		;

	return s;
}

static int parse_engine(const char *name, struct drm_client_fdinfo *info,
			const char **name_map, unsigned int map_entries,
			uint64_t *val)
{
	const char *p;
	size_t name_len;
	int found = -1;
	unsigned int i;

	p = strchr(name, ':');
	if (!p)
		return -1;

	name_len = p - name;
	if (name_len < 1)
		return -1;
	p++;

	if (name_map) {
		for (i = 0; i < map_entries; i++) {
			if (!strncmp(name, name_map[i], name_len)) {
				found = i;
				break;
			}
		}
	} else {
		for (i = 0; i < info->num_engines; i++) {
			if (!strncmp(name, info->names[i], name_len)) {
				found = i;
				break;
			}
		}

		if (found < 0) {
			assert((info->num_engines + 1) < ARRAY_SIZE(info->names));
			assert(name_len  < sizeof(info->names[0]));
			memcpy(info->names[info->num_engines], name, name_len);
			info->names[info->num_engines][name_len] = '\0';
			found = info->num_engines;
		}
	}

	p++;
	if (found >= 0)
		*val = strtoull(p, NULL, 10);

	return found;
}

static int parse_region(const char *name, struct drm_client_fdinfo *info,
			const char **region_map, unsigned int region_entries,
			uint64_t *val)
{
	char *p;
	ssize_t name_len;
	int found = -1;
	unsigned int i;

	p = strchr(name, ':');
	if (!p)
		return -1;

	name_len = p - name;
	if (name_len < 1)
		return -1;

	p++;

	if (region_map) {
		for (i = 0; i < region_entries; i++) {
			if (!strncmp(name, region_map[i], name_len)) {
				found = i;
				if (!info->region_names[info->num_regions][0]) {
					assert(name_len < sizeof(info->region_names[i]));
					strncpy(info->region_names[i], name, name_len);
				}
				break;
			}
		}
	} else {
		for (i = 0; i < info->num_regions; i++) {
			if (!strncmp(name, info->region_names[i], name_len)) {
				found = i;
				break;
			}
		}

		if (found < 0) {
			assert((info->num_regions + 1) < ARRAY_SIZE(info->region_names));
			assert((strlen(name) + 1) < sizeof(info->region_names[0]));
			strncpy(info->region_names[info->num_regions], name, name_len);
			found = info->num_regions;
		}
	}

	if (found < 0)
		goto out;

	p++;
	*val = strtoull(p, &p, 10);
	p = (char *)ignore_space(p);
	if (!*p)
		goto out;

	if (!strcmp(p, "KiB")) {
		*val *= 1024;
	} else if (!strcmp(p, "MiB")) {
		*val *= 1024 * 1024;
	} else if (!strcmp(p, "GiB")) {
		*val *= 1024 * 1024 * 1024;
	}

out:
	return found;
}

#define UPDATE_REGION(idx, region, val)					\
	do {								\
		if (idx >= 0) {						\
			info->region_mem[idx].region = val;		\
			if (!regions_found[idx]) {			\
				info->num_regions++;			\
				regions_found[idx] = true;		\
				if (idx > info->last_region_index)	\
					info->last_region_index = idx;	\
			}						\
		}							\
	} while (0)

#define UPDATE_ENGINE(idx, member, val, utilization_key)		\
	do {								\
		if (idx >= 0) {						\
			info->member[idx] = val;			\
			info->utilization_mask |= utilization_key;	\
			if (!info->capacity[idx])			\
				info->capacity[idx] = 1;		\
			if (!engines_found[idx]) {			\
				info->num_engines++;			\
				engines_found[idx] = true;		\
				if (idx > info->last_engine_index)	\
					info->last_engine_index = idx;	\
			}						\
		}							\
	} while (0)

#define strstartswith(a, b, plen__) ({					\
		*plen__ = strlen(b);					\
		strncmp(a, b, *plen__) == 0;				\
})

unsigned int
__igt_parse_drm_fdinfo(int dir, const char *fd, struct drm_client_fdinfo *info,
		       const char **name_map, unsigned int map_entries,
		       const char **region_map, unsigned int region_entries)
{
	bool regions_found[DRM_CLIENT_FDINFO_MAX_REGIONS] = { };
	bool engines_found[DRM_CLIENT_FDINFO_MAX_ENGINES] = { };
	unsigned int good = 0, num_capacity = 0;
	char buf[4096], *_buf = buf;
	char *l, *ctx = NULL;
	size_t count;

	count = read_fdinfo(buf, sizeof(buf), dir, fd);
	if (!count)
		return 0;

	while ((l = strtok_r(_buf, "\n", &ctx))) {
		uint64_t val = 0;
		size_t keylen;
		const char *v;
		char *end_ptr;
		int idx;

		_buf = NULL;

		if (strstartswith(l, "drm-driver:", &keylen)) {
			v = ignore_space(l + keylen);
			if (*v) {
				strncpy(info->driver, v, sizeof(info->driver) - 1);
				good++;
			}
		}  else if (strstartswith(l, "drm-client-id:", &keylen)) {
			v = l + keylen;
			info->id = strtol(v, &end_ptr, 10);
			if (end_ptr != v)
				good++;
		} else if (strstartswith(l, "drm-pdev:", &keylen)) {
			v = ignore_space(l + keylen);
			strncpy(info->pdev, v, sizeof(info->pdev) - 1);
		} else if (strstartswith(l, "drm-engine-capacity-", &keylen)) {
			idx = parse_engine(l + keylen, info,
					   name_map, map_entries, &val);
			if (idx >= 0) {
				info->capacity[idx] = val;
				num_capacity++;
			}
		} else if (strstartswith(l, "drm-engine-", &keylen)) {
			idx = parse_engine(l + keylen, info,
					   name_map, map_entries, &val);
			UPDATE_ENGINE(idx, engine_time, val, DRM_FDINFO_UTILIZATION_ENGINE_TIME);
		} else if (strstartswith(l, "drm-cycles-", &keylen)) {
			idx = parse_engine(l + keylen, info,
					   name_map, map_entries, &val);
			UPDATE_ENGINE(idx, cycles, val, DRM_FDINFO_UTILIZATION_CYCLES);
		} else if (strstartswith(l, "drm-total-cycles-", &keylen)) {
			idx = parse_engine(l + keylen, info,
					   name_map, map_entries, &val);
			UPDATE_ENGINE(idx, total_cycles, val, DRM_FDINFO_UTILIZATION_TOTAL_CYCLES);
		} else if (strstartswith(l, "drm-total-", &keylen)) {
			idx = parse_region(l + keylen, info,
					   region_map, region_entries, &val);
			UPDATE_REGION(idx, total, val);
		} else if (strstartswith(l, "drm-shared-", &keylen)) {
			idx = parse_region(l + keylen, info,
					   region_map, region_entries, &val);
			UPDATE_REGION(idx, shared, val);
		} else if (strstartswith(l, "drm-resident-", &keylen)) {
			idx = parse_region(l + keylen, info,
					   region_map, region_entries, &val);
			UPDATE_REGION(idx, resident, val);
		} else if (strstartswith(l, "drm-memory-", &keylen)) {
			/* amdgpu legacy key */
			idx = parse_region(l + keylen, info,
					   region_map, region_entries, &val);
			UPDATE_REGION(idx, resident, val);
		} else if (strstartswith(l, "drm-purgeable-", &keylen)) {
			idx = parse_region(l + keylen, info,
					   region_map, region_entries, &val);
			UPDATE_REGION(idx, purgeable, val);
		} else if (strstartswith(l, "drm-active-", &keylen)) {
			idx = parse_region(l + keylen, info,
					   region_map, region_entries, &val);
			UPDATE_REGION(idx, active, val);
		}
	}

	if (good < 2 || (!info->num_engines && !info->num_regions))
		return 0; /* fdinfo format not as expected */

	return good + info->num_engines + num_capacity + info->num_regions;
}

unsigned int
igt_parse_drm_fdinfo(int drm_fd, struct drm_client_fdinfo *info,
		     const char **name_map, unsigned int map_entries,
		     const char **region_map, unsigned int region_entries)
{
	unsigned int res;
	char fd[64];
	int dir, ret;

	ret = snprintf(fd, sizeof(fd), "%u", drm_fd);
	if (ret < 0 || ret == sizeof(fd))
		return false;

	dir = open("/proc/self/fdinfo", O_DIRECTORY | O_RDONLY);
	if (dir < 0)
		return false;

	res = __igt_parse_drm_fdinfo(dir, fd, info, name_map, map_entries,
				     region_map, region_entries);

	close(dir);

	return res;
}
