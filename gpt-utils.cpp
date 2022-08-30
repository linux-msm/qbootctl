/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 * Copyright (C) 2021-2022 Caleb Connolly <caleb@connolly.tech>
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

#define _LARGEFILE64_SOURCE /* enable lseek64() */

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/fs.h>
#include <limits.h>
#include <dirent.h>
#include <inttypes.h>
#include <linux/kernel.h>
#include <asm/byteorder.h>
#include <map>
#include <vector>
#include <string>
#include <endian.h>
#include <zlib.h>

#include "utils.h"
#include "gpt-utils.h"

/* list the names of the backed-up partitions to be swapped */
/* extension used for the backup partitions - tzbak, abootbak, etc. */
#define BAK_PTN_NAME_EXT "bak"
#define XBL_PRIMARY	 "/dev/disk/by-partlabel/xbl_a" // FIXME
#define XBL_BACKUP	 "/dev/disk/by-partlabel/xblbak"
#define XBL_AB_PRIMARY	 "/dev/disk/by-partlabel/xbl_a"
#define XBL_AB_SECONDARY "/dev/disk/by-partlabel/xbl_b"
/* GPT defines */
#define MAX_LUNS 26
//Size of the buffer that needs to be passed to the UFS ioctl
#define UFS_ATTR_DATA_SIZE 32
//This will allow us to get the root lun path from the path to the partition.
//i.e: from /dev/disk/sdaXXX get /dev/disk/sda. The assumption here is that
//the boot critical luns lie between sda to sdz which is acceptable because
//only user added external disks,etc would lie beyond that limit which do not
//contain partitions that interest us here.
#define PATH_TRUNCATE_LOC (sizeof("/dev/sda") - 1)

//From /dev/disk/sda get just sda
#define LUN_NAME_START_LOC (sizeof("/dev/") - 1)
#define BOOT_LUN_A_ID	   1
#define BOOT_LUN_B_ID	   2
/******************************************************************************
 * MACROS
 ******************************************************************************/

#define GET_4_BYTES(ptr)                                                       \
	((uint32_t) * ((uint8_t *)(ptr)) |                                     \
	 ((uint32_t) * ((uint8_t *)(ptr) + 1) << 8) |                          \
	 ((uint32_t) * ((uint8_t *)(ptr) + 2) << 16) |                         \
	 ((uint32_t) * ((uint8_t *)(ptr) + 3) << 24))

#define GET_8_BYTES(ptr)                                                       \
	((uint64_t) * ((uint8_t *)(ptr)) |                                     \
	 ((uint64_t) * ((uint8_t *)(ptr) + 1) << 8) |                          \
	 ((uint64_t) * ((uint8_t *)(ptr) + 2) << 16) |                         \
	 ((uint64_t) * ((uint8_t *)(ptr) + 3) << 24) |                         \
	 ((uint64_t) * ((uint8_t *)(ptr) + 4) << 32) |                         \
	 ((uint64_t) * ((uint8_t *)(ptr) + 5) << 40) |                         \
	 ((uint64_t) * ((uint8_t *)(ptr) + 6) << 48) |                         \
	 ((uint64_t) * ((uint8_t *)(ptr) + 7) << 56))

#define PUT_4_BYTES(ptr, y)                                                    \
	*((uint8_t *)(ptr)) = (y)&0xff;                                        \
	*((uint8_t *)(ptr) + 1) = ((y) >> 8) & 0xff;                           \
	*((uint8_t *)(ptr) + 2) = ((y) >> 16) & 0xff;                          \
	*((uint8_t *)(ptr) + 3) = ((y) >> 24) & 0xff;

/******************************************************************************
 * TYPES
 ******************************************************************************/
