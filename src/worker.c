/* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <pthread.h>
#include <sys/epoll.h>
#include <sys/queue.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <vhost_user.h>
#include <errno.h>
#include <string.h>
#include "worker.h"
#include "log.h"

static int epoll_fd = 0;
static pthread_t recv_pid;
static int pipefd[2];

struct fd_info {
    void *data;
    int fd;
    kick_handler cb;
    LIST_ENTRY(fd_info) fdinfo_list;
};

static LIST_HEAD(listhead, fd_info) fdlist_head;
static pthread_mutex_t fdmutex = PTHREAD_MUTEX_INITIALIZER;

static struct fd_info *find_fd_info_by_fd(int fd)
{
    struct fd_info *fdi;
    LIST_FOREACH(fdi, &fdlist_head, fdinfo_list) {
        if (fdi->fd == fd)
            return fdi;
    }
    return NULL;
}

static void *worker_thr(void *arg)
{
    struct epoll_event eventlist[20];
    struct fd_info *fi;
    int fd;
    int ret = 0, exit = 0, i;
    int len;
    char buf[20];

    for (;;) {
        pr_debug("start to epoll wait...   \n");
        ret = epoll_wait(epoll_fd, eventlist, 20, -1);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                pr_err("epoll_wait ret=%d err=%s(%d)\n", ret, strerror(errno), errno);
                break;
            }
        }
        pthread_mutex_lock(&fdmutex);
        for (i = 0; i < ret; i++) {
            fd = eventlist[i].data.fd;

            if (fd == pipefd[0]) {
                pr_info("recieve pipe event, need to exit\n");
                exit = 1;
                continue;
            }

            /* Lookup current fd_info by fd; if not found => stale event */
            fi = find_fd_info_by_fd(fd);
            if (!fi) {
                pr_debug("stale epoll event: fd=%d not in list, skipping\n", fd);
                continue;
            }

            do {
                len = read(fd, buf, sizeof(buf));
            } while (len == (int)sizeof(buf));

            if (fi->cb && fi->data) {
                pr_debug("handle virtio request fd = %d\n", fi->fd);
                (fi->cb)(fi->data);
            }
        }
        pthread_mutex_unlock(&fdmutex);
        if (exit)
            break;
    }
    close(pipefd[0]);
    pthread_exit(NULL);
    return NULL;
}

int start_watch_on_fd(int fd, kick_handler cb, void *data)
{
    struct epoll_event event, event_pipe;
    struct fd_info *fi;

    if (fd < 0) {
        pr_err("failed to watch on fd < 0\n");
        return -1;
    }

    if (epoll_fd == 0) {
        epoll_fd = epoll_create1(0);
        if (epoll_fd < 0)
            return -1;

        if (pipe2(pipefd, O_NONBLOCK) < 0)
            return -1;

        event_pipe.events = EPOLLIN;
        event_pipe.data.fd = pipefd[0];
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, pipefd[0], &event_pipe);

        pthread_create(&recv_pid, NULL, worker_thr, NULL);
        pr_info("start worker thread \n");
    }

    fi = calloc(1, sizeof(struct fd_info));
    if (!fi) {
        pr_err("failed to calloc the fd_info\n");
        return -1;
    }
    fi->cb = cb;
    fi->data = data;
    fi->fd = fd;

    /* Insert into list first (under lock), so worker can always find it by fd */
    pthread_mutex_lock(&fdmutex);
    LIST_INSERT_HEAD(&fdlist_head, fi, fdinfo_list);
    pthread_mutex_unlock(&fdmutex);

    /* Register fd with epoll using data.fd only */
    event.events = EPOLLIN;
    event.data.fd = fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0) {
        pr_err("failed to add fd to epoll: fd=%d err=%s(%d)\n", fd, strerror(errno), errno);
        /* rollback list insert */
        pthread_mutex_lock(&fdmutex);
        LIST_REMOVE(fi, fdinfo_list);
        pthread_mutex_unlock(&fdmutex);
        free(fi);
        return -1;
    }

    return 0;
}

void stop_watch_on_fd(int fd)
{
    struct fd_info *fdi;

    if (fd < 0)
        return;

    pr_debug("stop watching on fd = %d \n", fd);

    if (!epoll_fd)
        return;

    /* Remove fd from epoll interest list */
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL) < 0) {
        pr_err("Note! failed to del fd from epoll: fd=%d err=%s(%d)\n", fd, strerror(errno), errno);
        /* Continue to try to cleanup list anyway */
    }

    pthread_mutex_lock(&fdmutex);
    fdi = find_fd_info_by_fd(fd);
    if (fdi) {
        LIST_REMOVE(fdi, fdinfo_list);
        free(fdi);
    }
    pthread_mutex_unlock(&fdmutex);
}

void kill_worker(void)
{
    char buf = 0;
    void *jval;
    struct fd_info *fdi;

    if (epoll_fd && pipefd[1]) {
        (void)write(pipefd[1], &buf, 1);
        pthread_join(recv_pid, &jval);
        close(pipefd[1]);
        close(epoll_fd);
        epoll_fd = 0;
        pr_info("stop worker thread..... \n");
    }

    /* Free any remaining fd_info nodes (in case stop_watch_on_fd wasn't called) */
    pthread_mutex_lock(&fdmutex);
    while (!LIST_EMPTY(&fdlist_head)) {
        fdi = LIST_FIRST(&fdlist_head);
        LIST_REMOVE(fdi, fdinfo_list);
        free(fdi);
    }
    pthread_mutex_unlock(&fdmutex);
}