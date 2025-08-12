/*
Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause-Clear
*/
#include <stdio.h>
#include <type.h>
#define RESET_FLAGS_Autonomous_Reset_Mask 0x01U
#define RESET_FLAGS_Autonomous_Reset_Shift 0U
#define RESET_FLAGS_Explicit_Signal_Mask 0x02U
#define RESET_FLAGS_Explicit_Signal_Shift 1U
#define RESET_FLAGS_Async_Flag_Mask 0x4U
#define RESET_FLAGS_Async_Flag_Shift 2U
#define RESET_FLAGS_Reserved_Mask 0xFFFFFFF8U
#define RESET_FLAGS_Reserved_Shift 3U

#define RESET_STATUS_Reset_Type_Mask 0x80000000U
#define RESET_STATUS_Reset_ID_Mask 0x7FFFFFFFU

struct reset_resp_00 {
    int32_t status;
    uint32_t version;
} __attribute__((packed));

struct reset_resp_01 {
    int32_t status;
    /*
    Bits[31:16] Reserved, must be zero.
    Bits[15:0] Number of reset domains.
    */
    uint32_t attributes;
} __attribute__((packed));

struct reset_resp_02 {
    int32_t status;
    /*
    Reserved, must be zero.
    */
    uint32_t attributes;
}  __attribute__((packed));

struct reset_resp_03 {
    int32_t status;
    /*
    Bit[31] Asynchronous reset support.
    Set to 1 if this domain can be reset asynchronously.
    Set to 0 if this domain can only be reset synchronously.
    Bit[30] Reset notifications support.
    Set to 1 if reset notifications are supported for this domain.
    Set to 0 if reset notifications are not supported for this domain.
    Bit[29] Extended reset domain name.
    If set to 1, the reset domain name is greater than 16 bytes.
    If set to 0, extended reset domain name is not supported.
    Bits[28:0] Reserved, must be zero.
    */
    uint32_t attributes;
    /*
    Maximum time (in microseconds) required for the reset to take effect
    on the given domain. A value of 0xFFFFFFFF indicates this field is
    not supported by the platform.
    */
    uint32_t latency;
    /*
    Null-terminated ASCII string of up to 16 bytes in length describing
    the reset domain name. When Bit[29] of attributes field is set to 1,
    this field contains the lower 15 bytes of the NULL terminated reset domain name.
    */
    uint8_t name[16];
} __attribute__((packed));

struct reset_resp_04 {
    int32_t status;
} __attribute__((packed));

struct reset_resp_06 {
    int32_t status;
    /*
    Bits[31:0] Reserved, must be zero.
    */
    uint32_t flags;
    /*
    Null-terminated ASCII string of up to 64 bytes in length describing
    the reset domain extended name.
    */
    uint8_t name[64];
} __attribute__((packed));