using namespace std;
enum gpt_state { GPT_OK = 0, GPT_BAD_SIGNATURE, GPT_BAD_CRC };
//List of LUN's containing boot critical images.
//Required in the case of UFS devices
struct update_data {
	char lun_list[MAX_LUNS][PATH_MAX];
	uint32_t num_valid_entries;
};

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
void DumpHex(const void *data, size_t size)
{
	char ascii[17];
	size_t i, j;
	ascii[16] = '\0';
	for (i = 0; i < size; ++i) {
		printf("%02X ", ((unsigned char *)data)[i]);
		if (((unsigned char *)data)[i] >= ' ' &&
		    ((unsigned char *)data)[i] <= '~') {
			ascii[i % 16] = ((unsigned char *)data)[i];
		} else {
			ascii[i % 16] = '.';
		}
		if ((i + 1) % 8 == 0 || i + 1 == size) {
			printf(" ");
			if ((i + 1) % 16 == 0) {
				printf("|  %s \n", ascii);
			} else if (i + 1 == size) {
				ascii[(i + 1) % 16] = '\0';
				if ((i + 1) % 16 <= 8) {
					printf(" ");
				}
				for (j = (i + 1) % 16; j < 16; ++j) {
					printf("   ");
				}
				printf("|  %s \n", ascii);
			}
		}
	}
}

/**
 *  ==========================================================================
 *
 *  \brief  Read/Write len bytes from/to block dev
 *
 *  \param [in] fd      block dev file descriptor (returned from open)
 *  \param [in] rw      RW flag: 0 - read, != 0; - write
 *  \param [in] offset  block dev offset [bytes] - RW start position
 *  \param [in] buf     Pointer to the buffer containing the data
 *  \param [in] len     RW size in bytes. Buf must be at least that big
 *
 *  \return  0 on success
 *
 *  ==========================================================================
 */
static int blk_rw(int fd, int rw, uint64_t offset, uint8_t *buf, unsigned len)
{
	int r;

	if (lseek64(fd, offset, SEEK_SET) < 0) {
		fprintf(stderr, "block dev lseek64 %" PRIu64 " failed: %s\n",
			offset, strerror(errno));
		return -1;
	}

	if (rw)
		r = write(fd, buf, len);
	else
		r = read(fd, buf, len);

	if (r < 0) {
		fprintf(stderr, "block dev %s failed: %s\n",
			rw ? "write" : "read\n", strerror(errno));
	} else {
		if (rw) {
			r = fsync(fd);
			if (r < 0)
				fprintf(stderr, "fsync failed: %s\n",
					strerror(errno));
		} else {
			r = 0;
		}
	}

	return r;
}

/**
 *  ==========================================================================
 *
 *  \brief  Search within GPT for partition entry with the given name
 *  or it's backup twin (name-bak).
 *
 *  \param [in] ptn_name        Partition name to seek
 *  \param [in] pentries_start  Partition entries array start pointer
 *  \param [in] pentries_end    Partition entries array end pointer
 *  \param [in] pentry_size     Single partition entry size [bytes]
 *
 *  \return  First partition entry pointer that matches the name or NULL
 *
 *  ==========================================================================
 */
static uint8_t *gpt_pentry_seek(const char *ptn_name,
				const uint8_t *pentries_start,
				const uint8_t *pentries_end,
				uint32_t pentry_size)
{
	char *pentry_name;
	unsigned len = strlen(ptn_name);

	for (pentry_name = (char *)(pentries_start + PARTITION_NAME_OFFSET);
	     pentry_name < (char *)pentries_end; pentry_name += pentry_size) {
		char name8[MAX_GPT_NAME_SIZE / 2];
		unsigned i;

		/* Partition names in GPT are UTF-16 - ignoring UTF-16 2nd byte */
		for (i = 0; i < sizeof(name8); i++)
			name8[i] = pentry_name[i * 2];
		if (!strncmp(ptn_name, name8, len))
			if (name8[len] == 0 ||
			    !strcmp(&name8[len], BAK_PTN_NAME_EXT))
				return (uint8_t *)(pentry_name -
						   PARTITION_NAME_OFFSET);
	}

	return NULL;
}

// Defined in ufs-bsg.cpp
int32_t set_boot_lun(uint8_t lun_id);

