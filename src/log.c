/* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include "log.h"

#define STDOUT 1

static int log_fd = STDOUT;
#define ERR_LEVEL 0
#define INFO_LEVEL 1
#define DEBUG_LEVEL 2
static uint8_t log_level = INFO_LEVEL;

int parse_log_node(char *args)
{
    int log_file = 0;
    int log_stdio = 0;
    char *sr, *sn, *st, *stt;
    char *file_path = NULL;

    sr = sn = strdup(args);
    // -l log=file,path=<path>,level=<info/debug>
    while (st = strsep(&sn, ",")) {
        stt = strsep(&st, "=");
        if (!st) break;

        if (!strncmp(stt, "log", sizeof("log"))) {
            if (!strncmp(st, "file", sizeof("file"))) {
                log_file = 1;
            } else if (!strncmp(st, "stdio", sizeof("stdio"))) {
                log_stdio = 1;
            } else {
                printf("not a vaild log type, will use STDIO!");
                log_stdio = 1;
            }
        } else if (!strncmp(stt, "level", sizeof("level"))) {
            if (!strncmp(st, "info", sizeof("info"))) {
                log_level = INFO_LEVEL;
            } else if (!strncmp(st, "debug", sizeof("debug"))) {
                log_level = DEBUG_LEVEL;
            } else
                log_level = INFO_LEVEL;
        } else if (!strncmp(stt, "path", sizeof("path"))) {
            file_path = st;
        }
    }

    if (log_file && file_path) {
        unlink(file_path);
        log_fd = open(file_path, O_CREAT | O_RDWR | O_SYNC, 0666);
        if (log_fd < 0) {
            printf("failed to open log file, fallback to STDIO\n");
            log_stdio = 1;
            log_file = 0;
         }
    }

    if (log_stdio && !log_file) {
        log_fd = STDOUT;
    }
    free(sr);
    return 0;
}

void log_exit(void)
{
    if (log_fd > 0 && log_fd != STDOUT)
        close(log_fd);
}

void write_to_file(char *str)
{
    int size = strlen(str);
    if (write(log_fd, str, size) != size)
        printf("Failed to write log to file\n");
}

#define PUT_LOG(level)                          \
    va_list arg_ptr;                            \
    char buf[1024] = {'\0'};                    \
    va_start(arg_ptr, fmt);                     \
    if (log_fd > 0 && log_level >= (level))     \
        vsnprintf(buf, 1024, fmt, arg_ptr);            \
    va_end(arg_ptr);                            \
    write_to_file(buf);                         \


void pr_debug(char *fmt, ...)
{
    PUT_LOG(DEBUG_LEVEL);
}

void pr_err(char *fmt, ...)
{
    PUT_LOG(ERR_LEVEL);
}

void pr_info(char *fmt, ...)
{
    PUT_LOG(INFO_LEVEL);
}


