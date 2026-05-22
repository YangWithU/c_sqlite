#pragma once

#include <stdint.h>

typedef int32_t  page_id_t;
typedef int32_t  slot_id_t;
typedef int32_t  txn_id_t;
typedef int32_t  table_id_t;
typedef int32_t  column_id_t;
typedef int32_t  index_id_t;
typedef int32_t  lsn_t;
typedef uint32_t frame_id_t;

/* Record ID: identifies a tuple in a heap file */
typedef struct {
    page_id_t page_id;
    slot_id_t slot_id;
} rid_t;

/* Data type IDs for the value system */
typedef enum {
    TYPE_INTEGER = 1,
    TYPE_FLOAT   = 2,
    TYPE_VARCHAR = 3,
    TYPE_BOOLEAN = 4,
    TYPE_NULL    = 5,
} type_id_t;

#define INVALID_PAGE_ID  (-1)
#define INVALID_SLOT_ID  (-1)
#define INVALID_LSN      (0)
#define INVALID_TXN_ID   (-1)
#define INVALID_TABLE_ID (-1)
#define INVALID_INDEX_ID (-1)
#define INVALID_RID      ((rid_t){INVALID_PAGE_ID, INVALID_SLOT_ID})
