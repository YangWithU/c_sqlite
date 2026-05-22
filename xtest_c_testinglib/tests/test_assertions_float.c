#include "../xtest.h"
#include <math.h>

/* Positive tests */
TEST(assertions_float, eq_equal) {
    EXPECT_FLOAT_EQ(0.1f, 0.1f);
    EXPECT_FLOAT_EQ(0.0f, 0.0f);
    EXPECT_FLOAT_EQ(-1.0f, -1.0f);
}

TEST(assertions_float, eq_negative_zero) {
    EXPECT_FLOAT_EQ(0.0f, -0.0f);  /* IEEE 754: 0.0 == -0.0 */
}

TEST(assertions_float, eq_infinity) {
    EXPECT_FLOAT_EQ(INFINITY, INFINITY);
    EXPECT_FLOAT_EQ(-INFINITY, -INFINITY);
}

TEST(assertions_float, eq_within_epsilon) {
    EXPECT_FLOAT_EQ(0.1f, 0.1f + 1e-7f);  /* within 1e-6 tolerance */
}

TEST(assertions_double, eq_equal) {
    EXPECT_DOUBLE_EQ(0.1, 0.1);
    EXPECT_DOUBLE_EQ(0.0, 0.0);
}

TEST(assertions_double, eq_within_epsilon) {
    EXPECT_DOUBLE_EQ(0.1, 0.1 + 1e-16);  /* within 1e-15 tolerance */
}

/* Failure tests — non-fatal */
FAIL_TEST(assertions_float, eq_fail_outside_epsilon) {
    EXPECT_FLOAT_EQ(0.1f, 0.2f);
    EXPECT_TRUE(1);
}

FAIL_TEST(assertions_float, eq_fail_nan) {
    EXPECT_FLOAT_EQ(NAN, NAN);  /* NaN != NaN per IEEE 754 */
    EXPECT_TRUE(1);
}

FAIL_TEST(assertions_double, eq_fail_different) {
    EXPECT_DOUBLE_EQ(1.0, 2.0);
    EXPECT_TRUE(1);
}

FAIL_TEST(assertions_double, eq_fail_nan) {
    EXPECT_DOUBLE_EQ(NAN, NAN);
    EXPECT_TRUE(1);
}

/* ASSERT failure — fatal */
FAIL_TEST(assertions_float, assert_float_eq_fail) {
    ASSERT_FLOAT_EQ(3.14f, 2.71f);
    EXPECT_TRUE(0);
}

FAIL_TEST(assertions_double, assert_double_eq_fail) {
    ASSERT_DOUBLE_EQ(1.0, 2.0);
    EXPECT_TRUE(0);
}

/* Edge cases */
TEST(assertions_float, large_values) {
    EXPECT_FLOAT_EQ(1e30f, 1e30f);
}

TEST(assertions_double, very_small) {
    EXPECT_DOUBLE_EQ(1e-100, 1e-100);
}
