#include "pnkr/core/logger.hpp"

namespace pnkr::core {

std::shared_ptr<spdlog::logger> Logger::sLogger;

void Logger::init(const std::string &pattern) {
  if (sLogger) {
    return; // Already initialized
  }

  auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  const auto logger = std::make_shared<spdlog::logger>("pnkr", consoleSink);

  logger->set_pattern(pattern);

#ifdef DEBUG
  logger->set_level(spdlog::level::debug);
#else
  logger->set_level(spdlog::level::info);
#endif

  spdlog::register_logger(logger);
  sLogger = logger;

  info("Logger initialized");
}

} // namespace pnkr::core
