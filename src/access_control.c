/* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "access_control.h"
#include "log.h"

/* Polling parameters when waiting for a cpufreq device node to appear.*/
#define CPUFREQ_NODE_WAIT_TIMEOUT_S   2
#define CPUFREQ_NODE_WAIT_INTERVAL_MS 10

void add_domainid_to_name(char *org, uint32_t domain_id, char *new)
{
    int i;
    char *new_p = new;
    if (domain_id > 999) {
        pr_err("[Error] domain id is too large, domain name will be truncated !!\n");
    }
    // leave 3 bytes for domain_id
    // leave 1 byte for \0
    for (i = 0; i < MAX_DOMAIN_LENGTH - 4; i++) {
        if (*org != '\0')
            *new_p++ = *org++;
        else
            break;
    }
    snprintf(new_p, 4, "%d", domain_id);

    pr_debug("domain_name return to agent is %s \n", new);
}

//define safe_strlcpy to avoid the banned strncpy
size_t safe_strlcpy(char *dst, const char *src, size_t size)
{
    int copyed = 0;
    int i;

    if (!dst || !src || (size < 2))
        return copyed;

    for (i = 0; i < size - 1; i++) {
        if (*src != '\0') {
            *dst++ = *src++;
            copyed++;
        } else {
            break;
        }
    }
    *dst = '\0';

    return copyed;
}

/* A -c/--cpufreq device whose node may not exist yet: its device_map entry
 * is created right away (dev_fd = -1), and cpufreq_wait_thread() opens the
 * node in the background and fills dev_fd in once it appears. */
struct pending_cpufreq_dev {
    char dev_path[128];
    struct device_map *dm;
};

#define MAX_PENDING_CPUFREQ_DEV MAX_DEVICE_NUM
static struct pending_cpufreq_dev pending_cpufreq[MAX_PENDING_CPUFREQ_DEV];
static int pending_cpufreq_nums;

int parse_device_node(struct vhost_user_scmi *vscmi, char *args, dm_dev_type_t dev_type)
{
    struct device_resource *dr = &vscmi->dev_res;
    struct device_map *dm;
    struct protocol_domain *pd;
    char *name;
    int fd = -1;
    char dev_path[128]={'\0'};

    char *sr, *sn, *st, *stt;

    sr = sn = strdup(args);
    name = strsep(&sn, ",");
    if (!name || !sn) {
        free(sr);
        return 0;
    }
    snprintf(dev_path, 128, "%s", name);

    pr_debug("find device : %s\n", dev_path);

    if (dr->device_nums >= MAX_DEVICE_NUM) {
        pr_err("[Error] too many devices, max device number is %d!!\n", MAX_DEVICE_NUM);
        free(sr);
        return -1;
    }

    /* The kernel module that creates a cpufreq device node may not have
     * been inserted yet.  Register the protocol/domain entries now with
     * dev_fd = -1 and let cpufreq_wait_thread() open the node and fill
     * dev_fd in once it appears, instead of blocking here.
     */
    if (dev_type != DEV_CPUFREQ) {
        fd = open(dev_path, O_RDWR);
        if (fd <=0) {
            pr_err("[Error] failed to open %s, skip this device!! \n", dev_path);
            free(sr);
            return 0;
        }
        pr_debug("open file %s fd = %d\n", dev_path, fd);
    }

    dm = &dr->dev_map[dr->device_nums++];
    dm->dev_fd = fd;
    dm->dev_type = dev_type;
    while(st = strsep(&sn, ",")) {
        stt = strsep(&st, "/");
        if (!st | !stt) break;
        if (dm->pd_nums >= MAX_PROTOCOL_NUM) {
            pr_err("[Error] too many protocols for %s, max number is %d!!\n", dev_path, MAX_PROTOCOL_NUM);
            free(sr);
            return -1;
        }
        pd = &dm->prot_doms[dm->pd_nums++];
        pd->protocol_id = atoi(stt);

        stt = strsep(&st, "/");
        if (!st | !stt) break;

        pd->domain_id = atoi(stt);

        // only cpufreq device node support dynamic level get.
        if (dev_type == DEV_CPUFREQ) {
            if (pd->protocol_id != 0x13) {
                pr_err("[Error] only support perf protocal fro CPUFREQ device\n");
                free(sr);
                return -1;
            }
            if (pd->domain_id >= vscmi->pf_attr.domain_nums) {
                pr_err("[Error] cpufreq domain %d was not declared via -f!!\n", pd->domain_id);
                free(sr);
                return -1;
            }
        }

        if (strlen(st) > MAX_DOMAIN_LENGTH - 1) {
            pr_err("[Error] %s: IOCTL will fail as the name of domain is truncated, max name length is %d !!\n",
                __func__, MAX_DOMAIN_LENGTH);
        }
        snprintf(pd->domain_name, MAX_DOMAIN_LENGTH, "%s", st);

        pr_debug("%s : protocol_id = %d domian_id = %d domain_name = %s \n", name,
                        pd->protocol_id, pd->domain_id, pd->domain_name);

    }

    if (dev_type == DEV_CPUFREQ) {
        struct pending_cpufreq_dev *p = &pending_cpufreq[pending_cpufreq_nums++];
        snprintf(p->dev_path, sizeof(p->dev_path), "%s", dev_path);
        p->dm = dm;
    }

    free(sr);
    return 0;
}

