#include "xtest.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

/* ======================================================================== */
/*  Color output support                                                     */
/* ======================================================================== */

static int g_color = 1;
static int g_timeout_sec = 0;

/* Wrap a format string with ANSI color if color output is enabled.
   Usage: printf(COLOR(XTEST_COLOR_GREEN, "[  PASSED  ] %d test(s)\n"), n); */
#define COLOR(c, fmt)  (g_color ? c fmt XTEST_COLOR_RESET : fmt)

/* ======================================================================== */
/*  Example tests                                                             */
/* ======================================================================== */
/*
TEST(math, add) { EXPECT_EQ(1+1, 2); }
TEST(math, sub) { EXPECT_EQ(5-2, 3); }

// Example DISABLED test
DISABLED_TEST(math, multiply) { EXPECT_EQ(2*3, 6); }

// Example FAIL_TEST — expected to fail
FAIL_TEST(math, intentional_fail) { EXPECT_EQ(1, 2); }

// Example TEST_F with fixture descriptor
// example fixture — no setup needed
static void math_fixture_setup(void)    {}
// example fixture — no teardown needed
static void math_fixture_teardown(void) {}
TEST_DEFINE_FIXTURE(math_fxt, math_fixture_setup, math_fixture_teardown);

TEST_F(math_fixture, with_setup, math_fxt) {
    EXPECT_TRUE(1);
}
*/

/* ======================================================================== */
/*  Runner                                                                    */
/* ======================================================================== */

/* Per-test result codes for XML output */
#define XTEST_R_OK      0
#define XTEST_R_FAIL    1
#define XTEST_R_CRASH   2
#define XTEST_R_SKIP    3
#define XTEST_R_TIMEOUT 4

typedef struct {
    int repeat_count;
    int output_xml_stdout;
    const char *output_xml_file;
    int parallel_workers;
    int no_fork_mode;
} xxtest_runner_options;

typedef struct {
    xtest_case *tests;
    int count;
    int *test_results;
    int passed;
    int failed;
    int skipped;
    int xfailed;
    int xpassed;
    int crashed;
    int timeout_count;
} xtest_run_state;

static int xtest_detect_cpus(void);

static void xtest_write_xml_output(FILE *f, xtest_case *tests, int count, int *results) {
    int failures = 0, errors = 0, skipped = 0;
    for (int i = 0; i < count; i++) {
        if (results[i] == XTEST_R_FAIL) failures++;
        else if (results[i] == XTEST_R_CRASH) errors++;
        else if (results[i] == XTEST_R_TIMEOUT) errors++;
        else if (results[i] == XTEST_R_SKIP) skipped++;
    }
    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f, "<testsuites name=\"xtest\" tests=\"%d\" failures=\"%d\" errors=\"%d\" skipped=\"%d\">\n",
            count, failures, errors, skipped);
    fprintf(f, "  <testsuite name=\"all\" tests=\"%d\">\n", count);
    for (int i = 0; i < count; i++) {
        if (tests[i].flags & XTEST_FLAG_DISABLED) continue;
        fprintf(f, "    <testcase name=\"%s.%s\"", tests[i].suite_name, tests[i].test_name);
        switch (results[i]) {
        case XTEST_R_OK:    fprintf(f, " />\n"); break;
        case XTEST_R_FAIL:  fprintf(f, ">\n      <failure message=\"test failed\"/>\n    </testcase>\n"); break;
        case XTEST_R_CRASH: fprintf(f, ">\n      <error message=\"test crashed\" type=\"crash\"/>\n    </testcase>\n"); break;
                    case XTEST_R_SKIP:    fprintf(f, ">\n      <skipped/>\n    </testcase>\n"); break;
                    case XTEST_R_TIMEOUT: fprintf(f, ">\n      <error message=\"test timed out\" type=\"timeout\"/>\n    </testcase>\n"); break;
        }
    }
    fprintf(f, "  </testsuite>\n");
    fprintf(f, "</testsuites>\n");
}

static void xtest_print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("  --list              List all tests and exit\n");
    printf("  --output=xml[:FILE] Output JUnit XML (to file or stdout)\n");
    printf("  --verbose, -v       Verbose output (per-test details)\n");
    printf("  --repeat=N          Repeat tests N times\n");
    printf("  --parallel=N        Run N tests in parallel (0=auto)\n");
    printf("  --timeout=N         Per-test timeout in seconds (0=no limit)\n");
    printf("  --no-fork           Run tests in-process (for debugging)\n");
    printf("  --color=yes|no|auto Force/disable/auto color output\n");
    printf("  --help, -h          Show this help\n");
}

