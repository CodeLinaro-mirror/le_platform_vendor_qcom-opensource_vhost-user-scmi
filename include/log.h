/* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef LOG_H
#define LOG_H

int parse_log_node(char *args);
void log_exit(void);
void pr_debug(char *fmt, ...);
void pr_err(char *fmt, ...);
void pr_info(char *fmt, ...);

#endif
