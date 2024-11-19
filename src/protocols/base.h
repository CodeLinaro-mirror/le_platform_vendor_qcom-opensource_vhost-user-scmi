/*
Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <stdio.h>
#include <type.h>

struct base_resp_00 {
    int32_t status;
    uint32_t version;
} __attribute__((packed));

struct base_resp_01 {
    int32_t status;

    /*
    Bits[31:16] Reserved, must be zero.
    Bits[15:8] Number of agents in the system.
    Bits[7:0] Number of protocols that are implemented, excluding the Base protocol.
    */
    uint32_t attributes;
} __attribute__((packed));

struct base_resp_02 {
    int32_t status;
    /*
    Flags that are associated with a specific command in the protocol.
    For all commands in this protocol, this parameter has a value of 0.
    */
    uint32_t attributes;
} __attribute__((packed));

struct base_resp_03 {
    int32_t status;
    /*
    Null terminated ASCII string of up to 16 bytes with a vendor name.
    */
    uint8_t vendor_identifier[16];
} __attribute__((packed));

struct base_resp_04 {
    int32_t status;
    /*
    Null terminated ASCII string of up to 16 bytes with a vendor name.
    */
    uint8_t vendor_identifier [16];
} __attribute__((packed));

struct base_resp_05 {
    int32_t status;
    /*
    Format is vendor specific.
    */
    uint32_t implementation_version;
} __attribute__((packed));

#define MAX_RESP_PROTOCL_NUMS 16
struct base_resp_06 {
    int32_t status;

    /*
    Number of protocols that are returned by this call.
    */
    uint32_t num_protocols;
    /*
    Array of protocol identifiers that are implemented, excluding the
    Base protocol, with four protocol identifiers packed into each array
    element. The PROTOCOL_ATTRIBUTES command can be used to determine
    the number of protocols implemented
    */
    uint32_t protocols[MAX_RESP_PROTOCL_NUMS];
}  __attribute__((packed));
