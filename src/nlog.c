#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

#include "nlog.h"

static atomic_int g_nlog_level = NLOG_LEVEL_INFO;

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

void nlog_set_level(enum nlog_level level)
{
    if (level < NLOG_LEVEL_DEBUG || level > NLOG_LEVEL_ERROR)
        return;

    atomic_store_explicit(&g_nlog_level, level, memory_order_relaxed);
}

enum nlog_level nlog_get_level(void)
{
    return (enum nlog_level)atomic_load_explicit(&g_nlog_level,
                                                 memory_order_relaxed);
}

void nlog_write(enum nlog_level level, const char *tag, const char *fmt, ...)
{
    struct timespec ts;
    struct tm tm_now;
    char time_buf[32];
    va_list args;

    if (!fmt)
        return;

    if (level < nlog_get_level())
        return;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        return;

    if (!localtime_r(&ts.tv_sec, &tm_now))
        return;

    if (strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_now) == 0)
        return;

    /* 锁住 stderr，避免多个线程同时输出时把一条日志拆开 */
    flockfile(stderr);

    fprintf(stderr, "%s.%03ld [%s]",
            time_buf,
            ts.tv_nsec / 1000000L,
            nlog_level_name(level));

    if (tag && tag[0] != '\0')
        fprintf(stderr, " [%s]", tag);

    fputc(' ', stderr);

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);
    funlockfile(stderr);
}