//Swtich betwieen using either the primary or the backup
//boot LUN for boot. This is required since UFS boot partitions
//cannot have a backup GPT which is what we use for failsafe
//updates of the other 'critical' partitions. This function will
//not be invoked for emmc targets and on UFS targets is only required
//to be invoked for XBL.
//
//The algorithm to do this is as follows:
//- Find the real block device(eg: /dev/disk/sdb) that corresponds
//  to the /dev/disk/bootdevice/by-name/xbl(bak) symlink
//
//- Once we have the block device 'node' name(sdb in the above example)
//  use this node to to locate the scsi generic device that represents
//  it by checking the file /sys/block/sdb/device/scsi_generic/sgY
//
//- Once we locate sgY we call the query ioctl on /dev/sgy to switch
//the boot lun to either LUNA or LUNB
int gpt_utils_set_xbl_boot_partition(enum boot_chain chain)
{
	struct stat st;
	uint8_t boot_lun_id = 0;
	const char *boot_dev = NULL;

	(void)st;
	(void)boot_dev;

	if (chain == BACKUP_BOOT) {
		boot_lun_id = BOOT_LUN_B_ID;
		if (!stat(XBL_BACKUP, &st))
			boot_dev = XBL_BACKUP;
		else if (!stat(XBL_AB_SECONDARY, &st))
			boot_dev = XBL_AB_SECONDARY;
		else {
			fprintf(stderr, "%s: Failed to locate secondary xbl\n",
				__func__);
			goto error;
		}
	} else if (chain == NORMAL_BOOT) {
		boot_lun_id = BOOT_LUN_A_ID;
		if (!stat(XBL_PRIMARY, &st))
			boot_dev = XBL_PRIMARY;
		else if (!stat(XBL_AB_PRIMARY, &st))
			boot_dev = XBL_AB_PRIMARY;
		else {
			fprintf(stderr, "%s: Failed to locate primary xbl\n",
				__func__);
			goto error;
		}
	} else {
		fprintf(stderr, "%s: Invalid boot chain id\n", __func__);
		goto error;
	}
	//We need either both xbl and xblbak or both xbl_a and xbl_b to exist at
	//the same time. If not the current configuration is invalid.
	if ((stat(XBL_PRIMARY, &st) || stat(XBL_BACKUP, &st)) &&
	    (stat(XBL_AB_PRIMARY, &st) || stat(XBL_AB_SECONDARY, &st))) {
		fprintf(stderr, "%s:primary/secondary XBL prt not found(%s)\n",
			__func__, strerror(errno));
		goto error;
	}
	LOGD("%s: setting %s lun as boot lun\n", __func__, boot_dev);

	if (set_boot_lun(boot_lun_id)) {
		goto error;
	}
	return 0;
error:
	return -1;
}

//Given a parttion name(eg: rpm) get the path to the block device that
//represents the GPT disk the partition resides on. In the case of emmc it
//would be the default emmc dev(/dev/mmcblk0). In the case of UFS we look
//through the /dev/disk/bootdevice/by-name/ tree for partname, and resolve
//the path to the LUN from there.
static int get_dev_path_from_partition_name(const char *partname, char *buf,
					    size_t buflen)
{
	struct stat st;
	char path[PATH_MAX] = { 0 };
	int i;

	(void)st;

	if (!partname || !buf || buflen < ((PATH_TRUNCATE_LOC) + 1)) {
		fprintf(stderr, "%s: Invalid argument\n", __func__);
		return -1;
	}
	//Need to find the lun that holds partition partname
	snprintf(path, sizeof(path), "%s/%s", BOOT_DEV_DIR, partname);
	// if (rc = stat(path, &st)) {
	//         LOGD("stat failed: errno=%d\n", errno);
	//         goto error;
	// }
	buf = realpath(path, buf);
	if (!buf) {
		return -1;
	} else {
		for (i = strlen(buf); i > 0; i--)
			if (!isdigit(buf[i - 1]))
				break;

		if (i >= 2 && buf[i - 1] == 'p' && isdigit(buf[i - 2]))
			i--;

		buf[i] = 0;
	}
	return 0;
}

