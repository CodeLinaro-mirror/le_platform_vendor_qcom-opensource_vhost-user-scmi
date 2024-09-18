/* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdio.h>
#include <uapi/misc/qcom_uscmi.h>
#include <sys/ioctl.h>

#include "scmi_protocol.h"
#include "access_control.h"
#include "type.h"
#include "log.h"

#define SUPPORT_PROTOCOL_NUM    3
#define SUPPORT_LEVEL 3

int perf_operation_request(int fd, scmi_oper_ioctl_t *req, char *name, int level, scmi_prf_oper_t op)
{
    memset(req, 0, sizeof(*req));
    req->proto = SCMI_PROTO_PERFORMANCE;
    req->oper = op;
    req->level = level;
    strlcpy(req->name, name, MAX_DOMAIN_LENGTH);

    pr_debug("set perf level: name=%s op=[%d] level=[%d] \n", req->name, op, level);
    return ioctl(fd, SCMI_IOCTL_PRF, req);
}

static int get_sustained_perf_level(struct vhost_user_scmi *vscmi, uint32_t domain_id)
{
    if (domain_id >= vscmi->pf_attr.domain_nums) {
        pr_err("not support such domain id\n");
        return -1;
    }

    // TODO get from the real platform, currently just return 0
    return 0;
}

static int scmi_perf_req_process(struct vhost_user_scmi *vscmi, struct scmi_msg_info *hdr,
                        struct virtio_scmi_request *req, uint32_t req_len,
                        struct virtio_scmi_response *rsp, uint32_t *rsp_len)
{
    int domainid, level_start, level;
    struct perf_domain *pd;
    struct protocol_domain *proto_dm;
    char name[MAX_DOMAIN_LENGTH];
    scmi_oper_ioctl_t request;
    uint32_t sus_level;
    int i, fd;
    uint32_t ret_len = 1;
    int ret = 0;

    switch (hdr->msg_id) {
        case 0x0:
            pr_debug("msg id is protocol version\n");

            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            rsp->ret_values[ret_len++] = 0x30000;
            break;
        case 0x1:
            pr_debug("msg type is protocol attribute \n");
            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            /* [17:16] power unit
             * 2 - uW
             * 1 - mW
             * 0 - abstract linear scale
             * [15:0] number of performance domain
             */
            rsp->ret_values[ret_len++] = vscmi->pf_attr.domain_nums << 0;
            // low address for statistics shared memory region
            rsp->ret_values[ret_len++] = 0;
            // high address for statistics shared memory region
            rsp->ret_values[ret_len++] = 0;
            // static lens
            rsp->ret_values[ret_len++] = 0;
            break;
        case 0x2:
            pr_debug("msg type is protocol msg attribute \n");
            switch (req->params[0]) {
                case 0x0:
                case 0x1:
                case 0x2:
                case 0x3:
                case 0x4:
                case 0x7:
                case 0x8:
                     rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
                     break;
                case 0x5:
                case 0x6:
                case 0x9:
                case 0xA:
                case 0xB:
                case 0xC:
                     rsp->ret_values[0] = SCMI_RESP_STATUS_NOT_FOUND;
                     break;
                default:
                     rsp->ret_values[0] = SCMI_RESP_STATUS_INV;
            }

            rsp->ret_values[ret_len++] = 0;
            break;
        case 0x3:
            domainid = req->params[0];
            pr_debug("msg type is domain attribute, domainid = %d \n", domainid);

            proto_dm = get_dev_pd(&vscmi->dev_res, 0x13, domainid);
            if (!proto_dm) {
                rsp->ret_values[0] = SCMI_RESP_STATUS_INV;
                break;
            }

            /*
             *[31]: can set limit - 0
             *[30]: can set performance level - 1
             *[29]: performance limit change notification - 0
             *[28]: performance level change notification - 0
             *[27]: fastchannel support - 0
             *[26]: extended performence domain name - 0
             *[25-0]*/
            rsp->ret_values[ret_len++] = 0x40000000;
            //rate_limit, the minimum time required between successive
            //requests. A value of 0 indicates that this field is not
            //supported by the platform.
            rsp->ret_values[ret_len++] = 0;
            // sustained_freq - Base frequency corresponding to the
            // sustained performance level. Expressed in units of kHz.
            rsp->ret_values[ret_len++] = 0; // Currently hardcode here, may get from platform automatically.
            // sustained_perf_level - The performance level value that corresponds to the sustained
            // performance delivered by the platform.
            sus_level = get_sustained_perf_level(vscmi, domainid);
            if (sus_level < 0) {
                rsp->ret_values[0] = SCMI_RESP_STATUS_INV;
                ret_len = 1;
                break;
            }
            rsp->ret_values[ret_len++] = sus_level;

            add_domainid_to_name(proto_dm->domain_name, domainid, name);
            int n = strlcpy(&rsp->ret_values[ret_len], name, MAX_DOMAIN_LENGTH);
            ret_len += n / 4 + 1;

            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            break;
        case 0x4:
            domainid = req->params[0];
            level_start = req->params[1];
            pr_debug("msg type is get domain level description, domainid = %d level_start = %d\n",
                        domainid, level_start);

            if (domainid >= vscmi->pf_attr.domain_nums) {
                rsp->ret_values[0] = SCMI_RESP_STATUS_INV;
                break;
            }

            pd = &vscmi->pf_attr.pds[domainid];
            if (level_start >= pd->level_nums) {
                rsp->ret_values[0] = SCMI_RESP_STATUS_INV;
                break;
            }

            if (level_start == 0) {
                 pd->left_levels = pd->level_nums;
            }
            /* level_nums
             * Bits[31:16] Number of remaining performance levels.
             * Bits[15:12] Reserved, must be zero.
             * Bits[11:0] Number of performance levels that are returned by this call.
            */
            int trans_levels = pd->left_levels > MAX_TRANSFER_LEVEL ? MAX_TRANSFER_LEVEL : pd->left_levels;
            // record the left level
            pd->left_levels -= trans_levels;

            if ((level_start + trans_levels) > pd->level_nums) {
                rsp->ret_values[0] = SCMI_RESP_STATUS_INV;
                pr_err("ERROR: please make sure the level_start is continous!!\n");
                break;
            }

            rsp->ret_values[ret_len++] = (pd->left_levels << 16) |
                    (trans_levels /*number of level*/) << 0;

            for (i = level_start; i < (level_start + trans_levels) && i < pd->level_nums; i++) {
                rsp->ret_values[ret_len++] = pd->level[i];
                // Power cost. A value of zero indicates that the power cost is not reported by the platform.
                rsp->ret_values[ret_len++] = 0;
                // Bits[31:16] Reserved, must be zero.
                // Worst-case transition latency in microseconds to move from any supported performance to
                // the level indicated by this entry in the array.
                rsp->ret_values[ret_len++] = 1; // hardcode here, may get from platform.
                pr_debug("domain_id=%d, level=%d \n", domainid, pd->level[i]);
            }
            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            break;

        case 0x7:
            // set level
            domainid = req->params[0];
            level = req->params[1];
            pr_debug("msg type is set level, set domain %d to level %d \n", domainid, level);

            proto_dm = get_dev_pd(&vscmi->dev_res, 0x13, domainid);
            if (!proto_dm) {
                rsp->ret_values[0] = SCMI_RESP_STATUS_INV;
                break;
            }

            fd = get_dev_fd(&vscmi->dev_res, 0x13, domainid);
            if (fd < 0) {
                rsp->ret_values[0] = SCMI_RESP_STATUS_INV;
                break;
            }

            ret = perf_operation_request(fd, &request, (char *)proto_dm->domain_name, level, SCMI_PRF_LVL_SET);
            // ADD ioctl here to passdown the request.
            if (ret < 0) {
                rsp->ret_values[0] = SCMI_RESP_STATUS_NOT_FOUND;
            } else {
                rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            }
            break;
        case 0x8:
            // get level
            domainid = req->params[0];
            pr_debug("msg type is get leve, get domain %d level %d \n", domainid, 0);

            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            rsp->ret_values[ret_len++] = 0 /*TODO  get level with ioctl*/;
            // return the reocrd level
            break;
        default:
            rsp->ret_values[0] = SCMI_RESP_STATUS_INV;
            break;
    }

    rsp->hdr = req->hdr;
    *rsp_len = ret_len * 4;
    return ret;
}

