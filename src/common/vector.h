#pragma once

#include <stddef.h>

typedef struct {
    void*  data;
    size_t size;
    size_t capacity;
    size_t elem_size;
} vector_t;

void  vector_init(vector_t* v, size_t elem_size);
void  vector_destroy(vector_t* v);
int   vector_push(vector_t* v, const void* elem);
void* vector_at(const vector_t* v, size_t index);
void  vector_pop(vector_t* v);
void  vector_clear(vector_t* v);
size_t vector_size(const vector_t* v);
int   vector_empty(const vector_t* v);
