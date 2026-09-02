/* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <vu_atomic.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "scmi_protocol.h"
#include "worker.h"
#include "access_control.h"
#include "log.h"

#define SCMI_VIRTIO_FEATURES    ((1UL << VIRTIO_F_VERSION_1)|(1UL << VHOST_USER_F_PROTOCOL_FEATURES))
#define VIRTIO_SCMI_PROTOCOL_FEATURES    (1UL << VHOST_USER_PROTOCOL_F_REPLY_ACK)

static int is_daemon = 1;
SET_DECLARE(scmi_protolol_set, struct scmi_protocol_ops);

/* -----------------------------------------------------------------------
 * Test command support (-t / --test)
 *
 * Format:  <protocol_id>/<msg_id>[/param0:param1:param2...]
 *
 * protocol_id and msg_id may be decimal or hex (0x prefix).
 * params are colon-separated decimal or hex values.
 *
 * Examples:
 *   -t 0x13/0x4/4:0        perf: get level-list for domain 4, starting at index 0
 *   -t 0x13/0x7/4:1000000  perf: set level of domain 4 to 1000000
 *   -t 0x13/0x8/4          perf: get current level of domain 4
 *   -t 0x11/0x4/0:0:0      power: set domain 0 state
 *   -t 0x16/0x4/0:1:0      reset: reset domain 0
 * ----------------------------------------------------------------------- */

#define MAX_TEST_PARAMS     16
#define MAX_TEST_CMDS       16
#define TEST_RSP_BUF_SIZE   512

struct test_cmd {
    uint8_t  protocol_id;
    uint8_t  msg_id;
    uint32_t params[MAX_TEST_PARAMS];
    int      num_params;
};

static struct test_cmd g_test_cmds[MAX_TEST_CMDS];
static int             g_num_test_cmds = 0;

/* -----------------------------------------------------------------------
 * Forward declaration (defined later in this file as static)
 * ----------------------------------------------------------------------- */
static int scmi_msg_process_sync(struct vhost_user_scmi *vscmi,
                                 struct virtio_scmi_request *req, int req_len,
                                 struct virtio_scmi_response *rsp, uint32_t *rsp_len);

/* -----------------------------------------------------------------------
 * parse_test_cmd – parse one -t argument and append to g_test_cmds[]
 * ----------------------------------------------------------------------- */
static int parse_test_cmd(const char *arg)
{
    char *buf, *p, *tok;
    struct test_cmd *cmd;
    int ret = 0;

    if (g_num_test_cmds >= MAX_TEST_CMDS) {
        printf("[Error] too many -t commands, max is %d\n", MAX_TEST_CMDS);
        return -1;
    }

    cmd = &g_test_cmds[g_num_test_cmds];
    memset(cmd, 0, sizeof(*cmd));

    buf = strdup(arg);
    if (!buf)
        return -1;

    p = buf;

    /* --- protocol_id --- */
    tok = strsep(&p, "/");
    if (!tok || !p) {
        printf("[Error] -t: invalid format, expected <protocol>/<msg>[/p0:p1:...]\n");
        ret = -1;
        goto out;
    }
    cmd->protocol_id = (uint8_t)strtoul(tok, NULL, 0);

    /* --- msg_id --- */
    tok = strsep(&p, "/");
    if (!tok) {
        printf("[Error] -t: missing msg_id\n");
        ret = -1;
        goto out;
    }
    cmd->msg_id = (uint8_t)strtoul(tok, NULL, 0);

    /* --- optional params (colon-separated) --- */
    if (p) {
        char *param_tok;
        while ((param_tok = strsep(&p, ":")) != NULL) {
            if (cmd->num_params >= MAX_TEST_PARAMS) {
                printf("[Error] -t: too many params, max is %d\n", MAX_TEST_PARAMS);
                ret = -1;
                goto out;
            }
            cmd->params[cmd->num_params++] = (uint32_t)strtoul(param_tok, NULL, 0);
        }
    }

    g_num_test_cmds++;
    printf("[Test] queued cmd[%d]: protocol=0x%02x msg=0x%02x num_params=%d\n",
           g_num_test_cmds - 1, cmd->protocol_id, cmd->msg_id, cmd->num_params);

out:
    free(buf);
    return ret;
}

