/* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdio.h>
#include <sys/ioctl.h>
#include <qcom_uscmi.h>
#include "scmi_protocol.h"
#include "access_control.h"
#include "iface_scmi_dev.h"
#include "log.h"

//define strlcpy to avoid the banned strncpy
size_t strlcpy(char *dst, const char *src, size_t size)
{
    int copyed = 0;
    int i;

    if (!dst || !src || (size < 2))
        return copyed;

    for (i = 0; i < size - 1; i++) {
        if (*src != '\0') {
            *dst++ = *src++;
            copyed++;
        } else {
            break;
        }
    }
    *dst = '\0';

    return copyed;
}

int reset_operation_request(int fd, scmi_oper_ioctl_t *req, const char *id, scmi_rst_oper_t op)
{
	memset(req, 0, sizeof(*req));
	req->proto = SCMI_PROTO_RESET;
	req->oper = op;
	if (id)
		strlcpy(req->reset_id, id, RESET_ID_LEN - 1);

	return ioctl(fd, SCMI_IOCTL_RST, req);
}

static int scmi_reset_req_process(struct vhost_user_scmi *vscmi, struct scmi_msg_info *hdr,
                        struct virtio_scmi_request *req, uint32_t req_len,
                        struct virtio_scmi_response *rsp, uint32_t rsp_len)
{
    uint32_t domain_id;
    uint32_t reset_flag, reset_state;
    uint32_t ret;
    uint32_t channel_id;
    int fd;
    scmi_oper_ioctl_t request;

    switch (hdr->msg_id) {
        case 0x0:
            pr_debug("msg id is protocol version\n");

            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            rsp->ret_values[1] = 0x30000;

            break;
        case 0x1:
            pr_debug("msg type is protocol attribute \n");

            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            rsp->ret_values[1] = vscmi->rs_attr.domain_nums;
            break;
        case 0x2:
            pr_debug("msg type is protocol msg attribute \n");

            switch (req->params[0]) {
                case 0x0:
                case 0x1:
                case 0x2:
                case 0x3:
                case 0x4:
                     rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
                     break;
                case 0x5:
                case 0x6:
                     rsp->ret_values[0] = SCMI_RESP_STATUS_NOT_FOUND;
                     break;
                default:
                     rsp->ret_values[0] = SCMI_RESP_STATUS_INV;
            }
            rsp->ret_values[1] = 0;
            break;
        case 0x3:
            pr_debug("msg type is protocol domain attribute \n");
            domain_id = req->params[0];

            if (domain_id >= vscmi->rs_attr.domain_nums)
                return -1;
            // currenlty, each domain_id has the same attribute.
            rsp->ret_values[1] = 0x0;
            rsp->ret_values[2] = 0xFFFFFFFF; //indicates this field is not supported by the platform
            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            break;
        case 0x4:
            pr_debug("msg type is reset \n");

            domain_id = req->params[0];
            fd = get_dev_fd(&vscmi->dev_res, hdr->protocol_id, domain_id);
            if (fd < 0) {
                pr_debug("could not find device \n");
                return -1;
            }
            reset_flag = req->params[1];
            reset_state = req->params[2];
            ret = reset_operation_request(fd, &request,	NULL, SCMI_RST_RESET);
            if (ret < 0)
                rsp->ret_values[0] = SCMI_RESP_STATUS_NOT_FOUND;
            else
                rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            break;

        default:
            pr_err("msg id %d is not support\n", hdr->msg_id);
            rsp->ret_values[0] = SCMI_RESP_STATUS_NOT_FOUND;
            break;
    }

    rsp->hdr = req->hdr;
    return 0;
}

void parse_reset_node(struct vhost_user_scmi *vscmi, char *args)
{
    struct reset_attributes *ra = &vscmi->rs_attr;

    ra->domain_nums = atoi(args);
    pr_debug("reset domian num is %d\n", ra->domain_nums);
    add_to_protocol_list(vscmi, 0x16);
}

struct scmi_protocol_ops reset_ops = {
    .name = "reset",
    .id = 0x16,
    .req_process = scmi_reset_req_process,
};

SCMI_PROTOCOL_EMUL_SET(reset_ops);