/*
 * cpufreq_wait_thread - background retry to open cpufreq device nodes that
 * were not present at parse_args() time.  Polls all pending nodes
 * round-robin so a node that never appears doesn't block one that appears
 * sooner.  Only opens the node and stores the fd into the already-registered
 * device_map entry; perf levels are fetched lazily on first use (see
 * update_dynamic_perf_domain() callers in perf.c).  The thread is detached
 * and simply exits once every pending node is opened or times out.
 */
static void *cpufreq_wait_thread(void *arg)
{
    bool done[MAX_PENDING_CPUFREQ_DEV] = {false};
    const int timeout_ms = CPUFREQ_NODE_WAIT_TIMEOUT_S * 1000;
    const int interval_ms = CPUFREQ_NODE_WAIT_INTERVAL_MS;
    int elapsed_ms = 0;
    int remaining = pending_cpufreq_nums;
    int i, fd;

    while (remaining > 0 && elapsed_ms < timeout_ms) {
        for (i = 0; i < pending_cpufreq_nums; i++) {
            struct pending_cpufreq_dev *p = &pending_cpufreq[i];

            if (done[i])
                continue;

            fd = open(p->dev_path, O_RDWR);
            if (fd <= 0)
                continue;

            pr_info("cpufreq device node appeared: %s (after %d ms)\n", p->dev_path, elapsed_ms);
            __atomic_store_n(&p->dm->dev_fd, fd, __ATOMIC_RELEASE);
            done[i] = true;
            remaining--;
        }
        if (remaining > 0) {
            usleep((useconds_t)interval_ms * 1000);
            elapsed_ms += interval_ms;
        }
    }

    for (i = 0; i < pending_cpufreq_nums; i++) {
        if (!done[i])
            pr_err("[Error] timeout (%d s) waiting for cpufreq device node: %s\n",
                   CPUFREQ_NODE_WAIT_TIMEOUT_S, pending_cpufreq[i].dev_path);
    }
    return NULL;
}

int start_cpufreq_wait_thread(void)
{
    pthread_t tid;

    if (pending_cpufreq_nums == 0)
        return 0;

    if (pthread_create(&tid, NULL, cpufreq_wait_thread, NULL) != 0) {
        pr_err("[Error] failed to create cpufreq wait thread\n");
        return -1;
    }
    pthread_detach(tid);
    pr_info("started background thread to wait for %d pending cpufreq device(s)\n",
            pending_cpufreq_nums);
    return 0;
}

int get_dev_fd(struct device_resource *dev_res, int protocol, int domain_id)
{
    int i,j;
    struct device_map *dm;
    struct protocol_domain *pd;

    if (!dev_res)
        return -1;

    for (i = 0;i < dev_res->device_nums; i++) {
        dm = &dev_res->dev_map[i];
        for (j = 0; j < dm->pd_nums; j++) {
            pd = &dm->prot_doms[j];
            if ((protocol == pd->protocol_id) && (domain_id == pd->domain_id)) {
                return __atomic_load_n(&dm->dev_fd, __ATOMIC_ACQUIRE);
            }
        }
    }
    return -1;

}

int get_dev_type(struct device_resource *dev_res, int protocol, int domain_id)
{
    int i,j;
    struct device_map *dm;
    struct protocol_domain *pd;

    if (!dev_res)
        return -1;

    for (i = 0;i < dev_res->device_nums; i++) {
        dm = &dev_res->dev_map[i];
        for (j = 0; j < dm->pd_nums; j++) {
            pd = &dm->prot_doms[j];
            if ((protocol == pd->protocol_id) && (domain_id == pd->domain_id)) {
                return dm->dev_type;
            }
        }
    }
    return -1;
}

struct protocol_domain *get_dev_pd(struct device_resource *dev_res, int protocol, int domain_id)
{
    int i,j;
    struct device_map *dm;
    struct protocol_domain *pd;

    if (!dev_res)
        return NULL;

    for (i = 0;i < dev_res->device_nums; i++) {
        dm = &dev_res->dev_map[i];
        for (j = 0; j < dm->pd_nums; j++) {
            pd = &dm->prot_doms[j];
            if ((protocol == pd->protocol_id) && (domain_id == pd->domain_id)) {
                return pd;
            }
        }
    }
    return NULL;
}

bool access_is_ok_for_protocol(struct vhost_user_scmi *vscmi, int protocol)
{
    int i;
    if (protocol == 0x10)
        return true;

    for (i = 0; i < vscmi->support_proto_nums; i++) {
        if (vscmi->protos[i] == protocol)
            return true;
    }

    return false;
}

void add_to_protocol_list(struct vhost_user_scmi *vscmi, int protocol)
{

    if (access_is_ok_for_protocol(vscmi, protocol))
            return;

    if (vscmi->support_proto_nums < MAX_PROTOCOL_NUM)
        vscmi->protos[vscmi->support_proto_nums] = protocol;
    else
        pr_err("[Error] too much protocol!!!\n");

    vscmi->support_proto_nums++;
}

void access_exit(struct vhost_user_scmi *vscmi)
{
    struct device_resource *dr;
    struct device_map *dm;
    int i;

    if (!vscmi) {
        pr_err("[Error] NULL vscmi pointer in access_exit\n");
        return;
    }

    dr = &vscmi->dev_res;
    for (i = 0; i < dr->device_nums; i++) {
        dm = &dr->dev_map[i];
        if (dm->dev_fd > 0) {
            close(dm->dev_fd);
            dm->dev_fd = 0;
        }
    }
    pr_debug("close all the device fd\n");
}