/* -----------------------------------------------------------------------
 * scmi_status_str – human-readable SCMI status code
 * ----------------------------------------------------------------------- */
static const char *scmi_status_str(int32_t status)
{
    switch (status) {
    case  0: return "SUCCESS";
    case -1: return "NOT_SUPPORTED";
    case -2: return "INVALID_PARAMETERS";
    case -3: return "DENIED";
    case -4: return "NOT_FOUND";
    case -5: return "OUT_OF_RANGE";
    case -6: return "BUSY";
    case -7: return "COMMS_ERROR";
    case -8: return "GENERIC_ERROR";
    case -9: return "HARDWARE_ERROR";
    case -10: return "PROTOCOL_ERROR";
    case -11: return "IN_USE";
    default:  return "UNKNOWN";
    }
}

/* -----------------------------------------------------------------------
 * run_test_cmds – execute all queued test commands and print responses
 * ----------------------------------------------------------------------- */
static void run_test_cmds(struct vhost_user_scmi *vscmi)
{
    int i, j;
    /*
     * Request buffer: 4-byte header + up to MAX_TEST_PARAMS × 4-byte params.
     * Response buffer: fixed TEST_RSP_BUF_SIZE bytes.
     */
    uint8_t req_buf[sizeof(uint32_t) + MAX_TEST_PARAMS * sizeof(uint32_t)];
    uint8_t rsp_buf[TEST_RSP_BUF_SIZE];
    struct virtio_scmi_request  *req = (struct virtio_scmi_request  *)req_buf;
    struct virtio_scmi_response *rsp = (struct virtio_scmi_response *)rsp_buf;
    uint32_t rsp_len;
    int      req_len;

    printf("\n========== Running %d test command(s) ==========\n", g_num_test_cmds);

    for (i = 0; i < g_num_test_cmds; i++) {
        struct test_cmd *cmd = &g_test_cmds[i];

        memset(req_buf, 0, sizeof(req_buf));
        memset(rsp_buf, 0, sizeof(rsp_buf));

        /*
         * Build SCMI transport header (virtio-scmi / ARM SCMI spec):
         *   bits[ 7: 0]  msg_id
         *   bits[ 9: 8]  msg_type  (0 = command)
         *   bits[17:10]  protocol_id
         *   bits[27:18]  token     (use cmd index as token)
         *   bits[31:28]  reserved
         */
        req->hdr = ((uint32_t)(cmd->msg_id      & 0xFF) <<  0)
                 | ((uint32_t)(0                & 0x03) <<  8)   /* msg_type = command */
                 | ((uint32_t)(cmd->protocol_id & 0xFF) << 10)
                 | ((uint32_t)(i                & 0x3FF)<< 18);  /* token = cmd index  */

        /* Fill request params */
        for (j = 0; j < cmd->num_params; j++)
            req->params[j] = cmd->params[j];

        req_len = cmd->num_params * (int)sizeof(uint32_t);

        /*
         * rsp_len on entry = total response buffer size (including the 4-byte
         * response header).  The protocol handlers subtract 4 to get the space
         * available for the payload, then overwrite *rsp_len with the actual
         * payload size on return.
         */
        rsp_len = TEST_RSP_BUF_SIZE;

        /* Print what we are sending */
        printf("\n[Test %d] >>> protocol=0x%02x  msg=0x%02x", i,
               cmd->protocol_id, cmd->msg_id);
        for (j = 0; j < cmd->num_params; j++)
            printf("  param[%d]=0x%x(%u)", j, cmd->params[j], cmd->params[j]);
        printf("\n");

        int ret = scmi_msg_process_sync(vscmi, req, req_len, rsp, &rsp_len);
        if (ret < 0) {
            printf("[Test %d] <<< FAILED (scmi_msg_process_sync returned %d)\n", i, ret);
            continue;
        }

        /*
         * rsp_len now holds the payload size (bytes, excluding the 4-byte hdr).
         * ret_values[0] is always the SCMI status code (int32_t).
         */
        int num_ret_words = (int)(rsp_len / sizeof(uint32_t));

        printf("[Test %d] <<< rsp_hdr=0x%08x  payload_len=%u bytes\n",
               i, rsp->hdr, rsp_len);

        if (num_ret_words > 0) {
            int32_t status = (int32_t)rsp->ret_values[0];
            printf("         %-12s= %d (%s)\n", "status", status,
                   scmi_status_str(status));
        }
        for (j = 1; j < num_ret_words; j++) {
            /* Skip trailing zero words to keep output compact */
            if (rsp->ret_values[j] == 0) {
                /* Check if all remaining words are also zero */
                int all_zero = 1;
                int k;
                for (k = j; k < num_ret_words; k++) {
                    if (rsp->ret_values[k] != 0) { all_zero = 0; break; }
                }
                if (all_zero) {
                    printf("         ret[%2d..%2d] = 0x00000000 (0) [all zero]\n",
                           j, num_ret_words - 1);
                    break;
                }
            }
            printf("         ret[%2d]     = 0x%08x (%u)\n",
                   j, rsp->ret_values[j], rsp->ret_values[j]);
        }
    }

    printf("\n========== Test complete ==========\n\n");
}

