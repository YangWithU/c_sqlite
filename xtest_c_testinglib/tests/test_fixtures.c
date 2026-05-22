#include "../xtest.h"

/* ======================================================================== */
/* Fixture functions for TEST_F usage                                        */
/* ======================================================================== */

static int g_setup_called = 0;
static int g_teardown_called = 0;
static int g_value = 0;

static void fixtures_setup(void) {
    g_setup_called = 1;
    g_value = 42;
}

static void fixtures_teardown(void) {
    g_teardown_called = 1;
    g_value = 0;
}

TEST_DEFINE_FIXTURE(fixtures_fxt, fixtures_setup, fixtures_teardown);

TEST_F(fixtures, setup_teardown_called, fixtures_fxt) {
    EXPECT_EQ(g_value, 42);
    EXPECT_TRUE(g_setup_called);
}

/* DISABLED test verification */
DISABLED_TEST(fixtures, should_be_skipped) {
    EXPECT_TRUE(0); /* Should never run */
}

/* FAIL_TEST — expected to fail */
FAIL_TEST(fixtures, expected_to_fail) {
    EXPECT_EQ(1, 2); /* Will fail as expected */
}

/* Normal test to verify counters still work */
TEST(fixtures, normal_pass) {
    EXPECT_EQ(1, 1);
}
