#include "list.h"

void list_init(list_t* list) {
    list->sentinel.prev = &list->sentinel;
    list->sentinel.next = &list->sentinel;
    list->size = 0;
}

void list_push_front(list_t* list, list_node_t* node) {
    node->next = list->sentinel.next;
    node->prev = &list->sentinel;
    list->sentinel.next->prev = node;
    list->sentinel.next = node;
    list->size++;
}

void list_push_back(list_t* list, list_node_t* node) {
    node->prev = list->sentinel.prev;
    node->next = &list->sentinel;
    list->sentinel.prev->next = node;
    list->sentinel.prev = node;
    list->size++;
}

list_node_t* list_pop_front(list_t* list) {
    if (list->size == 0)
        return NULL;
    list_node_t* node = list->sentinel.next;
    list_remove(node);
    return node;
}

list_node_t* list_pop_back(list_t* list) {
    if (list->size == 0)
        return NULL;
    list_node_t* node = list->sentinel.prev;
    list_remove(node);
    return node;
}

void list_remove(list_node_t* node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->prev = NULL;
    node->next = NULL;
}

size_t list_size(const list_t* list) {
    return list->size;
}

int list_empty(const list_t* list) {
    return list->size == 0;
}
