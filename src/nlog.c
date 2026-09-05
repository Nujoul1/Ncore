#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/syscall.h>
#endif

#include "ncore/nlog.h"

static atomic_int g_nlog_level = NLOG_LEVEL_INFO;
static atomic_uint_fast64_t g_nlog_sequence = 0;

static int nlog_level_valid(enum nlog_level level)
{
    return level >= NLOG_LEVEL_DEBUG && level <= NLOG_LEVEL_ERROR;
}

static const char *nlog_level_name(enum nlog_level level)
{
    switch (level) {
    case NLOG_LEVEL_DEBUG:
        return "DEBUG";
    case NLOG_LEVEL_INFO:
        return "INFO";
    case NLOG_LEVEL_WARN:
        return "WARN";
    case NLOG_LEVEL_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

static const char *nlog_basename(const char *path)
{
    const char *slash;
    const char *backslash;

    if (!path)
        return NULL;

    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');

    if (!slash)
        slash = backslash;
    else if (backslash && backslash > slash)
        slash = backslash;

    return slash ? slash + 1 : path;
}

static uint64_t nlog_thread_id(void)
{
#ifdef __linux__
    return (uint64_t)syscall(SYS_gettid);
#else
    return (uint64_t)(uintptr_t)pthread_self();
#endif
}

void nlog_set_level(enum nlog_level level)
{
    if (!nlog_level_valid(level))
        return;

    atomic_store_explicit(&g_nlog_level, level, memory_order_relaxed);
}

enum nlog_level nlog_get_level(void)
{
    return (enum nlog_level)atomic_load_explicit(&g_nlog_level,
                                                 memory_order_relaxed);
}

static void nlog_vwrite(enum nlog_level level, const char *tag,
                        const char *source_file, int source_line,
                        const char *function, const char *fmt, va_list args)
{
    struct timespec real_ts;
    struct timespec mono_ts;
    struct tm tm_now;
    char time_buf[32];
    const char *tag_name;
    const char *source_name;
    uint64_t mono_ms = 0;
    uint64_t sequence;

    if (!nlog_level_valid(level) || !fmt)
        return;

    if (level < nlog_get_level())
        return;

    if (clock_gettime(CLOCK_REALTIME, &real_ts) != 0)
        return;

    if (!gmtime_r(&real_ts.tv_sec, &tm_now))
        return;

    if (strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%S", &tm_now) == 0)
        return;

    if (clock_gettime(CLOCK_MONOTONIC, &mono_ts) == 0) {
        mono_ms = (uint64_t)mono_ts.tv_sec * 1000U +
                  (uint64_t)mono_ts.tv_nsec / 1000000U;
    }

    sequence = atomic_fetch_add_explicit(&g_nlog_sequence, 1,
                                         memory_order_relaxed) + 1;
    tag_name = nlog_basename(tag);
    source_name = nlog_basename(source_file);

    /* 锁住 stderr, 保证多个线程同时写日志时每条日志保持完整 */
    flockfile(stderr);

    fprintf(stderr,
            "%s.%03ldZ mono_ms=%" PRIu64 " seq=%" PRIu64
            " level=%s pid=%ld tid=%" PRIu64,
            time_buf,
            real_ts.tv_nsec / 1000000L,
            mono_ms,
            sequence,
            nlog_level_name(level),
            (long)getpid(),
            nlog_thread_id());

    if (tag_name && tag_name[0] != '\0')
        fprintf(stderr, " tag=%s", tag_name);

    if (source_name && source_name[0] != '\0')
        fprintf(stderr, " source=%s:%d", source_name, source_line);

    if (function && function[0] != '\0')
        fprintf(stderr, " function=%s", function);

    fputc(' ', stderr);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    fflush(stderr);
    funlockfile(stderr);
}

void nlog_write(enum nlog_level level, const char *tag, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    nlog_vwrite(level, tag, NULL, 0, NULL, fmt, args);
    va_end(args);
}

void nlog_write_at(enum nlog_level level, const char *tag,
                   const char *source_file, int source_line,
                   const char *function, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    nlog_vwrite(level, tag, source_file, source_line, function, fmt, args);
    va_end(args);
}
