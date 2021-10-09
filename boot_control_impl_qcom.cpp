/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <map>
#include <list>
#include <string>
#include <vector>
#include <errno.h>
#include <regex>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>

#include "boot_control.h"
#include "gpt-utils.h"

#define BOOTDEV_DIR "/dev/disk/by-partlabel"
#define BOOT_IMG_PTN_NAME "boot_"
#define LUN_NAME_END_LOC 14
#define BOOT_SLOT_PROP "slot_suffix"

#define MAX_CMDLINE_SIZE 4096

#define SLOT_ACTIVE 1
#define SLOT_INACTIVE 2
#define UPDATE_SLOT(pentry, guid, slot_state) ({ \
		memcpy(pentry, guid, TYPE_GUID_SIZE); \
		if (slot_state == SLOT_ACTIVE)\
			*(pentry + AB_FLAG_OFFSET) = AB_SLOT_ACTIVE_VAL; \
		else if (slot_state == SLOT_INACTIVE) \
		*(pentry + AB_FLAG_OFFSET)  = (*(pentry + AB_FLAG_OFFSET)& \
			~AB_PARTITION_ATTR_SLOT_ACTIVE); \
		})

using namespace std;
const char *slot_suffix_arr[] = {
	AB_SLOT_A_SUFFIX,
	AB_SLOT_B_SUFFIX,
	NULL};

enum part_attr_type {
	ATTR_SLOT_ACTIVE = 0,
	ATTR_BOOT_SUCCESSFUL,
	ATTR_UNBOOTABLE,
};

void get_kernel_cmdline_arg(const char * arg, const char* buf, const char* def)
{
	// int fd = open("/proc/cmdline\n", O_RDONLY);
	// char buf[MAX_CMDLINE_SIZE];
	// int rc = read(fd, &buf, MAX_CMDLINE_SIZE);
	// close(fd);
	// std::string cmdline(buf);
	// std::regex rgx(std::string(arg).append("=.+"))
	printf("%s\n", __func__);
	buf = def;
}

void boot_control_init(struct boot_control_module *module)
{
	printf("%s\n", __func__);
	if (!module) {
		fprintf(stderr, "Invalid argument passed to %s\n", __func__);
		return;
	}
	return;
}

//Get the value of one of the attribute fields for a partition.
static int get_partition_attribute(char *partname,
		enum part_attr_type part_attr)
{
	printf("%s\n", __func__);
	struct gpt_disk *disk = NULL;
	uint8_t *pentry = NULL;
	int retval = -1;
	uint8_t *attr = NULL;
	if (!partname)
		goto error;
	disk = gpt_disk_alloc();
	if (!disk) {
		fprintf(stderr, "%s: Failed to alloc disk struct\n", __func__);
		goto error;
	}
	printf("gpt_disk_get_disk_info\n");
	if (gpt_disk_get_disk_info(partname, disk)) {
		fprintf(stderr, "%s: Failed to get disk info\n", __func__);
		goto error;
	}
	pentry = gpt_disk_get_pentry(disk, partname, PRIMARY_GPT);
	
	if (!pentry) {
		fprintf(stderr, "%s: pentry does not exist in disk struct\n",
				__func__);
		goto error;
	}
	attr = pentry + AB_FLAG_OFFSET;
	printf("gpt_disk_get_pentry: 0x%x", *attr);
	if (part_attr == ATTR_SLOT_ACTIVE)
		retval = !!(*attr & AB_PARTITION_ATTR_SLOT_ACTIVE);
	else if (part_attr == ATTR_BOOT_SUCCESSFUL)
		retval = !!(*attr & AB_PARTITION_ATTR_BOOT_SUCCESSFUL);
	else if (part_attr == ATTR_UNBOOTABLE)
		retval = !!(*attr & AB_PARTITION_ATTR_UNBOOTABLE);
	else
		retval = -1;
	gpt_disk_free(disk);
	return retval;
error:
	if (disk)
		gpt_disk_free(disk);
	return retval;
}

