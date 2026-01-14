#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include <pnkr/core/logger.hpp>

int main(int argc, char **argv) {
  pnkr::core::Logger::init();

  doctest::Context context;
  context.applyCommandLine(argc, argv);

  int res = context.run();

  pnkr::core::Logger::shutdown();

  if (context.shouldExit())
    return res;

  return res;
}
