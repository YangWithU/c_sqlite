#ifndef XTEST_H
#define XTEST_H

#define _GNU_SOURCE
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================== */
/* Version                                                                  */
/* ======================================================================== */

#define XTEST_VERSION "0.1.0"

/* ======================================================================== */
/* Types                                                                    */
/* ======================================================================== */

/* === TYPES === */

/* ------------------------------------------------------------------ */
/*  Fixture descriptor — function pointers for set_up/tear_down        */
/* ------------------------------------------------------------------ */

typedef struct xtest_fixture_desc {
    void (*set_up)(void);
    void (*tear_down)(void);
} xtest_fixture_desc;

/* ------------------------------------------------------------------ */
/*  Test case descriptor                                                */
/* ------------------------------------------------------------------ */

#define XTEST_FLAG_NORMAL   0
#define XTEST_FLAG_DISABLED 1
#define XTEST_FLAG_XFAIL    2

typedef struct xtest_case {
    const char *suite_name;
    const char *test_name;
    void       (*fn)(void);
    int         flags;
    const xtest_fixture_desc *fixture;  /* NULL=no fixture */
} xtest_case;

/* ------------------------------------------------------------------ */
/*  Aggregated test run results                                        */
/* ------------------------------------------------------------------ */

typedef struct xtest_result {
    int passed;
    int failed;
    int skipped;
    int xfailed;    /* expected failure — test failed as expected */
    int xpassed;    /* unexpected pass — test passed but was expected to fail */
    int crashed;
    int timeout;
    int total;
    long elapsed_ms;
} xtest_result;

/* ------------------------------------------------------------------ */
/*  Runtime configuration                                              */
/* ------------------------------------------------------------------ */

typedef struct xtest_config {
    int  parallel;         /* number of parallel workers, 0=auto */
    int  timeout_sec;      /* per-test timeout in seconds */
    int  verbose;          /* 0=summary only, 1=per-test output */
    int  repeat_count;     /* --repeat=N */
    int  no_fork;          /* --no-fork flag */
    int  color;            /* 0=no, 1=yes, 2=auto */
    const char *output_file; /* XML output file path, NULL=stdout */
} xtest_config;

/* ------------------------------------------------------------------ */
/*  Child process exit codes                                          */
/* ------------------------------------------------------------------ */

enum {
    XTEST_EXIT_PASS    = 0,
    XTEST_EXIT_FAIL    = 1,
    XTEST_EXIT_CRASH   = 2,
    XTEST_EXIT_SKIP    = 3,
    XTEST_EXIT_TIMEOUT = 4
};

/* ------------------------------------------------------------------ */
/*  Constants                                                          */
/* ------------------------------------------------------------------ */

#define XTEST_MAX_NAME_LEN       256
#define XTEST_MAX_TESTS          4096
#define XTEST_DEFAULT_TIMEOUT    30
#define XTEST_FLOAT_EPSILON      1e-6f
#define XTEST_DOUBLE_EPSILON     1e-15
#define XTEST_MAX_OUTPUT_LEN     10240  /* 10KB max per test output */

/* ------------------------------------------------------------------ */
/*  ANSI color codes                                                   */
/* ------------------------------------------------------------------ */

#define XTEST_COLOR_GREEN   "\033[32m"
#define XTEST_COLOR_RED     "\033[31m"
#define XTEST_COLOR_YELLOW  "\033[33m"
#define XTEST_COLOR_CYAN    "\033[36m"
#define XTEST_COLOR_RESET   "\033[0m"
#define XTEST_COLOR_BOLD    "\033[1m"

/* ======================================================================== */
/* Macros                                                                   */
/* ======================================================================== */

/* === MACROS === */

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                     */
/* ------------------------------------------------------------------ */

/* Global failure counter — defined in xtest_runner.c */
extern int _xtest_failure_count;

/* Record a non-fatal failure (EXPECT_* variants) */
#define _XTEST_FAIL(file, line, ...) \
    do { \
        fprintf(stderr, "%s:%d: ", file, line); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
        _xtest_failure_count++; \
    } while(0)