static int xtest_try_parse_color_arg(const char *arg) {
    if (strcmp(arg, "--color=yes") == 0) {
        g_color = 1;
        return 1;
    }
    if (strcmp(arg, "--color=no") == 0) {
        g_color = 0;
        return 1;
    }
    if (strcmp(arg, "--color=auto") == 0) {
        g_color = isatty(STDOUT_FILENO);
        return 1;
    }
    return 0;
}

static int xtest_try_handle_immediate_option(const char *arg, const char *prog) {
    if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
        xtest_print_usage(prog);
        return 1;
    }
    if (strcmp(arg, "--list") == 0) {
        int count = xtest_count_tests();
        xtest_case *tests = xtest_get_tests();
        for (int i = 0; i < count; i++) {
            const char *flag = "";
            if (tests[i].flags & XTEST_FLAG_DISABLED) {
                flag = " [DISABLED]";
            } else if (tests[i].flags & XTEST_FLAG_XFAIL) {
                flag = " [XFAIL]";
            }
            printf("%s.%s%s\n", tests[i].suite_name, tests[i].test_name, flag);
        }
        return 1;
    }
    return 0;
}

static void xtest_init_runner_options(xxtest_runner_options *opts) {
    opts->repeat_count = 1;
    opts->output_xml_stdout = 0;
    opts->output_xml_file = NULL;
    opts->parallel_workers = 0;
    opts->no_fork_mode = 0;
}

static int xtest_parse_cli_args(int argc, char **argv, xxtest_runner_options *opts) {
    for (int i = 1; i < argc; i++) {
        if (xtest_try_parse_color_arg(argv[i])) {
            continue;
        }
        if (xtest_try_handle_immediate_option(argv[i], argv[0])) {
            return 1;
        }
        if (strncmp(argv[i], "--output=xml", 12) == 0) {
            if (argv[i][12] == ':') {
                opts->output_xml_file = argv[i] + 13;
            } else {
                opts->output_xml_stdout = 1;
            }
            continue;
        }
        if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            continue;
        }
        if (strncmp(argv[i], "--repeat=", 9) == 0) {
            opts->repeat_count = atoi(argv[i] + 9);
            if (opts->repeat_count < 1) {
                opts->repeat_count = 1;
            }
            continue;
        }
        if (strncmp(argv[i], "--parallel=", 11) == 0) {
            opts->parallel_workers = atoi(argv[i] + 11);
            if (opts->parallel_workers < 0) {
                fprintf(stderr, "ERROR: --parallel must be >= 0\n");
                return -1;
            }
            continue;
        }
        if (strncmp(argv[i], "--timeout=", 10) == 0) {
            g_timeout_sec = atoi(argv[i] + 10);
            if (g_timeout_sec < 0) {
                g_timeout_sec = 0;
            }
            continue;
        }
        if (strcmp(argv[i], "--no-fork") == 0) {
            opts->no_fork_mode = 1;
            continue;
        }
    }
    return 0;
}

static int xtest_is_disabled(const xtest_case *test) {
    return (test->flags & XTEST_FLAG_DISABLED) != 0;
}

static int xtest_is_bench(const xtest_case *test) {
    return strcmp(test->suite_name, "[BENCH]") == 0;
}

static void xtest_print_run(const xtest_case *test) {
    printf(COLOR(XTEST_COLOR_GREEN, "[ RUN      ] %s.%s\n"), test->suite_name, test->test_name);
}

static void xtest_record_skip(xtest_run_state *state, int idx) {
    printf(COLOR(XTEST_COLOR_YELLOW, "[  SKIPPED ] %s.%s\n"),
           state->tests[idx].suite_name, state->tests[idx].test_name);
    state->skipped++;
    state->test_results[idx] = XTEST_R_SKIP;
}

static void xtest_record_bench(xtest_run_state *state, int idx) {
    xtest_print_run(&state->tests[idx]);
    long elapsed = xtest_bench_run(state->tests[idx].fn, 1000000);
    printf(COLOR(XTEST_COLOR_CYAN, "[  BENCH  ] %s.%s — %d iters, %ld ms total, %.3f us/iter\n"),
           state->tests[idx].suite_name, state->tests[idx].test_name,
           1000000, elapsed, (double)elapsed * 1000.0 / 1000000.0);
    state->passed++;
    state->test_results[idx] = XTEST_R_OK;
}

static void xtest_record_pass_fail(xtest_run_state *state, int idx, int child_passed) {
    const xtest_case *test = &state->tests[idx];
    if (test->flags & XTEST_FLAG_XFAIL) {
        if (child_passed) {
            printf(COLOR(XTEST_COLOR_RED, "[  XPASS  ] %s.%s\n"), test->suite_name, test->test_name);
            state->xpassed++;
            state->test_results[idx] = XTEST_R_FAIL;
        } else {
            printf(COLOR(XTEST_COLOR_YELLOW, "[  XFAIL  ] %s.%s\n"), test->suite_name, test->test_name);
            state->xfailed++;
            state->test_results[idx] = XTEST_R_OK;
        }
        return;
    }

    if (child_passed) {
        printf(COLOR(XTEST_COLOR_GREEN, "[       OK ] %s.%s\n"), test->suite_name, test->test_name);
        state->passed++;
        state->test_results[idx] = XTEST_R_OK;
    } else {
        printf(COLOR(XTEST_COLOR_RED, "[  FAILED  ] %s.%s\n"), test->suite_name, test->test_name);
        state->failed++;
        state->test_results[idx] = XTEST_R_FAIL;
    }
}