int gpt_utils_get_partition_map(vector<string> &ptn_list,
				map<string, vector<string> > &partition_map)
{
	char devpath[PATH_MAX] = { '\0' };
	map<string, vector<string> >::iterator it;
	if (ptn_list.size() < 1) {
		fprintf(stderr, "%s: Invalid ptn list\n", __func__);
		return -1;
	}
	//Go through the passed in list
	for (uint32_t i = 0; i < ptn_list.size(); i++) {
		//Key in the map is the path to the device that holds the
		//partition
		if (get_dev_path_from_partition_name(
			    ptn_list[i].c_str(), devpath, sizeof(devpath))) {
			//Not necessarily an error. The partition may just
			//not be present.
			continue;
		}
		string path = devpath;
		it = partition_map.find(path);
		if (it != partition_map.end()) {
			it->second.push_back(ptn_list[i]);
		} else {
			vector<string> str_vec;
			str_vec.push_back(ptn_list[i]);
			partition_map.insert(
				pair<string, vector<string> >(path, str_vec));
		}
		memset(devpath, '\0', sizeof(devpath));
	}
	return 0;
}

//Get the block size of the disk represented by decsriptor fd
static uint32_t gpt_get_block_size(int fd)
{
	uint32_t block_size = 0;
	if (fd < 0) {
		fprintf(stderr, "%s: invalid descriptor\n", __func__);
		goto error;
	}
	if (ioctl(fd, BLKSSZGET, &block_size) != 0) {
		fprintf(stderr, "%s: Failed to get GPT dev block size : %s\n",
			__func__, strerror(errno));
		goto error;
	}
	return block_size;
error:
	return 0;
}

//Write the GPT header present in the passed in buffer back to the
//disk represented by fd
static int gpt_set_header(uint8_t *gpt_header, int fd,
			  enum gpt_instance instance)
{
	uint32_t block_size = 0;
	off_t gpt_header_offset = 0;
	if (!gpt_header || fd < 0) {
		fprintf(stderr, "%s: Invalid arguments\n", __func__);
		goto error;
	}
	block_size = gpt_get_block_size(fd);
	LOGD("%s: Block size is : %d\n", __func__, block_size);
	if (block_size == 0) {
		fprintf(stderr, "%s: Failed to get block size\n", __func__);
		goto error;
	}
	if (instance == PRIMARY_GPT)
		gpt_header_offset = block_size;
	else
		gpt_header_offset = lseek64(fd, 0, SEEK_END) - block_size;
	if (gpt_header_offset <= 0) {
		fprintf(stderr, "%s: Failed to get gpt header offset\n",
			__func__);
		goto error;
	}
	LOGD("%s: Writing back header to offset %ld\n", __func__,
	     gpt_header_offset);
	if (blk_rw(fd, 1, gpt_header_offset, gpt_header, block_size)) {
		fprintf(stderr, "%s: Failed to write back GPT header\n",
			__func__);
		goto error;
	}
	return 0;
error:
	return -1;
}

//Read out the GPT header for the disk that contains the partition partname
static uint8_t *gpt_get_header(const char *partname, enum gpt_instance instance)
{
	uint8_t *hdr = NULL;
	char devpath[PATH_MAX] = { 0 };
	uint64_t hdr_offset = 0;
	uint32_t block_size = 0;
	int fd = -1;
	if (!partname) {
		fprintf(stderr, "%s: Invalid partition name\n", __func__);
		goto error;
	}
	if (get_dev_path_from_partition_name(partname, devpath,
					     sizeof(devpath)) != 0) {
		fprintf(stderr, "%s: Failed to resolve path for %s\n", __func__,
			partname);
		goto error;
	}
	fd = open(devpath, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "%s: Failed to open %s : %s\n", __func__,
			devpath, strerror(errno));
		goto error;
	}
	block_size = gpt_get_block_size(fd);
	if (block_size == 0) {
		fprintf(stderr, "%s: Failed to get gpt block size for %s\n",
			__func__, partname);
		goto error;
	}

	hdr = (uint8_t *)calloc(block_size, 1);
	if (!hdr) {
		fprintf(stderr,
			"%s: Failed to allocate memory for gpt header\n",
			__func__);
	}
	if (instance == PRIMARY_GPT)
		hdr_offset = block_size;
	else {
		hdr_offset = lseek64(fd, 0, SEEK_END) - block_size;
	}
	if (hdr_offset < 0) {
		fprintf(stderr, "%s: Failed to get gpt header offset\n",
			__func__);
		goto error;
	}
	if (blk_rw(fd, 0, hdr_offset, hdr, block_size)) {
		fprintf(stderr, "%s: Failed to read GPT header from device\n",
			__func__);
		goto error;
	}
	//DumpHex(hdr, block_size);
	close(fd);
	return hdr;
