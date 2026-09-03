#define _POSIX_C_SOURCE 200809L

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "nlog.h"

static atomic_int g_nlog_level = NLOG_LEVEL_INFO;

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

/* __FILE__ 可能包含完整路径，只保留文件名作为默认 tag */
static const char *nlog_tag_name(const char *tag)
{
    const char *slash;
    const char *backslash;

    if (!tag)
        return NULL;

    slash = strrchr(tag, '/');
    backslash = strrchr(tag, '\\');

    if (!slash)
        slash = backslash;
    else if (backslash && backslash > slash)
        slash = backslash;

    return slash ? slash + 1 : tag;
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

void nlog_write(enum nlog_level level, const char *tag, const char *fmt, ...)
{
    struct timespec ts;
    struct tm tm_now;
    char time_buf[32];
    const char *tag_name;
    va_list args;

    if (!nlog_level_valid(level) || !fmt)
        return;

    if (level < nlog_get_level())
        return;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        return;

    if (!localtime_r(&ts.tv_sec, &tm_now))
        return;

    if (strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_now) == 0)
        return;

    tag_name = nlog_tag_name(tag);

    /* 锁住 stderr，保证多个线程同时写日志时每条日志保持完整 */
    flockfile(stderr);

    fprintf(stderr, "%s.%03ld [%s]",
            time_buf,
            ts.tv_nsec / 1000000L,
            nlog_level_name(level));

    if (tag_name && tag_name[0] != '\0')
        fprintf(stderr, " [%s]", tag_name);

    fputc(' ', stderr);

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);
    funlockfile(stderr);
}
