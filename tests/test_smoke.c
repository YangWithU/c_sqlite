#include "xtest.h"
#include "src/common/error.h"
#include "src/common/types.h"
#include "src/common/config.h"
#include "src/common/macros.h"
#include "src/common/platform.h"

/* ---- Basic sanity checks ---- */

TEST(smoke, config_constants) {
    EXPECT_EQ(PAGE_SIZE, 4096);
    EXPECT_EQ(BUFFER_POOL_SIZE, 256);
    EXPECT_EQ(DB_VERSION, 1);
}

TEST(smoke, error_codes) {
    EXPECT_EQ(DB_OK, 0);
    EXPECT_NE(DB_IO_ERROR, 0);
    EXPECT_NE(DB_SYNTAX_ERROR, 0);
    EXPECT_STR_EQ(error_code_to_string(DB_OK), "OK");
    EXPECT_STR_EQ(error_code_to_string(DB_IO_ERROR), "I/O error");
    EXPECT_STR_EQ(error_code_to_string(DB_SYNTAX_ERROR), "Syntax error");
}

TEST(smoke, macros) {
    EXPECT_EQ(MIN(3, 5), 3);
    EXPECT_EQ(MAX(3, 5), 5);
    int arr[] = {1, 2, 3};
    EXPECT_EQ((int)ARRAY_SIZE(arr), 3);
}

TEST(smoke, types) {
    page_id_t pid = INVALID_PAGE_ID;
    EXPECT_EQ(pid, -1);
    txn_id_t tid = INVALID_TXN_ID;
    EXPECT_EQ(tid, -1);
}
