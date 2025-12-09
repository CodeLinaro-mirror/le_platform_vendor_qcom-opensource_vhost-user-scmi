/* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef ACCESS_CONTROL_H
#define ACCESS_CONTROL_H

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "scmi_protocol.h"

#define MAX_PROTOCOL 10
#define MAX_VM_NUM 10

void add_domainid_to_name(char *org, uint32_t domain_id, char *new);
size_t safe_strlcpy(char *dst, const char *src, size_t size);
bool access_is_ok_for_domain(struct device_resource *dev_res, int protocol, int domain_id);
bool access_is_ok_for_protocol(struct vhost_user_scmi *vscmi, int protocol);
void add_to_protocol_list(struct vhost_user_scmi *vscmi, int protocol);
int parse_device_node(struct vhost_user_scmi *vscmi, char *args);
int get_dev_fd(struct device_resource *dev_res, int protocol, int domian_id);
struct protocol_domain *get_dev_pd(struct device_resource *dev_res, int protocol, int domian_id);
void access_exit(struct vhost_user_scmi *vscmi);

#endif
