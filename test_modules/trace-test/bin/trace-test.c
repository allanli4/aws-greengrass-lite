// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <ggl/trace.h>
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static int failures = 0;

static void test_begin(void) {
    ggl_trace_clear();
    GglTraceCtx ctx = ggl_trace_begin();
    bool pass = (ctx.trace_id != 0) && (ctx.span_id != 0)
        && (ctx.parent_span_id == 0);
    printf("test_begin: %s\n", pass ? "PASS" : "FAIL");
    if (!pass) {
        failures++;
    }
}

static void test_child(void) {
    ggl_trace_clear();
    GglTraceCtx parent = ggl_trace_begin();
    GglTraceCtx child = ggl_trace_child();
    bool pass = (child.trace_id == parent.trace_id)
        && (child.parent_span_id == parent.span_id)
        && (child.span_id != parent.span_id) && (child.span_id != 0);
    printf("test_child: %s\n", pass ? "PASS" : "FAIL");
    if (!pass) {
        failures++;
    }
}

static void test_clear(void) {
    ggl_trace_begin();
    assert(ggl_trace_active());
    ggl_trace_clear();
    GglTraceCtx ctx = ggl_trace_get();
    bool pass = (ctx.trace_id == 0) && (ctx.span_id == 0)
        && (ctx.parent_span_id == 0) && !ggl_trace_active();
    printf("test_clear: %s\n", pass ? "PASS" : "FAIL");
    if (!pass) {
        failures++;
    }
}

static void *thread_func(void *arg) {
    (void) arg;
    GglTraceCtx ctx = ggl_trace_get();
    bool isolated = (ctx.trace_id == 0) && (ctx.span_id == 0)
        && (ctx.parent_span_id == 0) && !ggl_trace_active();
    if (isolated) {
        ggl_trace_begin();
    }
    return (void *) (uintptr_t) (isolated ? 1 : 0);
}

static void test_tls_isolation(void) {
    ggl_trace_clear();
    GglTraceCtx main_ctx = ggl_trace_begin();

    pthread_t tid;
    pthread_create(&tid, NULL, thread_func, NULL);
    void *ret;
    pthread_join(tid, &ret);

    bool thread_isolated = ((uintptr_t) ret == 1);
    GglTraceCtx after = ggl_trace_get();
    bool main_unchanged = (after.trace_id == main_ctx.trace_id)
        && (after.span_id == main_ctx.span_id);

    bool pass = thread_isolated && main_unchanged;
    printf("test_tls_isolation: %s\n", pass ? "PASS" : "FAIL");
    if (!pass) {
        failures++;
    }
}

static void test_performance(void) {
    struct timespec start;
    struct timespec end;
    const int iterations = 1000000;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < iterations; i++) {
        ggl_trace_begin();
        ggl_trace_child();
        ggl_trace_get();
        ggl_trace_clear();
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    long elapsed_ns = (end.tv_sec - start.tv_sec) * 1000000000L
        + (end.tv_nsec - start.tv_nsec);
    long avg_ns = elapsed_ns / iterations;

    bool pass = (avg_ns < 200);
    printf(
        "test_performance: %s (avg %ld ns/iter)\n",
        pass ? "PASS" : "FAIL",
        avg_ns
    );
    if (!pass) {
        failures++;
    }
}

int main(void) {
    test_begin();
    test_child();
    test_clear();
    test_tls_isolation();
    test_performance();

    printf("\nResult: %d failures\n", failures);
    return (failures > 0) ? 1 : 0;
}
