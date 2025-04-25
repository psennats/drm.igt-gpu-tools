// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <ctype.h>
#include <libudev.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "igt_core.h"
#include "igt_device_scan.h"
#include "igt_facts.h"
#include "igt_kmod.h"
#include "igt_list.h"
#include "igt_taints.h"

static struct igt_list_head igt_facts_list_drm_card_head;
static struct igt_list_head igt_facts_list_kmod_head;
static struct igt_list_head igt_facts_list_ktaint_head;
static struct igt_list_head igt_facts_list_pci_gpu_head;

static const char *kmod_fact = "kernel.kmod_is_loaded"; /* true or false */
static const char *ktaint_fact = "kernel.is_tainted"; /* name: taint_warn */
static const char *pci_gpu_fact = "hardware.pci.gpu_at_addr"; /*id model */
static const char *drm_card_fact = "hardware.pci.drm_card_at_addr"; /* cardX */

/* There is another module list at lib/drmtest.c. We can't use it here because
 * it's a static list. The drmtest list seems to have a different goal and
 * trying a merge may not work well.
 */
static const char * const igt_fact_kmod_list[] = {
	"amdgpu",
	"i915",
	"msm",
	"nouveau", /* Not on lib/drmtest.c */
	"panfrost",
	"radeon", /* Not on lib/drmtest.c */
	"v3d",
	"vc4",
	"vgem",
	"vmwgfx",
	"xe",
	"\0"
};

struct igt_facts_config igt_facts_config = {
	.disable_udev = false,
};

/**
 * igt_facts_lists_init:
 *
 * Initialize igt_facts linked lists.
 *
 * Returns: void
 */
void igt_facts_lists_init(void)
{
	IGT_INIT_LIST_HEAD(&igt_facts_list_drm_card_head);
	IGT_INIT_LIST_HEAD(&igt_facts_list_kmod_head);
	IGT_INIT_LIST_HEAD(&igt_facts_list_ktaint_head);
	IGT_INIT_LIST_HEAD(&igt_facts_list_pci_gpu_head);
}

/**
 * igt_facts_log:
 * @last_test: name of the test that triggered the fact
 * @name: name of the fact
 * @new_value: new value of the fact
 * @old_value: old value of the fact
 *
 * Reports fact changes:
 * - new fact: if old_value is NULL and new_value is not NULL
 * - deleted fact: if new_value is NULL and old_value is not NULL
 * - changed fact: if new_value is different from old_value
 *
 * Returns: void
 */
static void igt_facts_log(const char *last_test, const char *name,
			  const char *new_value, const char *old_value)
{
	struct timespec uptime_ts;
	char *uptime = NULL;
	const char *before_tests = "before any test";

	if (!old_value && !new_value)
		return;

	if (clock_gettime(CLOCK_BOOTTIME, &uptime_ts) != 0)
		return;

	asprintf(&uptime,
		 "%ld.%06ld",
		 uptime_ts.tv_sec,
		 uptime_ts.tv_nsec / 1000);

	/* New fact */
	if (!old_value && new_value) {
		igt_info("[%s] [FACT %s] new: %s: %s\n",
			 uptime,
			 last_test ? last_test : before_tests,
			 name,
			 new_value);
		goto out;
	}

	/* Update fact */
	if (old_value && new_value) {
		igt_info("[%s] [FACT %s] changed: %s: %s -> %s\n",
			 uptime,
			 last_test ? last_test : before_tests,
			 name,
			 old_value,
			 new_value);
		goto out;
	}

	/* Deleted fact */
	if (old_value && !new_value) {
		igt_info("[%s] [FACT %s] deleted: %s: %s\n",
			 uptime,
			 last_test ? last_test : before_tests,
			 name,
			 old_value);
		goto out;
	}

out:
	free(uptime);
}

/**
 * igt_facts_list_get:
 * @name: name of the fact to be added
 * @head: head of the list
 *
 * Get a fact from the list.
 *
 * Returns: pointer to the fact if found, NULL otherwise
 *
 */
static igt_fact *igt_facts_list_get(const char *name,
				    struct igt_list_head *head)
{
	igt_fact *fact = NULL;

	if (igt_list_empty(head))
		return NULL;

	igt_list_for_each_entry(fact, head, link) {
		if (strcmp(fact->name, name) == 0)
			return fact;
	}
	return NULL;
}

/**
 * igt_facts_list_del:
 * @name: name of the fact to be added
 * @head: head of the list
 * @last_test: name of the last test
 * @log: bool indicating if the delete operation should be logged
 *
 * Delete a fact from the list.
 *
 * Returns: bool indicating if fact was deleted from the list
 *
 */