/* Record a fatal failure and return immediately (ASSERT_* variants) */
#define _XTEST_FAIL_FATAL(file, line, ...) \
    do { \
        fprintf(stderr, "%s:%d: ", file, line); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
        _xtest_failure_count++; \
        return; \
    } while(0)

/* ------------------------------------------------------------------ */
/*  EXPECT_EQ / ASSERT_EQ — equality                                    */
/* ------------------------------------------------------------------ */

#define EXPECT_EQ(expected, actual) \
    do { \
        if (!((expected) == (actual))) { \
            _XTEST_FAIL(__FILE__, __LINE__, \
                "EXPECT_EQ(" #expected ", " #actual ") failed: " \
                "expected=%d, actual=%d", \
                (int)(expected), (int)(actual)); \
        } \
    } while(0)

#define ASSERT_EQ(expected, actual) \
    do { \
        if (!((expected) == (actual))) { \
            _XTEST_FAIL_FATAL(__FILE__, __LINE__, \
                "ASSERT_EQ(" #expected ", " #actual ") failed: " \
                "expected=%d, actual=%d", \
                (int)(expected), (int)(actual)); \
        } \
    } while(0)

/* ------------------------------------------------------------------ */
/*  EXPECT_NE / ASSERT_NE — inequality                                  */
/* ------------------------------------------------------------------ */

#define EXPECT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            _XTEST_FAIL(__FILE__, __LINE__, \
                "EXPECT_NE(" #a ", " #b ") failed: both are %d", \
                (int)(a)); \
        } \
    } while(0)

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            _XTEST_FAIL_FATAL(__FILE__, __LINE__, \
                "ASSERT_NE(" #a ", " #b ") failed: both are %d", \
                (int)(a)); \
        } \
    } while(0)

/* ------------------------------------------------------------------ */
/*  EXPECT_TRUE / ASSERT_TRUE — boolean truth                           */
/* ------------------------------------------------------------------ */

#define EXPECT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            _XTEST_FAIL(__FILE__, __LINE__, \
                "EXPECT_TRUE(" #condition ") failed: condition is false"); \
        } \
    } while(0)

#define ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            _XTEST_FAIL_FATAL(__FILE__, __LINE__, \
                "ASSERT_TRUE(" #condition ") failed: condition is false"); \
        } \
    } while(0)

/* ------------------------------------------------------------------ */
/*  EXPECT_FALSE / ASSERT_FALSE — boolean false                         */
/* ------------------------------------------------------------------ */

