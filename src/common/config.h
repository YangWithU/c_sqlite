#pragma once

#define PAGE_SIZE       4096
#define INVALID_PAGE_ID (-1)
#define INVALID_TXN_ID  (-1)
#define INVALID_LSN     (-1)
#define INVALID_FRAME_ID (-1)

#define MAX_TABLE_NAME   64
#define MAX_COLUMN_NAME  64
#define MAX_INDEX_NAME   64
#define MAX_FILE_NAME    256
#define MAX_ERROR_MSG    512

#define BUFFER_POOL_SIZE 256

#define DB_MAGIC        "MINISQLITE"
#define DB_MAGIC_LEN   10
#define DB_VERSION      1
