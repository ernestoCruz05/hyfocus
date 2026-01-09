/**
 * @file log.hpp
 * @brief Logging utilities for the HyFocus plugin.
 */
#pragma once

#include <hyprland/src/debug/log/Logger.hpp>

// Compatibility define for legacy log levels
#define LOG Log::DEBUG

/**
 * @brief Log a message using Hyprland's logging system.
 * 
 * @tparam Args Variadic template arguments for formatting
 * @param level Log level (DEBUG, INFO, WARN, ERR)
 * @param fmt Format string
 * @param args Arguments to format
 */
template <typename... Args>
void fe_log(Hyprutils::CLI::eLogLevel level, std::format_string<Args...> fmt, Args&&... args) {
    auto msg = std::vformat(fmt.get(), std::make_format_args(args...));
    Log::logger->log(level, "[hyfocus] {}", msg);
}

// Convenience macros for different log levels
#define FE_DEBUG(...) fe_log(Log::DEBUG, __VA_ARGS__)
#define FE_INFO(...)  fe_log(Log::INFO, __VA_ARGS__)
#define FE_WARN(...)  fe_log(Log::WARN, __VA_ARGS__)
#define FE_ERR(...)   fe_log(Log::ERR, __VA_ARGS__)