#define EXPECT_FALSE(condition) \
    do { \
        if (condition) { \
            _XTEST_FAIL(__FILE__, __LINE__, \
                "EXPECT_FALSE(" #condition ") failed: condition is true"); \
        } \
    } while(0)

#define ASSERT_FALSE(condition) \
    do { \
        if (condition) { \
            _XTEST_FAIL_FATAL(__FILE__, __LINE__, \
                "ASSERT_FALSE(" #condition ") failed: condition is true"); \
        } \
    } while(0)

/* ------------------------------------------------------------------ */
/*  EXPECT_NULL / ASSERT_NULL — pointer is NULL                        */
/* ------------------------------------------------------------------ */

#define EXPECT_NULL(ptr) \
    do { \
        if ((ptr) != NULL) { \
            _XTEST_FAIL(__FILE__, __LINE__, \
                "EXPECT_NULL(" #ptr ") failed: ptr=%p is not NULL", \
                (void*)(ptr)); \
        } \
    } while(0)

#define ASSERT_NULL(ptr) \
    do { \
        if ((ptr) != NULL) { \
            _XTEST_FAIL_FATAL(__FILE__, __LINE__, \
                "ASSERT_NULL(" #ptr ") failed: ptr=%p is not NULL", \
                (void*)(ptr)); \
        } \
    } while(0)

/* ------------------------------------------------------------------ */
/*  EXPECT_NOT_NULL / ASSERT_NOT_NULL — pointer is non-NULL            */
/* ------------------------------------------------------------------ */

#define EXPECT_NOT_NULL(ptr) \
    do { \
        if ((ptr) == NULL) { \
            _XTEST_FAIL(__FILE__, __LINE__, \
                "EXPECT_NOT_NULL(" #ptr ") failed: ptr is NULL"); \
        } \
    } while(0)

#define ASSERT_NOT_NULL(ptr) \
    do { \
        if ((ptr) == NULL) { \
            _XTEST_FAIL_FATAL(__FILE__, __LINE__, \
                "ASSERT_NOT_NULL(" #ptr ") failed: ptr is NULL"); \
        } \
    } while(0)

/* ------------------------------------------------------------------ */
/*  EXPECT_STR_EQ / ASSERT_STR_EQ — string equality (NULL-safe)       */
/* ------------------------------------------------------------------ */

#define EXPECT_STR_EQ(str1, str2) \
    do { \
        const char *_s1 = (str1); \
        const char *_s2 = (str2); \
        if (_s1 == _s2) { \
            /* both NULL or same pointer */ \
        } else if (_s1 == NULL || _s2 == NULL) { \
            _XTEST_FAIL(__FILE__, __LINE__, \
                "EXPECT_STR_EQ(" #str1 ", " #str2 ") failed: " \
                "one is NULL (str1=%p, str2=%p)", \
                (void*)_s1, (void*)_s2); \
        } else if (strcmp(_s1, _s2) != 0) { \
            _XTEST_FAIL(__FILE__, __LINE__, \
                "EXPECT_STR_EQ(" #str1 ", " #str2 ") failed: " \
                "expected=\"%.80s\", actual=\"%.80s\"", _s1, _s2); \
        } \
    } while(0)

#define ASSERT_STR_EQ(str1, str2) \
    do { \
        const char *_s1 = (str1); \
        const char *_s2 = (str2); \
        if (_s1 == _s2) { \
            /* both NULL or same pointer */ \
        } else if (_s1 == NULL || _s2 == NULL) { \
            _XTEST_FAIL_FATAL(__FILE__, __LINE__, \
                "ASSERT_STR_EQ(" #str1 ", " #str2 ") failed: " \
                "one is NULL (str1=%p, str2=%p)", \
                (void*)_s1, (void*)_s2); \
        } else if (strcmp(_s1, _s2) != 0) { \
            _XTEST_FAIL_FATAL(__FILE__, __LINE__, \
                "ASSERT_STR_EQ(" #str1 ", " #str2 ") failed: " \
                "expected=\"%.80s\", actual=\"%.80s\"", _s1, _s2); \
        } \
    } while(0)

/* ------------------------------------------------------------------ */
/*  EXPECT_STR_NE / ASSERT_STR_NE — string inequality                  */
/* ------------------------------------------------------------------ */

#define EXPECT_STR_NE(str1, str2) \
    do { \
        const char *_s1 = (str1); \
        const char *_s2 = (str2); \
        if (_s1 == NULL && _s2 == NULL) { \
            _XTEST_FAIL(__FILE__, __LINE__, \
                "EXPECT_STR_NE(" #str1 ", " #str2 ") failed: " \
                "both are NULL"); \
        } else if (_s1 != NULL && _s2 != NULL && strcmp(_s1, _s2) == 0) { \
            _XTEST_FAIL(__FILE__, __LINE__, \
                "EXPECT_STR_NE(" #str1 ", " #str2 ") failed: " \
                "both are \"%.80s\"", _s1); \
        } \
    } while(0)

#define ASSERT_STR_NE(str1, str2) \
    do { \
        const char *_s1 = (str1); \
        const char *_s2 = (str2); \
        if (_s1 == NULL && _s2 == NULL) { \
            _XTEST_FAIL_FATAL(__FILE__, __LINE__, \
                "ASSERT_STR_NE(" #str1 ", " #str2 ") failed: " \
                "both are NULL"); \
        } else if (_s1 != NULL && _s2 != NULL && strcmp(_s1, _s2) == 0) { \
            _XTEST_FAIL_FATAL(__FILE__, __LINE__, \
                "ASSERT_STR_NE(" #str1 ", " #str2 ") failed: " \
                "both are \"%.80s\"", _s1); \
        } \
    } while(0)

/* ------------------------------------------------------------------ */
/*  EXPECT_FLOAT_EQ / ASSERT_FLOAT_EQ — float tolerance comparison    */
/* ------------------------------------------------------------------ */

#define EXPECT_FLOAT_EQ(expected, actual) \
    do { \
        float _e = (float)(expected); \
        float _a = (float)(actual); \
        /* Infinity equality check: use == first to avoid NaN from fabsf(Inf-Inf) */ \
        if (_e == _a) { \
            /* equal, including Inf == Inf */ \
        } else if (isnan(_e) || isnan(_a)) { \
            _XTEST_FAIL(__FILE__, __LINE__, \
                "EXPECT_FLOAT_EQ(" #expected ", " #actual ") failed: " \
                "NaN comparison (%.6f vs %.6f)", (double)_e, (double)_a); \
        } else if (fabsf(_e - _a) >= XTEST_FLOAT_EPSILON) { \
            _XTEST_FAIL(__FILE__, __LINE__, \
                "EXPECT_FLOAT_EQ(" #expected ", " #actual ") failed: " \
                "expected=%.6f, actual=%.6f, diff=%.6e, epsilon=%.1e", \
                (double)_e, (double)_a, (double)fabsf(_e - _a), (double)XTEST_FLOAT_EPSILON); \
        } \
    } while(0)

#define ASSERT_FLOAT_EQ(expected, actual) \
    do { \
        float _e = (float)(expected); \
        float _a = (float)(actual); \
        if (_e == _a) { \
        } else if (isnan(_e) || isnan(_a)) { \
            _XTEST_FAIL_FATAL(__FILE__, __LINE__, \
                "ASSERT_FLOAT_EQ(" #expected ", " #actual ") failed: " \
                "NaN comparison (%.6f vs %.6f)", (double)_e, (double)_a); \
        } else if (fabsf(_e - _a) >= XTEST_FLOAT_EPSILON) { \
            _XTEST_FAIL_FATAL(__FILE__, __LINE__, \
                "ASSERT_FLOAT_EQ(" #expected ", " #actual ") failed: " \
                "expected=%.6f, actual=%.6f, diff=%.6e, epsilon=%.1e", \
                (double)_e, (double)_a, (double)fabsf(_e - _a), (double)XTEST_FLOAT_EPSILON); \
        } \
    } while(0)

/* ------------------------------------------------------------------ */
/*  EXPECT_DOUBLE_EQ / ASSERT_DOUBLE_EQ — double tolerance comparison */
/* ------------------------------------------------------------------ */

#define EXPECT_DOUBLE_EQ(expected, actual) \
    do { \
        double _e = (double)(expected); \
        double _a = (double)(actual); \
        if (_e == _a) { \
        } else if (isnan(_e) || isnan(_a)) { \
            _XTEST_FAIL(__FILE__, __LINE__, \
                "EXPECT_DOUBLE_EQ(" #expected ", " #actual ") failed: " \
                "NaN comparison (%.15g vs %.15g)", _e, _a); \
        } else if (fabs(_e - _a) >= XTEST_DOUBLE_EPSILON) { \
            _XTEST_FAIL(__FILE__, __LINE__, \
                "EXPECT_DOUBLE_EQ(" #expected ", " #actual ") failed: " \
                "expected=%.15g, actual=%.15g, diff=%.15e, epsilon=%.1e", \
                _e, _a, fabs(_e - _a), XTEST_DOUBLE_EPSILON); \
        } \
    } while(0)

#define ASSERT_DOUBLE_EQ(expected, actual) \
    do { \
        double _e = (double)(expected); \
        double _a = (double)(actual); \
        if (_e == _a) { \
        } else if (isnan(_e) || isnan(_a)) { \
            _XTEST_FAIL_FATAL(__FILE__, __LINE__, \
                "ASSERT_DOUBLE_EQ(" #expected ", " #actual ") failed: " \
                "NaN comparison (%.15g vs %.15g)", _e, _a); \
        } else if (fabs(_e - _a) >= XTEST_DOUBLE_EPSILON) { \
            _XTEST_FAIL_FATAL(__FILE__, __LINE__, \
                "ASSERT_DOUBLE_EQ(" #expected ", " #actual ") failed: " \
                "expected=%.15g, actual=%.15g, diff=%.15e, epsilon=%.1e", \
                _e, _a, fabs(_e - _a), XTEST_DOUBLE_EPSILON); \
        } \
    } while(0)

/* ------------------------------------------------------------------ */
/*  Internal array comparison helpers                                  */
/* ------------------------------------------------------------------ */

#define _XTEST_ARRAY_EQ_IMPL(arr1, arr2, size, type, cmp_fn, fmt_spec, eps) \
    do { \
        const type *_a1 = (const type*)(arr1); \
        const type *_a2 = (const type*)(arr2); \
        size_t _sz = (size_t)(size); \
        for (size_t _i = 0; _i < _sz; _i++) { \
            if (!(cmp_fn(_a1[_i], _a2[_i], eps))) { \
                _XTEST_FAIL(__FILE__, __LINE__, \
                    #arr1 "[" fmt_spec "] vs " #arr2 "[" fmt_spec "]: " \
                    "mismatch at index %zu", \
                    _a1[_i], _a2[_i], _i); \
                break; \
            } \
        } \
    } while(0)

#define _XTEST_ARRAY_EQ_FATAL_IMPL(arr1, arr2, size, type, cmp_fn, fmt_spec, eps) \
    do { \
        const type *_a1 = (const type*)(arr1); \
        const type *_a2 = (const type*)(arr2); \
        size_t _sz = (size_t)(size); \
        for (size_t _i = 0; _i < _sz; _i++) { \
            if (!(cmp_fn(_a1[_i], _a2[_i], eps))) { \
                _XTEST_FAIL_FATAL(__FILE__, __LINE__, \
                    #arr1 "[" fmt_spec "] vs " #arr2 "[" fmt_spec "]: " \
                    "mismatch at index %zu", \
                    _a1[_i], _a2[_i], _i); \
                return; \
            } \
        } \
    } while(0)

/* Comparison functions used by array macros */
#define _XTEST_INT_EQ(a, b, eps)     ((a) == (b))
#define _XTEST_INT_NE(a, b, eps)     ((a) != (b))
#define _XTEST_FLOAT_EQ(a, b, eps)   ((a) == (b) || fabsf((a)-(b)) < (float)(eps))
#define _XTEST_FLOAT_NE(a, b, eps)   (!_XTEST_FLOAT_EQ(a, b, eps))
#define _XTEST_DOUBLE_EQ(a, b, eps)  ((a) == (b) || fabs((a)-(b)) < (double)(eps))
#define _XTEST_DOUBLE_NE(a, b, eps)  (!_XTEST_DOUBLE_EQ(a, b, eps))
#define _XTEST_CHAR_EQ(a, b, eps)    ((a) == (b))
#define _XTEST_CHAR_NE(a, b, eps)    ((a) != (b))

/* ------------------------------------------------------------------ */
/*  EXPECT_ARRAY_EQ_INT / ASSERT_ARRAY_EQ_INT                         */
/* ------------------------------------------------------------------ */

#define EXPECT_ARRAY_EQ_INT(arr1, arr2, size) \
    _XTEST_ARRAY_EQ_IMPL(arr1, arr2, size, int, _XTEST_INT_EQ, "%d", 0)

#define ASSERT_ARRAY_EQ_INT(arr1, arr2, size) \
    _XTEST_ARRAY_EQ_FATAL_IMPL(arr1, arr2, size, int, _XTEST_INT_EQ, "%d", 0)

/* ------------------------------------------------------------------ */
/*  EXPECT_ARRAY_NE_INT / ASSERT_ARRAY_NE_INT                         */
/* ------------------------------------------------------------------ */

#define EXPECT_ARRAY_NE_INT(arr1, arr2, size) \
    _XTEST_ARRAY_EQ_IMPL(arr1, arr2, size, int, _XTEST_INT_NE, "%d", 0)

#define ASSERT_ARRAY_NE_INT(arr1, arr2, size) \
    _XTEST_ARRAY_EQ_FATAL_IMPL(arr1, arr2, size, int, _XTEST_INT_NE, "%d", 0)

/* ------------------------------------------------------------------ */
/*  EXPECT_ARRAY_EQ_FLOAT / ASSERT_ARRAY_EQ_FLOAT                      */
/* ------------------------------------------------------------------ */

#define EXPECT_ARRAY_EQ_FLOAT(arr1, arr2, size) \
    _XTEST_ARRAY_EQ_IMPL(arr1, arr2, size, float, _XTEST_FLOAT_EQ, "%.6f", XTEST_FLOAT_EPSILON)

#define ASSERT_ARRAY_EQ_FLOAT(arr1, arr2, size) \
    _XTEST_ARRAY_EQ_FATAL_IMPL(arr1, arr2, size, float, _XTEST_FLOAT_EQ, "%.6f", XTEST_FLOAT_EPSILON)

/* ------------------------------------------------------------------ */
/*  EXPECT_ARRAY_NE_FLOAT / ASSERT_ARRAY_NE_FLOAT                      */
/* ------------------------------------------------------------------ */

#define EXPECT_ARRAY_NE_FLOAT(arr1, arr2, size) \
    _XTEST_ARRAY_EQ_IMPL(arr1, arr2, size, float, _XTEST_FLOAT_NE, "%.6f", XTEST_FLOAT_EPSILON)

#define ASSERT_ARRAY_NE_FLOAT(arr1, arr2, size) \
    _XTEST_ARRAY_EQ_FATAL_IMPL(arr1, arr2, size, float, _XTEST_FLOAT_NE, "%.6f", XTEST_FLOAT_EPSILON)

/* ------------------------------------------------------------------ */
/*  EXPECT_ARRAY_EQ_DOUBLE / ASSERT_ARRAY_EQ_DOUBLE                     */
/* ------------------------------------------------------------------ */

#define EXPECT_ARRAY_EQ_DOUBLE(arr1, arr2, size) \
    _XTEST_ARRAY_EQ_IMPL(arr1, arr2, size, double, _XTEST_DOUBLE_EQ, "%.15g", XTEST_DOUBLE_EPSILON)

#define ASSERT_ARRAY_EQ_DOUBLE(arr1, arr2, size) \
    _XTEST_ARRAY_EQ_FATAL_IMPL(arr1, arr2, size, double, _XTEST_DOUBLE_EQ, "%.15g", XTEST_DOUBLE_EPSILON)

/* ------------------------------------------------------------------ */
/*  EXPECT_ARRAY_NE_DOUBLE / ASSERT_ARRAY_NE_DOUBLE                    */
/* ------------------------------------------------------------------ */

#define EXPECT_ARRAY_NE_DOUBLE(arr1, arr2, size) \
    _XTEST_ARRAY_EQ_IMPL(arr1, arr2, size, double, _XTEST_DOUBLE_NE, "%.15g", XTEST_DOUBLE_EPSILON)

#define ASSERT_ARRAY_NE_DOUBLE(arr1, arr2, size) \
    _XTEST_ARRAY_EQ_FATAL_IMPL(arr1, arr2, size, double, _XTEST_DOUBLE_NE, "%.15g", XTEST_DOUBLE_EPSILON)

/* ------------------------------------------------------------------ */
/*  EXPECT_ARRAY_EQ_CHAR / ASSERT_ARRAY_EQ_CHAR                       */
/* ------------------------------------------------------------------ */

#define EXPECT_ARRAY_EQ_CHAR(arr1, arr2, size) \
    _XTEST_ARRAY_EQ_IMPL(arr1, arr2, size, char, _XTEST_CHAR_EQ, "%c", 0)

#define ASSERT_ARRAY_EQ_CHAR(arr1, arr2, size) \
    _XTEST_ARRAY_EQ_FATAL_IMPL(arr1, arr2, size, char, _XTEST_CHAR_EQ, "%c", 0)

/* ------------------------------------------------------------------ */
/*  EXPECT_ARRAY_NE_CHAR / ASSERT_ARRAY_NE_CHAR                       */
/* ------------------------------------------------------------------ */

#define EXPECT_ARRAY_NE_CHAR(arr1, arr2, size) \
    _XTEST_ARRAY_EQ_IMPL(arr1, arr2, size, char, _XTEST_CHAR_NE, "%c", 0)

#define ASSERT_ARRAY_NE_CHAR(arr1, arr2, size) \
    _XTEST_ARRAY_EQ_FATAL_IMPL(arr1, arr2, size, char, _XTEST_CHAR_NE, "%c", 0)

/* ------------------------------------------------------------------ */
/*  TEST_BENCH — benchmark a section of code                           */
/* ------------------------------------------------------------------ */

#define TEST_BENCH(name, iterations) \
    static void _xtest_bench_fn_##name(void);                            \
    static xtest_case _xtest_bench_case_##name XTEST_SECTION = {         \
        "[BENCH]", #name, _xtest_bench_fn_##name, XTEST_FLAG_NORMAL, 0   \
    };                                                                    \
    static void _xtest_bench_fn_##name(void)

/* ------------------------------------------------------------------ */
/*  TEST_LOG — debug logging during test execution                     */
/* ------------------------------------------------------------------ */

#define TEST_LOG(fmt, ...) \
    fprintf(stderr, "[LOG     ] " fmt "\n", ##__VA_ARGS__)

/* ======================================================================== */
/* Runner                                                                   */
/* ======================================================================== */

/* === RUNNER === */

int xtest_run(int argc, char** argv);

/* Run a single benchmark iteration loop */
static inline long xtest_bench_run(void (*fn)(void), int iterations) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int _i = 0; _i < iterations; _i++) {
        fn();
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    long elapsed_ns = (end.tv_sec - start.tv_sec) * 1000000000L +
                      (end.tv_nsec - start.tv_nsec);
    return elapsed_ns / 1000000; /* return milliseconds */
}

/* Get the exit code a child process should use based on failure count */
static inline int xtest_child_exit_code(void) {
    return (_xtest_failure_count > 0) ? XTEST_EXIT_FAIL : XTEST_EXIT_PASS;
}

/* Crash handler with sigaction + sigaltstack */
static void _xtest_sigaction_handler(int sig, siginfo_t *info, void *ctx) {
    (void)ctx;
    const char *name = sig == SIGSEGV ? "SIGSEGV" :
                       sig == SIGABRT ? "SIGABRT" :
                       sig == SIGFPE  ? "SIGFPE"  :
                       sig == SIGILL  ? "SIGILL"   :
                       sig == SIGBUS  ? "SIGBUS"   : "UNKNOWN";
    /* Write crash info using only async-signal-safe write() */
    char buf[256];
    int len = snprintf(buf, sizeof(buf), "[  CRASHED ] signal=%d (%s), addr=%p\n",
                       sig, name, info ? info->si_addr : NULL);
    write(STDERR_FILENO, buf, len);
    _exit(XTEST_EXIT_CRASH);
}

/* Install crash signal handlers (sigaction + sigaltstack) */
static inline void xtest_install_crash_handlers(void) {
    static char alt_stack[16384];
    stack_t ss = { .ss_sp = alt_stack, .ss_size = sizeof(alt_stack), .ss_flags = 0 };
    sigaltstack(&ss, NULL);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = _xtest_sigaction_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
}

/* Timeout support */
static volatile sig_atomic_t _xtest_timed_out = 0;
static void _xtest_alarm_handler(int sig) {
    (void)sig;
    _xtest_timed_out = 1;
    _exit(XTEST_EXIT_TIMEOUT);
}

static inline void xtest_set_timeout(int seconds) {
    if (seconds <= 0) return;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = _xtest_alarm_handler;
    sa.sa_flags = SA_RESETHAND;  /* one-shot */
    sigaction(SIGALRM, &sa, NULL);
    alarm(seconds);
}

/* ======================================================================== */
/* Registration                                                             */
/* ======================================================================== */

/* Put test case structs into a dedicated ELF section.
   GNU ld auto-generates __start_xtest_suites / __stop_xtest_suites symbols. */
#define XTEST_SECTION __attribute__((section("xtest_suites"), used, aligned(sizeof(void*))))

/* Declare the linker-generated section boundary symbols.
   Use weak attribute so they default to NULL when the section is empty
   (no TEST() invocations in the entire program). */
extern xtest_case __start_xtest_suites[] __attribute__((weak));
extern xtest_case __stop_xtest_suites[] __attribute__((weak));

/* Get the array of all registered test cases */
static inline xtest_case *xtest_get_tests(void) {
    return __start_xtest_suites;
}

/* Get the count of registered test cases */
static inline int xtest_count_tests(void) {
    return (int)(__stop_xtest_suites - __start_xtest_suites);
}

/* ======================================================================== */
/* Test Registration Macros                                                  */
/* ======================================================================== */

/* TEST_DEFINE_FIXTURE — define a fixture descriptor for use with TEST_F */
#define TEST_DEFINE_FIXTURE(name, setup_fn, teardown_fn) \
    static const xtest_fixture_desc name = {              \
        .set_up    = (setup_fn),                          \
        .tear_down = (teardown_fn)                        \
    }

/* TEST — basic test with no fixture */
#define TEST(suite, name)                                                   \
    static void _xtest_fn_##suite##_##name(void);                           \
    static xtest_case _xtest_case_##suite##_##name XTEST_SECTION = {        \
        #suite, #name, _xtest_fn_##suite##_##name, XTEST_FLAG_NORMAL, 0     \
    };                                                                      \
    static void _xtest_fn_##suite##_##name(void)

/* DISABLED_TEST — skipped test */
#define DISABLED_TEST(suite, name)                                          \
    static void _xtest_fn_##suite##_##name(void);                           \
    static xtest_case _xtest_case_##suite##_##name XTEST_SECTION = {        \
        #suite, #name, _xtest_fn_##suite##_##name, XTEST_FLAG_DISABLED, 0   \
    };                                                                      \
    static void _xtest_fn_##suite##_##name(void)

/* FAIL_TEST — expected failure */
#define FAIL_TEST(suite, name)                                              \
    static void _xtest_fn_##suite##_##name(void);                           \
    static xtest_case _xtest_case_##suite##_##name XTEST_SECTION = {        \
        #suite, #name, _xtest_fn_##suite##_##name, XTEST_FLAG_XFAIL, 0      \
    };                                                                      \
    static void _xtest_fn_##suite##_##name(void)