error:
	if (fd >= 0)
		close(fd);
	if (hdr)
		free(hdr);
	return NULL;
}

//Returns the partition entry array based on the
//passed in buffer which contains the gpt header.
//The fd here is the descriptor for the 'disk' which
//holds the partition
static uint8_t *gpt_get_pentry_arr(uint8_t *hdr, int fd)
{
	uint64_t pentries_start = 0;
	uint32_t pentry_size = 0;
	uint32_t block_size = 0;
	uint32_t pentries_arr_size = 0;
	uint8_t *pentry_arr = NULL;
	int rc = 0;
	if (!hdr) {
		fprintf(stderr, "%s: Invalid header\n", __func__);
		goto error;
	}
	if (fd < 0) {
		fprintf(stderr, "%s: Invalid fd\n", __func__);
		goto error;
	}
	block_size = gpt_get_block_size(fd);
	if (!block_size) {
		fprintf(stderr, "%s: Failed to get gpt block size for\n",
			__func__);
		goto error;
	}
	pentries_start = GET_8_BYTES(hdr + PENTRIES_OFFSET) * block_size;
	pentry_size = GET_4_BYTES(hdr + PENTRY_SIZE_OFFSET);
	pentries_arr_size =
		GET_4_BYTES(hdr + PARTITION_COUNT_OFFSET) * pentry_size;
	pentry_arr = (uint8_t *)calloc(1, pentries_arr_size);
	if (!pentry_arr) {
		fprintf(stderr,
			"%s: Failed to allocate memory for partition array\n",
			__func__);
		goto error;
	}
	rc = blk_rw(fd, 0, pentries_start, pentry_arr, pentries_arr_size);
	if (rc) {
		fprintf(stderr, "%s: Failed to read partition entry array\n",
			__func__);
		goto error;
	}
	return pentry_arr;
error:
	if (pentry_arr)
		free(pentry_arr);
	return NULL;
}

static int gpt_set_pentry_arr(uint8_t *hdr, int fd, uint8_t *arr)
{
	uint32_t block_size = 0;
	uint64_t pentries_start = 0;
	uint32_t pentry_size = 0;
	uint32_t pentries_arr_size = 0;
	int rc = 0;
	if (!hdr || fd < 0 || !arr) {
		fprintf(stderr, "%s: Invalid argument\n", __func__);
		goto error;
	}
	block_size = gpt_get_block_size(fd);
	if (!block_size) {
		fprintf(stderr, "%s: Failed to get gpt block size for\n",
			__func__);
		goto error;
	}
	LOGD("%s : Block size is %d\n", __func__, block_size);
	pentries_start = GET_8_BYTES(hdr + PENTRIES_OFFSET) * block_size;
	pentry_size = GET_4_BYTES(hdr + PENTRY_SIZE_OFFSET);
	pentries_arr_size =
		GET_4_BYTES(hdr + PARTITION_COUNT_OFFSET) * pentry_size;
	LOGD("%s: Writing partition entry array of size %d to offset %" PRIu64
	     "\n",
	     __func__, pentries_arr_size, pentries_start);
	LOGD("pentries_start: %lu\n", pentries_start);
	rc = blk_rw(fd, 1, pentries_start, arr, pentries_arr_size);
	if (rc) {
		fprintf(stderr, "%s: Failed to read partition entry array\n",
			__func__);
		goto error;
	}
	return 0;
error:
	return -1;
}

//Allocate a handle used by calls to the "gpt_disk" api's
struct gpt_disk *gpt_disk_alloc()
{
	struct gpt_disk *disk;
	disk = (struct gpt_disk *)malloc(sizeof(struct gpt_disk));
	if (!disk) {
		fprintf(stderr, "%s: Failed to allocate memory\n", __func__);
		goto end;
	}
	memset(disk, 0, sizeof(struct gpt_disk));
end:
	return disk;
}

