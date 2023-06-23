/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2021-2022 Caleb Connolly <caleb@connolly.tech>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http:// www.gnu.org/licenses/>.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <list>
#include <map>
#include <regex>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#include "gpt-utils.h"
#include "ufs-bsg.h"
#include "utils.h"

#include "bootctrl.h"

#define BOOTDEV_DIR	  "/dev/disk/by-partlabel"
#define BOOT_IMG_PTN_NAME "boot_"
#define LUN_NAME_END_LOC  14
#define BOOT_SLOT_PROP	  "slot_suffix"

#define MAX_CMDLINE_SIZE  4096

#define SLOT_ACTIVE	  1
#define SLOT_INACTIVE	  2
#define UPDATE_SLOT(pentry, guid, slot_state)                                                      \
	({                                                                                         \
		memcpy(pentry, guid, TYPE_GUID_SIZE);                                              \
		if (slot_state == SLOT_ACTIVE)                                                     \
			*(pentry + AB_FLAG_OFFSET) = AB_SLOT_ACTIVE_VAL;                           \
		else if (slot_state == SLOT_INACTIVE)                                              \
			*(pentry + AB_FLAG_OFFSET) =                                               \
				(*(pentry + AB_FLAG_OFFSET) & ~AB_PARTITION_ATTR_SLOT_ACTIVE);     \
	})

using namespace std;
const char *slot_suffix_arr[] = { AB_SLOT_A_SUFFIX, AB_SLOT_B_SUFFIX, NULL };

enum part_attr_type {
	ATTR_SLOT_ACTIVE = 0,
	ATTR_BOOT_SUCCESSFUL,
	ATTR_UNBOOTABLE,
	ATTR_BOOTABLE,
};

void get_kernel_cmdline_arg(const char *arg, char *buf, const char *def)
{
	int fd;
	char pcmd[MAX_CMDLINE_SIZE];
	char *val, *found, *ptr = buf;
	fd = open("/proc/cmdline", O_RDONLY);
	int rc = read(fd, pcmd, MAX_CMDLINE_SIZE);
	if (rc < 0) {
		fprintf(stderr, "Couldn't open /proc/cmdline: %d (%s)\n", rc, strerror(errno));
		goto error;
	}
	close(fd);
	found = strstr(pcmd, arg);
	if (!found || !(val = strstr(found, "="))) {
		fprintf(stderr, "Couldn't find cmdline arg: '%s'\n", arg);
		goto error;
	}

	val++;
	// no this doesn't handle quotes lol
	while (*val != ' ') {
		*ptr++ = *val++;
	}

	return;

error:
	strcpy(buf, def);
}

// Get the value of one of the attribute fields for a partition.
static int get_partition_attribute(struct gpt_disk *disk, char *partname,
				   enum part_attr_type part_attr)
{
	uint8_t *pentry = nullptr;
	int retval = -1;
	uint8_t *attr = nullptr;
	if (!partname)
		return -1;

	// Will initialise the disk if null, or reinitialise it if
	// it's for a partition on a different disk
	if (gpt_disk_get_disk_info(partname, disk) < 0) {
		fprintf(stderr, "%s: gpt_disk_get_disk_info failed\n", __func__);
		return -1;
	}

	pentry = gpt_disk_get_pentry(disk, partname, PRIMARY_GPT);

	if (!pentry) {
		fprintf(stderr, "%s: pentry does not exist in disk struct\n", __func__);
		return -1;
	}

	attr = pentry + AB_FLAG_OFFSET;
	LOGD("%s() partname = %s, attr = 0x%x\n", __func__, partname, *attr);
	switch (part_attr) {
	case ATTR_SLOT_ACTIVE:
		retval = !!(*attr & AB_PARTITION_ATTR_SLOT_ACTIVE);
		LOGD("ATTR_SLOT_ACTIVE, retval = %d\n", retval);
		break;
	case ATTR_BOOT_SUCCESSFUL:
		retval = !!(*attr & AB_PARTITION_ATTR_BOOT_SUCCESSFUL);
		LOGD("AB_PARTITION_ATTR_BOOT_SUCCESSFUL, retval = %d\n", retval);
		break;
	case ATTR_UNBOOTABLE:
		retval = !!(*attr & AB_PARTITION_ATTR_UNBOOTABLE);
		LOGD("AB_PARTITION_ATTR_UNBOOTABLE, retval = %d\n", retval);
		break;
	default:
		retval = -1;
	}

