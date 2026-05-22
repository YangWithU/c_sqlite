#include "../xtest.h"

/* === INT ARRAY TESTS === */
TEST(assertions_array, int_eq_equal) {
    int a[] = {1, 2, 3};
    int b[] = {1, 2, 3};
    EXPECT_ARRAY_EQ_INT(a, b, 3);
}

TEST(assertions_array, int_eq_empty) {
    int a[] = {1};
    EXPECT_ARRAY_EQ_INT(a, a, 0);  /* size=0 always passes */
}

FAIL_TEST(assertions_array, int_eq_fail_mismatch) {
    int a[] = {1, 2, 3};
    int b[] = {1, 9, 3};
    EXPECT_ARRAY_EQ_INT(a, b, 3);
    EXPECT_TRUE(1);
}

TEST(assertions_array, int_ne_different) {
    int a[] = {1, 2, 3};
    int b[] = {4, 5, 6};
    EXPECT_ARRAY_NE_INT(a, b, 3);
}

FAIL_TEST(assertions_array, int_ne_fail_equal) {
    int a[] = {1, 2, 3};
    int b[] = {1, 2, 3};
    EXPECT_ARRAY_NE_INT(a, b, 3);
    EXPECT_TRUE(1);
}

/* === FLOAT ARRAY TESTS === */
TEST(assertions_array, float_eq_equal) {
    float a[] = {1.0f, 2.0f, 3.0f};
    float b[] = {1.0f, 2.0f, 3.0f};
    EXPECT_ARRAY_EQ_FLOAT(a, b, 3);
}

TEST(assertions_array, float_eq_within_epsilon) {
    float a[] = {0.1f, 0.2f};
    float b[] = {0.1000001f, 0.2000001f};
    EXPECT_ARRAY_EQ_FLOAT(a, b, 2);
}

TEST(assertions_array, float_ne_all_elements_different) {
    float a[] = {1.0f, 2.0f, 3.0f};
    float b[] = {4.0f, 5.0f, 6.0f};
    EXPECT_ARRAY_NE_FLOAT(a, b, 3);
}

/* === DOUBLE ARRAY TESTS === */
TEST(assertions_array, double_eq_equal) {
    double a[] = {1.0, 2.0, 3.0};
    double b[] = {1.0, 2.0, 3.0};
    EXPECT_ARRAY_EQ_DOUBLE(a, b, 3);
}

TEST(assertions_array, double_ne_all_elements_different) {
    double a[] = {1.0, 2.0, 3.0};
    double b[] = {4.0, 5.0, 6.0};
    EXPECT_ARRAY_NE_DOUBLE(a, b, 3);
}

/* === CHAR ARRAY TESTS === */
TEST(assertions_array, char_eq_equal) {
    char a[] = {'a', 'b', 'c'};
    char b[] = {'a', 'b', 'c'};
    EXPECT_ARRAY_EQ_CHAR(a, b, 3);
}

TEST(assertions_array, char_ne_all_elements_different) {
    char a[] = {'a', 'b', 'c'};
    char b[] = {'x', 'y', 'z'};
    EXPECT_ARRAY_NE_CHAR(a, b, 3);
}

FAIL_TEST(assertions_array, char_eq_fail) {
    char a[] = {'a', 'b', 'c'};
    char b[] = {'a', 'X', 'c'};
    EXPECT_ARRAY_EQ_CHAR(a, b, 3);
    EXPECT_TRUE(1);
}

TEST(assertions_array, ne_zero_size_boundary) {
    int a[] = {1, 2, 3};
    int b[] = {1, 2, 3};
    EXPECT_ARRAY_NE_INT(a, b, 0);
}

/* === ASSERT FATAL TESTS === */
FAIL_TEST(assertions_array, assert_array_int_fail) {
    int a[] = {1, 2};
    int b[] = {1, 9};
    ASSERT_ARRAY_EQ_INT(a, b, 2);
    EXPECT_TRUE(0);
}
