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

//define strlcpy to avoid the banned strncpy
size_t strlcpy(char *dst, const char *src, size_t size)
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

int parse_device_node(struct vhost_user_scmi *vscmi, char *args)
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
        return 0;
    snprintf(dev_path, 20, "%s", name);

    pr_debug("find device : %s\n", dev_path);

    if (dr->device_nums >= MAX_DEVICE_NUM) {
        pr_err("[Error] too many devices, max device number is %d!!\n", MAX_DEVICE_NUM);
        return -1;
    }
    fd = open(dev_path, O_RDWR | O_EXCL);
    if (fd <=0) {
        pr_err("[Error] failed to open %s\n", dev_path);
        return -1;
    }

    dm = &dr->dev_map[dr->device_nums++];
    dm->dev_fd = fd;
    while(st = strsep(&sn, ",")) {
        stt = strsep(&st, "/");
        if (!st | !stt) break;
        if (dm->pd_nums >= MAX_PROTOCOL_NUM) {
            pr_err("[Error] too many protocols for %s, max number is %d!!\n", dev_path, MAX_PROTOCOL_NUM);
            return -1;
        }
        pd = &dm->prot_doms[dm->pd_nums++];
        pd->protocol_id = atoi(stt);

        stt = strsep(&st, "/");
        if (!st | !stt) break;

        pd->domain_id = atoi(stt);

        if (strlen(st) > MAX_DOMAIN_LENGTH - 1) {
            pr_err("[Error] %s: IOCTL will fail as the name of domain is truncated, max name length is %d !!\n",
                __func__, MAX_DOMAIN_LENGTH);
        }
        snprintf(pd->domain_name, MAX_DOMAIN_LENGTH, "%s", st);

        pr_debug("%s : protocol_id = %d domian_id = %d domain_name = %s \n", name,
                        pd->protocol_id, pd->domain_id, pd->domain_name);

    }
    free(sr);
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
                return dm->dev_fd;
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