	return retval;
}

// Set a particular attribute for all the partitions in a
// slot
static int update_slot_attribute(struct gpt_disk *disk, const char *slot,
				 enum part_attr_type ab_attr)
{
	unsigned int i = 0;
	char buf[PATH_MAX];
	struct stat st;
	uint8_t *pentry = nullptr;
	uint8_t *pentry_bak = nullptr;
	int rc = -1;
	uint8_t *attr = nullptr;
	uint8_t *attr_bak = nullptr;
	char partName[MAX_GPT_NAME_SIZE + 1] = { 0 };
	static const char ptn_list[][MAX_GPT_NAME_SIZE - 1] = { AB_PTN_LIST };
	int slot_name_valid = 0;
	char devpath[PATH_MAX] = { 0 };

	if (!slot) {
		fprintf(stderr, "%s: Invalid argument\n", __func__);
		return -1;
	}

	for (i = 0; slot_suffix_arr[i] != nullptr; i++) {
		if (!strncmp(slot, slot_suffix_arr[i], strlen(slot_suffix_arr[i])))
			slot_name_valid = 1;
	}

	if (!slot_name_valid) {
		fprintf(stderr, "%s: Invalid slot name\n", __func__);
		return -1;
	}

	for (i = 0; i < ARRAY_SIZE(ptn_list); i++) {
		memset(buf, '\0', sizeof(buf));
		// Check if A/B versions of this ptn exist
		snprintf(buf, sizeof(buf) - 1, "%s/%s%s", BOOT_DEV_DIR, ptn_list[i],
			 AB_SLOT_A_SUFFIX);
		if (stat(buf, &st) < 0) {
			// partition does not have _a version
			continue;
		}

		memset(buf, '\0', sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "%s/%s%s", BOOT_DEV_DIR, ptn_list[i],
			 AB_SLOT_B_SUFFIX);
		if (stat(buf, &st) < 0) {
			// partition does not have _b version
			continue;
		}

		memset(partName, '\0', sizeof(partName));
		snprintf(partName, sizeof(partName) - 1, "%s%s", ptn_list[i], slot);

		// If the current partition is for a different disk (e.g. /dev/sde when the current disk is /dev/sda)
		// Then commit the current disk
		if (!partition_is_for_disk(partName, disk, devpath, sizeof(devpath))) {
			if (!gpt_disk_commit(disk)) {
				fprintf(stderr, "%s: Failed to commit disk\n", __func__);
				return -1;
			}
		}

		rc = gpt_disk_get_disk_info(partName, disk);
		if (rc != 0) {
			fprintf(stderr, "%s: Failed to get disk info for %s\n", __func__, partName);
			return -1;
		}

		pentry = gpt_disk_get_pentry(disk, partName, PRIMARY_GPT);
		pentry_bak = gpt_disk_get_pentry(disk, partName, SECONDARY_GPT);
		if (!pentry || !pentry_bak) {
			fprintf(stderr, "%s: Failed to get pentry/pentry_bak for %s\n", __func__,
				partName);
			return -1;
		}

		attr = pentry + AB_FLAG_OFFSET;
		LOGD("%s: got pentry for part '%s': 0x%lx (at flags: 0x%x)\n", __func__, partName,
		     *(uint64_t *)pentry, *attr);
		attr_bak = pentry_bak + AB_FLAG_OFFSET;
		switch (ab_attr) {
		case ATTR_BOOT_SUCCESSFUL:
			*attr = (*attr) | AB_PARTITION_ATTR_BOOT_SUCCESSFUL;
			*attr_bak = (*attr_bak) | AB_PARTITION_ATTR_BOOT_SUCCESSFUL;
			break;
		case ATTR_UNBOOTABLE:
			*attr = (*attr) | AB_PARTITION_ATTR_UNBOOTABLE;
			*attr_bak = (*attr_bak) | AB_PARTITION_ATTR_UNBOOTABLE;
			break;
		case ATTR_BOOTABLE:
			*attr = (*attr) ^ AB_PARTITION_ATTR_UNBOOTABLE;
			*attr_bak = (*attr_bak) ^ AB_PARTITION_ATTR_UNBOOTABLE;
			break;
		case ATTR_SLOT_ACTIVE:
			*attr = (*attr) | AB_PARTITION_ATTR_SLOT_ACTIVE;
			*attr_bak = (*attr) | AB_PARTITION_ATTR_SLOT_ACTIVE;
			break;
		default:
			fprintf(stderr, "%s: Unrecognized attr\n", __func__);
			return -1;
		}
	}

