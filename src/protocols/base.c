/* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdio.h>

#include "scmi_protocol.h"
#include "access_control.h"
#include "log.h"
#include "base.h"

#define RESP(msg_id) response_##msg_id

#define PRE_PROCESS(msg_id)  \
       if (resp_struct_len < sizeof(struct base_resp_##msg_id)) { \
            rsp->ret_values[0] = SCMI_RESP_STATUS_INV; \
            goto buffer_not_enough; \
       }   \
       *rsp_len = sizeof(struct base_resp_##msg_id); \
       struct base_resp_##msg_id *RESP(msg_id) = (struct base_resp_##msg_id *)rsp->ret_values;

static int scmi_base_req_process(struct vhost_user_scmi *vscmi, struct scmi_msg_info *hdr,
                        struct virtio_scmi_request *req, uint32_t req_len,
                        struct virtio_scmi_response *rsp, uint32_t *rsp_len)
{
    int i, skip, idx = 0;
    uint32_t resp_struct_len = *rsp_len - 4; // remove the return hdr length.

    if (resp_struct_len < 4)
        return -1;

    switch (hdr->msg_id) {
        case 0x0:
            pr_debug("msg id is protocol version\n");
            PRE_PROCESS(00);
            RESP(00)->status = SCMI_RESP_STATUS_OK;
            RESP(00)->version = 0x20000;
            break;
        case 0x1:
            pr_debug("msg type is protocol attribute \n");
            PRE_PROCESS(01);
            RESP(01)->status = SCMI_RESP_STATUS_OK;
            RESP(01)->attributes = (1 /*agent number*/ << 8) | (vscmi->support_proto_nums << 0);
            break;
        case 0x2:
            pr_debug("msg type is protocol msg attribute \n");
            PRE_PROCESS(02);
            switch (req->params[0]) {
                case 0x0:
                case 0x1:
                case 0x2:
                case 0x3:
                case 0x6:
                     RESP(02)->status = SCMI_RESP_STATUS_OK;
                     break;
                case 0x4:
                case 0x5:
                case 0x7:
                case 0x8:
                case 0x9:
                case 0xA:
                case 0xB:
                     RESP(02)->status = SCMI_RESP_STATUS_NOT_FOUND;
                     break;
                default:
                     RESP(02)->status = SCMI_RESP_STATUS_INV;
            }

            RESP(02)->attributes = 0;
            break;
        case 0x3:
            pr_debug("msg type is discover vendor \n");
            PRE_PROCESS(03);
            RESP(03)->status = SCMI_RESP_STATUS_OK;
            strlcpy(RESP(03)->vendor_identifier, "QualComm", sizeof("QualComm"));
            break;

        case 0x6:
            pr_debug("msg type is get protocol list \n");
            PRE_PROCESS(06);

            RESP(06)->status = SCMI_RESP_STATUS_INV;
            skip = req->params[0];
            if (skip != 0) {
                pr_err("not support non-zero skip!\n");
                break;
            }
            RESP(06)->num_protocols = vscmi->support_proto_nums;

            if (RESP(06)->num_protocols > MAX_RESP_PROTOCL_NUMS) {
                pr_err("not support num_protocols > %d\n", MAX_RESP_PROTOCL_NUMS);
                goto buffer_not_enough;
            }
            // prepare array of protocols
            for (i = 0; i < vscmi->support_proto_nums; i++) {
                idx += i / 4;
                if (idx >= MAX_RESP_PROTOCL_NUMS)
                    break;
                if (i % 4 == 0) {
                    RESP(06)->protocols[idx] = 0;
                }
                RESP(06)->protocols[idx] |= vscmi->protos[i] << ((i % 4) * 8);
            }

            RESP(06)->status = SCMI_RESP_STATUS_OK;
            break;
        default:
            pr_err("msg id %d is not support!\n", hdr->msg_id);
            rsp->ret_values[0] = SCMI_RESP_STATUS_INV;
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

struct scmi_protocol_ops base_ops = {
    .name = "base",
    .id = 0x10,
    .req_process = scmi_base_req_process,

};

SCMI_PROTOCOL_EMUL_SET(base_ops);
