#include "../xtest.h"
#include <stdio.h>

/* ======================================================================== */
/* Positive tests — all assertions pass                                       */
/* ======================================================================== */

TEST(assertions_basic, eq_pass) {
    EXPECT_EQ(42, 42);
    EXPECT_EQ(-1, -1);
    EXPECT_EQ(0, 0);
}

TEST(assertions_basic, ne_pass) {
    EXPECT_NE(1, 2);
    EXPECT_NE(0, -1);
    EXPECT_NE(100, 0);
}

TEST(assertions_basic, true_pass) {
    EXPECT_TRUE(1);
    EXPECT_TRUE(42);
    EXPECT_TRUE(-1);
}

TEST(assertions_basic, false_pass) {
    EXPECT_FALSE(0);
}

TEST(assertions_basic, null_pass) {
    EXPECT_NULL(NULL);
}

TEST(assertions_basic, not_null_pass) {
    int x = 42;
    EXPECT_NOT_NULL(&x);
}

/* ======================================================================== */
/* Tests that exercise EXPECT_* failure paths (non-fatal, continue)          */
/* ======================================================================== */

FAIL_TEST(assertions_basic, eq_fail) {
    /* This will fail but should not crash/abort */
    EXPECT_EQ(1, 2);
    /* Execution continues after EXPECT failure */
    EXPECT_TRUE(1);  /* should still pass */
}

FAIL_TEST(assertions_basic, ne_fail) {
    EXPECT_NE(42, 42);
    EXPECT_TRUE(1);  /* should still pass */
}

FAIL_TEST(assertions_basic, true_fail) {
    EXPECT_TRUE(0);
    EXPECT_TRUE(1);  /* should still pass */
}

FAIL_TEST(assertions_basic, false_fail) {
    EXPECT_FALSE(1);
    EXPECT_TRUE(1);  /* should still pass */
}

FAIL_TEST(assertions_basic, null_fail) {
    int x = 42;
    EXPECT_NULL(&x);
    EXPECT_TRUE(1);  /* should still pass */
}

FAIL_TEST(assertions_basic, not_null_fail) {
    EXPECT_NOT_NULL(NULL);
    EXPECT_TRUE(1);  /* should still pass */
}

/* ======================================================================== */
/* Tests that exercise ASSERT_* failure paths (fatal, return immediately)    */
/* ======================================================================== */

FAIL_TEST(assertions_basic, assert_eq_fail) {
    /* This will fail and return; code after should not execute */
    ASSERT_EQ(1, 2);
    /* This should NOT be reached */
    EXPECT_TRUE(0);  /* will be marked as failure by runner exit code */
}

FAIL_TEST(assertions_basic, assert_ne_fail) {
    ASSERT_NE(42, 42);
    EXPECT_TRUE(0);
}

FAIL_TEST(assertions_basic, assert_true_fail) {
    ASSERT_TRUE(0);
    EXPECT_TRUE(0);
}

FAIL_TEST(assertions_basic, assert_false_fail) {
    ASSERT_FALSE(1);
    EXPECT_TRUE(0);
}

FAIL_TEST(assertions_basic, assert_null_fail) {
    int x = 42;
    ASSERT_NULL(&x);
    EXPECT_TRUE(0);
}

FAIL_TEST(assertions_basic, assert_not_null_fail) {
    ASSERT_NOT_NULL(NULL);
    EXPECT_TRUE(0);
}

/* ======================================================================== */
/* ASSERT pass + execution stops if subsequent ASSERT fails                   */
/* ======================================================================== */

TEST(assertions_basic, assert_fatal_stops) {
    ASSERT_EQ(1, 1);   /* passes — continue */
    ASSERT_NE(1, 2);   /* passes — continue */
    ASSERT_TRUE(1);    /* passes — continue */
    ASSERT_FALSE(0);   /* passes — continue */
    ASSERT_NULL(NULL); /* passes — continue */
    int x = 42;
    ASSERT_NOT_NULL(&x); /* passes — continue */
    EXPECT_EQ(1, 1);     /* should also pass */
}
