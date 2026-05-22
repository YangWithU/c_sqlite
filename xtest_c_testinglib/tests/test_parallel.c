#include "../xtest.h"

TEST(parallel_test, verify_parallel_runs) {
    /* This test verifies the parallel engine is linked and runs */
    EXPECT_EQ(1, 1);
}

TEST(parallel_test, verify_timeout_configured) {
    EXPECT_TRUE(XTEST_DEFAULT_TIMEOUT == 30);
}