/* -----------------------------------------------------------------------
 * Normal vhost-user / SCMI device code
 * ----------------------------------------------------------------------- */

static void scmi_set_features(struct vhost_user_dev *dev, uint64_t features)
{
    struct vhost_user_scmi *vscmi = container_of(dev, struct vhost_user_scmi, dev);
    vscmi->features = features;
    pr_debug("%s: set features %lx\n", __func__, features);
}

static uint64_t scmi_get_features(struct vhost_user_dev *dev)
{
    struct vhost_user_scmi *vscmi = container_of(dev, struct vhost_user_scmi, dev);

    pr_debug("%s: get features %lx\n", __func__, vscmi->features);
    return vscmi->features;
}

static void scmi_set_protocol_features(struct vhost_user_dev *dev, uint64_t features)
{
    struct vhost_user_scmi *vscmi = container_of(dev, struct vhost_user_scmi, dev);
    vscmi->protocol_features = features;
    pr_debug("%s: set protocol features %lld\n", __func__, vscmi->protocol_features);
}

static uint64_t scmi_get_protocol_features(struct vhost_user_dev *dev)
{
    struct vhost_user_scmi *vscmi = container_of(dev, struct vhost_user_scmi, dev);
    pr_debug("%s: get protocol features %lld\n", __func__, vscmi->protocol_features);
    return vscmi->protocol_features;
}

struct scmi_protocol_ops *find_protocol(uint16_t protocol_id)
{
    struct scmi_protocol_ops **opspp, *opsp;

    SET_FOREACH(opspp, scmi_protolol_set) {
        opsp = *opspp;
        if (protocol_id == opsp->id)
            return opsp;
    }

    return NULL;
}

static int scmi_msg_process_sync(struct vhost_user_scmi *vscmi, struct virtio_scmi_request *req, int req_len,
            struct virtio_scmi_response *rsp, uint32_t *rsp_len)
{
    // parse the header
    struct scmi_msg_info hdr;
    struct scmi_protocol_ops *ops;
    int ret = 0;

    if (!vscmi || !req || !rsp || !rsp_len) {
        pr_err("[Error] NULL pointer in scmi_msg_process_sync\n");
        return -1;
    }

    if (req_len < 0) {
        pr_err("[Error] Invalid request length %d in scmi_msg_process_sync\n", req_len);
        return -1;
    }

    hdr.protocol_id = SCMI_GET_PROT_ID(req->hdr);
    hdr.msg_id = SCMI_GET_MSG_ID(req->hdr);
    pr_debug("%s: req hdr =%x protocol_id = %x msg_id = %x\n",
                    __func__, req->hdr, hdr.protocol_id, hdr.msg_id);