	if (gpt_disk_commit(disk)) {
		fprintf(stderr, "%s: Failed to write back entry for %s\n", __func__,
			partName);
		return -1;
	}

	return 0;
}

/*
 * Returns 0 for no slots, or the number of slots found.
 * Fun semantic note: Having "1" slot (ie just a "boot" partition)
 * is the same as having "no slots".
 *
 * This function will never return 1.
 */
unsigned get_number_slots()
{
	struct dirent *de = nullptr;
	DIR *dir_bootdev = nullptr;
	static int slot_count = 0;

	// If we've already counted the slots, return the cached value.
	// If there are no slots then we'll always rerun the search...
	if (slot_count > 0)
		return slot_count;

	static_assert(AB_SLOT_A_SUFFIX[0] == '_', "Breaking change to slot A suffix");
	static_assert(AB_SLOT_B_SUFFIX[0] == '_', "Breaking change to slot B suffix");

	dir_bootdev = opendir(BOOTDEV_DIR);
	// Shouldn't this be an assert?
	if (!dir_bootdev) {
		fprintf(stderr, "%s: Failed to open bootdev dir (%s)\n", __func__, strerror(errno));
		return 0;
	}

	while ((de = readdir(dir_bootdev))) {
		if (de->d_name[0] == '.')
			continue;
		if (!strncmp(de->d_name, BOOT_IMG_PTN_NAME, strlen(BOOT_IMG_PTN_NAME)) &&
		    !!strncmp(de->d_name, "boot_aging\n", strlen("boot_aging"))) {
			slot_count++;
		}
	}

	if (slot_count < 0)
		slot_count = 0;

	closedir(dir_bootdev);

	return slot_count;
}

static int boot_control_check_slot_sanity(unsigned slot)
{
	uint32_t num_slots = get_number_slots();
	if ((num_slots < 1) || (slot > num_slots - 1)) {
		fprintf(stderr, "Invalid slot number %u\n", slot);
		return -1;
	}
	return 0;
}

int get_boot_attr(struct gpt_disk *disk, unsigned slot, enum part_attr_type attr)
{
	char bootPartition[MAX_GPT_NAME_SIZE + 1] = { 0 };

	if (boot_control_check_slot_sanity(slot) != 0) {
		fprintf(stderr, "%s: Argument check failed\n", __func__);
		return -1;
	}

	snprintf(bootPartition, sizeof(bootPartition) - 1, "boot%s", slot_suffix_arr[slot]);

	return get_partition_attribute(disk, bootPartition, attr);
}

static unsigned int get_current_slot_from_kernel_cmdline()
{
	uint32_t num_slots = 0;
	char bootSlotProp[MAX_CMDLINE_SIZE] = { '\0' };
	unsigned i = 0;
	num_slots = get_number_slots();
	if (num_slots <= 1) {
		// Slot 0 is the only slot around.
		return 0;
	}

	get_kernel_cmdline_arg(BOOT_SLOT_PROP, bootSlotProp, "_a");
	if (!strncmp(bootSlotProp, "N/A\n", strlen("N/A"))) {
		fprintf(stderr, "%s: Unable to read boot slot property\n", __func__);
		return 0;
	}

	// Iterate through a list of partitons named as boot+suffix
	// and see which one is currently active.
	for (i = 0; slot_suffix_arr[i] != nullptr; i++) {
		if (!strncmp(bootSlotProp, slot_suffix_arr[i], strlen(slot_suffix_arr[i]))) {
			// printf("%s current_slot = %d\n", __func__, i);
			return i;
		}
	}

	// The HAL spec requires that we return a number between
	// 0 to num_slots - 1. Since something went wrong here we
	// are just going to return the default slot.
	return 0;
}

int is_slot_bootable(unsigned slot)
{
	int attr = 0;
	struct gpt_disk disk = { 0 };

	attr = get_boot_attr(&disk, slot, ATTR_UNBOOTABLE);
	if (attr >= 0)
		return !attr;

	return -1;
}

