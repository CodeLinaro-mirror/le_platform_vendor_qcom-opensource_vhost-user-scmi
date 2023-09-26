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
            struct virtio_scmi_response *rsp, int rsp_len)
{
    // parse the header
    struct scmi_msg_info hdr;
    struct scmi_protocol_ops *ops;
    int ret = 0;

    hdr.protocol_id = SCMI_GET_PROT_ID(req->hdr);
    hdr.msg_id = SCMI_GET_MSG_ID(req->hdr);
    pr_debug("%s: req hdr =%x protocol_id = %x msg_id = %x\n",
                    __func__, req->hdr, hdr.protocol_id, hdr.msg_id);

    if (!access_is_ok_for_protocol(vscmi, hdr.protocol_id)) {
        return -1; // not promiss
    }
    ops = find_protocol(hdr.protocol_id);
    if (ops && ops->req_process) {
        ret = ops->req_process(vscmi, &hdr, req, req_len, rsp, rsp_len);
        pr_debug("%s: rsp hdr = %x rsp_len =%d ret=%d \n",
                    __func__, rsp->hdr, rsp_len, ret);
    } else {
        pr_err("The protocol %d is not supported \n", hdr.protocol_id);
        ret = -1;
    }
    return ret;
}

static bool scmi_virtio_process_req(struct vhost_user_scmi *vscmi, struct vhost_virtqueue *vq)
{

    struct virtio_scmi_request *req;
    struct virtio_scmi_response *rsp;
    int req_len, rsp_len;
    int idx;
    int ret;
    struct iovec iov[2];

#ifdef __TEST__
    pr_info("%s: Do not access the vq in test case \n", __func__);
    return true;
#endif
    smp_mb();
    while (vq_has_data(vq)) {
        if (vq_getchain(vq, iov, 2, &idx) != 2) {
            pr_err("message is not correct!\n");
            return false;
        }
        req = iov[0].iov_base;
        req_len = iov[0].iov_len - sizeof(req->hdr);
        rsp = iov[1].iov_base;
        rsp_len = iov[1].iov_len - sizeof(rsp->hdr);

        ret = scmi_msg_process_sync(vscmi, req, req_len, rsp, rsp_len);
        if (!ret) {
            vq_relchain(vq, idx, iov[1].iov_len);
        }

        smp_mb();
    }

    vq_endchains(vq);

    return true;
}

static void scmi_process_vq(void *data)
{
    struct vhost_virtqueue *vq = data;
    struct vhost_user_dev *dev = vq->vudev;
    struct vhost_user_scmi *vscmi;
    int ret;

    vscmi = container_of(dev, struct vhost_user_scmi, dev);
    assert(vscmi);
    assert(vq);

    pr_debug("start to process vq\n");
    while (1) {
        ret = scmi_virtio_process_req(vscmi, vq);
        if (ret) {
            break;
        }
    }
    pr_debug("finish process\n");
}

static int scmi_set_vring_state(struct vhost_user_dev *dev, uint32_t idx, uint32_t state)
{
    struct vhost_virtqueue *vq = dev->virtqueue[idx];
    int ret = 0;

    pr_debug("set vring state to %s \n", state ? "enable" : "disable");
    if (state) {
        ret = start_watch_on_fd(vq->kickfd, scmi_process_vq, vq);
    } else {
        stop_watch_on_fd(vq->kickfd);
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
            "-r/--reset  <reset domian nums>\n"
            "-l/--log    log=<file/stdio>,[path=<path/to/logfile>],level=<info/debug>\n"
            "-d/--device  <devicenmae,protocol/domainid,protocol/domainid,...>\n");
}

static int
parse_args(struct vhost_user_scmi *vscmi, int argc, char **argv)
{
    int opt;
    static struct option long_options[] = {
        {"sock",    required_argument, 0,  's' },
        {"perf",    required_argument, 0,  'f' },
        {"reset",   required_argument, 0,  'r' },
        {"device",  required_argument, 0,  'd' },
        {"log",     required_argument, 0,  'l' },
        {"help",    required_argument, 0,  'h' },
        {0,         0,                 0,  0 }
    };

    while ((opt = getopt_long(argc, argv, "s:p:f:r:d:l:h",
                        long_options, NULL)) != -1) {
        switch (opt) {
            case 's':
                if (snprintf(vscmi->sock_path, 256, "%s", optarg) > 256) {
                    printf("socket path is too long, please limit it to < 256 bytes!\n");
                    return -1;
                }
                break;
            case 'f':
                parse_perf_node(vscmi, optarg);
                break;
            case 'r':
                parse_reset_node(vscmi, optarg);
                break;
            case 'd':
                parse_device_node(vscmi, optarg);
                break;
            case 'l':
                parse_log_node(optarg);
                break;
            case 'h':
            default:
                usage();
                return -1;
        }
    }

    return 0;
}

int
register_to_vmm_service(void)
{
// only when qcrosvm is crash, there will be an event come!

//TODO call vmm lib function to register.
// add states for vhost user be, if was waiting, return success.
// if is recvmsg, close the fd, deinit device, return success.
// wait for status to change to done

}

int main(int argc, char **argv)
{
    // Create a new device
    struct vhost_user_scmi *vscmi;
    int opt;
    int ret = 0;

    vscmi = calloc(sizeof(struct vhost_user_scmi), 1);
    if (!vscmi) {
        printf("failed to alloc vscmi structure!\n");
        return -1;
    }
    if (parse_args(vscmi, argc, argv) < 0)
        goto err;

    if (!vscmi->sock_path) {
        printf("please provide socket file name\n");
        goto err;
    }

    vscmi->features = SCMI_VIRTIO_FEATURES;
    register_to_vmm_service();

loop:
    pr_debug("vhost user wait for connect..\n");
    if (vhost_user_wait_for_connect(&vscmi->dev, vscmi->sock_path) < 0) {
        pr_err("failed to make connection with client\n");
        ret = -1;
        goto err;
    }
    pr_debug("vhost user device init..\n");
    if (vhost_user_init_device(&vscmi->dev, &dev_ops) < 0) {
        ret = -1;
        goto err;
    }

    pr_debug("vhost user start loop..\n");
    ret = vhost_user_start_loop(&vscmi->dev);
    pr_debug("vhost user loop exit, ret = %d \n", ret);

    vhost_user_deinit_device(&vscmi->dev);

    if (is_daemon)
        goto loop;

    kill_worker();
    log_exit();
    access_exit(vscmi);
err:
    // or do other operations which is needed when the thread is out.
    if (vscmi) {
        free(vscmi);
        vscmi = NULL;
    }
    return ret;
}

