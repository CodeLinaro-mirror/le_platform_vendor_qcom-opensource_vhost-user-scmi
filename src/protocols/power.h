/*
Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <stdio.h>
#include <type.h>

struct power_resp_00 {
    int32_t status;
    uint32_t version;
} __attribute__((packed));

struct power_resp_01 {
    int32_t status;
    /*
    Bits[31:16] Reserved, must be zero.
    Bits[15:0] Number of power domains.
    */
    uint32_t attributes;
    /*
    The lower 32 bits of the physical address where the statistics
    shared memory region is located. This value should be 64-bit aligned.
    The address must be in the memory map of the calling agent. This field
    is invalid and must be ignored if the statistics_len field is set to 0.
    */
    uint32_t statistics_address_low;
    /*
    The upper 32 bits of the physical address where the statistics
    shared memory region is located. The address must be in the memory
    map of the calling agent. This field is invalid and must be ignored
    if the statistics_len field is set to 0.
    */
    uint32_t statistics_address_high;
    /*
    The length in bytes of the statistics shared memory region. A value of
    0 in this field indicates that the platform doesn’t support the
    statistics shared memory region
    */
    uint32_t statistics_len;
} __attribute__((packed));

struct power_resp_02 {
    int32_t status;
    /*
    Flags that are associated with a specific command in the protocol.
    In the current version of the specification, this value is always 0.
    */
    uint32_t attributes;
} __attribute__((packed));

struct power_resp_03 {
    int32_t status;
    /*
    Bit[31] Power state change notifications support.
        Set to 1 if power state change notifications are supported on this domain.
        Set to 0 if power state change notifications are not supported on this domain.
    Bit[30] Power state asynchronous support.
        Set to 1 if power state can be set asynchronously.
        Set to 0 if power state cannot be set asynchronously.
    Bit[29] Power state synchronous support.
        Set to 1 if power state can be set synchronously.
        Set to 0 if power state cannot be set synchronously.
    Bit[28] Power state change requested notifications support.
        Set to 1 if power state change requested notifications are supported on this domain.
        Set to 0 if power state change requested notifications are not supported on this domain.
    Bit[27] Extended power domain name.
        If set to 1, the power domain name is greater than 16 bytes. The extended power domain name is provided by POWER_DOMAIN_NAME_GET command which is described in Section 4.3.2.9.
        If set to 0, extended power domain name is not supported.
    Bits[26:0] Reserved, must be zero.
    */
    uint32_t attributes;
    /*
    Null-terminated ASCII string of up to 16 bytes in length describing the power domain name. When Bit[27] of attributes field is set to 1, this field returns the NULL terminated lower 15 bytes of the power domain name.
    */
    uint8_t name[16];
} __attribute__((packed));

struct power_resp_04 {
    int32_t status;
} __attribute__((packed));

struct power_resp_05 {
    int32_t status;
    /*
    Platform-specific parameter identifying the power state of this domain.
    31           Reserved. Must be zero.
    30           StateType
        If set to 0, indicates that context is preserved.
        If set to 1, indicates that context is lost.
    29:28        Reserved. Must be zero.
    27:0         StateID
        A value of zero when StateType is set to 0 represents the ON state.
        A value of zero when StateType is set to 1 represents the OFF state.
        All other values are IMPLEMENTATION_DEFINED.

    */
    uint32_t power_state;
} __attribute__((packed));

struct power_resp_08 {
    int32_t status;
    /*
    Bits[31:0] Reserved, must be zero.
    */
    uint32_t flags;
    /*
    Null-terminated ASCII string of up to 64 bytes in length describing
    the power domain extended name.
    */
    uint8_t ext_name[64];
} __attribute__((packed));
