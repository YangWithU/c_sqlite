#pragma once

#include "config.h"
#include <stddef.h>
#include <stdlib.h>

/* Debug memory tracking */
#ifdef DEBUG_MEM
  #define db_malloc(size)       mem_debug_malloc(size, __FILE__, __LINE__)
  #define db_calloc(nm, sz)    mem_debug_calloc(nm, sz, __FILE__, __LINE__)
  #define db_realloc(ptr, sz)  mem_debug_realloc(ptr, sz, __FILE__, __LINE__)
  #define db_free(ptr)         mem_debug_free(ptr, __FILE__, __LINE__)

  void* mem_debug_malloc(size_t size, const char* file, int line);
  void* mem_debug_calloc(size_t nmemb, size_t size, const char* file, int line);
  void* mem_debug_realloc(void* ptr, size_t size, const char* file, int line);
  void  mem_debug_free(void* ptr, const char* file, int line);
  void  mem_debug_report(void);
#else
  #define db_malloc(size)       malloc(size)
  #define db_calloc(nm, sz)     calloc(nm, sz)
  #define db_realloc(ptr, sz)   realloc(ptr, sz)
  #define db_free(ptr)         free(ptr)
#endif

/* Page buffer allocator */
void* page_alloc(size_t count);
void  page_free(void* ptr);
