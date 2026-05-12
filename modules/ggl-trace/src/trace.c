// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <ggl/trace.h>
#include <stdatomic.h>
#include <time.h>

static _Thread_local GglTraceCtx current_ctx;
static _Thread_local bool ctx_active;

static _Atomic uint32_t seed_counter;

static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static uint32_t gen_id(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
    uint32_t state = (uint32_t) ts.tv_nsec
        ^ ((uint32_t) ts.tv_sec << 16)
        ^ atomic_fetch_add(&seed_counter, 1);
    if (state == 0) {
        state = 1;
    }
    return xorshift32(&state);
}

GglTraceCtx ggl_trace_get(void) {
    return current_ctx;
}

void ggl_trace_set(GglTraceCtx ctx) {
    current_ctx = ctx;
    ctx_active = true;
}

void ggl_trace_clear(void) {
    current_ctx = (GglTraceCtx) { 0 };
    ctx_active = false;
}

bool ggl_trace_active(void) {
    return ctx_active;
}

GglTraceCtx ggl_trace_begin(void) {
    GglTraceCtx ctx = {
        .trace_id = gen_id(),
        .span_id = gen_id(),
        .parent_span_id = 0,
    };
    current_ctx = ctx;
    ctx_active = true;
    return ctx;
}

GglTraceCtx ggl_trace_child(void) {
    GglTraceCtx ctx = {
        .trace_id = current_ctx.trace_id,
        .span_id = gen_id(),
        .parent_span_id = current_ctx.span_id,
    };
    return ctx;
}

GglTraceCtx ggl_trace_child_enter(void) {
    GglTraceCtx saved = current_ctx;
    current_ctx = (GglTraceCtx) {
        .trace_id = current_ctx.trace_id,
        .span_id = gen_id(),
        .parent_span_id = current_ctx.span_id,
    };
    return saved;
}

void ggl_trace_exit(GglTraceCtx saved) {
    current_ctx = saved;
}