static void xtest_record_crash(xtest_run_state *state, int idx, int sig) {
    printf(COLOR(XTEST_COLOR_RED, "[  CRASHED ] %s.%s — signal=%d (%s)\n"),
           state->tests[idx].suite_name, state->tests[idx].test_name, sig,
           sig == SIGSEGV ? "SIGSEGV" :
           sig == SIGABRT ? "SIGABRT" : "UNKNOWN");
    state->crashed++;
    state->test_results[idx] = XTEST_R_CRASH;
}

static void xtest_record_timeout(xtest_run_state *state, int idx) {
    printf(COLOR(XTEST_COLOR_YELLOW, "[  TIMEOUT ] %s.%s\n"),
           state->tests[idx].suite_name, state->tests[idx].test_name);
    state->timeout_count++;
    state->test_results[idx] = XTEST_R_TIMEOUT;
}

static pid_t xtest_spawn_child(const xtest_case *test) {
    pid_t pid = fork();
    if (pid == 0) {
        xtest_install_crash_handlers();
        xtest_set_timeout(g_timeout_sec);
        test->fn();
        _exit(xtest_child_exit_code());
    }
    return pid;
}

static void xtest_run_in_process(xtest_run_state *state, int idx) {
    _xtest_failure_count = 0;
    state->tests[idx].fn();
    xtest_record_pass_fail(state, idx, _xtest_failure_count == 0);
}

static void xtest_handle_wait_status(xtest_run_state *state, int idx, int status) {
    if (WIFEXITED(status)) {
        int ec = WEXITSTATUS(status);
        if (ec == XTEST_EXIT_TIMEOUT) {
            xtest_record_timeout(state, idx);
            return;
        }
        xtest_record_pass_fail(state, idx, ec == XTEST_EXIT_PASS);
        return;
    }
    if (WIFSIGNALED(status)) {
        xtest_record_crash(state, idx, WTERMSIG(status));
        return;
    }
    xtest_record_pass_fail(state, idx, 0);
}

static void xtest_run_sequential_iteration(xtest_run_state *state, int no_fork_mode) {
    for (int i = 0; i < state->count; i++) {
        if (xtest_is_disabled(&state->tests[i])) {
            xtest_record_skip(state, i);
            continue;
        }
        if (xtest_is_bench(&state->tests[i])) {
            xtest_record_bench(state, i);
            continue;
        }

        xtest_print_run(&state->tests[i]);
        if (no_fork_mode) {
            xtest_run_in_process(state, i);
            continue;
        }

        pid_t pid = xtest_spawn_child(&state->tests[i]);
        if (pid < 0) {
            fprintf(stderr, "WARNING: fork() failed for %s.%s, running in-process\n",
                    state->tests[i].suite_name, state->tests[i].test_name);
            xtest_run_in_process(state, i);
            continue;
        }

        int status = 0;
        waitpid(pid, &status, 0);
        xtest_handle_wait_status(state, i, status);
    }
}

typedef struct {
    pid_t pid;
    int idx;
} xtest_child_slot;

static void xtest_run_parallel_iteration(xtest_run_state *state, int workers) {
    int *work_queue = malloc((size_t)state->count * sizeof(int));
    int work_count = 0;
    for (int i = 0; i < state->count; i++) {
        if (xtest_is_disabled(&state->tests[i])) {
            xtest_record_skip(state, i);
        } else if (xtest_is_bench(&state->tests[i])) {
            xtest_record_bench(state, i);
        } else {
            work_queue[work_count++] = i;
        }
    }

    if (work_count == 0) {
        free(work_queue);
        return;
    }

    xtest_child_slot *slots = calloc((size_t)workers, sizeof(xtest_child_slot));
    int next_wq = 0;
    int running = 0;

    while (next_wq < work_count || running > 0) {
        while (running < workers && next_wq < work_count) {
            int ti = work_queue[next_wq];
            pid_t pid = xtest_spawn_child(&state->tests[ti]);
            if (pid > 0) {
                xtest_print_run(&state->tests[ti]);
                slots[running].pid = pid;
                slots[running].idx = ti;
                running++;
                next_wq++;
            } else {
                fprintf(stderr, "WARNING: fork() failed for %s.%s, running in-process\n",
                        state->tests[ti].suite_name, state->tests[ti].test_name);
                xtest_run_in_process(state, ti);
                next_wq++;
            }
        }

        if (running > 0) {
            int status = 0;
            pid_t done = waitpid(-1, &status, 0);
            if (done > 0) {
                int slot_idx = -1;
                for (int j = 0; j < running; j++) {
                    if (slots[j].pid == done) {
                        slot_idx = j;
                        break;
                    }
                }
                if (slot_idx >= 0) {
                    xtest_handle_wait_status(state, slots[slot_idx].idx, status);
                    slots[slot_idx] = slots[--running];
                }
            }
        }
    }

    free(slots);
    free(work_queue);
}

