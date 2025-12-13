#pragma once

/**
 * @file common.hpp
 * @brief Common utilities and macros for PNKR engine
 */

#include <stdexcept>
#include <string>
#include <utility>

// ============================================================================
// Assertion Macros (Debug-only)
// ============================================================================

#ifdef DEBUG
  #define PNKR_ASSERT(condition, message) \
    do { \
      if (!(condition)) { \
        throw std::runtime_error("ASSERTION FAILED: " + std::string(message) + \
          "\nFile: " __FILE__ "\nLine: " + std::to_string(__LINE__)); \
      } \
    } while(0)
#else
  #define PNKR_ASSERT(condition, message) (void)(0)
#endif

// ============================================================================
// Utilities
// ============================================================================

namespace pnkr::util {

/**
 * @brief RAII scope guard for cleanup operations
 * @example
 *   auto guard = make_scope_guard([] { std::cout << "cleanup\n"; });
 */
template<typename Func>
class ScopeGuard {
public:
  explicit ScopeGuard(Func&& f) : m_func(std::move(f)) {}

  ~ScopeGuard() noexcept(noexcept(m_func())) {
    try {
      m_func();
    } catch (...) {
      // Silently ignore exceptions in destructors
    }
  }

private:
  Func m_func;
};

template<typename Func>
[[nodiscard]] auto make_scope_guard(Func&& f) {
  return ScopeGuard<Func>(std::forward<Func>(f));
}

}  // namespace pnkr::util
