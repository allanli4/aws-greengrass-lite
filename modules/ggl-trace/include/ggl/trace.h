// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_TRACE_H
#define GGL_TRACE_H

#include <gg/eventstream/decode.h>
#include <gg/eventstream/types.h>
#include <stdbool.h>
#include <stddef.h>

/// Start a root trace if none is active on this thread (idempotent).
/// Generates fresh trace_id and span_id, sets TLS, emits one INFO log line.
void ggl_trace_root_begin(const char *kind, const char *fmt, ...);

/// Attach T/S/P trace headers to an outbound EventStream frame.
/// Returns 3 on success, 0 if no trace active or headers_capacity < 3.
size_t ggl_trace_attach_headers(
    EventStreamHeader *headers, size_t headers_capacity
);

/// Extract T/S/P from inbound header iterator and set TLS for the new span.
/// Returns true if trace context was found and applied; false otherwise.
bool ggl_trace_extract_and_apply(EventStreamHeaderIter headers);

#endif