static bool igt_facts_list_del(const char *name,
			       struct igt_list_head *head,
			       const char *last_test,
			       bool log)
{
	igt_fact *fact = NULL;

	if (igt_list_empty(head))
		return false;

	igt_list_for_each_entry(fact, head, link) {
		if (strcmp(fact->name, name) == 0) {
			if (log)
				igt_facts_log(last_test, fact->name,
					      NULL, fact->value);

			igt_list_del(&fact->link);
			free(fact->name);
			free(fact->value);
			free(fact->last_test);
			free(fact);
			return true;
		}
	}
	return false;
}

/**
 * igt_facts_list_add:
 * @name: name of the fact to be added
 * @value: value of the fact to be added
 * @last_test: name of the last test
 * @head: head of the list
 *
 * Returns: bool indicating if fact was added to the list
 *
 */
static bool igt_facts_list_add(const char *name,
			       const char *value,
			       const char *last_test,
			       struct igt_list_head *head)
{
	igt_fact *new_fact = NULL, *old_fact = NULL;
	bool logged = false;

	if (!name || !value)
		return false;

	old_fact = igt_facts_list_get(name, head);
	if (old_fact) {
		if (strcmp(old_fact->value, value) == 0) {
			old_fact->present = true;
			return false;
		}
		igt_facts_log(last_test, name, value, old_fact->value);
		logged = true;
		igt_facts_list_del(name, head, last_test, false);
	}

	new_fact = malloc(sizeof(igt_fact));
	if (!new_fact)
		return false;

	new_fact->name = strdup(name);
	new_fact->value = strdup(value);
	new_fact->last_test = last_test ? strdup(last_test) : NULL;
	new_fact->present = true;

	if (!logged)
		igt_facts_log(last_test, name, value, NULL);

	igt_list_add(&new_fact->link, head);

	return true;
}

/**
 * igt_facts_list_mark:
 * @head: head of the list
 *
 * Mark all facts in the list as not present. Opted for the mark and sweep
 * design pattern due to its simplicity and efficiency.
 *
 * Returns: void
 */
static void igt_facts_list_mark(struct igt_list_head *head)
{
	igt_fact *fact = NULL;

	if (igt_list_empty(head))
		return;

	igt_list_for_each_entry(fact, head, link)
		fact->present = false;
}

/**
 * igt_facts_list_sweep:
 * @head: head of the list
 * @last_test: name of the last test
 *
 * Sweep the list and delete all facts that are not present. Opted for the mark
 * and sweep design pattern due to its simplicity and efficiency.
 *
 * Returns: void
 */
static void igt_facts_list_sweep(struct igt_list_head *head,
				 const char *last_test)
{
	igt_fact *fact = NULL, *tmp = NULL;

	if (igt_list_empty(head))
		return;

	igt_list_for_each_entry_safe(fact, tmp, head, link)
		if (!fact->present)
			igt_facts_list_del(fact->name, head, last_test, true);
}

/**
 * igt_facts_list_mark_and_sweep:
 * @head: head of the list
 *
 * Clean up the list using mark and sweep. Opted for the mark and sweep
 * design pattern due to its simplicity and efficiency.
 *
 * Returns: void
 */
static void igt_facts_list_mark_and_sweep(struct igt_list_head *head)
{
	igt_facts_list_mark(head);
	igt_facts_list_sweep(head, NULL);
}

/**
 * igt_facts_are_all_lists_empty:
 *
 * Returns true if all lists are empty. Used by the tool lsfacts.
 *
 * Returns: bool
 */
bool igt_facts_are_all_lists_empty(void)
{
	return igt_list_empty(&igt_facts_list_drm_card_head) &&
	       igt_list_empty(&igt_facts_list_kmod_head) &&
	       igt_list_empty(&igt_facts_list_ktaint_head) &&
	       igt_list_empty(&igt_facts_list_pci_gpu_head);
}

/**
 * igt_facts_scan_pci_gpus:
 * @last_test: name of the last test
 *
 * This function scans the pci bus for gpus using udev. It uses
 * igt_facts_list_mark(), igt_facts_list_add() and igt_facts_list_sweep() to
 * update igt_facts_list_pci_gpu_head.
 *
 * Returns: void
 */
