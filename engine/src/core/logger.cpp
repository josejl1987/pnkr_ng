#include "pnkr/core/logger.hpp"

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/sinks/ConsoleSink.h>

namespace pnkr::core {

// Global logger instance
static quill::Logger *g_logger = nullptr;

quill::Logger *Logger::getLogger() { return g_logger; }

void Logger::init(const std::string &pattern) {
  if (g_logger) {
    return;
  }

  // Start the backend thread
  quill::Backend::start();

  // Create sink options
  quill::PatternFormatterOptions options;
  options.format_pattern = pattern;
  options.timestamp_pattern = "%H:%M:%S.%Qns";
  options.timestamp_timezone = quill::Timezone::LocalTime;

  // Create console sink config
  quill::ConsoleSinkConfig cfg;
  cfg.set_override_pattern_formatter_options(options);

  // Create a console sink
  auto console_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>(
      "console_sink", cfg);

  // Create the logger
  g_logger = quill::Frontend::create_or_get_logger("pnkr", console_sink);

#ifdef PNKR_DEBUG
  g_logger->set_log_level(quill::LogLevel::Debug);
#else
  g_logger->set_log_level(quill::LogLevel::Info);
#endif

  Core.info("Structured Logger initialized (Quill)");
}

void Logger::shutdown() {
  if (g_logger) {
    Core.info("Shutting down logger");
    g_logger->flush_log();
    g_logger = nullptr;
    // Note: Quill backend thread handles its own lifecycle, but we can
    // explicitly stop it if needed. Usually, letting it destroy statically is
    // fine.
  }
}

void Logger::setLevel(LogLevel level) {
  if (!g_logger)
    return;

  quill::LogLevel quillLevel = quill::LogLevel::Info;
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
  case LogLevel::Off:
    quillLevel = quill::LogLevel::None;
    break;
  }
  g_logger->set_log_level(quillLevel);
}

LogLevel Logger::getLevel() {
  if (!g_logger)
    return LogLevel::Off;

  auto qLevel = g_logger->get_log_level();
  switch (qLevel) {
  case quill::LogLevel::TraceL3:
  case quill::LogLevel::TraceL2:
  case quill::LogLevel::TraceL1:
    return LogLevel::Trace;
  case quill::LogLevel::Debug:
    return LogLevel::Debug;
  case quill::LogLevel::Info:
    return LogLevel::Info;
  case quill::LogLevel::Warning:
    return LogLevel::Warn;
  case quill::LogLevel::Error:
    return LogLevel::Error;
  case quill::LogLevel::Critical:
    return LogLevel::Critical;
  case quill::LogLevel::None:
    return LogLevel::Off;
  default:
    return LogLevel::Info;
  }
}

// ============================================================================
// Scope Management (Same as before)
// ============================================================================

static thread_local std::vector<std::string> gThreadScopeStack;

LogScope::LogScope(std::string_view name) { Logger::pushScope(name); }

LogScope::LogScope(const char *name) { Logger::pushScope(name); }

LogScope::LogScope(std::string &&name) : m_storage(std::move(name)) {
  Logger::pushScope(m_storage);
}

LogScope::~LogScope() noexcept { Logger::popScope(); }

void Logger::pushScope(std::string_view name) {
  gThreadScopeStack.emplace_back(name);
}

void Logger::popScope() noexcept {
  if (!gThreadScopeStack.empty()) {
    gThreadScopeStack.pop_back();
  }
}

std::string Logger::getContextPrefix() {
  if (gThreadScopeStack.empty()) {
    return "";
  }

  std::string result;
  for (const auto &scope : gThreadScopeStack) {
    result += "[";
    result += scope;
    result += "]";
  }
  result += " ";
  return result;
}

ScopeSnapshot Logger::captureScopes() { return {gThreadScopeStack}; }

void Logger::restoreScopes(const ScopeSnapshot &snapshot) {
  gThreadScopeStack = snapshot.scopes;
}

} // namespace pnkr::core