/* TEST_F — test with fixture via descriptor
 *
 * Generates a wrapper that calls set_up -> body -> tear_down automatically.
 * If set_up triggers an ASSERT (which increments _xtest_failure_count and
 * returns), the wrapper returns early so tear_down is NOT called.
 * If the body ASSERT fails, tear_down STILL runs for cleanup.
 * NULL set_up or tear_down function pointers are safely skipped.          */
#define TEST_F(suite, name, fixture_desc)                                         \
    static void _xtest_body_##suite##_##name(void);                               \
    static void _xtest_fn_##suite##_##name(void) {                                \
        int _failures_before = _xtest_failure_count;                              \
        if ((fixture_desc).set_up) (fixture_desc).set_up();                       \
        if (_xtest_failure_count > _failures_before) return;                      \
        _xtest_body_##suite##_##name();                                           \
        if ((fixture_desc).tear_down) (fixture_desc).tear_down();                 \
    }                                                                             \
    static xtest_case _xtest_case_##suite##_##name XTEST_SECTION = {              \
        #suite, #name, _xtest_fn_##suite##_##name, XTEST_FLAG_NORMAL,             \
        &(fixture_desc)                                                           \
    };                                                                            \
    static void _xtest_body_##suite##_##name(void)

#ifdef __cplusplus
}
#endif

#endif /* XTEST_H */
