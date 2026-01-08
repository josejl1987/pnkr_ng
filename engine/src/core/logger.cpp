#include "pnkr/core/logger.hpp"

namespace pnkr::core {

static std::shared_ptr<spdlog::logger> &getInstanceRef() {
  static std::shared_ptr<spdlog::logger> sLogger;
  return sLogger;
}

spdlog::logger *Logger::get_logger() { return getInstanceRef().get(); }

void Logger::init(const std::string &pattern) {
  auto &sLogger = getInstanceRef();
  if (sLogger) {
    return;
  }

  auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  auto logger = std::make_shared<spdlog::logger>("pnkr", consoleSink);

  logger->set_pattern(pattern);

#ifdef PNKR_DEBUG
  logger->set_level(spdlog::level::debug);
#else
  logger->set_level(spdlog::level::info);
#endif

  logger->flush_on(spdlog::level::err);
  sLogger = logger;

  Core.info("Structured Logger initialized");
}

void Logger::shutdown() {
  auto &sLogger = getInstanceRef();
  if (sLogger) {
    Core.info("Shutting down logger");
    sLogger.reset();
  }
    spdlog::shutdown();
}

void Logger::setLevel(LogLevel level) {
    auto* logger = get_logger();
    if (logger == nullptr) {
      return;
    }

    spdlog::level::level_enum spdLevel = spdlog::level::info;
    switch(level) {
        case LogLevel::Trace: spdLevel = spdlog::level::trace; break;
        case LogLevel::Debug: spdLevel = spdlog::level::debug; break;
        case LogLevel::Info:  spdLevel = spdlog::level::info; break;
        case LogLevel::Warn:  spdLevel = spdlog::level::warn; break;
        case LogLevel::Error: spdLevel = spdlog::level::err; break;
        case LogLevel::Critical: spdLevel = spdlog::level::critical; break;
        case LogLevel::Off:   spdLevel = spdlog::level::off; break;
    }
    logger->set_level(spdLevel);
}

LogLevel Logger::getLevel() {
    auto* logger = get_logger();
    if (logger == nullptr) {
      return LogLevel::Off;
    }

    switch (logger->level()) {
        case spdlog::level::trace: return LogLevel::Trace;
        case spdlog::level::debug: return LogLevel::Debug;
        case spdlog::level::info:  return LogLevel::Info;
        case spdlog::level::warn:  return LogLevel::Warn;
        case spdlog::level::err:   return LogLevel::Error;
        case spdlog::level::critical: return LogLevel::Critical;
        case spdlog::level::off:   return LogLevel::Off;
        default: return LogLevel::Info;
    }
}

static thread_local std::vector<std::string> gThreadScopeStack;

LogScope::LogScope(std::string_view name) {
    Logger::pushScope(name);
}

LogScope::LogScope(const char* name) {
    Logger::pushScope(name);
}

LogScope::LogScope(std::string&& name) : m_storage(std::move(name)) {
    Logger::pushScope(m_storage);
}

LogScope::~LogScope() noexcept {
    Logger::popScope();
}

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

void Logger::restoreScopes(const ScopeSnapshot& snapshot) {
  gThreadScopeStack = snapshot.scopes;
}

}