#include "vector.h"
#include "mem.h"
#include <string.h>

#define VECTOR_INIT_CAP 8

void vector_init(vector_t* v, size_t elem_size) {
    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
    v->elem_size = elem_size;
}

void vector_destroy(vector_t* v) {
    if (v->data) {
        db_free(v->data);
        v->data = NULL;
    }
    v->size = 0;
    v->capacity = 0;
}

static int vector_grow(vector_t* v) {
    size_t new_cap = v->capacity == 0 ? VECTOR_INIT_CAP : v->capacity * 2;
    void* new_data = db_realloc(v->data, new_cap * v->elem_size);
    if (!new_data)
        return -1;
    v->data = new_data;
    v->capacity = new_cap;
    return 0;
}

int vector_push(vector_t* v, const void* elem) {
    if (v->size >= v->capacity) {
        if (vector_grow(v) != 0)
            return -1;
    }
    memcpy((char*)v->data + v->size * v->elem_size, elem, v->elem_size);
    v->size++;
    return 0;
}

void* vector_at(const vector_t* v, size_t index) {
    if (index >= v->size)
        return NULL;
    return (char*)v->data + index * v->elem_size;
}

void vector_pop(vector_t* v) {
    if (v->size > 0)
        v->size--;
}

void vector_clear(vector_t* v) {
    v->size = 0;
}

size_t vector_size(const vector_t* v) {
    return v->size;
}

int vector_empty(const vector_t* v) {
    return v->size == 0;
}
