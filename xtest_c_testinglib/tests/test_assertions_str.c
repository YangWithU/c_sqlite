#include "../xtest.h"
#include <string.h>

/* Positive tests */
TEST(assertions_str, eq_equal) {
    EXPECT_STR_EQ("hello", "hello");
    EXPECT_STR_EQ("", "");
}

TEST(assertions_str, eq_both_null) {
    EXPECT_STR_EQ(NULL, NULL);
}

TEST(assertions_str, ne_different) {
    EXPECT_STR_NE("hello", "world");
    EXPECT_STR_NE(NULL, "hello");
    EXPECT_STR_NE("hello", NULL);
}

/* Failure tests -- non-fatal, execution continues */
FAIL_TEST(assertions_str, eq_fail_different) {
    EXPECT_STR_EQ("hello", "world");
    EXPECT_TRUE(1); /* should still execute */
}

FAIL_TEST(assertions_str, eq_fail_one_null) {
    EXPECT_STR_EQ(NULL, "hello");
    EXPECT_TRUE(1);
}

FAIL_TEST(assertions_str, ne_fail_equal) {
    EXPECT_STR_NE("same", "same");
    EXPECT_TRUE(1);
}

FAIL_TEST(assertions_str, ne_fail_both_null) {
    EXPECT_STR_NE(NULL, NULL);
    EXPECT_TRUE(1);
}

/* ASSERT failure -- fatal */
FAIL_TEST(assertions_str, assert_str_eq_fail) {
    ASSERT_STR_EQ("abc", "xyz");
    EXPECT_TRUE(0); /* should NOT execute */
}

FAIL_TEST(assertions_str, assert_str_ne_fail) {
    ASSERT_STR_NE("same", "same");
    EXPECT_TRUE(0);
}