static void igt_facts_scan_pci_gpus(const char *last_test)
{
	static struct igt_list_head *head = &igt_facts_list_pci_gpu_head;
	struct udev *udev = NULL;
	struct udev_enumerate *enumerate = NULL;
	struct udev_list_entry *devices, *dev_list_entry;
	struct igt_device_card card;
	char pcistr[10];
	int ret;
	char *factname = NULL;
	char *factvalue = NULL;

	if (igt_facts_config.disable_udev)
		return; /* Intentinally silent */

	udev = udev_new();
	if (!udev) {
		igt_warn("Failed to create udev context\n");
		igt_facts_config.disable_udev = true;
		return;
	}

	enumerate = udev_enumerate_new(udev);
	if (!enumerate) {
		igt_warn("Failed to create udev enumerate\n");
		udev_unref(udev);
		return;
	}

	ret = udev_enumerate_add_match_subsystem(enumerate, "pci");
	if (ret < 0)
		goto out;

	ret = udev_enumerate_add_match_property(enumerate,
						"PCI_CLASS",
						"30000");
	if (ret < 0)
		goto out;

	ret = udev_enumerate_add_match_property(enumerate,
						"PCI_CLASS",
						"38000");
	if (ret < 0)
		goto out;

	ret = udev_enumerate_scan_devices(enumerate);
	if (ret < 0)
		goto out;

	devices = udev_enumerate_get_list_entry(enumerate);
	if (!devices)
		goto out;

	igt_facts_list_mark(head);

	udev_list_entry_foreach(dev_list_entry, devices) {
		const char *path;
		struct udev_device *udev_dev;
		struct udev_list_entry *entry;
		char *model = NULL;
		char *codename = NULL;
		igt_fact *old_fact = NULL;

		path = udev_list_entry_get_name(dev_list_entry);
		udev_dev = udev_device_new_from_syspath(udev, path);
		if (!udev_dev)
			continue;

		/* Strip path to only the content after the last / */
		path = strrchr(path, '/');
		if (path)
			path++;
		else
			path = "unknown";

		strcpy(card.pci_slot_name, "-");

		entry = udev_device_get_properties_list_entry(udev_dev);
		while (entry) {
			const char *name = udev_list_entry_get_name(entry);
			const char *value = udev_list_entry_get_value(entry);

			entry = udev_list_entry_get_next(entry);
			if (!strcmp(name, "ID_MODEL_FROM_DATABASE"))
				model = strdup(value);
			else if (!strcmp(name, "PCI_ID"))
				igt_assert_eq(sscanf(value, "%hx:%hx",
						     &card.pci_vendor,
						     &card.pci_device), 2);
		}
		snprintf(pcistr, sizeof(pcistr), "%04x:%04x",
			 card.pci_vendor, card.pci_device);
		codename = igt_device_get_pretty_name(&card, false);

		/* Set codename to null if it is the same string as pci_id */
		if (codename && strcmp(pcistr, codename) == 0) {
			free(codename);
			codename = NULL;
		}
		asprintf(&factname, "%s.%s", pci_gpu_fact, path);
		asprintf(&factvalue,
			"%s %s %s",
			pcistr,
			codename ? codename : "",
			model ? model : "");

		/**
		 * Loading and unloading the kmod may change the human
		 * readeable string in value. Do not change value if the
		 * pci id is the same.
		 */
		old_fact = igt_facts_list_get(factname, head);
		if (old_fact && strncmp(old_fact->value, factvalue, 9) == 0)
			old_fact->present = true;
		else
			igt_facts_list_add(factname, factvalue, last_test, head);

		free(codename);
		free(model);
		free(factname);
		free(factvalue);
		udev_device_unref(udev_dev);
	}

	igt_facts_list_sweep(head, last_test);

out:
	udev_enumerate_unref(enumerate);
	udev_unref(udev);
}

/**
 * igt_facts_scan_pci_drm_cards:
 * @last_test: name of the last test
 *
 * This function scans the pci bus for drm cards using udev. It uses the
 * igt_facts_list_mark(), igt_facts_list_add() and igt_facts_list_sweep() to
 * update igt_facts_list_drm_card_head.
 *
 * Returns: void
 */
