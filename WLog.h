#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <chrono>
#include <memory>
#include <string>

// =========================================
// 切面式函数/作用域耗时记录
// 使用 RAII + 宏实现零侵入切面编程
// =========================================

namespace wlog {

/// @brief 初始化带文件轮转的默认日志器（多线程安全 sink）
/// @param filename   日志文件路径，如 "logs/app.log"
/// @param maxSize    单个日志文件最大字节数，超出后轮转（默认 5 MB）
/// @param maxFiles   保留的历史日志文件数量（默认 3 个：app.1.log、app.2.log ...）
/// @param rotateOnOpen 启动时是否先执行一次轮转（旧文件顺延，新文件从空开始）
/// @code
/// wlog::init_rotating_logger("logs/app.log");
/// spdlog::info("hello file");  // 写入 logs/app.log
/// @endcode
inline void init_rotating_logger(const std::string& filename,
                                 size_t             maxSize      = 5ull * 1024 * 1024,
                                 size_t             maxFiles     = 3,
                                 bool               rotateOnOpen = false)
{
    auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        filename, maxSize, maxFiles, rotateOnOpen);

    auto logger = std::make_shared<spdlog::logger>("wlog", sink);
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::warn); // warn 及以上立即刷盘
    spdlog::set_default_logger(logger);
}

/// @brief 关闭并刷盘所有日志器（程序退出前调用）
inline void shutdown()
{
    spdlog::default_logger()->flush();
    spdlog::shutdown();
}

class ScopedTimer {
public:
    ScopedTimer(const char* func, const char* file, int line, const char* name = nullptr)
        : func_(func), file_(file), line_(line), name_(name)
        , start_(std::chrono::high_resolution_clock::now()) {}

    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count();
        if (name_) {
            spdlog::info("[WLog] scope '{}' in {} ({}:{}) took {} ms", name_, func_, file_, line_, us/1000.0);
        } else {
            spdlog::info("[WLog] function {} ({}:{}) took {} ms", func_, file_, line_, us/1000.0);
        }
    }

private:
    const char* func_;
    const char* file_;
    int line_;
    const char* name_;
    std::chrono::high_resolution_clock::time_point start_;
};

} // namespace wlog

/// @brief 自动记录当前函数耗时（函数退出时输出）
/// @code
/// void foo() {
///     WLOG_FUNCTION_TIMER();
///     // ... 业务逻辑
/// } // 析构时输出耗时
/// @endcode
#define WLOG_FUNCTION_TIMER() \
    wlog::ScopedTimer _wlog_timer_##__LINE__(__FUNCTION__, __FILE__, __LINE__)

/// @brief 记录自定义作用域耗时
/// @param name 作用域名称标签
/// @code
/// void bar() {
///     {
///         WLOG_SCOPE_TIMER("heavy_computation");
///         // ... 耗时逻辑
///     } // 析构时输出耗时
/// }
/// @endcode
#define WLOG_SCOPE_TIMER(name) \
    wlog::ScopedTimer _wlog_timer_##__LINE__(__FUNCTION__, __FILE__, __LINE__, name)
