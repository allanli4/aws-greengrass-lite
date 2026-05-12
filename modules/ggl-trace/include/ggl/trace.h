// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_TRACE_H
#define GGL_TRACE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t trace_id;
    uint32_t span_id;
    uint32_t parent_span_id;
} GglTraceCtx;

/// Get the current thread's trace context.
GglTraceCtx ggl_trace_get(void);

/// Set the current thread's trace context.
void ggl_trace_set(GglTraceCtx ctx);

/// Clear the current thread's trace context.
void ggl_trace_clear(void);

/// Check if a trace is active on the current thread.
bool ggl_trace_active(void);

/// Begin a new root trace span.
GglTraceCtx ggl_trace_begin(void);

/// Begin a child span from the current trace context.
GglTraceCtx ggl_trace_child(void);

/// Enter a child span (mutates TLS). Returns previous context for restore.
GglTraceCtx ggl_trace_child_enter(void);

/// Restore a previously saved trace context.
void ggl_trace_exit(GglTraceCtx saved);

#endif