int mark_boot_successful(unsigned slot)
{
	struct gpt_disk disk = { 0 };
	int successful = get_boot_attr(&disk, slot, ATTR_BOOT_SUCCESSFUL);
	int bootable = get_boot_attr(&disk, slot, ATTR_UNBOOTABLE);
	int ret = 0;

	if (successful < 0 || bootable < 0) {
		fprintf(stderr, "SLOT %s: Failed to read attributes\n", slot_suffix_arr[slot]);
		ret = -1;
		goto out;
	}

	if (!is_slot_bootable(slot)) {
		printf("SLOT %s: was marked unbootable, fixing this"
		       " (I hope you know what you're doing...)\n",
		       slot_suffix_arr[slot]);
		update_slot_attribute(&disk, slot_suffix_arr[slot], ATTR_BOOTABLE);
	}

	if (successful) {
		fprintf(stderr, "SLOT %s: already marked successful\n", slot_suffix_arr[slot]);
		goto out;
	}

	if (update_slot_attribute(&disk, slot_suffix_arr[slot], ATTR_BOOT_SUCCESSFUL)) {
		fprintf(stderr, "SLOT %s: Failed to mark boot successful\n", slot_suffix_arr[slot]);
		ret = -1;
		goto out;
	}

out:
	gpt_disk_free(&disk);
	return ret;
}

const char *get_suffix(unsigned slot)
{
	if (boot_control_check_slot_sanity(slot) != 0)
		return "";
	else
		return slot_suffix_arr[slot];
}

