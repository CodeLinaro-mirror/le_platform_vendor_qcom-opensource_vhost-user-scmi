/* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdlib.h>
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

#define SCMI_VIRTIO_FEATURES    (1UL << VIRTIO_F_VERSION_1)

static int is_daemon = 1;
SET_DECLARE(scmi_protolol_set, struct scmi_protocol_ops);

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
            "-d/--device  <devicenmae,protocol/domainid/domainname,protocol/domainid/domainname,...>\n");
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
        {"log",     required_argument, 0,  'l' },
        {"help",    required_argument, 0,  'h' },
        {0,         0,                 0,  0 }
    };

    while (((opt = getopt_long(argc, argv, "s:p:f:r:d:l:h",
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
                ret = parse_device_node(vscmi, optarg);
                break;
            case 'l':
                ret = parse_log_node(optarg);
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

    if (!vscmi->sock_path) {
        pr_err("[Error] please provide socket file name\n");
        goto err;
    }

    if (sanity_check(vscmi) < 0)
        goto err;

    vscmi->features = SCMI_VIRTIO_FEATURES;
    register_to_vmm_service();

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