    if (!access_is_ok_for_protocol(vscmi, hdr.protocol_id)) {
        return -1; // not promiss
    }
    ops = find_protocol(hdr.protocol_id);
    if (ops && ops->req_process) {
        if (ops->req_process(vscmi, &hdr, req, req_len, rsp, rsp_len) < 0)
            pr_err("[Error] %s: ERROR: rsp hdr = %x rsp_len =%d ret=%d \n",
                    __func__, rsp->hdr, *rsp_len, ret);
        else
            pr_debug("%s: rsp hdr = %x rsp_len =%d ret=%d \n",
                    __func__, rsp->hdr, *rsp_len, ret);
    } else {
        pr_err("[Error] The protocol %d is not supported \n", hdr.protocol_id);
        ret = -1;
    }
    return ret;
}

static bool scmi_virtio_process_req(struct vhost_user_scmi *vscmi, struct vhost_virtqueue *vq)
{
    struct virtio_scmi_request *req;
    struct virtio_scmi_response *rsp;
    unsigned int req_len, rsp_len;
    int idx;
    int ret;
    struct iovec iov[2];

    if (!vscmi || !vq) {
        pr_err("[Error] NULL pointer in scmi_virtio_process_req\n");
        return false;
    }

#ifdef __TEST__
    pr_info("%s: Do not access the vq in test case \n", __func__);
    return true;
#endif
    smp_mb();
    while (vq_has_data(vq)) {
        if (vq_getchain(vq, iov, 2, &idx) != 2) {
            pr_err("[Error] message is not correct!\n");
            return false;
        }
        if ((iov[0].iov_len < sizeof(req->hdr)) || (iov[1].iov_len < sizeof(rsp->hdr))) {
            pr_err("[Error] message length is not correct!\n");
            return false;
        }

        req = iov[0].iov_base;
        if (!req) {
            pr_err("[Error] NULL request pointer\n");
            return false;
        }

        req_len = iov[0].iov_len - sizeof(req->hdr);
        rsp = iov[1].iov_base;
        if (!rsp) {
            pr_err("[Error] NULL response pointer\n");
            return false;
        }

        rsp_len = iov[1].iov_len;

        ret = scmi_msg_process_sync(vscmi, req, req_len, rsp, &rsp_len);
        if (!ret) {
            if (rsp_len > (iov[1].iov_len - sizeof(rsp->hdr))) {
                pr_err("[Error] response size %d is larger than %d! \n",
                    rsp_len, iov[1].iov_len - sizeof(rsp->hdr));
            } else {
                vq_relchain(vq, idx, rsp_len + sizeof(rsp->hdr));
            }
        }

        smp_mb();
    }

    vq_endchains(vq);

    return true;
}

static void scmi_process_vq(void *data)
{
    struct vhost_virtqueue *vq;
    struct vhost_user_dev *dev;
    struct vhost_user_scmi *vscmi;
    int ret;

    if (!data) {
        pr_err("[Error] NULL data pointer in scmi_process_vq\n");
        return;
    }

    vq = (struct vhost_virtqueue *)data;
    if (!vq->vudev) {
        pr_err("[Error] NULL vudev pointer in scmi_process_vq\n");
        return;
    }

    dev = vq->vudev;
    vscmi = container_of(dev, struct vhost_user_scmi, dev);
    if (!vscmi) {
        pr_err("[Error] NULL vscmi pointer in scmi_process_vq\n");
        return;
    }

    /* Acquire the mutex and mark the virtqueue as in use */
    pthread_mutex_lock(&vscmi->vq_mutex);

    /* Check if the virtqueue is marked for deletion */
    if (vscmi->vq_marked_for_deletion) {
        pthread_mutex_unlock(&vscmi->vq_mutex);
        pr_debug("virtqueue is marked for deletion, skipping processing\n");
        return;
    }

    /* Mark the virtqueue as in use */
    vscmi->vq_in_use++;
    pthread_mutex_unlock(&vscmi->vq_mutex);

    pr_debug("start to process vq\n");
    while (1) {
        ret = scmi_virtio_process_req(vscmi, vq);
        if (ret) {
            break;
        }
    }
    pr_debug("finish process\n");

    /* Release the virtqueue */
    pthread_mutex_lock(&vscmi->vq_mutex);
    vscmi->vq_in_use--;

    /* Signal that we're done with the virtqueue */
    if (vscmi->vq_in_use == 0 && vscmi->vq_marked_for_deletion) {
        pthread_cond_signal(&vscmi->vq_cond);
    }
    pthread_mutex_unlock(&vscmi->vq_mutex);
}