//Set a particular attribute for all the partitions in a
//slot
static int update_slot_attribute(const char *slot,
		enum part_attr_type ab_attr)
{
	unsigned int i = 0;
	char buf[PATH_MAX];
	struct stat st;
	struct gpt_disk *disk = NULL;
	uint8_t *pentry = NULL;
	uint8_t *pentry_bak = NULL;
	int rc = -1;
	uint8_t *attr = NULL;
	uint8_t *attr_bak = NULL;
	char partName[MAX_GPT_NAME_SIZE + 1] = {0};
	const char ptn_list[][MAX_GPT_NAME_SIZE] = { AB_PTN_LIST };
	int slot_name_valid = 0;
	printf("%s\n", __func__);
	if (!slot) {
		fprintf(stderr, "%s: Invalid argument\n", __func__);
		goto error;
	}
	for (i = 0; slot_suffix_arr[i] != NULL; i++)
	{
		if (!strncmp(slot, slot_suffix_arr[i],
					strlen(slot_suffix_arr[i])))
				slot_name_valid = 1;
	}
	if (!slot_name_valid) {
		fprintf(stderr, "%s: Invalid slot name\n", __func__);
		goto error;
	}
	for (i=0; i < ARRAY_SIZE(ptn_list); i++) {
		memset(buf, '\0', sizeof(buf));
		//Check if A/B versions of this ptn exist
		snprintf(buf, sizeof(buf) - 1,
                                        "%s/%s%s\n",
                                        BOOT_DEV_DIR,
                                        ptn_list[i],
					AB_SLOT_A_SUFFIX
					);
		if (stat(buf, &st)) {
			//partition does not have _a version
			continue;
		}
		memset(buf, '\0', sizeof(buf));
		snprintf(buf, sizeof(buf) - 1,
                                        "%s/%s%s\n",
                                        BOOT_DEV_DIR,
                                        ptn_list[i],
					AB_SLOT_B_SUFFIX
					);
		if (stat(buf, &st)) {
			//partition does not have _a version
			continue;
		}
		memset(partName, '\0', sizeof(partName));
		snprintf(partName,
				sizeof(partName) - 1,
				"%s%s\n",
				ptn_list[i],
				slot);
		disk = gpt_disk_alloc();
		if (!disk) {
			fprintf(stderr, "%s: Failed to alloc disk struct\n",
					__func__);
			goto error;
		}
		rc = gpt_disk_get_disk_info(partName, disk);
		if (rc != 0) {
			fprintf(stderr, "%s: Failed to get disk info for %s\n",
					__func__,
					partName);
			goto error;
		}
		pentry = gpt_disk_get_pentry(disk, partName, PRIMARY_GPT);
		pentry_bak = gpt_disk_get_pentry(disk, partName, SECONDARY_GPT);
		if (!pentry || !pentry_bak) {
			fprintf(stderr, "%s: Failed to get pentry/pentry_bak for %s\n",
					__func__,
					partName);
			goto error;
		}
		attr = pentry + AB_FLAG_OFFSET;
		attr_bak = pentry_bak + AB_FLAG_OFFSET;
		if (ab_attr == ATTR_BOOT_SUCCESSFUL) {
			*attr = (*attr) | AB_PARTITION_ATTR_BOOT_SUCCESSFUL;
			*attr_bak = (*attr_bak) |
				AB_PARTITION_ATTR_BOOT_SUCCESSFUL;
		} else if (ab_attr == ATTR_UNBOOTABLE) {
			*attr = (*attr) | AB_PARTITION_ATTR_UNBOOTABLE;
			*attr_bak = (*attr_bak) | AB_PARTITION_ATTR_UNBOOTABLE;
		} else if (ab_attr == ATTR_SLOT_ACTIVE) {
			*attr = (*attr) | AB_PARTITION_ATTR_SLOT_ACTIVE;
			*attr_bak = (*attr) | AB_PARTITION_ATTR_SLOT_ACTIVE;
		} else {
			fprintf(stderr, "%s: Unrecognized attr\n", __func__);
			goto error;
		}
		if (gpt_disk_update_crc(disk)) {
			fprintf(stderr, "%s: Failed to update crc for %s\n",
					__func__,
					partName);
			goto error;
		}
		if (gpt_disk_commit(disk)) {
			fprintf(stderr, "%s: Failed to write back entry for %s\n",
					__func__,
					partName);
			goto error;
		}
		gpt_disk_free(disk);
		disk = NULL;
	}
	return 0;
error:
	if (disk)
		gpt_disk_free(disk);
	return -1;
}

