/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
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

#include <linux/bsg.h>
#include <scsi/scsi_bsg_ufs.h>
#include <endian.h>
#include <dirent.h>
#include <string.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "utils.h"
#include "ufs-bsg.h"

/* UFS BSG device node */
static char ufs_bsg_dev[FNAME_SZ] = "/dev/bsg/ufs-bsg0";

static int fd_ufs_bsg = 0;

int ufs_bsg_dev_open()
{
	if (fd_ufs_bsg)
		return 0;

	fd_ufs_bsg = open(ufs_bsg_dev, O_RDWR);
	if (fd_ufs_bsg < 0) {
		fprintf(stderr, "Unable to open '%s': %s\n", ufs_bsg_dev,
			strerror(errno));
		fprintf(stderr,
			"Is CONFIG_SCSI_UFS_BSG is enabled in your kernel?\n");
		fd_ufs_bsg = 0;
		return -1;
	}

	return 0;
}

void ufs_bsg_dev_close()
{
	if (fd_ufs_bsg) {
		close(fd_ufs_bsg);
		fd_ufs_bsg = 0;
	}
}

static int ufs_bsg_ioctl(int fd, struct ufs_bsg_request *req,
			 struct ufs_bsg_reply *rsp, __u8 *buf, __u32 buf_len,
			 enum bsg_ioctl_dir dir)
{
	int ret;
	struct sg_io_v4 sg_io = {
		.guard = 'Q',
		.protocol = BSG_PROTOCOL_SCSI,
		.subprotocol = BSG_SUB_PROTOCOL_SCSI_TRANSPORT,
		.request_len = sizeof(*req),
		.request = (__u64)req,
		.response = (__u64)rsp,
		.max_response_len = sizeof(*rsp),
	};

	if (dir == BSG_IOCTL_DIR_FROM_DEV) {
		sg_io.din_xfer_len = buf_len;
		sg_io.din_xferp = (__u64)(buf);
	} else {
		sg_io.dout_xfer_len = buf_len;
		sg_io.dout_xferp = (__u64)(buf);
	}

	ret = ioctl(fd, SG_IO, &sg_io);
	if (ret)
		fprintf(stderr,
			"%s: Error from sg_io ioctl (return value: %d, error no: %d, reply result from LLD: %d\n)",
			__func__, ret, errno, rsp->result);

	if (sg_io.info || rsp->result) {
		fprintf(stderr,
			"%s: Error from sg_io info (check sg info: device_status: 0x%x, transport_status: 0x%x, driver_status: 0x%x, reply result from LLD: %d\n)",
			__func__, sg_io.device_status, sg_io.transport_status,
			sg_io.driver_status, rsp->result);
		ret = -EAGAIN;
	}

	return ret;
}

static void compose_ufs_bsg_query_req(struct ufs_bsg_request *req, __u8 func,
				      __u8 opcode, __u8 idn, __u8 index,
				      __u8 sel, __u16 length)
{
	struct utp_upiu_header *hdr = &req->upiu_req.header;
	struct utp_upiu_query *qr = &req->upiu_req.qr;

	req->msgcode = UTP_UPIU_QUERY_REQ;
	hdr->dword_0 = DWORD(UTP_UPIU_QUERY_REQ, 0, 0, 0);
	hdr->dword_1 = DWORD(0, func, 0, 0);
	hdr->dword_2 = DWORD(0, 0, length >> 8, (__u8)length);
	qr->opcode = opcode;
	qr->idn = idn;
	qr->index = index;
	qr->selector = sel;
	qr->length = htobe16(length);
}

static int ufs_query_attr(int fd, __u32 value, __u8 func, __u8 opcode, __u8 idn,
			  __u8 index, __u8 sel)
{
	struct ufs_bsg_request req = { 0 };
	struct ufs_bsg_reply rsp = { 0 };
	enum bsg_ioctl_dir dir = BSG_IOCTL_DIR_FROM_DEV;
	int ret = 0;

	if (opcode == QUERY_REQ_OP_WRITE_DESC ||
	    opcode == QUERY_REQ_OP_WRITE_ATTR)
		dir = BSG_IOCTL_DIR_TO_DEV;

	req.upiu_req.qr.value = htobe32(value);

	compose_ufs_bsg_query_req(&req, func, opcode, idn, index, sel, 0);

	ret = ufs_bsg_ioctl(fd, &req, &rsp, 0, 0, dir);
	if (ret)
		fprintf(stderr,
			"%s: Error from ufs_bsg_ioctl (return value: %d, error no: %d\n)",
			__func__, ret, errno);

	return ret;
}

int32_t set_boot_lun(__u8 lun_id)
{
	int32_t ret;
	__u32 boot_lun_id = lun_id;

	LOGD("Using UFS bsg device: %s\n", ufs_bsg_dev);

	ret = ufs_bsg_dev_open();
	if (ret)
		return ret;
	LOGD("Opened ufs bsg dev: %s\n", ufs_bsg_dev);

	ret = ufs_query_attr(fd_ufs_bsg, boot_lun_id, QUERY_REQ_FUNC_STD_WRITE,
			     QUERY_REQ_OP_WRITE_ATTR, QUERY_ATTR_IDN_BOOT_LU_EN,
			     0, 0);
	if (ret)
		fprintf(stderr,
			"Error requesting ufs attr idn %d via query ioctl (return value: %d, error no: %d)",
			QUERY_ATTR_IDN_BOOT_LU_EN, ret, errno);


	ufs_bsg_dev_close();
	return ret;
}
