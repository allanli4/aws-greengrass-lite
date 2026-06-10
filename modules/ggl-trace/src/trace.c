// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <ggl/trace.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef GG_TRACE_ENABLED

#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/eventstream/decode.h>
#include <gg/eventstream/types.h>
#include <gg/log.h>
#include <gg/rand.h>
#include <stdbool.h>

static uint16_t gen_nonzero_id(void) {
    uint16_t id;
    gg_rand_fill((GgBuffer) { .data = (uint8_t *) &id, .len = sizeof(id) });
    if (id == 0) {
        id = 1;
    }
    return id;
}
// print first line I[ggipcd] [A34F:34BD:----:client_handler.c:142] trace_start: ipc_request op=PublishToTopic
//                                                            └─kind─┘    └──fmt expanded──┘
void ggl_trace_root_begin(const char *kind, const char *fmt, ...) {
    if (gg_log_current_trace_id() != 0) {
        return;
    }

    uint16_t trace_id = gen_nonzero_id();
    uint16_t span_id = gen_nonzero_id();
    gg_log_set_trace(trace_id, span_id, 0);

    if (fmt != NULL) {
        char buf[128];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        GG_LOGI("trace_start: %s %s", kind, buf);
    } else {
        GG_LOGI("trace_start: %s", kind);
    }
}

size_t ggl_trace_attach_headers(
    EventStreamHeader *headers, size_t headers_capacity
) {
    uint16_t t, s, p;
    gg_log_get_trace(&t, &s, &p);
    if (t == 0) {
        return 0;
    }
    if (headers_capacity < 3) {
        return 0;
    }

    headers[0] = (EventStreamHeader) {
        GG_STR("T"), { EVENTSTREAM_INT32, { .int32 = (int32_t) t } }
    };
    headers[1] = (EventStreamHeader) {
        GG_STR("S"), { EVENTSTREAM_INT32, { .int32 = (int32_t) s } }
    };
    headers[2] = (EventStreamHeader) {
        GG_STR("P"), { EVENTSTREAM_INT32, { .int32 = (int32_t) p } }
    };
    return 3;
}

bool ggl_trace_extract_and_apply(EventStreamHeaderIter headers) {
    uint16_t trace_id = 0;
    uint16_t caller_span = 0;
    bool found_t = false;

    // Copy the iterator so the caller's iter is undisturbed.
    EventStreamHeaderIter it = headers;
    EventStreamHeader h;
    while (eventstream_header_next(&it, &h) == GG_ERR_OK) {
        if (gg_buffer_eq(h.name, GG_STR("T"))
            && h.value.type == EVENTSTREAM_INT32) {
            trace_id = (uint16_t) h.value.int32;
            found_t = true;
        } else if (gg_buffer_eq(h.name, GG_STR("S"))
                   && h.value.type == EVENTSTREAM_INT32) {
            caller_span = (uint16_t) h.value.int32;
        }
    }

    if (!found_t) {
        return false;
    }

    uint16_t fresh_span = gen_nonzero_id();
    gg_log_set_trace(trace_id, fresh_span, caller_span);
    return true;
}

#else // !GG_TRACE_ENABLED

void ggl_trace_root_begin(const char *kind, const char *fmt, ...) {
    (void) kind;
    (void) fmt;
}

size_t ggl_trace_attach_headers(
    EventStreamHeader *headers, size_t headers_capacity
) {
    (void) headers;
    (void) headers_capacity;
    return 0;
}

bool ggl_trace_extract_and_apply(EventStreamHeaderIter headers) {
    (void) headers;
    return false;
}

#endif // GG_TRACE_ENABLED
