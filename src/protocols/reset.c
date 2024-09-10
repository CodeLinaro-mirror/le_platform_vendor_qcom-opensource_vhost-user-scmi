/* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdio.h>
#include <sys/ioctl.h>
#include <uapi/misc/qcom_uscmi.h>
#include "scmi_protocol.h"
#include "access_control.h"
#include "iface_scmi_dev.h"
#include "log.h"

int reset_operation_request(int fd, scmi_oper_ioctl_t *req, const char *name, scmi_rst_oper_t op)
{
    memset(req, 0, sizeof(*req));
    req->proto = SCMI_PROTO_RESET;
    req->oper = op;
    strlcpy(req->name, name, MAX_DOMAIN_LENGTH);

    pr_debug("set reset: name=%s op=[%d]\n", req->name, op);
    return ioctl(fd, SCMI_IOCTL_RST, req);
}

static int scmi_reset_req_process(struct vhost_user_scmi *vscmi, struct scmi_msg_info *hdr,
                        struct virtio_scmi_request *req, uint32_t req_len,
                        struct virtio_scmi_response *rsp, uint32_t *rsp_len)
{
    uint32_t domain_id;
    uint32_t reset_flag, reset_state;
    uint32_t ret;
    char name[MAX_DOMAIN_LENGTH];
    uint32_t channel_id;
    int fd;
    scmi_oper_ioctl_t request;
    struct protocol_domain *proto_dm;
    uint32_t ret_len = 1;

    switch (hdr->msg_id) {
        case 0x0:
            pr_debug("msg id is protocol version\n");

            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            rsp->ret_values[ret_len++] = 0x30000;

            break;
        case 0x1:
            pr_debug("msg type is protocol attribute \n");

            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            rsp->ret_values[ret_len++] = vscmi->rs_attr.domain_nums;
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
            rsp->ret_values[ret_len++] = 0;
            break;
        case 0x3:
            pr_debug("msg type is protocol domain attribute \n");
            domain_id = req->params[0];

            proto_dm = get_dev_pd(&vscmi->dev_res, 0x16, domain_id);
            if (!proto_dm) {
                rsp->ret_values[0] = SCMI_RESP_STATUS_INV;
                break;
            }

            // currenlty, each domain_id has the same attribute.

            rsp->ret_values[ret_len++] = 0x0;
            rsp->ret_values[ret_len++] = 0xFFFFFFFF; //indicates this field is not supported by the platform

            add_domainid_to_name(proto_dm->domain_name, domain_id, name);
            int n = strlcpy(&rsp->ret_values[ret_len], name, MAX_DOMAIN_LENGTH);
            ret_len += n / 4 + 1;
 
            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            break;
        case 0x4:
            pr_debug("msg type is reset \n");

            domain_id = req->params[0];

            proto_dm = get_dev_pd(&vscmi->dev_res, 0x16, domain_id);
            if (!proto_dm) {
                rsp->ret_values[0] = SCMI_RESP_STATUS_INV;
                break;
            }
            fd = get_dev_fd(&vscmi->dev_res, hdr->protocol_id, domain_id);
            if (fd < 0) {
                pr_debug("could not find device \n");
                rsp->ret_values[0] = SCMI_RESP_STATUS_INV;
                break;
            }
            reset_flag = req->params[1];
            reset_state = req->params[2];
            ret = reset_operation_request(fd, &request,	(char *)proto_dm->domain_name, SCMI_RST_RESET);
            if (ret < 0)
                rsp->ret_values[0] = SCMI_RESP_STATUS_NOT_FOUND;
            else
                rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            break;

        default:
            pr_err("[Error] msg id %d is not support\n", hdr->msg_id);
            rsp->ret_values[0] = SCMI_RESP_STATUS_NOT_FOUND;
            break;
    }

    rsp->hdr = req->hdr;
    *rsp_len = ret_len * 4;
    return ret;
}

int parse_reset_node(struct vhost_user_scmi *vscmi, char *args)
{
    struct reset_attributes *ra = &vscmi->rs_attr;

    ra->domain_nums = atoi(args);
    if (ra->domain_nums >= MAX_RESET_DOMAIN) {
        pr_err("[Error] reset domain number should not larger than %d\n",
            MAX_RESET_DOMAIN);
        return -1;
    }

    pr_debug("reset domian num is %d\n", ra->domain_nums);
    add_to_protocol_list(vscmi, 0x16);
    return 0;
}

static void scmi_reset_reset(struct vhost_user_scmi *vscmi)
{

}

struct scmi_protocol_ops reset_ops = {
    .name = "reset",
    .id = 0x16,
    .req_process = scmi_reset_req_process,
    .reset = scmi_reset_reset,
};

SCMI_PROTOCOL_EMUL_SET(reset_ops);
