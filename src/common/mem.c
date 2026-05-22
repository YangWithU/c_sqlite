#include "mem.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef DEBUG_MEM
/* Allocation tracking entry */
typedef struct mem_track_entry {
    void*               ptr;
    size_t              size;
    const char*         file;
    int                 line;
    struct mem_track_entry* next;
} mem_track_entry_t;

#define MEM_TRACK_TABLE_SIZE 1024

static mem_track_entry_t* mem_track_table[MEM_TRACK_TABLE_SIZE];
static size_t mem_total_allocated = 0;
static size_t mem_total_freed = 0;

static size_t mem_track_hash(void* ptr) {
    return ((size_t)ptr >> 4) % MEM_TRACK_TABLE_SIZE;
}

static void mem_track_add(void* ptr, size_t size, const char* file, int line) {
    mem_track_entry_t* entry = malloc(sizeof(mem_track_entry_t));
    if (!entry) return;
    entry->ptr = ptr;
    entry->size = size;
    entry->file = file;
    entry->line = line;
    size_t idx = mem_track_hash(ptr);
    entry->next = mem_track_table[idx];
    mem_track_table[idx] = entry;
    mem_total_allocated += size;
}

static mem_track_entry_t* mem_track_find(void* ptr) {
    size_t idx = mem_track_hash(ptr);
    mem_track_entry_t* prev = NULL;
    mem_track_entry_t* cur = mem_track_table[idx];
    while (cur) {
        if (cur->ptr == ptr) {
            if (prev)
                prev->next = cur->next;
            else
                mem_track_table[idx] = cur->next;
            return cur;
        }
        prev = cur;
        cur = cur->next;
    }
    return NULL;
}

void* mem_debug_malloc(size_t size, const char* file, int line) {
    void* ptr = malloc(size);
    if (ptr)
        mem_track_add(ptr, size, file, line);
    return ptr;
}

void* mem_debug_calloc(size_t nmemb, size_t size, const char* file, int line) {
    void* ptr = calloc(nmemb, size);
    if (ptr)
        mem_track_add(ptr, nmemb * size, file, line);
    return ptr;
}

void* mem_debug_realloc(void* ptr, size_t size, const char* file, int line) {
    if (ptr) {
        /* Remove old entry from tracking, but don't free — realloc handles that */
        mem_track_entry_t* entry = mem_track_find(ptr);
        if (entry) {
            mem_total_freed += entry->size;
            free(entry);
        }
    }
    void* new_ptr = realloc(ptr, size);
    if (new_ptr)
        mem_track_add(new_ptr, size, file, line);
    return new_ptr;
}

void mem_debug_free(void* ptr, const char* file, int line) {
    if (!ptr) return;
    mem_track_entry_t* entry = mem_track_find(ptr);
    if (entry) {
        mem_total_freed += entry->size;
        free(entry);
    }
    free(ptr);
    (void)file; (void)line;
}

void mem_debug_report(void) {
    int leak_count = 0;
    size_t leak_bytes = 0;
    for (int i = 0; i < MEM_TRACK_TABLE_SIZE; i++) {
        mem_track_entry_t* cur = mem_track_table[i];
        while (cur) {
            fprintf(stderr, "LEAK: %p (%zu bytes) allocated at %s:%d\n",
                    cur->ptr, cur->size, cur->file, cur->line);
            leak_bytes += cur->size;
            leak_count++;
            cur = cur->next;
        }
    }
    fprintf(stderr, "Memory summary: allocated=%zu, freed=%zu, leaked=%zu bytes (%d blocks)\n",
            mem_total_allocated, mem_total_freed, leak_bytes, leak_count);
}
#endif /* DEBUG_MEM */

/* Page buffer allocator */
void* page_alloc(size_t count) {
    return calloc(count, PAGE_SIZE);
}

void page_free(void* ptr) {
    free(ptr);
}