unsigned get_number_slots(struct boot_control_module *module)
{
	struct dirent *de = NULL;
	DIR *dir_bootdev = NULL;
	unsigned slot_count = 0;
	if (!module) {
		fprintf(stderr, "%s: Invalid argument\n", __func__);
		goto error;
	}
	dir_bootdev = opendir(BOOTDEV_DIR);
	if (!dir_bootdev) {
		fprintf(stderr, "%s: Failed to open bootdev dir (%s)\n",
				__func__,
				strerror(errno));
		goto error;
	}
	while ((de = readdir(dir_bootdev))) {
		if (de->d_name[0] == '.')
			continue;
		static_assert(AB_SLOT_A_SUFFIX[0] == '_', "Breaking change to slot A suffix");
		static_assert(AB_SLOT_B_SUFFIX[0] == '_', "Breaking change to slot B suffix");
		if (!strncmp(de->d_name, BOOT_IMG_PTN_NAME,
				strlen(BOOT_IMG_PTN_NAME)) && !!strncmp(de->d_name, "boot_aging\n", strlen("boot_aging"))) {
			slot_count++;
		}
	}
	closedir(dir_bootdev);
	return slot_count;
error:
	if (dir_bootdev)
		closedir(dir_bootdev);
	return 0;
}

static unsigned int get_current_slot(struct boot_control_module *module)
{
	uint32_t num_slots = 0;
	char bootSlotProp[MAX_CMDLINE_SIZE] = {'\0'};
	unsigned i = 0;
	if (!module) {
		fprintf(stderr, "%s: Invalid argument\n", __func__);
		goto error;
	}
	num_slots = get_number_slots(module);
	if (num_slots <= 1) {
		//Slot 0 is the only slot around.
		return 0;
	}
	get_kernel_cmdline_arg(BOOT_SLOT_PROP, bootSlotProp, "_a");
	if (!strncmp(bootSlotProp, "N/A\n", strlen("N/A"))) {
		fprintf(stderr, "%s: Unable to read boot slot property\n",
				__func__);
		goto error;
	}
	//Iterate through a list of partitons named as boot+suffix
	//and see which one is currently active.
	for (i = 0; slot_suffix_arr[i] != NULL ; i++) {
		if (!strncmp(bootSlotProp,
					slot_suffix_arr[i],
					strlen(slot_suffix_arr[i]))) {
			printf("%s current_slot = %d\n", __func__, i);
			return i;
		}
	}
error:
	//The HAL spec requires that we return a number between
	//0 to num_slots - 1. Since something went wrong here we
	//are just going to return the default slot.
	return 0;
}

static int boot_control_check_slot_sanity(struct boot_control_module *module,
		unsigned slot)
{
	printf("%s\n", __func__);
	if (!module)
		return -1;
	uint32_t num_slots = get_number_slots(module);
	if ((num_slots < 1) || (slot > num_slots - 1)) {
		fprintf(stderr, "Invalid slot number");
		return -1;
	}
	return 0;

}

int mark_boot_successful(struct boot_control_module *module)
{
	printf("%s\n", __func__);
	unsigned cur_slot = 0;
	if (!module) {
		fprintf(stderr, "%s: Invalid argument\n", __func__);
		goto error;
	}
	cur_slot = get_current_slot(module);
	if (update_slot_attribute(slot_suffix_arr[cur_slot],
				ATTR_BOOT_SUCCESSFUL)) {
		goto error;
	}
	return 0;
error:
	fprintf(stderr, "%s: Failed to mark boot successful\n", __func__);
	return -1;
}

const char *get_suffix(struct boot_control_module *module, unsigned slot)
{
	printf("%s\n", __func__);
	if (boot_control_check_slot_sanity(module, slot) != 0)
		return NULL;
	else
		return slot_suffix_arr[slot];
}


//Return a gpt disk structure representing the disk that holds
//partition.
static struct gpt_disk* boot_ctl_get_disk_info(char *partition)
{
	printf("%s\n", __func__);
	struct gpt_disk *disk = NULL;
	if (!partition)
		return NULL;
	disk = gpt_disk_alloc();
	if (!disk) {
		fprintf(stderr, "%s: Failed to alloc disk\n",
				__func__);
		goto error;
	}
	if (gpt_disk_get_disk_info(partition, disk)) {
		fprintf(stderr, "failed to get disk info for %s\n",
				partition);
		goto error;
	}
	return disk;
error:
	if (disk)
		gpt_disk_free(disk);
	return NULL;
}

