#pragma once

#include <memory>
#include <string_view>
#include <source_location>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/fmt/fmt.h>
#include <cpptrace/basic.hpp>
#include <vector>
#include <string>

namespace pnkr::core {

enum class LogLevel : int {
  Trace = 0,
  Debug,
  Info,
  Warn,
  Error,
  Critical,
  Off
};

struct LogFormat {
    std::string_view fmt;
    std::source_location loc;

    LogFormat(const char* f, const std::source_location& l = std::source_location::current())
        : fmt(f), loc(l) {}
    LogFormat(std::string_view f, const std::source_location& l = std::source_location::current())
        : fmt(f), loc(l) {}
    LogFormat(const std::string& f, const std::source_location& l = std::source_location::current())
        : fmt(f), loc(l) {}
};

class Logger;

struct ScopeSnapshot {
    std::vector<std::string> scopes;
};

struct Channel {
    const char* name;

    constexpr explicit Channel(const char* n) : name(n) {}

    template <typename... Args>
    void trace(LogFormat fmt, Args &&...args) const;

    template <typename... Args>
    void debug(LogFormat fmt, Args &&...args) const;

    template <typename... Args>
    void info(LogFormat fmt, Args &&...args) const;

    template <typename... Args>
    void warn(LogFormat fmt, Args &&...args) const;

    template <typename... Args>
    void error(LogFormat fmt, Args &&...args) const;

    template <typename... Args>
    void critical(LogFormat fmt, Args &&...args) const;
};

class LogScope {
public:
    explicit LogScope(std::string_view name);
    explicit LogScope(const char* name);
    explicit LogScope(std::string&& name);
    ~LogScope() noexcept;

    LogScope(const LogScope&) = delete;
    LogScope& operator=(const LogScope&) = delete;

private:
    std::string m_storage;
};

class Logger {
  friend struct Channel;
public:
  using Channel = pnkr::core::Channel;

  static constexpr Channel Core{"Core"};
  static constexpr Channel RHI{"RHI"};
  static constexpr Channel Render{"Render"};
  static constexpr Channel Asset{"Asset"};
  static constexpr Channel Scene{"Scene"};
  static constexpr Channel UI{"UI"};
  static constexpr Channel Platform{"Platform"};

  static void init(const std::string &pattern = "%^[%H:%M:%S.%e] [%l] %v%$   @%s:%#");
  static void shutdown();
  static void setLevel(LogLevel level);
  static LogLevel getLevel();

  static void pushScope(std::string_view name);
  static void popScope() noexcept;
  static std::string getContextPrefix();

  static ScopeSnapshot captureScopes();
  static void restoreScopes(const ScopeSnapshot& snapshot);

  template <typename... Args>
  static void info(LogFormat fmt, Args &&...args) {
    Core.info(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void warn(LogFormat fmt, Args &&...args) {
    Core.warn(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void error(LogFormat fmt, Args &&...args) {
    Core.error(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void debug(LogFormat fmt, Args &&...args) {
    Core.debug(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void trace(LogFormat fmt, Args &&...args) {
    Core.trace(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void critical(LogFormat fmt, Args &&...args) {
    Core.critical(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void fatal(LogFormat fmt, Args &&...args) {
    auto* logger = get_logger();
    if (logger) {
      std::string userMsg = std::vformat(fmt.fmt, std::make_format_args(args...));
      std::string trace = cpptrace::generate_trace().to_string();

      spdlog::source_loc sloc{fmt.loc.file_name(), static_cast<int>(fmt.loc.line()), fmt.loc.function_name()};
      logger->log(sloc, spdlog::level::critical, "[Core] {}\nStack Trace:\n{}", userMsg, trace);
      logger->flush();
    }
  }

private:
  static spdlog::logger* get_logger();

  template <typename... Args>
  static void log_impl(LogLevel level, const char* tag, const std::source_location& loc, std::string_view fmt, Args &&...args) {
      auto* logger = get_logger();
      if (!logger) return;

      spdlog::level::level_enum spdLevel;
      switch(level) {
          case LogLevel::Trace: spdLevel = spdlog::level::trace; break;
          case LogLevel::Debug: spdLevel = spdlog::level::debug; break;
          case LogLevel::Info:  spdLevel = spdlog::level::info; break;
          case LogLevel::Warn:  spdLevel = spdlog::level::warn; break;
          case LogLevel::Error: spdLevel = spdlog::level::err; break;
          case LogLevel::Critical: spdLevel = spdlog::level::critical; break;
          default: spdLevel = spdlog::level::info; break;
      }

      if (logger->should_log(spdLevel)) {
          try {
              std::string userMsg = std::vformat(fmt, std::make_format_args(args...));
              std::string scopePrefix = getContextPrefix();

              if (!scopePrefix.empty()) {
                  userMsg = std::format("{}{}", scopePrefix, userMsg);
              }

              if (tag != nullptr && strlen(tag) > 0) {
                  userMsg = std::format("[{}] {}", tag, userMsg);
              }

              spdlog::source_loc sloc{loc.file_name(), static_cast<int>(loc.line()), loc.function_name()};
              logger->log(sloc, spdLevel, "{}", userMsg);
          } catch (const std::exception& e) {
              logger->error("Logger format error: {}", e.what());
          }
      }
  }
};

template <typename... Args>
void Channel::trace(LogFormat fmt, Args &&...args) const {
    Logger::log_impl(LogLevel::Trace, name, fmt.loc, fmt.fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void Channel::debug(LogFormat fmt, Args &&...args) const {
    Logger::log_impl(LogLevel::Debug, name, fmt.loc, fmt.fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void Channel::info(LogFormat fmt, Args &&...args) const {
    Logger::log_impl(LogLevel::Info, name, fmt.loc, fmt.fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void Channel::warn(LogFormat fmt, Args &&...args) const {
    Logger::log_impl(LogLevel::Warn, name, fmt.loc, fmt.fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void Channel::error(LogFormat fmt, Args &&...args) const {
    Logger::log_impl(LogLevel::Error, name, fmt.loc, fmt.fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void Channel::critical(LogFormat fmt, Args &&...args) const {
    Logger::log_impl(LogLevel::Critical, name, fmt.loc, fmt.fmt, std::forward<Args>(args)...);
}

}

#define PNKR_LOG_SCOPE(name) \
    ::pnkr::core::LogScope _pnkr_scope_##__LINE__(name)