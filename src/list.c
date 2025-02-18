/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef LIST_C
#define LIST_C

#include <assert.h>
#include <pthread.h>
#include "list.h"
#include "log.h"

pthread_mutex_t list_mutex;

void list_push(list_t **head, list_t **tail, list_t *entry)
{
    list_t *e;

    pthread_mutex_lock(&list_mutex);

    entry->prev = NULL;
    entry->next = NULL;

    e = *tail;
    if (*head == NULL) {
        *head = entry;
    } else {
        assert(e != NULL);
        e->next = entry;
        entry->prev = e;
    }
    *tail = entry;
    pthread_mutex_unlock(&list_mutex);
}

bool list_remove(list_t **head, list_t **tail, void *addr)
{
    list_t *e, *n, *p;

    pthread_mutex_lock(&list_mutex);
    e = *head;
    while(e) {
        if (e == addr) {
            // find the node
            n = e->next;
            p = e->prev;
            if (n && p) {
                // in the middle
                p->next = n;
                n->prev = p;
            } else if (p == NULL) {
                // in the head
                *head = n;
                if (n == NULL) {
                    // tail
                    *tail = NULL;
                } else {
                    n->prev = NULL;
                }
            } else {
                //n == NULL, in the tail
                *tail = p;
                p->next = NULL;
            }
            // clear the entry
            e->next = NULL;
            e->prev = NULL;
            pthread_mutex_unlock(&list_mutex);
            return true;
        }
        e = e->next;
    }
    pthread_mutex_unlock(&list_mutex);

    return false;
}

list_t *list_pop(list_t **head, list_t **tail)
{
    list_t *e, *p, *n;
    e = *tail;

    pthread_mutex_lock(&list_mutex);
    if (e) {
        if (e == *head) {
            *head = NULL;
            *tail = NULL;
        } else {
            p = e->prev;
            if (p)
                p->next = NULL;
            *tail = p;
        }

        // clear the link info
        e->next = NULL;
        e->prev = NULL;
    }
    pthread_mutex_unlock(&list_mutex);
    return e;
}

void list_print(list_t *head, list_t *tail)
{
    list_t *e;
    int i = 0;

    pthread_mutex_lock(&list_mutex);
    pr_info("show list from head to tail:\n");
    e = head;
    while(e) {
        pr_info("list entry[%d] is found, data = %x\n", i++, e->data);
        e = e->next;
    }

    e = tail;

    i = 0;
    pr_info("show list from tail to head:\n");
    while(e) {
        pr_info("list entry[%d] is found, data = %x\n", i++, e->data);
        e = e->prev;
    }

    pthread_mutex_unlock(&list_mutex);
}
#endif