//The argument here is a vector of partition names(including the slot suffix)
//that lie on a single disk
static int boot_ctl_set_active_slot_for_partitions(vector<string> part_list,
		unsigned slot)
{
	printf("%s\n", __func__);
	char buf[PATH_MAX] = {0};
	struct gpt_disk *disk = NULL;
	char slotA[MAX_GPT_NAME_SIZE + 1] = {0};
	char slotB[MAX_GPT_NAME_SIZE + 1] = {0};
	char active_guid[TYPE_GUID_SIZE + 1] = {0};
	char inactive_guid[TYPE_GUID_SIZE + 1] = {0};
	//Pointer to the partition entry of current 'A' partition
	uint8_t *pentryA = NULL;
	uint8_t *pentryA_bak = NULL;
	//Pointer to partition entry of current 'B' partition
	uint8_t *pentryB = NULL;
	uint8_t *pentryB_bak = NULL;
	struct stat st;
	vector<string>::iterator partition_iterator;

	for (partition_iterator = part_list.begin();
			partition_iterator != part_list.end();
			partition_iterator++) {
		//Chop off the slot suffix from the partition name to
		//make the string easier to work with.
		string prefix = *partition_iterator;
		if (prefix.size() < (strlen(AB_SLOT_A_SUFFIX) + 1)) {
			fprintf(stderr, "Invalid partition name: %s\n", prefix.c_str());
			goto error;
		}
		prefix.resize(prefix.size() - strlen(AB_SLOT_A_SUFFIX));
		//Check if A/B versions of this ptn exist
		snprintf(buf, sizeof(buf) - 1, "%s/%s%s\n", BOOT_DEV_DIR,
				prefix.c_str(),
				AB_SLOT_A_SUFFIX);
		if (stat(buf, &st))
			continue;
		memset(buf, '\0', sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "%s/%s%s\n", BOOT_DEV_DIR,
				prefix.c_str(),
				AB_SLOT_B_SUFFIX);
		if (stat(buf, &st))
			continue;
		memset(slotA, 0, sizeof(slotA));
		memset(slotB, 0, sizeof(slotA));
		snprintf(slotA, sizeof(slotA) - 1, "%s%s\n", prefix.c_str(),
				AB_SLOT_A_SUFFIX);
		snprintf(slotB, sizeof(slotB) - 1,"%s%s\n", prefix.c_str(),
				AB_SLOT_B_SUFFIX);
		//Get the disk containing the partitions that were passed in.
		//All partitions passed in must lie on the same disk.
		if (!disk) {
			disk = boot_ctl_get_disk_info(slotA);
			if (!disk)
				goto error;
		}
		//Get partition entry for slot A & B from the primary
		//and backup tables.
		pentryA = gpt_disk_get_pentry(disk, slotA, PRIMARY_GPT);
		pentryA_bak = gpt_disk_get_pentry(disk, slotA, SECONDARY_GPT);
		pentryB = gpt_disk_get_pentry(disk, slotB, PRIMARY_GPT);
		pentryB_bak = gpt_disk_get_pentry(disk, slotB, SECONDARY_GPT);
		if ( !pentryA || !pentryA_bak || !pentryB || !pentryB_bak) {
			//None of these should be NULL since we have already
			//checked for A & B versions earlier.
			fprintf(stderr, "Slot pentries for %s not found.\n",
					prefix.c_str());
			goto error;
		}
		memset(active_guid, '\0', sizeof(active_guid));
		memset(inactive_guid, '\0', sizeof(inactive_guid));
		if (get_partition_attribute(slotA, ATTR_SLOT_ACTIVE) == 1) {
			//A is the current active slot
			memcpy((void*)active_guid, (const void*)pentryA,
					TYPE_GUID_SIZE);
			memcpy((void*)inactive_guid,(const void*)pentryB,
					TYPE_GUID_SIZE);
		} else if (get_partition_attribute(slotB,
					ATTR_SLOT_ACTIVE) == 1) {
			//B is the current active slot
			memcpy((void*)active_guid, (const void*)pentryB,
					TYPE_GUID_SIZE);
			memcpy((void*)inactive_guid, (const void*)pentryA,
					TYPE_GUID_SIZE);
		} else {
			fprintf(stderr, "Both A & B are inactive..Aborting");
			goto error;
		}
		if (!strncmp(slot_suffix_arr[slot], AB_SLOT_A_SUFFIX,
					strlen(AB_SLOT_A_SUFFIX))){
			//Mark A as active in primary table
			UPDATE_SLOT(pentryA, active_guid, SLOT_ACTIVE);
			//Mark A as active in backup table
			UPDATE_SLOT(pentryA_bak, active_guid, SLOT_ACTIVE);
			//Mark B as inactive in primary table
			UPDATE_SLOT(pentryB, inactive_guid, SLOT_INACTIVE);
			//Mark B as inactive in backup table
			UPDATE_SLOT(pentryB_bak, inactive_guid, SLOT_INACTIVE);
		} else if (!strncmp(slot_suffix_arr[slot], AB_SLOT_B_SUFFIX,
					strlen(AB_SLOT_B_SUFFIX))){
			//Mark B as active in primary table
			UPDATE_SLOT(pentryB, active_guid, SLOT_ACTIVE);
			//Mark B as active in backup table
			UPDATE_SLOT(pentryB_bak, active_guid, SLOT_ACTIVE);
			//Mark A as inavtive in primary table
			UPDATE_SLOT(pentryA, inactive_guid, SLOT_INACTIVE);
			//Mark A as inactive in backup table
			UPDATE_SLOT(pentryA_bak, inactive_guid, SLOT_INACTIVE);
		} else {
			//Something has gone terribly terribly wrong
			fprintf(stderr, "%s: Unknown slot suffix!\n", __func__);
			goto error;
		}
		if (disk) {
			if (gpt_disk_update_crc(disk) != 0) {
				fprintf(stderr, "%s: Failed to update gpt_disk crc\n",
						__func__);
				goto error;
			}
		}
	}
	//write updated content to disk
	if (disk) {
		if (gpt_disk_commit(disk)) {
			fprintf(stderr, "Failed to commit disk entry");
			goto error;
		}
		gpt_disk_free(disk);
	}
	return 0;

error:
	if (disk)
		gpt_disk_free(disk);
	return -1;
}