void parse_perf_node(struct vhost_user_scmi *vscmi, char *args)
{
    // perf,{domainid:level_0:level_1:..level_n}
    // perf,{0:1|2},{1:1|2|3}
    struct perf_attributes *pa = &vscmi->pf_attr;
    struct perf_domain *pd;
    char *sr, *sn, *st, *stt, *sttt;

    sr = sn = strdup(args);
   // make sure pa is clear during initates.
    while(st = strsep(&sn, ",")) {
        stt = strsep(&st, "/");
        if (!st) break;

        pd = &pa->pds[pa->domain_nums++];
        if (pa->domain_nums > MAX_PERF_DOMAIN) {
            pr_err("too many domain!\n");
            break;
        }
        pd->domain_id = atoi(stt);
        while (sttt = strsep(&st, ":")) {
            pd->level[pd->level_nums++] = atoi(sttt);
            if (pd->level_nums > MAX_PERF_LEVEL) {
                pr_err("too many level!\n");
                break;
            }
            pr_debug("domain = %d level = %d\n", pd->domain_id, atoi(sttt));
        }
        pd->left_levels = pd->level_nums;
    }
    free(sr);
    add_to_protocol_list(vscmi, 0x13);
}

static void scmi_perf_reset(struct vhost_user_scmi *vscmi)
{

}
struct scmi_protocol_ops perf_ops = {
    .name = "perf",
    .id = 0x13,
    .req_process = scmi_perf_req_process,
    .reset = scmi_perf_reset,
};

SCMI_PROTOCOL_EMUL_SET(perf_ops);
