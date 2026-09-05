#ifndef NLOG_H
#define NLOG_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file nlog.h
 * @brief Ncore 轻量日志接口
 *
 * nlog 提供统一的日志 level, tag 和格式化接口, 默认 sink 为 stderr
 * NLOG_D/NLOG_I/NLOG_W/NLOG_E 会自动记录调用位置
 * 如需自定义 tag, 可在包含本头文件前定义 NLOG_TAG
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
 * 低于该 level 的日志不会输出, 默认值为 NLOG_LEVEL_INFO
 */
void nlog_set_level(enum nlog_level level);

/** 获取当前日志过滤 level */
enum nlog_level nlog_get_level(void);

/**
 * 写入一条日志
 *
 * @param level 日志 level
 * @param tag   模块或功能 tag, 可传 NULL
 * @param fmt   printf 风格格式字符串
 */
void nlog_write(enum nlog_level level, const char *tag, const char *fmt, ...);

void nlog_write_at(enum nlog_level level, const char *tag,
                   const char *source_file, int source_line,
                   const char *function, const char *fmt, ...);

/*
 * 默认使用当前源文件名作为 tag
 * 调用者可在 #include <ncore/nlog.h> 前定义 NLOG_TAG 覆盖默认值
 */
#ifndef NLOG_TAG
#define NLOG_TAG __FILE__
#endif

#define NLOG_D(...) \
    nlog_write_at(NLOG_LEVEL_DEBUG, NLOG_TAG, __FILE__, __LINE__, __func__, \
                  __VA_ARGS__)

#define NLOG_I(...) \
    nlog_write_at(NLOG_LEVEL_INFO, NLOG_TAG, __FILE__, __LINE__, __func__, \
                  __VA_ARGS__)

#define NLOG_W(...) \
    nlog_write_at(NLOG_LEVEL_WARN, NLOG_TAG, __FILE__, __LINE__, __func__, \
                  __VA_ARGS__)

#define NLOG_E(...) \
    nlog_write_at(NLOG_LEVEL_ERROR, NLOG_TAG, __FILE__, __LINE__, __func__, \
                  __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* NLOG_H */