// The argument here is a vector of partition names(including the slot suffix)
// that lie on a single disk
static int boot_ctl_set_active_slot_for_partitions(struct gpt_disk *disk, vector<string> part_list,
						   unsigned slot)
{
	char buf[PATH_MAX] = { 0 };
	char slotA[MAX_GPT_NAME_SIZE + 1] = { 0 };
	char slotB[MAX_GPT_NAME_SIZE + 1] = { 0 };
	char active_guid[TYPE_GUID_SIZE + 1] = { 0 };
	char inactive_guid[TYPE_GUID_SIZE + 1] = { 0 };
	int rc;
	// Pointer to the partition entry of current 'A' partition
	uint8_t *pentryA = nullptr;
	uint8_t *pentryA_bak = nullptr;
	// Pointer to partition entry of current 'B' partition
	uint8_t *pentryB = nullptr;
	uint8_t *pentryB_bak = nullptr;
	struct stat st;
	vector<string>::iterator partition_iterator;

	LOGD("Marking slot %s as active:\n", slot_suffix_arr[slot]);

	for (partition_iterator = part_list.begin(); partition_iterator != part_list.end();
	     partition_iterator++) {
		// Chop off the slot suffix from the partition name to
		// make the string easier to work with.
		string prefix = *partition_iterator;
		LOGD("Part: %s\n", prefix.c_str());
		if (prefix.size() < (strlen(AB_SLOT_A_SUFFIX) + 1)) {
			fprintf(stderr, "Invalid partition name: %s\n", prefix.c_str());
			return -1;
		}
		prefix.resize(prefix.size() - strlen(AB_SLOT_A_SUFFIX));
		// Check if A/B versions of this ptn exist
		snprintf(buf, sizeof(buf) - 1, "%s/%s%s", BOOT_DEV_DIR, prefix.c_str(),
			 AB_SLOT_A_SUFFIX);
		LOGD("\t_a Path: '%s'\n", buf);
		rc = stat(buf, &st);
		if (rc < 0) {
			fprintf(stderr, "Failed to stat() path: %d: %s\n", rc, strerror(errno));
			continue;
		}
		memset(buf, '\0', sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "%s/%s%s", BOOT_DEV_DIR, prefix.c_str(),
			 AB_SLOT_B_SUFFIX);
		// LOGD("\t_b Path: '%s'\n", buf);
		rc = stat(buf, &st);
		if (rc < 0) {
			fprintf(stderr, "Failed to stat() path: %d: %s\n", rc, strerror(errno));
			continue;
		}
		memset(slotA, 0, sizeof(slotA));
		memset(slotB, 0, sizeof(slotA));
		snprintf(slotA, sizeof(slotA) - 1, "%s%s", prefix.c_str(), AB_SLOT_A_SUFFIX);
		snprintf(slotB, sizeof(slotB) - 1, "%s%s", prefix.c_str(), AB_SLOT_B_SUFFIX);

		// Get the disk containing the partitions that were passed in.
		// All partitions passed in must lie on the same disk.
		if (!gpt_disk_is_valid(disk)) {
			if (gpt_disk_get_disk_info(slotA, disk) < 0)
				return -1;
		}
		// Get partition entry for slot A & B from the primary
		// and backup tables.
		pentryA = gpt_disk_get_pentry(disk, slotA, PRIMARY_GPT);
		pentryA_bak = gpt_disk_get_pentry(disk, slotA, SECONDARY_GPT);
		pentryB = gpt_disk_get_pentry(disk, slotB, PRIMARY_GPT);
		pentryB_bak = gpt_disk_get_pentry(disk, slotB, SECONDARY_GPT);
		if (!pentryA || !pentryA_bak || !pentryB || !pentryB_bak) {
			// None of these should be NULL since we have already
			// checked for A & B versions earlier.
			fprintf(stderr, "Slot pentries for %s not found.\n", prefix.c_str());
			return -1;
		}
		LOGD("\tAB attr (A): 0x%x (backup: 0x%x)\n", *(uint16_t *)(pentryA + AB_FLAG_OFFSET),
		     *(uint16_t *)(pentryA_bak + AB_FLAG_OFFSET));
		LOGD("\tAB attr (B): 0x%x (backup: 0x%x)\n", *(uint16_t *)(pentryB + AB_FLAG_OFFSET),
		     *(uint16_t *)(pentryB_bak + AB_FLAG_OFFSET));
		memset(active_guid, '\0', sizeof(active_guid));
		memset(inactive_guid, '\0', sizeof(inactive_guid));
		if (get_partition_attribute(disk, slotA, ATTR_SLOT_ACTIVE) == 1) {
			// A is the current active slot
			memcpy((void *)active_guid, (const void *)pentryA, TYPE_GUID_SIZE);
			memcpy((void *)inactive_guid, (const void *)pentryB, TYPE_GUID_SIZE);
		} else if (get_partition_attribute(disk, slotB, ATTR_SLOT_ACTIVE) == 1) {
			// B is the current active slot
			memcpy((void *)active_guid, (const void *)pentryB, TYPE_GUID_SIZE);
			memcpy((void *)inactive_guid, (const void *)pentryA, TYPE_GUID_SIZE);
		} else {
			fprintf(stderr, "Both A & B are inactive..Aborting");
			return -1;
		}
		int a_state = slot == 0 ? SLOT_ACTIVE : SLOT_INACTIVE;
		int b_state = slot == 1 ? SLOT_ACTIVE : SLOT_INACTIVE;

		// This check *Really* shouldn't be here... But I don't know this codebase
		// well enough to remove it.
		if (slot > 1) {
			fprintf(stderr, "%s: Unknown slot %d!\n", __func__, slot);
			return -1;
		}

		// Mark A as active in primary table
		UPDATE_SLOT(pentryA, active_guid, a_state);
		// Mark A as active in backup table
		UPDATE_SLOT(pentryA_bak, active_guid, a_state);
		// Mark B as inactive in primary table
		UPDATE_SLOT(pentryB, inactive_guid, b_state);
		// Mark B as inactive in backup table
		UPDATE_SLOT(pentryB_bak, inactive_guid, b_state);
	}

	// write updated content to disk
	if (gpt_disk_commit(disk)) {
		fprintf(stderr, "Failed to commit disk entry");
		return -1;
	}

	return 0;
}

unsigned get_active_boot_slot()
{
	struct gpt_disk disk = { 0 };
	uint32_t num_slots = get_number_slots();

	if (num_slots <= 1) {
		// Slot 0 is the only slot around.
		return 0;
	}

	for (uint32_t i = 0; i < num_slots; i++) {
		if (get_boot_attr(&disk, i, ATTR_SLOT_ACTIVE)) {
			gpt_disk_free(&disk);
			return i;
		}
	}

	fprintf(stderr, "%s: Failed to find the active boot slot\n", __func__);
	gpt_disk_free(&disk);
	return 0;
}