//Free previously allocated/initialized handle
void gpt_disk_free(struct gpt_disk *disk)
{
	if (!disk)
		return;
	if (disk->hdr)
		free(disk->hdr);
	if (disk->hdr_bak)
		free(disk->hdr_bak);
	if (disk->pentry_arr)
		free(disk->pentry_arr);
	if (disk->pentry_arr_bak)
		free(disk->pentry_arr_bak);
	free(disk);
	return;
}

//fills up the passed in gpt_disk struct with information about the
//disk represented by path dev. Returns 0 on success and -1 on error.
int gpt_disk_get_disk_info(const char *dev, struct gpt_disk *dsk)
{
	struct gpt_disk *disk = NULL;
	int fd = -1;
	uint32_t gpt_header_size = 0;

	if (!dsk || !dev) {
		fprintf(stderr, "%s: Invalid arguments\n", __func__);
		goto error;
	}
	disk = dsk;
	disk->hdr = gpt_get_header(dev, PRIMARY_GPT);
	if (!disk->hdr) {
		fprintf(stderr, "%s: Failed to get primary header\n", __func__);
		goto error;
	}
	gpt_header_size = GET_4_BYTES(disk->hdr + HEADER_SIZE_OFFSET);
	// FIXME: pointer offsets crc bleh
	disk->hdr_crc = crc32(0, disk->hdr, gpt_header_size);
	disk->hdr_bak = gpt_get_header(dev, PRIMARY_GPT);
	if (!disk->hdr_bak) {
		fprintf(stderr, "%s: Failed to get backup header\n", __func__);
		goto error;
	}
	disk->hdr_bak_crc = crc32(0, disk->hdr_bak, gpt_header_size);

	//Descriptor for the block device. We will use this for further
	//modifications to the partition table
	if (get_dev_path_from_partition_name(dev, disk->devpath,
					     sizeof(disk->devpath)) != 0) {
		fprintf(stderr, "%s: Failed to resolve path for %s\n", __func__,
			dev);
		goto error;
	}
	fd = open(disk->devpath, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "%s: Failed to open %s: %s\n", __func__,
			disk->devpath, strerror(errno));
		goto error;
	}
	disk->pentry_arr = gpt_get_pentry_arr(disk->hdr, fd);
	if (!disk->pentry_arr) {
		fprintf(stderr, "%s: Failed to obtain partition entry array\n",
			__func__);
		goto error;
	}
	disk->pentry_arr_bak = gpt_get_pentry_arr(disk->hdr_bak, fd);
	if (!disk->pentry_arr_bak) {
		fprintf(stderr,
			"%s: Failed to obtain backup partition entry array\n",
			__func__);
		goto error;
	}
	disk->pentry_size = GET_4_BYTES(disk->hdr + PENTRY_SIZE_OFFSET);
	disk->pentry_arr_size =
		GET_4_BYTES(disk->hdr + PARTITION_COUNT_OFFSET) *
		disk->pentry_size;
	disk->pentry_arr_crc = GET_4_BYTES(disk->hdr + PARTITION_CRC_OFFSET);
	disk->pentry_arr_bak_crc =
		GET_4_BYTES(disk->hdr_bak + PARTITION_CRC_OFFSET);
	disk->block_size = gpt_get_block_size(fd);
	close(fd);
	disk->is_initialized = GPT_DISK_INIT_MAGIC;
	return 0;
error:
	if (fd >= 0)
		close(fd);
	return -1;
}

//Get pointer to partition entry from a allocated gpt_disk structure
uint8_t *gpt_disk_get_pentry(struct gpt_disk *disk, const char *partname,
			     enum gpt_instance instance)
{
	uint8_t *ptn_arr = NULL;
	if (!disk || !partname || disk->is_initialized != GPT_DISK_INIT_MAGIC) {
		fprintf(stderr, "%s: Invalid argument\n", __func__);
		goto error;
	}
	ptn_arr = (instance == PRIMARY_GPT) ? disk->pentry_arr :
						    disk->pentry_arr_bak;
	return (gpt_pentry_seek(partname, ptn_arr,
				ptn_arr + disk->pentry_arr_size,
				disk->pentry_size));
error:
	return NULL;
}