static void scmi_device_reset(struct vhost_user_scmi *vscmi)
{
    struct scmi_protocol_ops **opspp, *opsp;

    if (!vscmi)
        return;

    SET_FOREACH(opspp, scmi_protolol_set) {
        opsp = *opspp;
        if (opsp && opsp->reset)
            opsp->reset(vscmi);
    }
}

static int scmi_set_vring_state(struct vhost_user_dev *dev, uint32_t idx, uint32_t state)
{
    struct vhost_virtqueue *vq;
    struct vhost_user_scmi *vscmi;
    int ret = 0;

    if (!dev) {
        pr_err("[Error] NULL dev pointer in scmi_set_vring_state\n");
        return -1;
    }

    if (idx >= VHOST_MAX_VRING) {
        pr_err("[Error] Invalid virtqueue index %d in scmi_set_vring_state\n", idx);
        return -1;
    }

    vq = dev->virtqueue[idx];
    if (!vq) {
        pr_err("[Error] NULL virtqueue pointer for index %d in scmi_set_vring_state\n", idx);
        return -1;
    }

    vscmi = container_of(dev, struct vhost_user_scmi, dev);
    if (!vscmi) {
        pr_err("[Error] NULL vscmi pointer in scmi_set_vring_state\n");
        return -1;
    }

    pr_debug("set vring state to %s \n", state ? "enable" : "disable");
    if (state) {
        ret = start_watch_on_fd(vq->kickfd, scmi_process_vq, vq);
    } else {
        stop_watch_on_fd(vq->kickfd);
        scmi_device_reset(vscmi);
    }

    return ret;
}

// this is device specific, so provide these handler in each device.
struct vhost_dev_ops dev_ops = {
    .set_features = scmi_set_features,
    .get_features = scmi_get_features,
    .set_protocol_features = scmi_set_protocol_features,
    .get_protocol_features = scmi_get_protocol_features,
    .set_vring_state = scmi_set_vring_state,
};

static void
usage(void)
{
    printf("Usage:\n"
            "vhost-user-scmi\n"
            "-h/--help\n"
            "-s/--sock   <socket_path>\n"
            "-f/--perf   <domainid/level0:level1:..leveln,domainid1/level0:..leveln>\n"
            "-p/--power  <power domian nums>\n"
            "-r/--reset  <reset domian nums>\n"
            "-l/--log    log=<file/stdio>,[path=<path/to/logfile>],level=<info/debug>\n"
            "-d/--device  <devicename,protocol/domainid/domainname,protocol/domainid/domainname,...>\n"
            "-c/--cpufreq  <cpufreq,protocol/domainid/domainname,protocol/domainid/domainname,...>\n"
            "-L/--latency <us>  system-level latency overhead in microseconds for the\n"
            "                   GVM SCMI FE to PVM SCMI BE path; added to the device\n"
            "                   transition latency reported in PERF_DESCRIBE_LEVELS\n"
            "                   (default: 0)\n"
            "-t/--test   <protocol/msg[/param0:param1:...]>  inject a fake SCMI command\n"
            "            (may be repeated; exits after running all test commands)\n"
            "  Examples:\n"
            "    -t 0x13/0x4/4:0        perf: describe levels of domain 4 from index 0\n"
            "    -t 0x13/0x7/4:1000000  perf: set level of domain 4 to 1000000\n"
            "    -t 0x13/0x8/4          perf: get current level of domain 4\n"
            "    -t 0x11/0x4/0:0:0      power: set domain 0 state\n"
            "    -t 0x16/0x4/0:1:0      reset: assert reset on domain 0\n");
}