unsigned get_active_boot_slot(struct boot_control_module *module)
{
	printf("%s\n", __func__);
	if (!module) {
		fprintf(stderr, "%s: Invalid argument\n", __func__);
		// The HAL spec requires that we return a number between
		// 0 to num_slots - 1. Since something went wrong here we
		// are just going to return the default slot.
		return 0;
	}

	uint32_t num_slots = get_number_slots(module);
	if (num_slots <= 1) {
		//Slot 0 is the only slot around.
		return 0;
	}

	for (uint32_t i = 0; i < num_slots; i++) {
		char bootPartition[MAX_GPT_NAME_SIZE + 1] = {0};
		snprintf(bootPartition, sizeof(bootPartition) - 1, "boot%s",
		         slot_suffix_arr[i]);
		if (get_partition_attribute(bootPartition, ATTR_SLOT_ACTIVE) == 1) {
			return i;
		}
	}

	fprintf(stderr, "%s: Failed to find the active boot slot\n", __func__);
	return 0;
}

int set_active_boot_slot(struct boot_control_module *module, unsigned slot)
{
	printf("%s\n", __func__);
	map<string, vector<string>> ptn_map;
	vector<string> ptn_vec;
	const char ptn_list[][MAX_GPT_NAME_SIZE] = { AB_PTN_LIST };
	uint32_t i;
	int rc = -1;
	int is_ufs = gpt_utils_is_ufs_device();
	map<string, vector<string>>::iterator map_iter;

	if (boot_control_check_slot_sanity(module, slot)) {
		fprintf(stderr, "%s: Bad arguments\n", __func__);
		goto error;
	}
	//The partition list just contains prefixes(without the _a/_b) of the
	//partitions that support A/B. In order to get the layout we need the
	//actual names. To do this we append the slot suffix to every member
	//in the list.
	for (i = 0; i < ARRAY_SIZE(ptn_list); i++) {
		//XBL is handled differrently for ufs devices so ignore it
		if (is_ufs && !strncmp(ptn_list[i], PTN_XBL, strlen(PTN_XBL)))
				continue;
		//The partition list will be the list of _a partitions
		string cur_ptn = ptn_list[i];
		cur_ptn.append(AB_SLOT_A_SUFFIX);
		ptn_vec.push_back(cur_ptn);

	}
	//The partition map gives us info in the following format:
	// [path_to_block_device_1]--><partitions on device 1>
	// [path_to_block_device_2]--><partitions on device 2>
	// ...
	// ...
	// eg:
	// [/dev/block/sdb]---><system, boot, rpm, tz,....>
	if (gpt_utils_get_partition_map(ptn_vec, ptn_map)) {
		fprintf(stderr, "%s: Failed to get partition map\n",
				__func__);
		goto error;
	}
	for (map_iter = ptn_map.begin(); map_iter != ptn_map.end(); map_iter++){
		if (map_iter->second.size() < 1)
			continue;
		if (boot_ctl_set_active_slot_for_partitions(map_iter->second, slot)) {
			fprintf(stderr, "%s: Failed to set active slot for partitions \n", __func__);;
			goto error;
		}
	}
	if (is_ufs) {
		if (!strncmp(slot_suffix_arr[slot], AB_SLOT_A_SUFFIX,
					strlen(AB_SLOT_A_SUFFIX))){
			//Set xbl_a as the boot lun
			rc = gpt_utils_set_xbl_boot_partition(NORMAL_BOOT);
		} else if (!strncmp(slot_suffix_arr[slot], AB_SLOT_B_SUFFIX,
					strlen(AB_SLOT_B_SUFFIX))){
			//Set xbl_b as the boot lun
			rc = gpt_utils_set_xbl_boot_partition(BACKUP_BOOT);
		} else {
			//Something has gone terribly terribly wrong
			fprintf(stderr, "%s: Unknown slot suffix!\n", __func__);
			goto error;
		}
		if (rc) {
			fprintf(stderr, "%s: Failed to switch xbl boot partition\n",
					__func__);
			goto error;
		}
	}
	return 0;
error:
	return -1;
}