static void igt_facts_scan_pci_drm_cards(const char *last_test)
{
	static struct igt_list_head *head = &igt_facts_list_drm_card_head;
	struct udev *udev = NULL;
	struct udev_enumerate *enumerate = NULL;
	struct udev_list_entry *devices, *dev_list_entry;
	int ret;
	char *factname = NULL;
	char *factvalue = NULL;

	if (igt_facts_config.disable_udev)
		return; /* Intentinally silent */

	udev = udev_new();
	if (!udev) {
		igt_warn("Failed to create udev context\n");
		igt_facts_config.disable_udev = true;
		return;
	}

	enumerate = udev_enumerate_new(udev);
	if (!enumerate) {
		udev_unref(udev);
		return;
	}

	ret = udev_enumerate_add_match_subsystem(enumerate, "drm");
	if (ret < 0)
		goto out;

	ret = udev_enumerate_scan_devices(enumerate);
	if (ret < 0)
		goto out;

	devices = udev_enumerate_get_list_entry(enumerate);
	if (!devices)
		goto out;

	igt_facts_list_mark(head);

	udev_list_entry_foreach(dev_list_entry, devices) {
		const char *path;
		struct udev_device *drm_dev, *pci_dev;
		const char *drm_name, *pci_addr;

		path = udev_list_entry_get_name(dev_list_entry);
		drm_dev = udev_device_new_from_syspath(udev, path);
		if (!drm_dev)
			continue;

		drm_name = udev_device_get_sysname(drm_dev);
		/* Filter the device by name. Want devices such as card0 and card1.
		 * If the device has '-' in the name, contine
		 */
		if (strncmp(drm_name, "card", 4) != 0 ||
		    strchr(drm_name, '-')) {
			udev_device_unref(drm_dev);
			continue;
		}

		/* Get the pci address of the gpu associated with the drm_dev*/
		pci_dev = udev_device_get_parent_with_subsystem_devtype(drm_dev,
									"pci",
									NULL);
		if (pci_dev) {
			pci_addr = udev_device_get_sysattr_value(pci_dev,
								 "address");
			if (!pci_addr)
				pci_addr = udev_device_get_sysname(pci_dev);
			if (!pci_addr) {
				udev_device_unref(drm_dev);
				continue;
			}
		} else {
			/* Some GPUs are platform devices. Ignore them. */
			pci_addr = NULL;
			udev_device_unref(drm_dev);
			continue;
		}

		asprintf(&factname, "%s.%s", drm_card_fact, pci_addr);
		asprintf(&factvalue, "%s", drm_name);

		igt_facts_list_add(factname, factvalue, last_test, head);

		free(factname);
		free(factvalue);
		udev_device_unref(drm_dev);
	}

	igt_facts_list_sweep(head, last_test);

out:
	udev_enumerate_unref(enumerate);
	udev_unref(udev);
}

/**
 * igt_facts_scan_kernel_taints:
 * @last_test: name of the last test
 *
 * This function scans for kernel taints using igt_kernel_tainted() and
 * igt_explain_taints(). It will cut off the explanation keeping only the
 * taint name.
 *
 * Returns: void
 */
static void igt_facts_scan_kernel_taints(const char *last_test)
{
	static struct igt_list_head *head = &igt_facts_list_ktaint_head;
	unsigned long taints = 0;
	const char *reason = NULL;
	char *taint_name = NULL;
	char *fact_name = NULL;

	taints = igt_kernel_tainted(&taints);
	/* For testing, set all bits to 1
	 * taints = 0xFFFFFFFF;
	 */

	igt_facts_list_mark(head);

	while ((reason = igt_explain_taints(&taints))) {
		/* Cut at the ':' to get only the taint name */
		taint_name = strtok(strdup(reason), ":");
		if (!taint_name)
			continue;

		/* Lowercase taint_name */
		for (int i = 0; taint_name[i]; i++)
			taint_name[i] = tolower(taint_name[i]);

		asprintf(&fact_name, "%s.%s", ktaint_fact, taint_name);
		igt_facts_list_add(fact_name, "true", last_test, head);

		free(taint_name);
		free(fact_name);
	}

	igt_facts_list_sweep(head, last_test);
}

/**
 * igt_facts_scan_kernel_loaded_kmods:
 * @last_test: name of the last test
 *
 * This function scans for loaded kmods using igt_fact_kmod_list and
 * igt_kmod_is_loaded().
 *
 * Returns: void
 */
static void igt_facts_scan_kernel_loaded_kmods(const char *last_test)
{
	static struct igt_list_head *head = &igt_facts_list_kmod_head;
	char *name = NULL;

	igt_facts_list_mark(head);

	/* Iterate over igt_fact_kmod_list[] until the element contains "\0" */
	for (int i = 0; strcmp(igt_fact_kmod_list[i], "\0") != 0; i++) {
		asprintf(&name, "%s.%s", kmod_fact, igt_fact_kmod_list[i]);
		if (igt_kmod_is_loaded(igt_fact_kmod_list[i]))
			igt_facts_list_add(name, "true", last_test, head);

		free(name);
	}

	igt_facts_list_sweep(head, last_test);
}

/**
 * igt_facts:
 * @last_test: name of the last test
 *
 * Call this function where you want to gather and report facts.
 *
 * Returns: void
 */
