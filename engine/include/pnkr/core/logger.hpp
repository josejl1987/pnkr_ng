#pragma once

#include <cpptrace/basic.hpp>
#include <format>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

// Quill Includes
#include <quill/Logger.h>
#include <quill/core/MacroMetadata.h>

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

  LogFormat(const char *f,
            const std::source_location &l = std::source_location::current())
      : fmt(f), loc(l) {}
  LogFormat(std::string_view f,
            const std::source_location &l = std::source_location::current())
      : fmt(f), loc(l) {}
  LogFormat(const std::string &f,
            const std::source_location &l = std::source_location::current())
      : fmt(f), loc(l) {}
};

class Logger;

struct ScopeSnapshot {
  std::vector<std::string> scopes;
};

struct Channel {
  const char *name;

  constexpr explicit Channel(const char *n) : name(n) {}

  template <typename... Args> void trace(LogFormat fmt, Args &&...args) const;

  template <typename... Args> void debug(LogFormat fmt, Args &&...args) const;

  template <typename... Args> void info(LogFormat fmt, Args &&...args) const;

  template <typename... Args> void warn(LogFormat fmt, Args &&...args) const;

  template <typename... Args> void error(LogFormat fmt, Args &&...args) const;

  template <typename... Args>
  void critical(LogFormat fmt, Args &&...args) const;
};

class LogScope {
public:
  explicit LogScope(std::string_view name);
  explicit LogScope(const char *name);
  explicit LogScope(std::string &&name);
  ~LogScope() noexcept;

  LogScope(const LogScope &) = delete;
  LogScope &operator=(const LogScope &) = delete;

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

  static void
  init(const std::string &pattern =
           "[%(time)] [%(log_level)] %(message)"); // simplified default pattern
                                                   // for quill
  static void shutdown();
  static void setLevel(LogLevel level);
  static LogLevel getLevel();

  static void pushScope(std::string_view name);
  static void popScope() noexcept;
  static std::string getContextPrefix();

  static ScopeSnapshot captureScopes();
  static void restoreScopes(const ScopeSnapshot &snapshot);

  template <typename... Args> static void info(LogFormat fmt, Args &&...args) {
    Core.info(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args> static void warn(LogFormat fmt, Args &&...args) {
    Core.warn(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args> static void error(LogFormat fmt, Args &&...args) {
    Core.error(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args> static void debug(LogFormat fmt, Args &&...args) {
    Core.debug(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args> static void trace(LogFormat fmt, Args &&...args) {
    Core.trace(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void critical(LogFormat fmt, Args &&...args) {
    Core.critical(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args> static void fatal(LogFormat fmt, Args &&...args) {
    // For fatal, we construct the message and log it as critical, then flush.
    // Since we can't easily reuse the private log_impl here without exposing
    // details, we'll duplicate a bit of logic or use Core.critical but add
    // stack trace.
    std::string userMsg = std::vformat(fmt.fmt, std::make_format_args(args...));
    std::string trace = cpptrace::generate_trace().to_string();

    // We pass the already formatted message to avoid double formatting issues
    // if it contains {} But wait, Core.critical expects a format string. Let's
    // use a specialized call.
    log_impl(LogLevel::Critical, "Core", fmt.loc, "{}\nStack Trace:\n{}",
             userMsg, trace);

    // Flush happens in shutdown or manually if we could access the logger.
    // We'll rely on the fact that Critical often flushes or we can force it in
    // log_impl.
  }

private:
  static quill::Logger *get_logger();

  template <typename... Args>
  static void log_impl(LogLevel level, const char *tag,
                       const std::source_location &loc, std::string_view fmt,
                       Args &&...args) {
    auto *logger = get_logger();
    if (!logger)
      return;

    quill::LogLevel quillLevel;
    switch (level) {
    case LogLevel::Trace:
      quillLevel = quill::LogLevel::TraceL3;
      break;
    case LogLevel::Debug:
      quillLevel = quill::LogLevel::Debug;
      break;
    case LogLevel::Info:
      quillLevel = quill::LogLevel::Info;
      break;
    case LogLevel::Warn:
      quillLevel = quill::LogLevel::Warning;
      break;
    case LogLevel::Error:
      quillLevel = quill::LogLevel::Error;
      break;
    case LogLevel::Critical:
      quillLevel = quill::LogLevel::Critical;
      break;
    default:
      quillLevel = quill::LogLevel::Info;
      break;
    }

    if (logger->should_log_statement(quillLevel)) {
      try {
        // Format the user message first
        std::string userMsg = std::vformat(fmt, std::make_format_args(args...));

        // Prepend scope and tag
        std::string scopePrefix = getContextPrefix();
        if (!scopePrefix.empty()) {
          userMsg = std::format("{}{}", scopePrefix, userMsg);
        }

        if (tag != nullptr && std::strlen(tag) > 0) {
          userMsg = std::format("[{}] {}", tag, userMsg);
        }

        // Use Runtime Metadata to pass explicit source location
        // Note: This relies on internal API availability.
        // If this fails to compile, we might need a simpler approach
        // (LOG_DYNAMIC) but that loses the custom source location.

        static constexpr quill::MacroMetadata macro_metadata{
            "",
            "",
            "",
            nullptr,
            quill::LogLevel::None,
            quill::MacroMetadata::Event::LogWithRuntimeMetadataDeepCopy};

        // We pass "{}" as fmt and userMsg as arg to avoid double formatting
        logger->log_statement_runtime_metadata<
            false>( // false = no immediate flush (let backend handle it)
            &macro_metadata, "{}", loc.file_name(), loc.function_name(),
            "", // tags
            static_cast<int>(loc.line()), quillLevel, userMsg);

      } catch (const std::exception &e) {
        // Fallback?
      }
    }
  }
};

template <typename... Args>
void Channel::trace(LogFormat fmt, Args &&...args) const {
  Logger::log_impl(LogLevel::Trace, name, fmt.loc, fmt.fmt,
                   std::forward<Args>(args)...);
}

template <typename... Args>
void Channel::debug(LogFormat fmt, Args &&...args) const {
  Logger::log_impl(LogLevel::Debug, name, fmt.loc, fmt.fmt,
                   std::forward<Args>(args)...);
}

template <typename... Args>
void Channel::info(LogFormat fmt, Args &&...args) const {
  Logger::log_impl(LogLevel::Info, name, fmt.loc, fmt.fmt,
                   std::forward<Args>(args)...);
}

template <typename... Args>
void Channel::warn(LogFormat fmt, Args &&...args) const {
  Logger::log_impl(LogLevel::Warn, name, fmt.loc, fmt.fmt,
                   std::forward<Args>(args)...);
}

template <typename... Args>
void Channel::error(LogFormat fmt, Args &&...args) const {
  Logger::log_impl(LogLevel::Error, name, fmt.loc, fmt.fmt,
                   std::forward<Args>(args)...);
}

template <typename... Args>
void Channel::critical(LogFormat fmt, Args &&...args) const {
  Logger::log_impl(LogLevel::Critical, name, fmt.loc, fmt.fmt,
                   std::forward<Args>(args)...);
}

} // namespace pnkr::core

#define PNKR_LOG_SCOPE(name) ::pnkr::core::LogScope _pnkr_scope_##__LINE__(name)
