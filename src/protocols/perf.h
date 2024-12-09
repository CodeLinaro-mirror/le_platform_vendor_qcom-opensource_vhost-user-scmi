/*
Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <stdio.h>
#include <type.h>


struct perf_resp_00 {
    int32_t status;
    uint32_t version;
} __attribute__((packed));

struct perf_resp_01 {
    int32_t status;
    /*
    Bits[31:18] Reserved, must be zero.
    Bits[17:16] Power Unit:
    Set to 2 if the power consumption of performance levels is expressed in uW.
    Set to 1 if the power consumption of performance levels is expressed in mW.
    Set to 0 if the power consumption of performance levels is expressed in an
        abstract linear scale.
    All other values are reserved and must not be used.
    Bits[15:0] Number of performance domains.
    */
    uint32_t attributes;
    /*
    The lower 32 bits of the physical address where the statistics shared memory
    region is located. This value should be 64-bit aligned. The address must be
    in the memory map of the calling agent. If the statistics_len field is 0,
    then this field is invalid and must be ignored.
    */
    uint32_t statistics_address_low;
    /*
    The upper 32 bit of the physical address where the shared memory region is
    located. The address must be in the memory map of the calling agent. If the
    statistics_len field is 0, then this field is invalid and must be ignored.
    */
    uint32_t statistics_address_high;
    /*
    The length in bytes of the shared memory region. A value of 0 in this field
    indicates that the platform doesn’t support the statistics shared memory region.
    */
    uint32_t statistics_len;
} __attribute__((packed));

struct perf_resp_02 {
    int32_t status;
    /*
    Flags associated with a specific command in the protocol.
        Bits[31:1] Reserved, must be zero.
        Bit[0] FastChannel Support.
        Set to 1 if there is at least one dedicated FastChannel available for this message.
        Set to 0 if there are no FastChannels available this message.
    */
    uint32_t attributes;
} __attribute__((packed));

struct perf_resp_03 {
    int32_t status;
    /*
    Bit[31] Can set limits.
    Set to 1 if calling agent is allowed to set the performance limits on the domain.
    Set to 0 if a calling agent is not allowed to set limits on the performance limits on the domain.
    Bit[30] Can set performance level.
    Set to 1 if calling agent is allowed to set the performance of a domain.
    Set to 0 if a calling agent is not allowed to set the performance of a domain.
    Only one agent can set the performance of a given domain.
    Bit[29] Performance limits change notifications support.
    Set to 1 if performance limits change notifications are supported for this domain.
    Set to 0 if performance limits change notifications are not supported for this domain.
    Bit[28] Performance level change notifications support.
    Set to 1 if performance level change notifications are supported for this domain.
    Set to 0 if performance level change notifications are not supported for this domain.
    Bit[27] FastChannel Support.
    Set to 1 if there is at least one FastChannel available for this domain.
    Set to 0 if there are no FastChannels available for this domain.
    Bit[26] Extended performance domain name.
    If set to 1, the performance domain name is greater than 16 bytes.
    Bits[25:0] Reserved and set to zero.
    */
    uint32_t attributes;
    /*
    Bits[31:20] Reserved and set to zero.
    Bits[19:0] Rate Limit in microseconds, indicating the minimum time required between
        successive requests. A value of 0 indicates that this field is not supported by
        the platform. This field does not apply to FastChannels.
    */
    uint32_t rate_limit;
    /*
    Base frequency corresponding to the sustained performance level. Expressed in units of kHz.
    */
    uint32_t sustained_freq;
    /*
    The performance level value that corresponds to the sustained performance delivered by
    the platform.
    */
    uint32_t sustained_perf_level;
    /*
    Null terminated ASCII string of up to 16 bytes in length describing a domain name.
    When Bit[26] of attributes field is set to 1, this field contains the lower 15 bytes of
    the NULL terminated performance domain name.
    */
    uint8_t name[16];
} __attribute__((packed));

struct perf_levels {
    uint32_t value;
    uint32_t power_cost;
    uint32_t attributes;
} __attribute__((packed));


struct perf_resp_04 {
    int32_t status;
    uint32_t num_levels;
    /*
    Array of performance levels, in numeric ascending order, to be described. N is specified
    by Bits[11:0] of num_levels field. Each array entry is composed of three 32-bit words with
    the following format:
        uint32 entry[0] Performance level value.
        uint32 entry[1] Power cost.
            A value of zero indicates that the power cost is not reported by the platform.
        uint32 entry[2] Attributes
            Bits[31:16] Reserved, must be zero.
            Bits[15:0] Worst-case transition latency in microseconds to move from any supported
                performance to the level indicated by this entry in the array.
    */
    struct perf_levels perf_levels[MAX_TRANSFER_LEVEL];
} __attribute__((packed));

struct perf_resp_07 {
    int32_t status;
} __attribute__((packed));

struct perf_resp_08 {
    int32_t status;
    /*
    Current performance level of the domain.
    */
    uint32_t perf_level;
} __attribute__((packed));

struct perf_resp_0c {
    int32_t status;
    uint32_t flags;
    /*
    Null-terminated ASCII string of up to 64 bytes in length describing the performance domain extended name.
    */
    uint8_t name[64];
} __attribute__((packed));
