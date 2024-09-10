
/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef LIST_HEADER_H
#define LIST_HEADER_H
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

typedef unsigned int uint32_t;
struct list;

typedef struct list {
    uint32_t data;
    struct list *prev;
    struct list *next;
} list_t;

void list_push(list_t **head, list_t **tail, list_t *entry);
bool list_remove(list_t **head, list_t **tail, void *addr);
list_t *list_pop(list_t **head, list_t **tail);
void list_print(list_t *head, list_t *tail);

#endif
