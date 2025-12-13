#include "pnkr/core/logger.hpp"

namespace pnkr::core {

std::shared_ptr<spdlog::logger> Logger::s_logger;

void Logger::init(const std::string& pattern) {
  if (s_logger) {
    return;  // Already initialized
  }

  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  auto logger = std::make_shared<spdlog::logger>("pnkr", console_sink);

  logger->set_pattern(pattern);

#ifdef DEBUG
  logger->set_level(spdlog::level::debug);
#else
  logger->set_level(spdlog::level::info);
#endif

  spdlog::register_logger(logger);
  s_logger = logger;

  info("Logger initialized");
}

}  // namespace pnkr::core
