#pragma once

/**
 * @file logger.hpp
 * @brief Centralized logging facade using spdlog
 */

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <string>

namespace pnkr::core {

class Logger {
public:
  // Static-only interface
  Logger() = delete;
  ~Logger() = delete;
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  // Initialize logger (call once on startup)
  static void init(const std::string& pattern = "[%H:%M:%S] [%l] %v");

  // Logging interface (C++20 format strings)
  template<typename... Args>
  static void trace(spdlog::format_string_t<Args...> fmt, Args&&... args) {
    if (s_logger) s_logger->trace(fmt, std::forward<Args>(args)...);
  }

  template<typename... Args>
  static void debug(spdlog::format_string_t<Args...> fmt, Args&&... args) {
    if (s_logger) s_logger->debug(fmt, std::forward<Args>(args)...);
  }

  template<typename... Args>
  static void info(spdlog::format_string_t<Args...> fmt, Args&&... args) {
    if (s_logger) s_logger->info(fmt, std::forward<Args>(args)...);
  }

  template<typename... Args>
  static void warn(spdlog::format_string_t<Args...> fmt, Args&&... args) {
    if (s_logger) s_logger->warn(fmt, std::forward<Args>(args)...);
  }

  template<typename... Args>
  static void error(spdlog::format_string_t<Args...> fmt, Args&&... args) {
    if (s_logger) s_logger->error(fmt, std::forward<Args>(args)...);
  }

  template<typename... Args>
  static void critical(spdlog::format_string_t<Args...> fmt, Args&&... args) {
    if (s_logger) s_logger->critical(fmt, std::forward<Args>(args)...);
  }

private:
  static std::shared_ptr<spdlog::logger> s_logger;
};

}  // namespace pnkr::core
