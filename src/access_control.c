/* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "access_control.h"
#include "log.h"

void parse_device_node(struct vhost_user_scmi *vscmi, char *args)
{
    struct device_resource *dr = &vscmi->dev_res;
    struct device_map *dm;
    struct protocol_domain *pd;
    char *name;
    int fd;
    char dev_path[20]={'\0'};

    char *sr, *sn, *st, *stt;

    sr = sn = strdup(args);
    name = strsep(&sn, ",");
    if (!name || !sn)
        return;
    snprintf(dev_path, 20, "%s", name);

    pr_debug("find device : %s\n", dev_path);
    fd = open(dev_path, O_RDWR);
    if (fd <=0) {
        pr_err("failed to open %s\n", dev_path);
        return;
    }

    dm = &dr->dev_map[dr->device_nums++];
    dm->dev_fd = fd;
    while(st = strsep(&sn, ",")) {
        stt = strsep(&st, "/");
        if (!st | !stt) break;
        pd = &dm->prot_doms[dm->pd_nums++];
        pd->protocol_id = atoi(stt);
        pd->domain_id = atoi(st);
        pr_debug("%s : protocol_id = %d domian_id = %d \n", name, pd->protocol_id, pd->domain_id);

    }
    free(sr);
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
                return dm->dev_fd;
            }
        }
    }
    return -1;

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

    pr_debug("could not find access for protocol %d\n", protocol);
    return false;
}

void add_to_protocol_list(struct vhost_user_scmi *vscmi, int protocol)
{

    if (access_is_ok_for_protocol(vscmi, protocol))
            return;

    if (vscmi->support_proto_nums < MAX_PROTOCOL_NUM)
        vscmi->protos[vscmi->support_proto_nums] = protocol;
    else
        pr_err("too much protocol!!!\n");

    vscmi->support_proto_nums++;
}

void access_exit(struct vhost_user_scmi *vscmi)
{
    struct device_resource *dr = &vscmi->dev_res;
    struct device_map *dm;
    int i;

    for (i = 0; i < dr->device_nums; i++) {
        dm = &dr->dev_map[i];
        if (dm->dev_fd) {
            close(dm->dev_fd);
            dm->dev_fd = 0;
        }
    }
    pr_debug("close all the device fd\n");
}