static int
parse_args(struct vhost_user_scmi *vscmi, int argc, char **argv)
{
    int opt;
    int ret = 0;

    static struct option long_options[] = {
        {"sock",    required_argument, 0,  's' },
        {"power",   required_argument, 0,  'p' },
        {"perf",    required_argument, 0,  'f' },
        {"reset",   required_argument, 0,  'r' },
        {"device",  required_argument, 0,  'd' },
        {"cpufreq", required_argument, 0,  'c' },
        {"log",     required_argument, 0,  'l' },
        {"latency", required_argument, 0,  'L' },
        {"test",    required_argument, 0,  't' },
        {"help",    required_argument, 0,  'h' },
        {0,         0,                 0,  0 }
    };

    while (((opt = getopt_long(argc, argv, "s:p:f:r:d:c:l:L:t:h",
                        long_options, NULL)) != -1) && (!ret)) {
        switch (opt) {
            case 's':
                if (snprintf(vscmi->sock_path, 256, "%s", optarg) > 256) {
                    printf("socket path is too long, please limit it to < 256 bytes!\n");
                    return -1;
                }
                break;
            case 'p':
                ret = parse_power_node(vscmi, optarg);
                break;
            case 'f':
                ret = parse_perf_node(vscmi, optarg);
                break;
            case 'r':
                ret = parse_reset_node(vscmi, optarg);
                break;
            case 'd':
                ret = parse_device_node(vscmi, optarg, DEV_USCMI);
                break;
            case 'c':
                ret = parse_device_node(vscmi, optarg, DEV_CPUFREQ);
                break;
            case 'l':
                ret = parse_log_node(optarg);
                break;
            case 'L':
                vscmi->scmi_latency_us = (uint32_t)strtoul(optarg, NULL, 0);
                printf("scmi system latency set to %u us\n", vscmi->scmi_latency_us);
                break;
            case 't':
                ret = parse_test_cmd(optarg);
                break;
            case 'h':
            default:
                usage();
                return -1;
        }
    }

    return ret;
}

int
register_to_vmm_service(void)
{
// only when qcrosvm is crash, there will be an event come!

//TODO call vmm lib function to register.
// add states for vhost user be, if was waiting, return success.
// if is recvmsg, close the fd, deinit device, return success.
// wait for status to change to done
    return 0;
}

bool check_and_add(uint32_t *domain_list, uint32_t *num, uint32_t domain_id)
{
    int i;

    for (i = 0; i < *num; i++) {
        if (domain_list[i] == domain_id) {
            return false;
        }
    }
    domain_list[*num] = domain_id;
    *num = *num + 1; 
    return true;
}

static int sanity_check(struct vhost_user_scmi *vscmi)
{
    struct device_resource *dev_res = &vscmi->dev_res;
    struct perf_attributes *perf = &vscmi->pf_attr;      
    struct power_attributes *power = &vscmi->pw_attr;      
    struct reset_attributes *reset = &vscmi->rs_attr;      

    uint32_t perf_domains[MAX_PERF_DOMAIN] = {0};
    uint32_t power_domains[MAX_POWER_DOMAIN] = {0};
    uint32_t reset_domains[MAX_RESET_DOMAIN] = {0};

    uint32_t perf_n = 0;
    uint32_t power_n = 0;
    uint32_t reset_n = 0;

    int i, j;
    struct device_map *dm;
    struct protocol_domain *pd;

    for (i = 0; i < dev_res->device_nums; i++) {
        dm = &dev_res->dev_map[i];
        for (j = 0; j < dm->pd_nums; j++) {
            pd = &dm->prot_doms[j];
            switch (pd->protocol_id) {
                case 0x11:
                    // power
                    if ((pd->domain_id >= power->domain_nums) ||
                        (!check_and_add(power_domains, &power_n, pd->domain_id)))
                        goto err;
                    break;
                case 0x13:
                    // perf
                    if ((pd->domain_id >= perf->domain_nums) ||
                        (!check_and_add(perf_domains, &perf_n, pd->domain_id)))
                        goto err;
                    break;
                case 0x16:
                    //reset
                    if ((pd->domain_id >= reset->domain_nums) ||
                        (!check_and_add(reset_domains, &reset_n, pd->domain_id)))
                        goto err;
                    break;
                default:
                    break;
            }
        }
    }
    return 0;

err:
    pr_err("[Error] use duplicated or non exist domain id %d for protocol %d !\n", pd->domain_id, pd->protocol_id);
    return -1;
}

