#ifndef NLOG_H
#define NLOG_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file nlog.h
 * @brief Ncore 轻量日志接口
 *
 * nlog 提供统一的日志 level、tag 和格式化接口，默认 sink 为 stderr
 * 业务代码只依赖 NLOG_D/NLOG_I/NLOG_W/NLOG_E，不直接依赖具体 sink
 */

enum nlog_level {
    NLOG_LEVEL_DEBUG = 0,
    NLOG_LEVEL_INFO,
    NLOG_LEVEL_WARN,
    NLOG_LEVEL_ERROR,
};

/**
 * 设置日志过滤 level
 *
 * 低于该 level 的日志不会输出，默认值为 NLOG_LEVEL_INFO
 */
void nlog_set_level(enum nlog_level level);

/** 获取当前日志过滤 level */
enum nlog_level nlog_get_level(void);

/**
 * 写入一条日志
 *
 * @param level 日志 level
 * @param tag   模块或功能 tag，可传 NULL
 * @param fmt   printf 风格格式字符串
 */
void nlog_write(enum nlog_level level, const char *tag, const char *fmt, ...);

#define NLOG_D(tag, fmt, ...) \
    nlog_write(NLOG_LEVEL_DEBUG, (tag), (fmt), ##__VA_ARGS__)

#define NLOG_I(tag, fmt, ...) \
    nlog_write(NLOG_LEVEL_INFO, (tag), (fmt), ##__VA_ARGS__)

#define NLOG_W(tag, fmt, ...) \
    nlog_write(NLOG_LEVEL_WARN, (tag), (fmt), ##__VA_ARGS__)

#define NLOG_E(tag, fmt, ...) \
    nlog_write(NLOG_LEVEL_ERROR, (tag), (fmt), ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* NLOG_H */
