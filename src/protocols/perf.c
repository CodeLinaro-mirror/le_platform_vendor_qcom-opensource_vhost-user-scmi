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

int perf_operation_request(int fd, scmi_oper_ioctl_t *req, int level, scmi_prf_oper_t op)
{
   	memset(req, 0, sizeof(*req));
	req->proto = SCMI_PROTO_PERFORMANCE;
	req->oper = op;
	req->level = level;

	return ioctl(fd, SCMI_IOCTL_PRF, req);
}

static int get_sustained_perf_level(struct vhost_user_scmi *vscmi, uint32_t domain_id)
{
    if (domain_id >= vscmi->pf_attr.domain_nums) {
        pr_err("not support such domain id\n");
        return -1;
    }

    // TODO get from the real platform, currently just return the first level
    return vscmi->pf_attr.pds[domain_id].level[0];
}

static int scmi_perf_req_process(struct vhost_user_scmi *vscmi, struct scmi_msg_info *hdr,
                        struct virtio_scmi_request *req, uint32_t req_len,
                        struct virtio_scmi_response *rsp, uint32_t rsp_len)
{
    int domainid, level_start, level, offset;
    struct perf_domain *pd;
    scmi_oper_ioctl_t request;
    uint32_t sus_level;
    int i, fd;

    switch (hdr->msg_id) {
        case 0x0:
            pr_debug("msg id is protocol version\n");

            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            rsp->ret_values[1] = 0x30000;
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
            rsp->ret_values[1] = vscmi->pf_attr.domain_nums << 0;
            // low address for statistics shared memory region
            rsp->ret_values[2] = 0;
            // high address for statistics shared memory region
            rsp->ret_values[3] = 0;
            // static lens
            rsp->ret_values[4] = 0;
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
                case 0xC:
                     rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
                     break;
                case 0x5:
                case 0x6:
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
            domainid = req->params[0];
            pr_debug("msg type is domain attribute, domainid = %d \n", domainid);
            /*
             *[31]: can set limit - 0
             *[30]: can set performance level - 1
             *[29]: performance limit change notification - 0
             *[28]: performance level change notification - 0
             *[27]: fastchannel support - 0
             *[26]: extended performence domain name - 0
             *[25-0]*/
            rsp->ret_values[1] = 0x4000;
            //rate_limit, the minimum time required between successive
            //requests. A value of 0 indicates that this field is not
            //supported by the platform.
            rsp->ret_values[2] = 0;
            // sustained_freq - Base frequency corresponding to the
            // sustained performance level. Expressed in units of kHz.
            rsp->ret_values[3] = 1000; // Currently hardcode here, may get from platform automatically.
            // sustained_perf_level - The performance level value that corresponds to the sustained
            // performance delivered by the platform.
            sus_level = get_sustained_perf_level(vscmi, domainid);
            if (sus_level < 0) {
                 rsp->ret_values[0] = SCMI_RESP_STATUS_INV;
                 break;
            }
            rsp->ret_values[4] = sus_level;
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
            /* level_nums
             * Bits[31:16] Number of remaining performance levels.
             * Bits[15:12] Reserved, must be zero.
             * Bits[11:0] Number of performance levels that are returned by this call.
            */
            rsp->ret_values[1] = (0 /*no remaining levles*/ << 16) |
                    (pd->level_nums - level_start /*number of level*/) << 0;
            offset = 2;
            for (i = level_start; i < pd->level_nums; i++) {
                rsp->ret_values[offset++] = pd->level[i];
                // Power cost. A value of zero indicates that the power cost is not reported by the platform.
                rsp->ret_values[offset++] = 0;
                // Bits[31:16] Reserved, must be zero.
                // Worst-case transition latency in microseconds to move from any supported performance to
                // the level indicated by this entry in the array.
                rsp->ret_values[offset++] = 1; // hardcode here, may get from platform.
            }
            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            break;

        case 0x7:
            // set level
            domainid = req->params[0];
            level = req->params[1];
            pr_debug("msg type is set level, set domain %d to level %d \n", domainid, level);

            fd = get_dev_fd(&vscmi->dev_res, 0x13, domainid);
            if (fd < 0) {
                rsp->ret_values[0] = SCMI_RESP_STATUS_INV;
                break;
            }

            perf_operation_request(fd, &request, level, SCMI_PRF_LVL_SET);
            // ADD ioctl here to passdown the request.
            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            break;
        case 0x8:
            // get level
            domainid = req->params[0];
            pr_debug("msg type is get leve, get domain %d level %d \n", domainid, 0);

            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            rsp->ret_values[1] = 0 /*TODO  get level with ioctl*/;
            // return the reocrd level
            break;
        case 0xC:
            pr_debug("msg type is get name\n");
            domainid = req->params[0];
            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            rsp->ret_values[1] = 0;
            rsp->ret_values[2] = 'D' << 0 | 'o' << 8 | 'm' << 16 | '0' << 24;
            rsp->ret_values[2] = '\0';
            break;
        default:
            rsp->ret_values[0] = SCMI_RESP_STATUS_INV;
            break;
    }

    rsp->hdr = req->hdr;
    return 0;
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
        if (pa->domain_nums >= MAX_PERF_DOMAIN) {
            pr_err("too many domain!\n");
            break;
        }
        pd->domain_id = atoi(stt);
        while (sttt = strsep(&st, ":")) {
            pd->level[pd->level_nums++] = atoi(sttt);
            if (pd->level_nums >= MAX_PERF_LEVEL) {
                pr_err("too many level!\n");
                break;
            }
            pr_debug("domain = %d level = %d\n", pd->domain_id, atoi(sttt));
        }
    }
    free(sr);
    add_to_protocol_list(vscmi, 0x13);
}

struct scmi_protocol_ops perf_ops = {
    .name = "perf",
    .id = 0x13,
    .req_process = scmi_perf_req_process,
};

SCMI_PROTOCOL_EMUL_SET(perf_ops);