int main(int argc, char **argv)
{
    // Create a new device
    struct vhost_user_scmi *vscmi;
    int opt;
    int ret = 0;

    vscmi = calloc(sizeof(struct vhost_user_scmi), 1);
    if (!vscmi) {
        pr_err("[Error] failed to alloc vscmi structure!\n");
        return -1;
    }

    /* Initialize synchronization variables */
    pthread_mutex_init(&vscmi->vq_mutex, NULL);
    pthread_cond_init(&vscmi->vq_cond, NULL);
    vscmi->vq_in_use = 0;
    vscmi->vq_marked_for_deletion = 0;
    if (parse_args(vscmi, argc, argv) < 0)
        goto err;

    /*
     * Test mode: if one or more -t commands were given, execute them directly
     * against the protocol handlers (no vhost-user socket / VM needed) and
     * exit.  The caller must still supply the relevant protocol configuration
     * flags (-f / -p / -r) so that the handlers have valid domain data.
     */
    if (g_num_test_cmds > 0) {
        run_test_cmds(vscmi);
        goto err;
    }

    if (!vscmi->sock_path) {
        pr_err("[Error] please provide socket file name\n");
        goto err;
    }

    if (sanity_check(vscmi) < 0)
        goto err;

    vscmi->features = SCMI_VIRTIO_FEATURES;
    vscmi->protocol_features = VIRTIO_SCMI_PROTOCOL_FEATURES;
    register_to_vmm_service();

    if (start_cpufreq_wait_thread() < 0)
        goto err;

loop:
    pr_debug("vhost user wait for connect ..\n");
    if (vhost_user_wait_for_connect(&vscmi->dev, vscmi->sock_path) < 0) {
        pr_err("[Error] failed to make connection with client\n");
        ret = -1;
        goto err;
    }
    pr_debug("vhost user device init..\n");
    if (vhost_user_init_device(&vscmi->dev, &dev_ops) < 0) {
        ret = -1;
        goto err;
    }

    pr_debug("vhost user start loop..\n");
    vhost_user_start_loop(&vscmi->dev);
    pr_debug("vhost user loop exit\n");

    /* Mark virtqueue for deletion and wait for any ongoing processing to complete */
    pthread_mutex_lock(&vscmi->vq_mutex);
    vscmi->vq_marked_for_deletion = 1;

    /* Wait for any ongoing virtqueue processing to complete */
    while (vscmi->vq_in_use > 0) {
        pr_debug("waiting for virtqueue processing to complete, in_use=%d\n", vscmi->vq_in_use);
        pthread_cond_wait(&vscmi->vq_cond, &vscmi->vq_mutex);
    }
    pthread_mutex_unlock(&vscmi->vq_mutex);

    vhost_user_deinit_device(&vscmi->dev);

    /* Reset the deletion flag for next connection */
    pthread_mutex_lock(&vscmi->vq_mutex);
    vscmi->vq_marked_for_deletion = 0;
    pthread_mutex_unlock(&vscmi->vq_mutex);

    if (is_daemon)
        goto loop;

    kill_worker();

err:
    log_exit();
    access_exit(vscmi);
    // or do other operations which is needed when the thread is out.
    if (vscmi) {
        /* Clean up synchronization resources */
        pthread_mutex_destroy(&vscmi->vq_mutex);
        pthread_cond_destroy(&vscmi->vq_cond);
        free(vscmi);
        vscmi = NULL;
    }
    return ret;
}