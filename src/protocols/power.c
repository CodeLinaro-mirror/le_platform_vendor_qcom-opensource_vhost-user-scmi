/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdio.h>
#include <sys/ioctl.h>
#include <uapi/misc/qcom_uscmi.h>
#include <assert.h>
#include "scmi_protocol.h"
#include "access_control.h"
#include "iface_scmi_dev.h"
#include "log.h"
#include "list.h"
#include "power.h"

#define POWER_TYPEID_MASK  ((1UL << 28) - 1)
#define POWER_STATETYPE_SHFIT 30
#define POWER_STATETYPE_MASK 0x1

#define POWER_STATE_ON  (((0 & POWER_STATETYPE_MASK) << POWER_STATETYPE_SHFIT) \
                | (0 & POWER_TYPEID_MASK))
#define POWER_STATE_OFF (((1 & POWER_STATETYPE_MASK) << POWER_STATETYPE_SHFIT) \
                | (0 & POWER_TYPEID_MASK))

#define RESP(msg_id) response_##msg_id

#define PRE_PROCESS(msg_id)  \
       if (resp_struct_len < sizeof(struct power_resp_##msg_id)) { \
            rsp->ret_values[0] = SCMI_RESP_STATUS_INV; \
            goto buffer_not_enough; \
       }   \
       *rsp_len = sizeof(struct power_resp_##msg_id); \
       struct power_resp_##msg_id *RESP(msg_id) = (struct power_resp_##msg_id *)rsp->ret_values;

int power_operation_request(int fd, scmi_oper_ioctl_t *req, char *name, scmi_pwr_oper_t op)
{
    memset(req, 0, sizeof(*req));
    req->proto = SCMI_PROTO_POWER;
    req->oper = op;
    strlcpy(req->name, name, MAX_DOMAIN_LENGTH);

    pr_debug("set power: name=%s op=[%d] \n", name, op);
    return ioctl(fd, SCMI_IOCTL_PWR, req);
}

static int scmi_power_req_process(struct vhost_user_scmi *vscmi, struct scmi_msg_info *hdr,
                        struct virtio_scmi_request *req, uint32_t req_len,
                        struct virtio_scmi_response *rsp, uint32_t *rsp_len)
{
    uint32_t domain_id;
    uint32_t power_stat;
    uint32_t channel_id;
    char name[MAX_DOMAIN_LENGTH];
    scmi_oper_ioctl_t request;
    scmi_pwr_oper_t pwr_oper;
    struct protocol_domain *proto_dm;
    int fd, ret;
    struct power_attributes *pa = &vscmi->pw_attr;
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
            RESP(01)->attributes= vscmi->pw_attr.domain_nums; //get_power_domain_number();
            RESP(01)->statistics_address_low = 0;
            RESP(01)->statistics_address_high = 0;
            RESP(01)->statistics_len = 0; // no shared memory region.
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
                case 0x5:
                     RESP(02)->status = SCMI_RESP_STATUS_OK;
                     break;
                case 0x6:
                case 0x7:
                case 0x8:
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
            proto_dm = get_dev_pd(&vscmi->dev_res, 0x11, domain_id);
            if (!proto_dm) {
                RESP(03)->status = SCMI_RESP_STATUS_INV;
                break;
            }
            // currently, each domain has same attribute.
            RESP(03)->attributes = 0x1 << 29; //Power state synchronous support.

            add_domainid_to_name(proto_dm->domain_name, domain_id, name);
            int n = strlcpy(RESP(03)->name, name, MAX_DOMAIN_LENGTH);

            RESP(03)->status = SCMI_RESP_STATUS_OK;
            break;
        case 0x4:
            domain_id = req->params[1];
            pr_debug("msg type is power set for domain %d \n", domain_id);
            PRE_PROCESS(04);
            proto_dm = get_dev_pd(&vscmi->dev_res, 0x11, domain_id);
            if (!proto_dm) {
                RESP(04)->status = SCMI_RESP_STATUS_INV;
                break;
            }

            fd = get_dev_fd(&vscmi->dev_res, hdr->protocol_id, domain_id);
            if (fd < 0) {
                pr_debug("no such device, pleae check!\n");
                RESP(04)->status = SCMI_RESP_STATUS_INV;
                break;
            }
            power_stat = req->params[2];
            if ((power_stat & POWER_TYPEID_MASK) == 0) {
                if ((power_stat & (1UL << POWER_STATETYPE_SHFIT)) == 0) {
                    pwr_oper = SCMI_PWR_ON;
                } else {
                    pwr_oper = SCMI_PWR_OFF;
                }
            } else {
                RESP(04)->status = SCMI_RESP_STATUS_INV;
                break;
            }
            ret = power_operation_request(fd, &request, (char *)proto_dm->domain_name, pwr_oper);
            if (ret < 0)
                RESP(04)->status = SCMI_RESP_STATUS_NOT_FOUND;
            else {
                RESP(04)->status = SCMI_RESP_STATUS_OK;
                if (pwr_oper == SCMI_PWR_ON)  {
                    if (pa->rps_list[domain_id].data != POWER_STATE_ON) {
                        pa->rps_list[domain_id].data = POWER_STATE_ON;
                        // only record power on domain in the list
                        list_push(&pa->rps_head, &pa->rps_tail, &pa->rps_list[domain_id]);
                        pr_debug("[power] record poweron domain%d to list\n", domain_id);
                    } else {
                        pr_debug("[power] domain%d is already in poweron statue, adjust the node position \n", domain_id);
                        ret = list_remove(&pa->rps_head, &pa->rps_tail, &pa->rps_list[domain_id]);
                        pr_debug("[power] remove poweron domain%d from list, ret=%d\n", domain_id, ret);
                        list_push(&pa->rps_head, &pa->rps_tail, &pa->rps_list[domain_id]);
                        pr_debug("[power] record poweron domain%d to list\n", domain_id);
                    }
                } else {
                    // if the power is off, remove the domain from list
                    ret = list_remove(&pa->rps_head, &pa->rps_tail, &pa->rps_list[domain_id]);
                    pa->rps_list[domain_id].data = POWER_STATE_OFF;
                    pr_debug("[power] remove poweron domain%d from list, ret=%d\n", domain_id, ret);
                }
                //list_print(pa->rps_head, pa->rps_tail);
            }
            break;
        case 0x5:
            domain_id = req->params[0];
            pr_debug("msg type is power get for domain %d \n", domain_id);
            PRE_PROCESS(05);
            fd = get_dev_fd(&vscmi->dev_res, hdr->protocol_id, domain_id);
            if (fd < 0) {
                RESP(05)->status = SCMI_RESP_STATUS_INV;
                pr_debug("no such device, pleae check!\n");
                break;
            }
            // Currently no IOCTL for to get power state, so just return the record state.
            RESP(05)->status = SCMI_RESP_STATUS_OK;
            RESP(05)->power_state = pa->rps_list[domain_id].data;
            pr_debug("get power state = 0x%x \n", pa->rps_list[domain_id].data);
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

int parse_power_node(struct vhost_user_scmi *vscmi, char *args)
{
    struct power_attributes *pa = &vscmi->pw_attr;
    int i;

    pa->domain_nums = atoi(args);
    pr_debug("power domian num is %d\n", pa->domain_nums);

    if (pa->domain_nums > MAX_POWER_DOMAIN) {
        pr_err("[Error] power domain number should not larger than %d\n",
            MAX_POWER_DOMAIN);
        return -1;
    }
    memset(pa->rps_list, 0, sizeof(list_t) * MAX_POWER_DOMAIN);

    for (i = 0; i < pa->domain_nums; i++)
        pa->rps_list[i].data = POWER_STATE_OFF;

    pa->rps_head = NULL;
    pa->rps_tail = NULL;

    add_to_protocol_list(vscmi, 0x11);
    return 0;
}

static void scmi_power_reset(struct vhost_user_scmi *vscmi)
{
    // turn off the power for all domains.
    scmi_oper_ioctl_t request;
    uint16_t domain_id;
    int fd, ret;
    struct power_attributes *pa;
    struct protocol_domain *proto_dm;
    list_t *domain_poweron;

    if (!vscmi) {
        pr_err("[Error] NULL vscmi pointer in scmi_power_reset\n");
        return;
    }

    pa = &vscmi->pw_attr;
    pr_debug("start power reset..\n");

    //list_print(pa->rps_head, pa->rps_tail);
    while (pa->rps_head) {
        domain_poweron = list_pop(&pa->rps_head, &pa->rps_tail);
        if (!domain_poweron)
            break;
        domain_id = domain_poweron - &pa->rps_list[0];
        pr_debug("[reset] got domain_id = %d in the list \n", domain_id);

        if (domain_id >= MAX_POWER_DOMAIN)
            break;
        // get the device fd which use the domain
        fd = get_dev_fd(&vscmi->dev_res, 0x11, domain_id);
        proto_dm = get_dev_pd(&vscmi->dev_res, 0x11, domain_id);
        if ((fd > 0) && (proto_dm != NULL) && (domain_poweron->data == POWER_STATE_ON)) {
            ret = power_operation_request(fd, &request, (char *)proto_dm->domain_name, SCMI_PWR_OFF);
            if (ret < 0) {
                pr_err("[Error] [reset] failed to power off domain %d ret=%d !!\n", domain_id, ret);
            } else {
                domain_poweron->data = POWER_STATE_OFF;
                pr_debug("[reset]: power off domain %d\n", domain_id);
            }
        }
    }
}

struct scmi_protocol_ops power_ops = {
    .name = "power",
    .id = 0x11,
    .req_process = scmi_power_req_process,
    .reset = scmi_power_reset,
};

SCMI_PROTOCOL_EMUL_SET(power_ops);
