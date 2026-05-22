#include "../xtest.h"
#include <time.h>

TEST_BENCH(empty_loop, 1000000) {
    /* Measure empty iteration overhead */
}

TEST_BENCH(simple_calc, 500000) {
    volatile int x = 0;
    for (int i = 0; i < 100; i++) {
        x += i;
    }
}

TEST(bench_log, demo) {
    TEST_LOG("starting test with value=%d", 42);
    EXPECT_EQ(42, 42);
    TEST_LOG("test completed successfully");
}