int set_active_boot_slot(unsigned slot)
{
	map<string, vector<string>> ptn_map;
	vector<string> ptn_vec;
	const char ptn_list[][MAX_GPT_NAME_SIZE] = { AB_PTN_LIST };
	struct gpt_disk disk = { 0 };
	uint32_t i;
	int rc = -1;
	map<string, vector<string>>::iterator map_iter;
	bool ismmc;

	if (boot_control_check_slot_sanity(slot)) {
		fprintf(stderr, "%s: Bad arguments\n", __func__);
		goto out;
	}

	ismmc = gpt_utils_is_partition_backed_by_emmc(PTN_XBL AB_SLOT_A_SUFFIX);

	if (!ismmc && ufs_bsg_dev_open() < 0) {
		goto out;
	}

	// The partition list just contains prefixes(without the _a/_b) of the
	// partitions that support A/B. In order to get the layout we need the
	// actual names. To do this we append the slot suffix to every member
	// in the list.
	for (i = 0; i < ARRAY_SIZE(ptn_list); i++) {
		// XBL is handled differrently for ufs devices so ignore it
		if (!ismmc && !strncmp(ptn_list[i], PTN_XBL, strlen(PTN_XBL)))
			continue;
		// The partition list will be the list of _a partitions
		string cur_ptn = ptn_list[i];
		cur_ptn.append(AB_SLOT_A_SUFFIX);
		ptn_vec.push_back(cur_ptn);
	}

	// The partition map gives us info in the following format:
	// [path_to_block_device_1]--><partitions on device 1>
	// [path_to_block_device_2]--><partitions on device 2>
	// ...
	// ...
	// eg:
	// [/dev/block/sdb]---><system, boot, rpm, tz,....>
	if (gpt_utils_get_partition_map(ptn_vec, ptn_map)) {
		fprintf(stderr, "%s: Failed to get partition map\n", __func__);
		goto out;
	}
	for (map_iter = ptn_map.begin(); map_iter != ptn_map.end(); map_iter++) {
		if (map_iter->second.size() < 1)
			continue;
		if (boot_ctl_set_active_slot_for_partitions(&disk, map_iter->second, slot)) {
			fprintf(stderr, "%s: Failed to set active slot for partitions \n", __func__);
			goto out;
		}
	}

	// EMMC doesn't need attributes to be set.
	if (ismmc)
		return 0;

	if (slot == 0) {
		// Set xbl_a as the boot lun
		rc = gpt_utils_set_xbl_boot_partition(NORMAL_BOOT);
	} else if (slot == 1) {
		// Set xbl_b as the boot lun
		rc = gpt_utils_set_xbl_boot_partition(BACKUP_BOOT);
	} else {
		// Something has gone terribly terribly wrong
		fprintf(stderr, "%s: Unknown slot suffix!\n", __func__);
		goto out;
	}
	if (rc) {
		fprintf(stderr, "%s: Failed to switch xbl boot partition\n", __func__);
		goto out;
	}

	gpt_disk_free(&disk);
	return 0;

out:
	gpt_disk_free(&disk);
	return -1;
}

int set_slot_as_unbootable(unsigned slot)
{
	struct gpt_disk disk = { 0 };
	int ret;

	if (boot_control_check_slot_sanity(slot) != 0)
		return -1;

	ret = update_slot_attribute(&disk, slot_suffix_arr[slot], ATTR_UNBOOTABLE);

	gpt_disk_free(&disk);
	return ret;
}

int is_slot_marked_successful(unsigned slot)
{
	int ret;
	struct gpt_disk disk = { 0 };

	if (boot_control_check_slot_sanity(slot) != 0)
		return -1;

	ret = get_boot_attr(&disk, slot, ATTR_BOOT_SUCCESSFUL);
	gpt_disk_free(&disk);
	return ret;
}

const struct boot_control_module bootctl = {
	.getCurrentSlot = get_current_slot_from_kernel_cmdline,
	.markBootSuccessful = mark_boot_successful,
	.setActiveBootSlot = set_active_boot_slot,
	.setSlotAsUnbootable = set_slot_as_unbootable,
	.isSlotBootable = is_slot_bootable,
	.getSuffix = get_suffix,
	.isSlotMarkedSuccessful = is_slot_marked_successful,
	.getActiveBootSlot = get_active_boot_slot,
};
