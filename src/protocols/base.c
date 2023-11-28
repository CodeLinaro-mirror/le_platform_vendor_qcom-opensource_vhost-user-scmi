/* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdio.h>

#include "scmi_protocol.h"
#include "access_control.h"
#include "log.h"

static int scmi_base_req_process(struct vhost_user_scmi *vscmi, struct scmi_msg_info *hdr,
                        struct virtio_scmi_request *req, uint32_t req_len,
                        struct virtio_scmi_response *rsp, uint32_t rsp_len)
{
    int i, skip, idx;

    switch (hdr->msg_id) {
        case 0x0:
            pr_debug("msg id is protocol version\n");

            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            rsp->ret_values[1] = 0x20000;

            break;
        case 0x1:
            pr_debug("msg type is protocol attribute \n");

            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            rsp->ret_values[1] = (1 /*agent number*/ << 8) | (vscmi->support_proto_nums << 0);
            break;
        case 0x2:
            pr_debug("msg type is protocol msg attribute \n");

            switch (req->params[0]) {
                case 0x0:
                case 0x1:
                case 0x2:
                case 0x3:
                case 0x6:
                     rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
                     break;
                case 0x4:
                case 0x5:
                case 0x7:
                case 0x8:
                case 0x9:
                case 0xA:
                case 0xB:
                     rsp->ret_values[0] = SCMI_RESP_STATUS_NOT_FOUND;
                     break;
                default:
                     rsp->ret_values[0] = SCMI_RESP_STATUS_INV;
            }

            rsp->ret_values[1] = 0;
            break;
        case 0x3:
            pr_debug("msg type is discover vendor \n");

            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            rsp->ret_values[1] = 'Q' << 0 | 'u' << 8 | 'a' << 16 | 'l' << 24;
            rsp->ret_values[2] = 'C' << 0 | 'o' << 8 | 'm' << 16 | 'm' << 24;
            rsp->ret_values[3] = '\0';
            break;
        case 0x6:
            skip = req->params[0];
            pr_debug("msg type is get protocol list, skip = %d \n", skip);

            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            if (skip >= vscmi->support_proto_nums)
                rsp->ret_values[1] = 0;
            else
                rsp->ret_values[1] = vscmi->support_proto_nums - skip;
            for (i = 0; i < vscmi->support_proto_nums; i++) {
                idx = 2 + i / 4;
                if (i % 4 == 0)
                    rsp->ret_values[idx] = 0;
                rsp->ret_values[idx] |= vscmi->protos[i] << ((i % 4) * 8);
            }
            break;
        default:
            pr_err("msg id %d is not support!\n", hdr->msg_id);
            break;
    }
    rsp->hdr = req->hdr;

    return 0;
}

struct scmi_protocol_ops base_ops = {
    .name = "base",
    .id = 0x10,
    .req_process = scmi_base_req_process,

};

SCMI_PROTOCOL_EMUL_SET(base_ops);