void igt_facts(const char *last_test)
{
	igt_facts_scan_pci_gpus(last_test);
	igt_facts_scan_pci_drm_cards(last_test);
	igt_facts_scan_kernel_taints(last_test);
	igt_facts_scan_kernel_loaded_kmods(last_test);

	fflush(stdout);
	fflush(stderr);
}

/*
 * Testing
 *
 * Defined here to keep most of the functions static
 *
 */

/**
 * igt_facts_test_add_get:
 * @head: head of the list
 *
 * Tests igt_facts_list_add and igt_facts_list_get.
 *
 * Returns: void
 */
static void igt_facts_test_add_get(struct igt_list_head *head)
{
	igt_fact *fact = NULL;
	bool ret;
	const char *name = "hardware.pci.gpu_at_addr.0000:00:02.0";
	const char *value = "8086:64a0 Intel Lunarlake (Gen20)";
	const char *last_test = NULL;

	ret = igt_facts_list_add(name, value, last_test, head);
	igt_assert(ret);

	/* Assert that there is one element in the linked list */
	igt_assert_eq(igt_list_length(head), 1);

	/* Assert that the element in the linked list is the one we added */
	fact = igt_facts_list_get(name, head);
	igt_assert(fact);
	igt_assert_eq(strcmp(fact->name, name), 0);
	igt_assert_eq(strcmp(fact->value, value), 0);
	igt_assert(fact->present);
	igt_assert(!fact->last_test);
}

/**
 * igt_facts_test_mark_and_sweep:
 * @head: head of the list
 *
 * - Add 3 elements to the list and mark them as not present.
 * - Update two of the elements and mark them as present.
 * - Sweep the list and assert that
 *   - Only the two updated elements are present
 *   - The third element was deleted
 *
 * Returns: void
 */
static void igt_facts_test_mark_and_sweep(struct igt_list_head *head)
{
	igt_fact *fact = NULL;
	const char *name1 = "hardware.pci.gpu_at_addr.0000:00:02.0";
	const char *value1 = "8086:64a0 Intel Lunarlake (Gen20)";
	const char *name2 = "hardware.pci.gpu_at_addr.0000:00:03.0";
	const char *value2 = "8086:64a1 Intel Lunarlake (Gen21)";
	const char *name3 = "hardware.pci.gpu_at_addr.0000:00:04.0";
	const char *value3 = "8086:64a2 Intel Lunarlake (Gen22)";

	igt_facts_list_add(name1, value1, NULL, head);
	igt_facts_list_add(name2, value2, NULL, head);
	igt_facts_list_add(name3, value3, NULL, head);

	igt_facts_list_mark(head);

	igt_facts_list_add(name1, value1, NULL, head);
	igt_facts_list_add(name2, value2, NULL, head);

	igt_facts_list_sweep(head, NULL);

	/* Assert that there are two elements in the linked list */
	igt_assert_eq(igt_list_length(head), 2);

	/* Assert that the two updated elements are present */
	fact = igt_facts_list_get(name1, head);
	igt_assert(fact != NULL);
	igt_assert(fact->present);

	fact = igt_facts_list_get(name2, head);
	igt_assert(fact != NULL);
	igt_assert(fact->present);

	/* Assert that the third element was deleted */
	fact = igt_facts_list_get(name3, head);
	igt_assert(fact == NULL);
}

/**
 * igt_facts_test:
 *
 * Main function for testing the igt_facts module
 *
 * Returns: bool indicating if the tests passed
 */
void igt_facts_test(void)
{
	const char *last_test = "Unit Testing";

	igt_facts_lists_init();

	/* Assert that all lists are empty */
	igt_assert(igt_list_empty(&igt_facts_list_kmod_head));
	igt_assert(igt_list_empty(&igt_facts_list_ktaint_head));
	igt_assert(igt_list_empty(&igt_facts_list_pci_gpu_head));
	igt_assert(igt_list_empty(&igt_facts_list_drm_card_head));

	/* Assert that add and get work. Will add one element to the list */
	igt_facts_test_add_get(&igt_facts_list_pci_gpu_head);

	/* Assert that igt_facts_list_mark_and_sweep() cleans up the list */
	igt_assert(!igt_list_empty(&igt_facts_list_pci_gpu_head));
	igt_facts_list_mark_and_sweep(&igt_facts_list_pci_gpu_head);
	igt_assert(igt_list_empty(&igt_facts_list_pci_gpu_head));

	/* Test the mark and sweep pattern used to delete elements
	 * from the list
	 */
	igt_facts_test_mark_and_sweep(&igt_facts_list_pci_gpu_head);

	/* Clean up the list and call igt_facts(). This should not crash */
	igt_facts_list_mark_and_sweep(&igt_facts_list_pci_gpu_head);
	igt_facts(last_test);
}
