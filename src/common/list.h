#pragma once

#include <stddef.h>

typedef struct list_node {
    struct list_node* prev;
    struct list_node* next;
} list_node_t;

typedef struct {
    list_node_t sentinel;
    size_t      size;
} list_t;

void          list_init(list_t* list);
void          list_push_front(list_t* list, list_node_t* node);
void          list_push_back(list_t* list, list_node_t* node);
list_node_t*  list_pop_front(list_t* list);
list_node_t*  list_pop_back(list_t* list);
void          list_remove(list_node_t* node);
size_t        list_size(const list_t* list);
int           list_empty(const list_t* list);
