/* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef IFACE_SCMI_DEV_H
#define IFACE_SCMI_DEV_H
#include <stdint.h>

int set_power_status(int channel_id, uint32_t status);
int get_power_status(int channel_id, uint32_t *status);
int get_power_attributes(int channel_id, uint32_t *attr, uint32_t *name);
int get_power_domain_name(int channel_id, uint32_t *name);
int get_reset_attributes(int channel_id, uint32_t *attr, uint32_t *latency, uint32_t *name);
int set_reset_status(int channel_id, uint32_t flag, uint32_t state);
int get_reset_domain_name(int channel_id, uint32_t *name);

#endif
