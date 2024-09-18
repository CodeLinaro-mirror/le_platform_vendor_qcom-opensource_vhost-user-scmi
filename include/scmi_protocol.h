/* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef SCMI_PROTOCOL
#define SCMI_PROTOCOL

#include <stdlib.h>
#include <stdint.h>
#include "resource.h"
#include "type.h"
#include "vhost_user.h"
#include "list.h"

#define SCMI_RESP_STATUS_OK 0
#define SCMI_RESP_STATUS_NOT_SUPPORT -1
#define SCMI_RESP_STATUS_INV -2
#define SCMI_RESP_STATUS_DENIED -3
#define SCMI_RESP_STATUS_NOT_FOUND -4
#define SCMI_RESP_STATUS_OUT_OF_RANGE -5
#define SCMI_RESP_STATUS_BUSY -6
#define SCMI_RESP_STATUS_COMM_ERR -7
#define SCMI_RESP_STATUS_GENERIC -8
#define SCMI_RESP_STATUS_HW_ERR -9
#define SCMI_RESP_STATUS_PROT_ERR -10
#define SCMI_RESP_STATUS_IN_USE -11


#define DOMAIN2CHANNEL(domain_id)  (domain_id)

#define SCMI_PROTOCOL_EMUL_SET(x)   DATA_SET(scmi_protolol_set, x)
#define MAX_DOMAIN_LENGTH  16
#define MAX_TRANSFER_LEVEL 10

struct virtio_scmi_request {
        le32 hdr;
        le32 params[];
};

struct virtio_scmi_response {
        le32 hdr;
        le32 ret_values[];
};

struct perf_domain {
    uint16_t domain_id;
    uint16_t level_nums;
#define MAX_PERF_LEVEL  16
    uint32_t level[MAX_PERF_LEVEL];
    uint32_t left_levels;
};

struct perf_attributes {
    uint16_t domain_nums;
#define MAX_PERF_DOMAIN 32
    struct perf_domain pds[MAX_PERF_DOMAIN];
};

struct power_attributes {
    uint16_t domain_nums;
#define MAX_POWER_DOMAIN 16
    // record power status
    list_t rps_list[MAX_POWER_DOMAIN];
    // point to the list head;
    list_t *rps_head;
    // point to the list tail;
    list_t *rps_tail;
};

struct reset_attributes {
    uint16_t domain_nums;
};

struct protocol_domain {
    uint16_t protocol_id;
    uint16_t domain_id;
    char domain_name[MAX_DOMAIN_LENGTH];
};
// domian id + protocol id -> dev_fd
struct device_map {
    int      dev_fd;
    uint16_t    pd_nums;
#define MAX_PROTOCOL_NUM 16
    struct protocol_domain prot_doms[MAX_PROTOCOL_NUM];
};

struct device_resource {
    uint16_t device_nums;
#define MAX_DEVICE_NUM  16
    struct device_map dev_map[MAX_DEVICE_NUM];
};

struct vhost_user_scmi {
    uint16_t vmid;
    struct vhost_user_dev dev;
    struct device_resource dev_res;
    struct perf_attributes pf_attr;
    struct power_attributes pw_attr;
    struct reset_attributes rs_attr;
    uint16_t protos[MAX_PROTOCOL_NUM];
    uint16_t support_proto_nums;
    char name[16];
    char sock_path[512];
    uint64_t features;
    uint64_t protocol_features;
};

struct scmi_msg_info {
#define MSG_ID_MASK     0xffu
#define SCMI_GET_MSG_ID(hdr)    ((hdr & MSG_ID_MASK) >> 0) //0-7
	uint8_t msg_id;
#define MSG_PROTOCOL_ID_MASK    0x3FC00u
#define SCMI_GET_PROT_ID(hdr)    (((hdr) & MSG_PROTOCOL_ID_MASK) >> 10) // 10-17
	uint8_t protocol_id;
};

#ifndef container_of
#define container_of(ptr, type, member) ({                      \
        const typeof(((type *) 0)->member) *__mptr = (ptr);     \
        (type *) ((char *) __mptr - offsetof(type, member));})
#endif


struct scmi_protocol_ops {
    char name[16];
    int id;
    int (*req_process)(struct vhost_user_scmi *, struct scmi_msg_info *, struct virtio_scmi_request *,
        uint32_t, struct virtio_scmi_response *, uint32_t *);
    void (*reset)(struct vhost_user_scmi *);
};


void parse_power_node(struct vhost_user_scmi *vscmi, char *args);
void parse_perf_node(struct vhost_user_scmi *vscmi, char *args);
void parse_reset_node(struct vhost_user_scmi *vscmi, char *args);
#endif

