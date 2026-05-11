// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <ggl/trace.h>
#include <errno.h>
#include <gg/cleanup.h>
#include <gg/log.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static bool enable_systemd_log_prefix = false;

__attribute__((constructor)) static void configure_trace_logging(void) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char *journal_stream = getenv("JOURNAL_STREAM");
    if (journal_stream == NULL) {
        return;
    }

    struct stat stderr_stat;
    if (fstat(STDERR_FILENO, &stderr_stat) != 0) {
        return;
    }

    char *endptr;
    unsigned long journal_dev = strtoul(journal_stream, &endptr, 10);
    if (*endptr != ':') {
        return;
    }
    unsigned long journal_ino = strtoul(endptr + 1, NULL, 10);

    if (stderr_stat.st_dev == journal_dev
        && stderr_stat.st_ino == journal_ino) {
        enable_systemd_log_prefix = true;
    }
}

// This definition takes priority over gg-sdk's static archive version
// because object files from the linking module are resolved before archive
// members.
void gg_log(
    uint32_t level,
    const char *file,
    int line,
    const char *tag,
    const char *format,
    ...
) {
    static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

    int saved_errno = errno;

    const char *prefix = "";

    if (enable_systemd_log_prefix) {
        switch (level) {
        case GG_LOG_ERROR:
            prefix = "<3>";
            break;
        case GG_LOG_WARN:
            prefix = "<4>";
            break;
        case GG_LOG_INFO:
            prefix = "<6>";
            break;
        case GG_LOG_DEBUG:
        case GG_LOG_TRACE:
            prefix = "<7>";
            break;
        default:
            break;
        }
    }

    unsigned char level_c;
    switch (level) {
    case GG_LOG_ERROR:
        level_c = 'E';
        break;
    case GG_LOG_WARN:
        level_c = 'W';
        break;
    case GG_LOG_INFO:
        level_c = 'I';
        break;
    case GG_LOG_DEBUG:
        level_c = 'D';
        break;
    case GG_LOG_TRACE:
        level_c = 'T';
        break;
    default:
        level_c = '?';
    }

    {
        GG_MTX_SCOPE_GUARD(&log_mutex);

        if (ggl_trace_active()) {
            GglTraceCtx ctx = ggl_trace_get();
            fprintf(
                stderr,
                "%s%c[%s] %s:%d: [T=%08x S=%08x P=%08x] ",
                prefix,
                level_c,
                tag,
                file,
                line,
                ctx.trace_id,
                ctx.span_id,
                ctx.parent_span_id
            );
        } else {
            fprintf(
                stderr, "%s%c[%s] %s:%d: ", prefix, level_c, tag, file, line
            );
        }

        va_list args;
        va_start(args, format);
        errno = saved_errno;
        vfprintf(stderr, format, args);
        va_end(args);

        fprintf(stderr, "\n");
        fflush(stderr);
    }

    errno = saved_errno;
}
