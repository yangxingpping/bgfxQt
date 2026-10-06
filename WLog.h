#pragma once

#include <spdlog/spdlog.h>
#include <chrono>

// =========================================
// 切面式函数/作用域耗时记录
// 使用 RAII + 宏实现零侵入切面编程
// =========================================

namespace wlog {

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
