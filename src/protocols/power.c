/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdio.h>
#include <sys/ioctl.h>
#include <uapi/misc/qcom_uscmi.h>
#include "scmi_protocol.h"
#include "access_control.h"
#include "iface_scmi_dev.h"
#include "log.h"

#define POWER_TYPEID_MASK  ((1UL << 28) - 1)
#define POWER_STATETYPE_SHFIT 30
int power_operation_request(int fd, scmi_oper_ioctl_t *req, scmi_pwr_oper_t op)
{
	memset(req, 0, sizeof(*req));
	req->proto = SCMI_PROTO_POWER;
	req->oper = op;

	return ioctl(fd, SCMI_IOCTL_PWR, req);
}

static int scmi_power_req_process(struct vhost_user_scmi *vscmi, struct scmi_msg_info *hdr,
                        struct virtio_scmi_request *req, uint32_t req_len,
                        struct virtio_scmi_response *rsp, uint32_t rsp_len)
{
    uint32_t domain_id;
    uint32_t power_stat;
    uint32_t ret;
    uint32_t channel_id;
    scmi_oper_ioctl_t request;
    scmi_pwr_oper_t pwr_oper;
    int fd;
    static uint32_t record_power_stat = 0;

    switch (hdr->msg_id) {
        case 0x0:
            pr_debug("msg id is protocol version\n");
            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            rsp->ret_values[1] = 0x20000;

            break;
        case 0x1:
            pr_debug("msg type is protocol attribute \n");
            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            rsp->ret_values[1] = vscmi->pw_attr.domain_nums; //get_power_domain_number();
            rsp->ret_values[2] = 0;
            rsp->ret_values[3] = 0;
            rsp->ret_values[4] = 0; // no shared memory region.
            break;
        case 0x2:
            pr_debug("msg type is protocol msg attribute \n");
            switch (req->params[0]) {
                case 0x0:
                case 0x1:
                case 0x2:
                case 0x3:
                case 0x4:
                case 0x5:
                     rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
                     break;
                case 0x6:
                case 0x7:
                case 0x8:
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
            if (domain_id >= vscmi->pw_attr.domain_nums)
                return -1;
            // currently, each domain has same attribute.
            rsp->ret_values[1] = 0x1 << 29; //Power state synchronous support.
            rsp->ret_values[2] = ('p' << 0) | ('o' << 8) | ('w' << 16) | ('e' << 24);
            rsp->ret_values[3] = ('r' << 0) | ((domain_id + '0') << 8) | ('\0' << 16);

            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            break;
        case 0x4:
            pr_debug("msg type is power set for domain %d \n", domain_id);
            domain_id = req->params[0];
            fd = get_dev_fd(&vscmi->dev_res, hdr->protocol_id, domain_id);
            if (fd < 0) {
                pr_debug("no such device, pleae check!\n");
                return -1;
            }
            power_stat = req->params[1];
            if ((power_stat & POWER_TYPEID_MASK) == 0) {
                if (power_stat & (1UL << POWER_STATETYPE_SHFIT) == 0) {
                    pwr_oper = SCMI_PWR_ON;
                } else {
                    pwr_oper = SCMI_PWR_OFF;
                }
            } else {
                rsp->ret_values[0] = SCMI_RESP_STATUS_INV;
                break;
            }
            ret = power_operation_request(fd, &request, pwr_oper);
            if (ret < 0)
                rsp->ret_values[0] = SCMI_RESP_STATUS_NOT_FOUND;
            else {
                rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
                record_power_stat = power_stat;
            }
            break;
        case 0x5:
            domain_id = req->params[0];
            pr_debug("msg type is power get for domain %d \n", domain_id);
            fd = get_dev_fd(&vscmi->dev_res, hdr->protocol_id, domain_id);
            if (fd < 0) {
                pr_debug("no such device, pleae check!\n");
                return -1;
            }
            // Currently no IOCTL for to get power state, so just return the record state.
            rsp->ret_values[0] = SCMI_RESP_STATUS_OK;
            rsp->ret_values[1] = record_power_stat;
            break;
        default:
            pr_err("msg id %d is not support\n", hdr->msg_id);
            rsp->ret_values[0] = SCMI_RESP_STATUS_NOT_FOUND;
            break;
    }

    rsp->hdr = req->hdr;
    return 0;
}

void parse_power_node(struct vhost_user_scmi *vscmi, char *args)
{
    struct power_attributes *pa = &vscmi->pw_attr;

    pa->domain_nums = atoi(args);
    pr_debug("power domian num is %d\n", pa->domain_nums);
    add_to_protocol_list(vscmi, 0x11);
}

struct scmi_protocol_ops power_ops = {
    .name = "power",
    .id = 0x11,
    .req_process = scmi_power_req_process,
};

SCMI_PROTOCOL_EMUL_SET(power_ops);
