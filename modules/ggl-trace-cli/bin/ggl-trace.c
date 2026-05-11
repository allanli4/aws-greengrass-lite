// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <argp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_SPANS 256
#define MAX_TRACES 64
#define MAX_LINE 512
#define MAX_DEPTH 16
#define MAX_LINES_PER_SPAN 8

struct span {
    uint32_t trace_id;
    uint32_t span_id;
    uint32_t parent_span_id;
    char tag[32];
    char lines[MAX_LINES_PER_SPAN][MAX_LINE];
    int line_count;
};

static struct span spans[MAX_SPANS];
static int span_count;

static uint32_t filter_trace_id;
static bool has_filter;
static bool follow_mode;
static bool verbose_mode;

static struct argp_option options[] = {
    { "trace-id", 't', "ID", 0, "Filter to specific trace ID (hex)", 0 },
    { "follow", 'f', NULL, 0, "Continuously read stdin", 0 },
    { "verbose", 'v', NULL, 0, "Show all log lines (not just key events)", 0 },
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
    case 'v':
        verbose_mode = true;
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

// Skip noisy low-value log lines unless verbose
static bool is_noise(const char *line) {
    if (verbose_mode) {
        return false;
    }
    if (strstr(line, "epoll callback") != NULL) {
        return true;
    }
    if (strstr(line, "Accepted new client") != NULL) {
        return true;
    }
    if (strstr(line, "Registered fd") != NULL) {
        return true;
    }
    if (strstr(line, "Releasing fd") != NULL) {
        return true;
    }
    if (strstr(line, "Handling client data") != NULL) {
        return true;
    }
    return false;
}

static struct span *find_span(uint32_t trace_id, uint32_t span_id) {
    for (int i = 0; i < span_count; i++) {
        if (spans[i].trace_id == trace_id && spans[i].span_id == span_id) {
            return &spans[i];
        }
    }
    return NULL;
}

static void store_span(
    uint32_t trace_id,
    uint32_t span_id,
    uint32_t parent_id,
    const char *tag,
    const char *line
) {
    if (has_filter && trace_id != filter_trace_id) {
        return;
    }
    if (is_noise(line)) {
        return;
    }

    struct span *existing = find_span(trace_id, span_id);
    if (existing != NULL) {
        if (existing->line_count < MAX_LINES_PER_SPAN) {
            strncpy(
                existing->lines[existing->line_count],
                line,
                MAX_LINE - 1
            );
            existing->lines[existing->line_count][MAX_LINE - 1] = '\0';
            existing->line_count++;
        }
        return;
    }

    if (span_count >= MAX_SPANS) {
        return;
    }

    struct span *s = &spans[span_count++];
    s->trace_id = trace_id;
    s->span_id = span_id;
    s->parent_span_id = parent_id;
    strncpy(s->tag, tag, sizeof(s->tag) - 1);
    s->tag[sizeof(s->tag) - 1] = '\0';
    strncpy(s->lines[0], line, MAX_LINE - 1);
    s->lines[0][MAX_LINE - 1] = '\0';
    s->line_count = 1;
}

static int count_children(uint32_t trace_id, uint32_t parent_id) {
    int count = 0;
    for (int i = 0; i < span_count; i++) {
        if (spans[i].trace_id == trace_id
            && spans[i].parent_span_id == parent_id
            && spans[i].span_id != parent_id) {
            count++;
        }
    }
    return count;
}

static void print_span(struct span *s, int depth, bool is_last) {
    const char *prefix = is_last ? "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80 "
                                 : "\xe2\x94\x9c\xe2\x94\x80\xe2\x94\x80 ";

    for (int d = 0; d < depth; d++) {
        printf("\xe2\x94\x82   ");
    }
    if (depth > 0) {
        printf("%s", prefix);
    } else {
        printf("  ");
    }

    // Print first line with tag
    printf("[%s] %s\n", s->tag, s->lines[0]);

    // Print additional lines for this span
    for (int l = 1; l < s->line_count; l++) {
        for (int d = 0; d < depth; d++) {
            printf("\xe2\x94\x82   ");
        }
        if (depth > 0) {
            printf(is_last ? "    " : "\xe2\x94\x82   ");
        } else {
            printf("  ");
        }
        printf("  %s\n", s->lines[l]);
    }
}

static void print_children(uint32_t trace_id, uint32_t parent_id, int depth) {
    if (depth >= MAX_DEPTH) {
        return;
    }

    int total = count_children(trace_id, parent_id);
    int printed = 0;

    for (int i = 0; i < span_count; i++) {
        if (spans[i].trace_id == trace_id
            && spans[i].parent_span_id == parent_id
            && spans[i].span_id != parent_id) {
            printed++;
            bool is_last = (printed == total);
            print_span(&spans[i], depth, is_last);
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
        // Count spans for this trace
        int trace_spans = 0;
        for (int i = 0; i < span_count; i++) {
            if (spans[i].trace_id == seen_traces[t]) {
                trace_spans++;
            }
        }

        printf("\n\xe2\x94\x80\xe2\x94\x80 Trace %08x (%d spans)\n",
            seen_traces[t], trace_spans);

        // Print root spans (parent == 0)
        int root_count = 0;
        for (int i = 0; i < span_count; i++) {
            if (spans[i].trace_id == seen_traces[t]
                && spans[i].parent_span_id == 0) {
                root_count++;
            }
        }

        int root_printed = 0;
        for (int i = 0; i < span_count; i++) {
            if (spans[i].trace_id == seen_traces[t]
                && spans[i].parent_span_id == 0) {
                root_printed++;
                print_span(&spans[i], 0, root_printed == root_count);
                print_children(seen_traces[t], spans[i].span_id, 1);
            }
        }
    }
    printf("\n");
}

static void extract_tag(const char *buf, char *tag, size_t tag_size) {
    // Look for pattern like "I[ggipcd]" or "D[core-bus]"
    const char *open = NULL;
    const char *p = buf;
    while (*p) {
        if (*p == '[' && p > buf
            && (*(p - 1) == 'I' || *(p - 1) == 'D' || *(p - 1) == 'W'
                || *(p - 1) == 'E' || *(p - 1) == 'T')) {
            open = p + 1;
            break;
        }
        p++;
    }
    if (open != NULL) {
        const char *close = strchr(open, ']');
        if (close != NULL) {
            size_t len
                = (size_t) (close - open) < tag_size - 1
                ? (size_t) (close - open)
                : tag_size - 1;
            memcpy(tag, open, len);
            tag[len] = '\0';
            return;
        }
    }
    strncpy(tag, "?", tag_size);
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

    char tag[32];
    extract_tag(buf, tag, sizeof(tag));

    // Extract the log text after the trace bracket
    const char *after = strstr(bracket, "] ");
    const char *line_text = (after != NULL) ? after + 2 : "";

    store_span(trace_id, span_id, parent_id, tag, line_text);
    return true;
}

int main(int argc, char **argv) {
    argp_parse(&argp_config, argc, argv, 0, NULL, NULL);

    char buf[MAX_LINE];

    if (follow_mode) {
        while (true) {
            if (fgets(buf, sizeof(buf), stdin) == NULL) {
                clearerr(stdin);
                render_traces();
                span_count = 0;
                fflush(stdout);
                continue;
            }
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