static int xtest_resolve_parallel_workers(int requested_workers, int no_fork_mode) {
    if (no_fork_mode) {
        return 1;
    }
    if (requested_workers == 0) {
        requested_workers = xtest_detect_cpus();
    }
    if (requested_workers < 1) {
        requested_workers = 1;
    }
    return requested_workers;
}

static void xtest_run_all_tests(xtest_run_state *state, const xxtest_runner_options *opts) {
    int workers = xtest_resolve_parallel_workers(opts->parallel_workers, opts->no_fork_mode);
    for (int r = 0; r < opts->repeat_count; r++) {
        if (opts->no_fork_mode) {
            xtest_run_sequential_iteration(state, 1);
        } else if (workers > 1) {
            xtest_run_parallel_iteration(state, workers);
        } else {
            xtest_run_sequential_iteration(state, 0);
        }
    }
}

static void xtest_print_summary(const xtest_run_state *state) {
    int total = state->passed + state->failed + state->skipped +
                state->xfailed + state->xpassed + state->crashed + state->timeout_count;
    printf(COLOR(XTEST_COLOR_GREEN, "[==========] %d test(s) ran.\n"), total);
    printf(COLOR(XTEST_COLOR_GREEN, "[  PASSED  ] %d test(s)\n"), state->passed);
    if (state->skipped > 0) {
        printf(COLOR(XTEST_COLOR_YELLOW, "[  SKIPPED ] %d test(s)\n"), state->skipped);
    }
    if (state->xfailed > 0) {
        printf(COLOR(XTEST_COLOR_YELLOW, "[  XFAIL  ] %d test(s)\n"), state->xfailed);
    }
    if (state->xpassed > 0) {
        printf(COLOR(XTEST_COLOR_RED, "[  XPASS  ] %d test(s)\n"), state->xpassed);
    }
    if (state->crashed > 0) {
        printf(COLOR(XTEST_COLOR_RED, "[  CRASHED ] %d test(s)\n"), state->crashed);
    }
    if (state->timeout_count > 0) {
        printf(COLOR(XTEST_COLOR_YELLOW, "[  TIMEOUT ] %d test(s)\n"), state->timeout_count);
    }
    printf(COLOR(XTEST_COLOR_RED, "[  FAILED  ] %d test(s)\n"), state->failed + state->xpassed);
}

static void xtest_write_xml_if_requested(const xxtest_runner_options *opts, xtest_run_state *state) {
    if (opts->output_xml_stdout) {
        xtest_write_xml_output(stdout, state->tests, state->count, state->test_results);
    }
    if (opts->output_xml_file) {
        FILE *f = fopen(opts->output_xml_file, "w");
        if (f) {
            xtest_write_xml_output(f, state->tests, state->count, state->test_results);
            fclose(f);
        } else {
            fprintf(stderr, "ERROR: Cannot open XML output file: %s\n", opts->output_xml_file);
        }
    }
}

/* ======================================================================== */
/*  CPU detection (sysconf)                                                  */
/* ======================================================================== */

static int xtest_detect_cpus(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (int)n : 1;
}

/* Global failure counter definition (extern in xtest.h) */
int _xtest_failure_count = 0;

int main(int argc, char** argv) {
    g_color = isatty(STDOUT_FILENO);
    xxtest_runner_options opts;
    xtest_init_runner_options(&opts);
    int parse_status = xtest_parse_cli_args(argc, argv, &opts);
    if (parse_status != 0) {
        return parse_status > 0 ? 0 : 1;
    }

    int count = xtest_count_tests();
    xtest_case *tests = xtest_get_tests();

    printf(COLOR(XTEST_COLOR_GREEN, "[==========] Running %d test(s)\n"), count);

    xtest_run_state state;
    memset(&state, 0, sizeof(state));
    state.tests = tests;
    state.count = count;
    state.test_results = calloc((size_t)count, sizeof(int));

    xtest_run_all_tests(&state, &opts);
    xtest_print_summary(&state);
    xtest_write_xml_if_requested(&opts, &state);

    int exit_code = (state.failed + state.xpassed + state.crashed + state.timeout_count > 0) ? 1 : 0;
    free(state.test_results);
    return exit_code;
}
