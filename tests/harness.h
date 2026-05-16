/*
 * harness.h — Minimal test harness for csplayer unit and integration tests.
 *
 * Usage:
 *   TEST_BEGIN("test_name");
 *   CHECK(condition);
 *   CHECK_NEAR(a, b, epsilon);
 *   TEST_END();
 *
 * g_passed and g_failed are defined in test_main.c.
 */

#ifndef CSPLAYER_TEST_HARNESS_H
#define CSPLAYER_TEST_HARNESS_H

#include <stdio.h>
#include <time.h>
#include <math.h>
#include "types.h"

extern i32         g_passed;
extern i32         g_failed;
extern const char *g_current_test;
extern i32         g_test_failed;
extern clock_t     g_test_clock;

#define TEST_BEGIN(name) do { \
    g_current_test = (name); \
    g_test_failed  = 0; \
    g_test_clock   = clock(); \
} while (0)

#define TEST_END() do { \
    f64 ms = 1000.0 * (f64)(clock() - g_test_clock) / (f64)CLOCKS_PER_SEC; \
    if (g_test_failed) { \
        g_failed++; \
    } else { \
        g_passed++; \
        printf("  PASS  %-52s  (%.3f ms)\n", g_current_test, ms); \
    } \
    fflush(stdout); \
} while (0)

#define CHECK(cond) do { \
    if (!(cond)) { \
        if (!g_test_failed) \
            printf("  FAIL  %-52s  [%s:%d]  " #cond "\n", \
                   g_current_test, __FILE__, __LINE__); \
        g_test_failed = 1; \
    } \
} while (0)

#define CHECK_NEAR(a, b, eps) \
    CHECK(fabsf((f32)(a) - (f32)(b)) < (f32)(eps))

#endif /* CSPLAYER_TEST_HARNESS_H */
