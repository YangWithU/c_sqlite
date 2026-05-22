#pragma once

#define PAGE_SIZE       4096
#define BUFFER_POOL_SIZE 256

#define MAX_TABLE_NAME   64
#define MAX_COLUMN_NAME  64
#define MAX_INDEX_NAME   64
#define MAX_FILE_NAME    256
#define MAX_ERROR_MSG    512

#define DB_MAGIC        "MINISQLITE"
#define DB_MAGIC_LEN    10
#define DB_VERSION      1

/* Slotted page constants */
#define PAGE_HEADER_SIZE 24
#define SLOT_SIZE        4
#define SLOT_OFFSET_TUPLE_OFFSET 0
#define SLOT_OFFSET_TUPLE_SIZE   2

/* Page header field offsets */
#define PAGE_HEADER_OFFSET_PAGE_ID     0
#define PAGE_HEADER_OFFSET_PAGE_LSN    4
#define PAGE_HEADER_OFFSET_NUM_TUPLES  12
#define PAGE_HEADER_OFFSET_FREE_SPACE  16
#define PAGE_HEADER_OFFSET_NEXT_PAGE   20

/* B+ tree node constants */
#define BP_TREE_NODE_TYPE_OFFSET   PAGE_HEADER_SIZE
#define BP_TREE_NODE_SIZE_OFFSET   (PAGE_HEADER_SIZE + 1)
#define BP_TREE_MAX_KEYS           200
#define BP_TREE_KEY_SIZE           8
#define BP_TREE_INTERNAL_VAL_SIZE  4
