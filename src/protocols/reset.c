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
#include "reset.h"

#define RESP(msg_id) response_##msg_id

#define PRE_PROCESS(msg_id)  \
       if (resp_struct_len < sizeof(struct reset_resp_##msg_id)) { \
            rsp->ret_values[0] = SCMI_RESP_STATUS_INV; \
            goto buffer_not_enough; \
       }   \
       *rsp_len = sizeof(struct reset_resp_##msg_id); \
       struct reset_resp_##msg_id *RESP(msg_id) = (struct reset_resp_##msg_id *)rsp->ret_values;

int reset_operation_request(int fd, scmi_oper_ioctl_t *req, const char *name, scmi_rst_oper_t op)
{
    memset(req, 0, sizeof(*req));
    req->proto = SCMI_PROTO_RESET;
    req->oper = op;
    safe_strlcpy(req->name, name, MAX_DOMAIN_LENGTH);

    pr_debug("set reset: name=%s op=[%d]\n", req->name, op);
    return ioctl(fd, SCMI_IOCTL_RST, req);
}

static int scmi_reset_req_process(struct vhost_user_scmi *vscmi, struct scmi_msg_info *hdr,
                        struct virtio_scmi_request *req, uint32_t req_len,
                        struct virtio_scmi_response *rsp, uint32_t *rsp_len)
{
    uint32_t domain_id;
    uint32_t reset_flag, reset_state;
    scmi_rst_oper_t reset_type;
    char name[MAX_DOMAIN_LENGTH];
    uint32_t channel_id;
    int fd, ret;
    scmi_oper_ioctl_t request;
    struct protocol_domain *proto_dm;
    uint32_t resp_struct_len = *rsp_len - 4; // remove the return hdr length.

    if (resp_struct_len < 4)
        return -1;

    switch (hdr->msg_id) {
        case 0x0:
            pr_debug("msg id is protocol version\n");
            PRE_PROCESS(00);

            RESP(00)->status = SCMI_RESP_STATUS_OK;
            RESP(00)->version = 0x30000;

            break;
        case 0x1:
            pr_debug("msg type is protocol attribute \n");
            PRE_PROCESS(01);
            RESP(01)->status = SCMI_RESP_STATUS_OK;
            RESP(01)->attributes = vscmi->rs_attr.domain_nums;
            break;
        case 0x2:
            pr_debug("msg type is protocol msg attribute \n");

            PRE_PROCESS(02);
            switch (req->params[0]) {
                case 0x0:
                case 0x1:
                case 0x2:
                case 0x3:
                case 0x4:
                     RESP(02)->status = SCMI_RESP_STATUS_OK;
                     break;
                case 0x5:
                case 0x6:
                     RESP(02)->status = SCMI_RESP_STATUS_NOT_FOUND;
                     break;
                default:
                     RESP(02)->status = SCMI_RESP_STATUS_INV;
            }
            RESP(02)->attributes = 0;
            break;
        case 0x3:
            domain_id = req->params[0];
            pr_debug("msg type is protocol domain attribute, domain id %d \n", domain_id);
            PRE_PROCESS(03);
            proto_dm = get_dev_pd(&vscmi->dev_res, 0x16, domain_id);
            if (!proto_dm) {
                RESP(03)->status = SCMI_RESP_STATUS_INV;
                break;
            }

            // currenlty, each domain_id has the same attribute.

            RESP(03)->attributes = 0x0;
            RESP(03)->latency = 0xFFFFFFFF; //indicates this field is not supported by the platform

            add_domainid_to_name(proto_dm->domain_name, domain_id, name);
            int n = safe_strlcpy(RESP(03)->name, name, MAX_DOMAIN_LENGTH);
 
            RESP(03)->status = SCMI_RESP_STATUS_OK;
            break;
        case 0x4:
            domain_id = req->params[0];
            pr_debug("msg type is reset domain %d \n", domain_id);

            PRE_PROCESS(04);
            proto_dm = get_dev_pd(&vscmi->dev_res, 0x16, domain_id);
            if (!proto_dm) {
                RESP(04)->status = SCMI_RESP_STATUS_INV;
                break;
            }
            fd = get_dev_fd(&vscmi->dev_res, hdr->protocol_id, domain_id);
            if (fd < 0) {
                pr_debug("could not find device \n");
                RESP(04)->status = SCMI_RESP_STATUS_INV;
                break;
            }
            reset_flag = req->params[1];
            reset_state = req->params[2];
            pr_debug("reset flags = 0x%x \n", reset_flag);
            pr_debug("reset state = 0x%x \n", reset_state);

            if (reset_flag & RESET_FLAGS_Reserved_Mask) {
                pr_err("Invalid Reset Flag: 0x%x \n", reset_flag);
                RESP(04)->status = SCMI_RESP_STATUS_INV;
                break;
            }
            // only reset type = 0 and reset_id = 0 are supported
            if ((reset_state & RESET_STATUS_Reset_Type_Mask) || (reset_state & RESET_STATUS_Reset_ID_Mask)) {
                pr_err("Invalid Reset Type\n");
                RESP(04)->status = SCMI_RESP_STATUS_INV;
            }

            if ((reset_flag & RESET_FLAGS_Autonomous_Reset_Mask) >> RESET_FLAGS_Autonomous_Reset_Shift)
            {
                reset_type = SCMI_RST_RESET;
                if (reset_flag & RESET_FLAGS_Async_Flag_Mask) {
                    pr_err("Not support async reset!!, will fallback to sync reset\n");
                }
            } else if ((reset_flag & RESET_FLAGS_Explicit_Signal_Mask) >> RESET_FLAGS_Explicit_Signal_Shift) {
                reset_type = SCMI_RST_ASSERT;
            } else {
                reset_type = SCMI_RST_DEASSERT;
            }
            ret = reset_operation_request(fd, &request,	(char *)proto_dm->domain_name, reset_type);
            if (ret < 0)
                RESP(04)->status = SCMI_RESP_STATUS_NOT_FOUND;
            else
                RESP(04)->status = SCMI_RESP_STATUS_OK;
            break;

        default:
            pr_err("[Error] msg id %d is not support\n", hdr->msg_id);
            rsp->ret_values[0] = SCMI_RESP_STATUS_NOT_FOUND;
            *rsp_len = 4;
            break;
    }

    rsp->hdr = req->hdr;
    return 0;

buffer_not_enough:
    pr_err("%s: response data len %d is not correct for message %d \n",
                __func__, resp_struct_len, hdr->msg_id);
    *rsp_len = 4;
    rsp->hdr = req->hdr;
    return 0;
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
