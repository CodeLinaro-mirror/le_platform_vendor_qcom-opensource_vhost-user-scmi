/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef LIST_C
#define LIST_C

#include <assert.h>
#include "list.h"

void list_push(list_t **head, list_t **tail, list_t *entry)
{
    list_t *e;

    e = *tail;
    if (*head == NULL) {
        *head = entry;
    } else {
        assert(e != NULL);
        e->next = entry;
        entry->prev = e;
    }
    *tail = entry;
}

bool list_remove(list_t **head, list_t **tail, void *addr)
{
    list_t *e, *n, *p;

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
                }
            } else {
                //n == NULL, in the tail
                *tail = p;
                p->next = NULL;
            }
            // clear the entry
            e->next = NULL;
            e->prev = NULL;
            return true;
        }
        e = e->next;
    }
    return false;
}

list_t *list_pop(list_t **head, list_t **tail)
{
    list_t *e, *p, *n;
    e = *tail;

    if (e) {
        if (e == *head) {
            *head = NULL;
            *tail = NULL;
        } else {
            p = e->prev;
            p->next = NULL;
            *tail = p;
        }
    }

    return e;
}

void list_print(list_t *head, list_t *tail)
{
    list_t *e;
    int i = 0;

    pr_info("show list from head to tail:\n");
    e = head;
    while(e) {
        pr_info("list entry[%d] is found, data = %x\n", i++, e->data);
        e = e->next;
    }

    e = tail;

    pr_info("show list from tail to head:\n");
    while(e) {
        pr_info("list entry[%d] is found, data = %x\n", i++, e->data);
        e = e->prev;
    }
}
#endif
