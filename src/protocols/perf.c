/* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <uapi/misc/qcom_uscmi.h>
#include <uapi/misc/qcom_vm_cpufreq.h>
#include <sys/ioctl.h>

#include "scmi_protocol.h"
#include "access_control.h"
#include "type.h"
#include "log.h"
#include "perf.h"

#define PERF_PROTOCOL_VERSION   0x40000

/*
 * Bit definitions for PERF_DOMAIN_ATTRIBUTES response (cmd 0x3)
 */
#define PERF_ATTR_CAN_SET_LEVEL     (1u << 30)
#define PERF_ATTR_LEVEL_INDEX_MODE  (1u << 25)

#define RESP(msg_id) response_##msg_id

#define PRE_PROCESS(msg_id)  \
       if (resp_struct_len < sizeof(struct perf_resp_##msg_id)) { \
            rsp->ret_values[0] = SCMI_RESP_STATUS_INV; \
            goto buffer_not_enough; \
       }   \
       *rsp_len = sizeof(struct perf_resp_##msg_id); \
       struct perf_resp_##msg_id *RESP(msg_id) = (struct perf_resp_##msg_id *)rsp->ret_values;

int perf_operation_request(int fd, scmi_oper_ioctl_t *req, char *name, int level, scmi_prf_oper_t op)
{
    memset(req, 0, sizeof(*req));
    req->proto = SCMI_PROTO_PERFORMANCE;
    req->oper = op;
    req->level = level;
    safe_strlcpy(req->name, name, MAX_DOMAIN_LENGTH);

    pr_debug("set perf level: name=%s op=[%d] level=[%d]\n", req->name, op, level);
    return ioctl(fd, SCMI_IOCTL_PRF, req);
}

static int get_sustained_perf_level(struct vhost_user_scmi *vscmi, uint32_t domain_id)
{
    if (domain_id >= vscmi->pf_attr.domain_nums) {
        pr_err("[Error] not support such domain id\n");
        return -1;
    }

    /* TODO: get from the real platform; currently just return 0 */
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
    int sus_level;
    int i, fd, ret;
    int dev_type;
    int trans_levels;
    uint32_t resp_struct_len = *rsp_len - 4; /* subtract response header length */

    if (resp_struct_len < 4)
        return -1;

    switch (hdr->msg_id) {
        case 0x0:
            pr_debug("msg id is protocol version\n");
            PRE_PROCESS(00);
            RESP(00)->status  = SCMI_RESP_STATUS_OK;
            RESP(00)->version = PERF_PROTOCOL_VERSION;
            break;

        case 0x1:
            pr_debug("msg type is protocol attribute\n");
            PRE_PROCESS(01);
            RESP(01)->status = SCMI_RESP_STATUS_OK;
            /*
             * Bits[17:16] Power unit:
             *   2 = uW, 1 = mW, 0 = abstract linear scale
             * Bits[15:0] Number of performance domains
             */
            RESP(01)->attributes              = vscmi->pf_attr.domain_nums << 0;
            RESP(01)->statistics_address_low  = 0;
            RESP(01)->statistics_address_high = 0;
            RESP(01)->statistics_len          = 0;
            break;

        case 0x2:
            pr_debug("msg type is protocol msg attribute\n");
            PRE_PROCESS(02);
            switch (req->params[0]) {
                case 0x0:
                case 0x1:
                case 0x2:
                case 0x3:
                case 0x4:
                case 0x7:
                case 0x8:
                    RESP(02)->status = SCMI_RESP_STATUS_OK;
                    break;
                case 0x5:
                case 0x6:
                case 0x9:
                case 0xA:
                case 0xB:
                case 0xC:
                    RESP(02)->status = SCMI_RESP_STATUS_NOT_FOUND;
                    break;
                default:
                    RESP(02)->status = SCMI_RESP_STATUS_INV;
            }
            RESP(02)->attributes = 0;
            break;

        case 0x3:
            domainid = req->params[0];
            pr_debug("msg type is domain attribute, domainid = %d\n", domainid);

            PRE_PROCESS(03);
            proto_dm = get_dev_pd(&vscmi->dev_res, 0x13, domainid);
            if (!proto_dm) {
                RESP(03)->status = SCMI_RESP_STATUS_INV;
                break;
            }

            dev_type = get_dev_type(&vscmi->dev_res, 0x13, domainid);
            if (dev_type < 0) {
                RESP(03)->status = SCMI_RESP_STATUS_INV;
                break;
            }

            fd = get_dev_fd(&vscmi->dev_res, 0x13, domainid);
            if (fd < 0) {
                /* cpufreq device node not opened yet by the background
                 * wait thread; report as if the domain didn't exist. */
                RESP(03)->status = SCMI_RESP_STATUS_NOT_FOUND;
                break;
            }

            pd = &vscmi->pf_attr.pds[domainid];
            if (dev_type == DEV_CPUFREQ && !pd->levels_fetched) {
                if (update_dynamic_perf_domain(vscmi, dev_type, domainid, fd) < 0) {
                    RESP(03)->status = SCMI_RESP_STATUS_NOT_FOUND;
                    break;
                }
            }

            /*
             * Bit[30]: Can set performance level = 1
             * Bit[25]: Level Indexing Mode — set only for DEV_CPUFREQ.
             *          When set, PERF_LEVEL_SET / PERF_LEVEL_GET and
             *          PERF_DESCRIBE_LEVELS use the level index instead
             *          of the raw performance level value.
             */
            if (dev_type == DEV_CPUFREQ)
                RESP(03)->attributes = PERF_ATTR_CAN_SET_LEVEL | PERF_ATTR_LEVEL_INDEX_MODE;
            else
                RESP(03)->attributes = PERF_ATTR_CAN_SET_LEVEL;

            /*
             * rate_limit: minimum time between successive requests.
             * 0 = not supported by the platform.
             */
            RESP(03)->rate_limit = 0;
            /*
             * sustained_freq: base frequency for the sustained performance
             * level, in kHz.  For DEV_CPUFREQ, pd->level[] holds frequencies
             * in kHz, so return the latest (highest) entry.  For other device
             * types (e.g. DEV_USCMI), pd->level[] holds abstract performance
             * values, not frequencies, so report 0.
             */
            if (dev_type == DEV_CPUFREQ) {
                RESP(03)->sustained_freq = (pd->level_nums > 0) ?
                                           pd->level[pd->level_nums - 1] : 0;
            } else {
                RESP(03)->sustained_freq = 0;
            }

            sus_level = get_sustained_perf_level(vscmi, domainid);
            if (sus_level < 0) {
                RESP(03)->status = SCMI_RESP_STATUS_INV;
                break;
            }
            RESP(03)->sustained_perf_level = (uint32_t)sus_level;

            add_domainid_to_name(proto_dm->domain_name, domainid, name);
            safe_strlcpy(RESP(03)->name, name, MAX_DOMAIN_LENGTH);
            RESP(03)->status = SCMI_RESP_STATUS_OK;
            break;

        case 0x4:
            /* offsetof gives the fixed header size (status + num_levels) */
            const int hdr_size = offsetof(struct perf_resp_04_v4, perf_levels_v4);
            int max_levels_in_buf;
            /* Declare with the name RESP(04_v4) expands to so the macro still works */
            struct perf_resp_04_v4 *response_04_v4;

            domainid    = req->params[0];
            level_start = req->params[1];
            pr_debug("msg type is get domain level description, domainid=%d level_start=%d\n",
                     domainid, level_start);

            if ((int)resp_struct_len < hdr_size) {
                rsp->ret_values[0] = SCMI_RESP_STATUS_INV;
                goto buffer_not_enough;
            }
            response_04_v4 = (struct perf_resp_04_v4 *)rsp->ret_values;

            dev_type = get_dev_type(&vscmi->dev_res, 0x13, domainid);
            if (dev_type < 0) {
                RESP(04_v4)->status = SCMI_RESP_STATUS_INV;
                *rsp_len = hdr_size;
                break;
            }

            fd = get_dev_fd(&vscmi->dev_res, 0x13, domainid);
            if (fd < 0) {
                RESP(04_v4)->status = SCMI_RESP_STATUS_NOT_FOUND;
                *rsp_len = hdr_size;
                break;
            }

            pd = &vscmi->pf_attr.pds[domainid];
            if (dev_type == DEV_CPUFREQ && !pd->levels_fetched) {
                if (update_dynamic_perf_domain(vscmi, dev_type, domainid, fd) < 0) {
                    RESP(04_v4)->status = SCMI_RESP_STATUS_NOT_FOUND;
                    *rsp_len = hdr_size;
                    break;
                }
            }

            if (level_start == 0)
                pd->left_levels = pd->level_nums;

            /*
             * Calculate how many v4 level entries fit in the response buffer,
             * capped at MAX_TRANSFER_LEVEL (the perf_levels_v4[] array bound).
             */
            max_levels_in_buf = ((int)resp_struct_len - hdr_size) /
                                 (int)sizeof(struct perf_levels_v4);
            if (max_levels_in_buf > MAX_TRANSFER_LEVEL)
                max_levels_in_buf = MAX_TRANSFER_LEVEL;

            trans_levels = (int)pd->left_levels > max_levels_in_buf ?
                           max_levels_in_buf : (int)pd->left_levels;
            pd->left_levels -= trans_levels;

            if ((level_start + trans_levels) > pd->level_nums) {
                RESP(04_v4)->status = SCMI_RESP_STATUS_INV;
                pr_err("[Error] please make sure the level_start is continuous!!\n");
                *rsp_len = hdr_size;
                break;
            }

            /*
             * num_levels field layout:
             *   Bits[31:16] Number of remaining performance levels.
             *   Bits[15:12] Reserved, must be zero.
             *   Bits[11:0]  Number of performance levels returned by this call.
             */
            RESP(04_v4)->num_levels = (pd->left_levels << 16) | (trans_levels << 0);

            for (i = level_start; i < (level_start + trans_levels) && i < pd->level_nums; i++) {
                /*
                 * Total transition latency = device latency (from IOCTL) +
                 * system latency (GVM SCMI FE → PVM SCMI BE overhead,
                 * user-configurable via -L/--latency, default 0).
                 * Clamp to UINT16_MAX to fit the 16-bit wire field.
                 */
                uint32_t total_latency_us = pd->transition_latency_us +
                                            vscmi->scmi_latency_us;
                if (total_latency_us > UINT16_MAX)
                    total_latency_us = UINT16_MAX;

                RESP(04_v4)->perf_levels_v4[i - level_start].perf_val              = pd->level[i];
                /* Power cost: 0 means not reported by the platform */
                RESP(04_v4)->perf_levels_v4[i - level_start].power_cost            = 0;
                RESP(04_v4)->perf_levels_v4[i - level_start].transition_latency_us =
                    (uint16_t)total_latency_us;
                RESP(04_v4)->perf_levels_v4[i - level_start].reserved              = 0;
                RESP(04_v4)->perf_levels_v4[i - level_start].indicative_freq       = pd->level[i];
                RESP(04_v4)->perf_levels_v4[i - level_start].level_index           = pd->level_index[i];
                pr_debug("domain_id=%d, level=%d, level_index=%d, "
                         "dev_latency_us=%u scmi_latency_us=%u total_latency_us=%u\n",
                         domainid, pd->level[i], pd->level_index[i],
                         pd->transition_latency_us, vscmi->scmi_latency_us,
                         total_latency_us);
            }
            RESP(04_v4)->status = SCMI_RESP_STATUS_OK;
            /* Set *rsp_len to the exact bytes used (same role as PRE_PROCESS in other cases) */
            *rsp_len = hdr_size + trans_levels * (int)sizeof(struct perf_levels_v4);
            break;

        case 0x7:
            /* PERF_LEVEL_SET */
            domainid = req->params[0];
            level    = req->params[1];
            pr_debug("msg type is set level, set domain %d to level %d\n", domainid, level);

            PRE_PROCESS(07);
            proto_dm = get_dev_pd(&vscmi->dev_res, 0x13, domainid);
            if (!proto_dm) {
                RESP(07)->status = SCMI_RESP_STATUS_INV;
                break;
            }

            dev_type = get_dev_type(&vscmi->dev_res, 0x13, domainid);
            if (dev_type < 0) {
                RESP(07)->status = SCMI_RESP_STATUS_INV;
                break;
            }

            fd = get_dev_fd(&vscmi->dev_res, 0x13, domainid);
            if (fd < 0) {
                RESP(07)->status = SCMI_RESP_STATUS_INV;
                break;
            }

            switch (dev_type) {
                case DEV_USCMI:
                    ret = perf_operation_request(fd, &request,
                              (char *)proto_dm->domain_name, level,
                              SCMI_PRF_LVL_SET);
                    break;
                case DEV_CPUFREQ:
                    /*
                     * Level Indexing Mode is active for DEV_CPUFREQ.
                     * The 'level' parameter received from the agent is
                     * already a level index (as advertised via bit 25).
                     * Pass it directly to the ioctl.
                     */
                    struct cpu_perf_level_req cpu_req;
                    cpu_req.level = level;
                    pr_debug("set cpu_perf level index: [%d]\n", level);
                    ret = ioctl(fd, CPU_PERF_LEVEL_SET, &cpu_req);
                    break;
                default:
                    ret = -1;
                    break;
            }

            if (ret < 0) {
                pr_err("failed to set level for domain %d, error %d\n", domainid, ret);
                RESP(07)->status = SCMI_RESP_STATUS_NOT_FOUND;
            } else {
                RESP(07)->status = SCMI_RESP_STATUS_OK;
            }
            break;

        case 0x8:
            /* PERF_LEVEL_GET */
            domainid = req->params[0];
            pr_debug("msg type is get level, domain %d\n", domainid);

            PRE_PROCESS(08);

            if (domainid >= vscmi->pf_attr.domain_nums) {
                RESP(08)->status = SCMI_RESP_STATUS_INV;
                break;
            }

            proto_dm = get_dev_pd(&vscmi->dev_res, 0x13, domainid);
            if (!proto_dm) {
                RESP(08)->status = SCMI_RESP_STATUS_INV;
                break;
            }

            dev_type = get_dev_type(&vscmi->dev_res, 0x13, domainid);
            if (dev_type < 0) {
                RESP(08)->status = SCMI_RESP_STATUS_INV;
                break;
            }

            fd = get_dev_fd(&vscmi->dev_res, 0x13, domainid);
            if (fd < 0) {
                RESP(08)->status = SCMI_RESP_STATUS_INV;
                break;
            }

            switch (dev_type) {
                case DEV_USCMI:
                    RESP(08)->status     = SCMI_RESP_STATUS_OK;
                    RESP(08)->perf_level = 0;
                    break;
                case DEV_CPUFREQ:
                    /*
                     * Level Indexing Mode is active for DEV_CPUFREQ.
                     * The ioctl returns the current level index; return
                     * it directly as the perf_level field.
                     */
                    struct cpu_perf_level_req cpu_req;
                    memset(&cpu_req, 0, sizeof(cpu_req));
                    ret = ioctl(fd, CPU_PERF_LEVEL_GET, &cpu_req);
                    if (ret < 0) {
                        RESP(08)->status = SCMI_RESP_STATUS_NOT_FOUND;
                        pr_err("CPU_PERF_LEVEL_GET ioctl failed: ret=%d, fd=%d, errno=%d (%s)\n",
                               ret, fd, errno, strerror(errno));
                    } else {
                        RESP(08)->status     = SCMI_RESP_STATUS_OK;
                        RESP(08)->perf_level = cpu_req.level;
                        pr_debug("cpufreq get perf level index: %d\n", cpu_req.level);
                    }
                    break;
                default:
                    RESP(08)->status = SCMI_RESP_STATUS_INV;
                    break;
            }
            break;

        default:
            rsp->ret_values[0] = SCMI_RESP_STATUS_INV;
            *rsp_len = 4;
            break;
    }

    rsp->hdr = req->hdr;
    return 0;

buffer_not_enough:
    pr_err("%s: response buffer len %d is too small for message 0x%x\n",
           __func__, resp_struct_len, hdr->msg_id);
    *rsp_len = 4;
    rsp->hdr = req->hdr;
    return 0;
}

int parse_perf_node(struct vhost_user_scmi *vscmi, char *args)
{
    /* Format: perf,{domain_id}/{level_0:level_1:...:level_n},{domain_id}/... */
    struct perf_attributes *pa = &vscmi->pf_attr;
    struct perf_domain *pd;
    char *sr, *sn, *st, *stt, *sttt;
    int domain_id;

    sr = sn = strdup(args);
    while ((st = strsep(&sn, ","))) {
        stt = strsep(&st, "/");
        if (!st) break;

        if (pa->domain_nums >= MAX_PERF_DOMAIN) {
            pr_err("[Error] perf domain number should not be larger than %d\n",
                   MAX_PERF_DOMAIN);
            goto err;
        }

        domain_id = atoi(stt);
        if (domain_id >= MAX_PERF_DOMAIN) {
            pr_err("[Error] perf domain id should not be larger than %d\n", MAX_PERF_DOMAIN);
            goto err;
        }
        pd = &pa->pds[domain_id];
        if (pd->level_nums > 0) {
            pr_err("should not use the duplicated domain_id %d!\n", domain_id);
            goto err;
        }
        pd->domain_id = domain_id;
        pd->transition_latency_us = 0;

        while ((sttt = strsep(&st, ":"))) {
            if (pd->level_nums >= MAX_PERF_LEVEL) {
                pr_err("[Error] perf levels should not be larger than %d\n",
                       MAX_PERF_LEVEL);
                goto err;
            }
            pd->level[pd->level_nums++] = atoi(sttt);
            pr_debug("domain=%d level=%d\n", pd->domain_id, atoi(sttt));
        }
        pd->left_levels = pd->level_nums;
        pa->domain_nums++;
    }
    free(sr);
    add_to_protocol_list(vscmi, 0x13);
    return 0;

err:
    free(sr);
    return -1;
}

/* Update the perf domain id and perf levels obtained through IOCTL.
 * Called lazily on the first SCMI request that touches a DEV_CPUFREQ
 * domain (see scmi_perf_req_process()), not at device-open time, so that
 * device bring-up never blocks on this IOCTL. */
int update_dynamic_perf_domain(struct vhost_user_scmi *vscmi, dm_dev_type_t dev_type,
                                uint32_t domain_id, int fd)
{
    struct perf_domain *pd;
    struct perf_attributes *pa = &vscmi->pf_attr;
    struct cpu_perf_levels_available cpu_req;
    int i;

    if (domain_id >= MAX_PERF_DOMAIN) {
        pr_err("domain id %d is larger than the max %d\n", domain_id, MAX_PERF_DOMAIN);
        return -1;
    }
    pd = &pa->pds[domain_id];

    switch (dev_type) {
        case DEV_CPUFREQ: {
            struct cpu_perf_transition_latency lat_req;

            memset(&cpu_req, 0, sizeof(cpu_req));
            if (ioctl(fd, CPU_PERF_LEVELS_GET_AVAILABLE, &cpu_req) < 0) {
                pr_err("failed to get the perf levels\n");
                return -1;
            }
            for (i = 0; i < cpu_req.num_levels; i++) {
                pd->level[i]       = cpu_req.freq_khz[i];
                pd->level_index[i] = cpu_req.levels[i];
                pr_debug("get cpu_perf level[%d]: freq=%d index=%d\n",
                         i, cpu_req.freq_khz[i], cpu_req.levels[i]);
            }
            pd->level_nums  = cpu_req.num_levels;
            pd->left_levels = pd->level_nums;

            /*
             * Query the transition latency for this cpufreq device node.
             * The latency is the same for all levels on a given device,
             * so fetch it once here during bringup and cache it in pd.
             */
            memset(&lat_req, 0, sizeof(lat_req));
            if (ioctl(fd, CPU_PERF_TRANSITION_LATENCY_GET, &lat_req) < 0) {
                pr_err("failed to get transition latency for domain %d, defaulting to 0\n",
                       domain_id);
                pd->transition_latency_us = 0;
            } else {
                pd->transition_latency_us = lat_req.latency_us;
                pr_debug("domain %d transition_latency_us=%u\n",
                         domain_id, pd->transition_latency_us);
            }

            /* domain_id is already counted in pa->domain_nums via the -f
             * placeholder entry that reserved it; don't count it again. */
            pd->levels_fetched = true;
            break;
        }
        default:
            pd->transition_latency_us = 0; /* not a cpufreq device; no latency IOCTL */
            break;
    }
    return 0;
}

static void scmi_perf_reset(struct vhost_user_scmi *vscmi)
{
    (void)vscmi;
}

struct scmi_protocol_ops perf_ops = {
    .name        = "perf",
    .id          = 0x13,
    .req_process = scmi_perf_req_process,
    .reset       = scmi_perf_reset,
};

SCMI_PROTOCOL_EMUL_SET(perf_ops);