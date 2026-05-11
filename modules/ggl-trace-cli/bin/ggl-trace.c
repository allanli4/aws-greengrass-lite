// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <argp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_SPANS 1024
#define MAX_TRACES 64
#define MAX_LINE 512
#define MAX_DEPTH 32

struct span {
    uint32_t trace_id;
    uint32_t span_id;
    uint32_t parent_span_id;
    char line[MAX_LINE];
};

static struct span spans[MAX_SPANS];
static int span_count;

static uint32_t filter_trace_id;
static bool has_filter;
static bool follow_mode;

static struct argp_option options[] = {
    { "trace-id", 't', "ID", 0, "Filter to specific trace ID (hex)", 0 },
    { "follow", 'f', NULL, 0, "Continuously read stdin", 0 },
    { 0 },
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    (void) state;
    switch (key) {
    case 't':
        if (sscanf(arg, "%x", &filter_trace_id) == 1) {
            has_filter = true;
        }
        break;
    case 'f':
        follow_mode = true;
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp_config = {
    options, parse_opt, NULL,
    "ggl-trace - Render causal trace trees from Greengrass log lines",
    0, 0, 0
};

static void store_span(
    uint32_t trace_id, uint32_t span_id, uint32_t parent_id, const char *line
) {
    if (span_count >= MAX_SPANS) {
        return;
    }
    if (has_filter && trace_id != filter_trace_id) {
        return;
    }
    spans[span_count].trace_id = trace_id;
    spans[span_count].span_id = span_id;
    spans[span_count].parent_span_id = parent_id;
    strncpy(spans[span_count].line, line, MAX_LINE - 1);
    spans[span_count].line[MAX_LINE - 1] = '\0';
    span_count++;
}

static void print_children(uint32_t trace_id, uint32_t parent_id, int depth) {
    if (depth >= MAX_DEPTH) {
        return;
    }
    for (int i = 0; i < span_count; i++) {
        if (spans[i].trace_id == trace_id
            && spans[i].parent_span_id == parent_id
            && spans[i].span_id != 0) {
            for (int d = 0; d < depth + 1; d++) {
                printf("  ");
            }
            printf(
                "[%08x\xe2\x86\x92%08x] %s\n",
                spans[i].parent_span_id,
                spans[i].span_id,
                spans[i].line
            );
            print_children(trace_id, spans[i].span_id, depth + 1);
        }
    }
}

static void render_traces(void) {
    uint32_t seen_traces[MAX_TRACES];
    int trace_count = 0;

    for (int i = 0; i < span_count; i++) {
        bool found = false;
        for (int j = 0; j < trace_count; j++) {
            if (seen_traces[j] == spans[i].trace_id) {
                found = true;
                break;
            }
        }
        if (!found && trace_count < MAX_TRACES) {
            seen_traces[trace_count++] = spans[i].trace_id;
        }
    }

    for (int t = 0; t < trace_count; t++) {
        printf("Trace %08x:\n", seen_traces[t]);
        // Print root spans (parent == 0)
        for (int i = 0; i < span_count; i++) {
            if (spans[i].trace_id == seen_traces[t]
                && spans[i].parent_span_id == 0) {
                printf(
                    "  [%08x\xe2\x86\x92%08x] %s\n",
                    spans[i].parent_span_id,
                    spans[i].span_id,
                    spans[i].line
                );
                print_children(seen_traces[t], spans[i].span_id, 1);
            }
        }
    }
}

static bool parse_line(const char *buf) {
    uint32_t trace_id;
    uint32_t span_id;
    uint32_t parent_id;

    const char *bracket = strstr(buf, "[T=");
    if (bracket == NULL) {
        return false;
    }

    int matched = sscanf(
        bracket, "[T=%08x S=%08x P=%08x]", &trace_id, &span_id, &parent_id
    );
    if (matched != 3) {
        return false;
    }

    // Extract the log text after the trace bracket
    const char *after = strstr(bracket, "] ");
    const char *line_text = (after != NULL) ? after + 2 : "";

    store_span(trace_id, span_id, parent_id, line_text);
    return true;
}

int main(int argc, char **argv) {
    argp_parse(&argp_config, argc, argv, 0, NULL, NULL);

    char buf[MAX_LINE];

    if (follow_mode) {
        while (true) {
            if (fgets(buf, sizeof(buf), stdin) == NULL) {
                // In follow mode, keep trying
                clearerr(stdin);
                render_traces();
                span_count = 0;
                fflush(stdout);
                continue;
            }
            // Strip trailing newline
            size_t len = strlen(buf);
            if (len > 0 && buf[len - 1] == '\n') {
                buf[len - 1] = '\0';
            }
            parse_line(buf);
        }
    } else {
        while (fgets(buf, sizeof(buf), stdin) != NULL) {
            size_t len = strlen(buf);
            if (len > 0 && buf[len - 1] == '\n') {
                buf[len - 1] = '\0';
            }
            parse_line(buf);
        }
        render_traces();
    }

    return 0;
}