//Update CRC values for the various components of the gpt_disk
//structure. This function should be called after any of the fields
//have been updated before the structure contents are written back to
//disk.
int gpt_disk_update_crc(struct gpt_disk *disk)
{
	uint32_t gpt_header_size = 0;
	if (!disk || (disk->is_initialized != GPT_DISK_INIT_MAGIC)) {
		fprintf(stderr, "%s: invalid argument\n", __func__);
		goto error;
	}
	//Recalculate the CRC of the primary partiton array
	disk->pentry_arr_crc =
		crc32(0, disk->pentry_arr, disk->pentry_arr_size);
	//Recalculate the CRC of the backup partition array
	disk->pentry_arr_bak_crc =
		crc32(0, disk->pentry_arr_bak, disk->pentry_arr_size);
	//Update the partition CRC value in the primary GPT header
	PUT_4_BYTES(disk->hdr + PARTITION_CRC_OFFSET, disk->pentry_arr_crc);
	//Update the partition CRC value in the backup GPT header
	PUT_4_BYTES(disk->hdr_bak + PARTITION_CRC_OFFSET,
		    disk->pentry_arr_bak_crc);
	//Update the CRC value of the primary header
	gpt_header_size = GET_4_BYTES(disk->hdr + HEADER_SIZE_OFFSET);
	//Header CRC is calculated with its own CRC field set to 0
	PUT_4_BYTES(disk->hdr + HEADER_CRC_OFFSET, 0);
	PUT_4_BYTES(disk->hdr_bak + HEADER_CRC_OFFSET, 0);
	disk->hdr_crc = crc32(0, disk->hdr, gpt_header_size);
	disk->hdr_bak_crc = crc32(0, disk->hdr_bak, gpt_header_size);
	PUT_4_BYTES(disk->hdr + HEADER_CRC_OFFSET, disk->hdr_crc);
	PUT_4_BYTES(disk->hdr_bak + HEADER_CRC_OFFSET, disk->hdr_bak_crc);
	return 0;
error:
	return -1;
}

//Write the contents of struct gpt_disk back to the actual disk
int gpt_disk_commit(struct gpt_disk *disk)
{
	int fd = -1;
	if (!disk || (disk->is_initialized != GPT_DISK_INIT_MAGIC)) {
		fprintf(stderr, "%s: Invalid args\n", __func__);
		goto error;
	}
	fd = open(disk->devpath, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "%s: Failed to open %s: %s\n", __func__,
			disk->devpath, strerror(errno));
		goto error;
	}
	LOGD("%s: Writing back primary GPT header\n", __func__);
	//Write the primary header
	if (gpt_set_header(disk->hdr, fd, PRIMARY_GPT) != 0) {
		fprintf(stderr, "%s: Failed to update primary GPT header\n",
			__func__);
		goto error;
	}
	LOGD("%s: Writing back primary partition array\n", __func__);
	//Write back the primary partition array
	if (gpt_set_pentry_arr(disk->hdr, fd, disk->pentry_arr)) {
		fprintf(stderr,
			"%s: Failed to write primary GPT partition arr\n",
			__func__);
		goto error;
	}
	fsync(fd);
	close(fd);
	return 0;
error:
	if (fd >= 0)
		close(fd);
	return -1;
}

//Determine whether to handle the given partition as eMMC or UFS, using the
//name of the backing device.
//
//Note: In undefined cases (i.e. /dev/mmcblk1 and unresolvable), this function
//will tend to prefer UFS behavior. If it incorrectly reports this, then the
//program should exit (e.g. by failing) before making any changes.
bool gpt_utils_is_partition_backed_by_emmc(const char *part) {
	char devpath[PATH_MAX] = { '\0' };

	if (get_dev_path_from_partition_name(part, devpath, sizeof(devpath)))
		return false;

	return !strcmp(devpath, EMMC_DEVICE);
}
