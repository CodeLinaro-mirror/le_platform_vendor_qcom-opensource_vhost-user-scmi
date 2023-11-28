/* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef ASYNC_CONTEXT_H
#define ASYNC_CONTEXT_H

#include <stdint.h>
/**
 * Thread function
 *
 * Function pointer to thread start routine.
 *
 * @param arg
 *   Argument passed to thread_create().
 * @return
 *   Thread function exit value.
 */
typedef uint32_t (*thread_func) (void *arg);

int start_watch_on_fd(int fd, kick_handler cb, void *data);
void stop_watch_on_fd(int fd);
void kill_worker(void);

#endif
