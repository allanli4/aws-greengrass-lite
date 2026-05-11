// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <ggl/trace.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

// __real_gg_log is the original gg_log from gg-sdk, made available by
// the linker's --wrap=gg_log flag.
void __real_gg_log(
    uint32_t level,
    const char *file,
    int line,
    const char *tag,
    const char *format,
    ...
);

// __wrap_gg_log intercepts all calls to gg_log. It prints the trace
// context prefix, then delegates to the real implementation.
void __wrap_gg_log(
    uint32_t level,
    const char *file,
    int line,
    const char *tag,
    const char *format,
    ...
) {
    if (ggl_trace_active()) {
        GglTraceCtx ctx = ggl_trace_get();
        char new_format[512];
        int prefix_len = snprintf(
            new_format,
            sizeof(new_format),
            "[T=%08x S=%08x P=%08x] %s",
            ctx.trace_id,
            ctx.span_id,
            ctx.parent_span_id,
            format
        );
        if (prefix_len > 0 && (size_t) prefix_len < sizeof(new_format)) {
            va_list args;
            va_start(args, format);
            // Call real gg_log with modified format that includes trace prefix.
            // We can't forward varargs, so we vsnprintf into a buffer and pass
            // that as a pre-formatted string.
            char msg[1024];
            vsnprintf(msg, sizeof(msg), format, args);
            va_end(args);
            __real_gg_log(level, file, line, tag, "[T=%08x S=%08x P=%08x] %s",
                ctx.trace_id, ctx.span_id, ctx.parent_span_id, msg);
            return;
        }
    }

    // No trace active or format too long — pass through to real gg_log.
    va_list args;
    va_start(args, format);
    char msg[1024];
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);
    __real_gg_log(level, file, line, tag, "%s", msg);
}