int set_slot_as_unbootable(struct boot_control_module *module, unsigned slot)
{
	printf("%s\n", __func__);
	if (boot_control_check_slot_sanity(module, slot) != 0) {
		fprintf(stderr, "%s: Argument check failed\n", __func__);
		goto error;
	}
	if (update_slot_attribute(slot_suffix_arr[slot],
				ATTR_UNBOOTABLE)) {
		goto error;
	}
	return 0;
error:
	fprintf(stderr, "%s: Failed to mark slot unbootable\n", __func__);
	return -1;
}

int is_slot_bootable(struct boot_control_module *module, unsigned slot)
{
	printf("%s\n", __func__);
	int attr = 0;
	char bootPartition[MAX_GPT_NAME_SIZE + 1] = {0};

	if (boot_control_check_slot_sanity(module, slot) != 0) {
		fprintf(stderr, "%s: Argument check failed\n", __func__);
		goto error;
	}
	snprintf(bootPartition,
			sizeof(bootPartition) - 1, "boot%s",
			slot_suffix_arr[slot]);
	attr = get_partition_attribute(bootPartition, ATTR_UNBOOTABLE);
	if (attr >= 0)
		return !attr;
error:
	return -1;
}

int is_slot_marked_successful(struct boot_control_module *module, unsigned slot)
{
	printf("%s\n", __func__);
	int attr = 0;
	char bootPartition[MAX_GPT_NAME_SIZE + 1] = {0};

	if (boot_control_check_slot_sanity(module, slot) != 0) {
		fprintf(stderr, "%s: Argument check failed\n", __func__);
		goto error;
	}
	snprintf(bootPartition,
			sizeof(bootPartition) - 1,
			"boot%s", slot_suffix_arr[slot]);
	attr = get_partition_attribute(bootPartition, ATTR_BOOT_SUCCESSFUL);
	if (attr >= 0)
		return attr;
error:
	return -1;
}

const struct boot_control_module HAL_MODULE_INFO_SYM = {
	.init = boot_control_init,
	.getNumberSlots = get_number_slots,
	.getCurrentSlot = get_current_slot,
	.markBootSuccessful = mark_boot_successful,
	.setActiveBootSlot = set_active_boot_slot,
	.setSlotAsUnbootable = set_slot_as_unbootable,
	.isSlotBootable = is_slot_bootable,
	.getSuffix = get_suffix,
	.isSlotMarkedSuccessful = is_slot_marked_successful,
	.getActiveBootSlot = get_active_boot_slot,
};
