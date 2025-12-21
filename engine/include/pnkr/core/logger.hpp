#pragma once

/**
 * @file logger.hpp
 * @brief Centralized logging facade using spdlog
 */

#include <memory>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string>
#include <cpptrace/basic.hpp>

namespace pnkr::core {

class Logger {
public:
  // Static-only interface
  Logger() = delete;
  ~Logger() = delete;
  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;

  // Initialize logger (call once on startup)
  static void init(const std::string &pattern = "[%H:%M:%S] [%l] %v");

  // Logging interface (C++20 format strings)
  template <typename... Args>
  static void trace(spdlog::format_string_t<Args...> fmt, Args &&...args) {
    if (sLogger)
      sLogger->trace(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void debug(spdlog::format_string_t<Args...> fmt, Args &&...args) {
    if (sLogger)
      sLogger->debug(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void info(spdlog::format_string_t<Args...> fmt, Args &&...args) {
    if (sLogger)
      sLogger->info(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void warn(spdlog::format_string_t<Args...> fmt, Args &&...args) {
    if (sLogger)
      sLogger->warn(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void error(spdlog::format_string_t<Args...> fmt, Args &&...args) {
    if (sLogger) {
      sLogger->error(fmt, std::forward<Args>(args)...);
    }
  }

  template <typename... Args>
  static void critical(spdlog::format_string_t<Args...> fmt, Args &&...args) {
    if (sLogger) {
      sLogger->critical(fmt, std::forward<Args>(args)...);
    }
  }

  template <typename... Args>
  static void fatal(spdlog::format_string_t<Args...> fmt, Args &&...args) {
    if (sLogger) {
      std::string userMsg = std::format(fmt, std::forward<Args>(args)...);
      std::string trace = cpptrace::generate_trace().to_string();
      sLogger->critical("{}\nStack Trace:\n{}", userMsg, trace);
    }
  }

private:
  static std::shared_ptr<spdlog::logger> sLogger;
};

} // namespace pnkr::core
