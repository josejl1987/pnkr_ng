#pragma once

#include <utility>
#include <algorithm>
#include <vector>
#include <bit>
#include <type_traits>
#include <cpptrace/cpptrace.hpp>

#include "profiler.hpp"
#include "logger.hpp"

#ifdef DEBUG
#define PNKR_ASSERT(condition, message)                                        \
  do {                                                                         \
    if (!(condition)) {                                                        \
        std::string trace = cpptrace::generate_trace().to_string();            \
        pnkr::core::Logger::critical("ASSERTION FAILED: {}\nStack Trace:\n?", \
            message, trace);                                                   \
      throw cpptrace::runtime_error(                                           \
          "ASSERTION FAILED: " + std::string(message) +                        \
          "\nFile: " __FILE__ "\nLine: " + std::to_string(__LINE__));          \
    }                                                                          \
  } while (0)
#else
#define PNKR_ASSERT(condition, message) (void)(0)
#endif

#define PNKR_EXPECTS(cond) PNKR_ASSERT(cond, "Precondition failed: " #cond)
#define PNKR_ENSURES(cond) PNKR_ASSERT(cond, "Postcondition failed: " #cond)
#define PNKR_CHECK(cond) do { if(!(cond)) { pnkr::core::Logger::error("Check failed: {} ({}:{})\n", #cond, __FILE__, __LINE__); } } while(0)

#include "result.hpp"

namespace pnkr::util {

template <typename Func> class ScopeGuard {
public:
  explicit ScopeGuard(Func &&f) : m_func(std::move(f)) {}

  ~ScopeGuard() noexcept(noexcept(m_func())) {
    try {
      m_func();
    } catch (...) {

    }
  }

private:
  Func m_func;
};

template <typename Func> [[nodiscard]] auto makeScopeGuard(Func &&func) {
  return ScopeGuard<Func>(std::forward<Func>(func));
}

template <typename T>
requires (!std::is_floating_point_v<std::remove_reference_t<T>> &&
          requires(T v) { static_cast<uint32_t>(v); })
constexpr uint32_t u32(T value) noexcept {
  return static_cast<uint32_t>(value);
}

template <typename T>
requires (std::is_pointer_v<T>)
constexpr uint64_t u64(T value) noexcept {
  static_assert(sizeof(T) == sizeof(uint64_t));
  return std::bit_cast<uint64_t>(value);
}

template <typename T>
requires (!std::is_pointer_v<T> &&
          !std::is_floating_point_v<std::remove_reference_t<T>> &&
          requires(T v) { static_cast<uint64_t>(v); })
constexpr uint64_t u64(T value) noexcept {
  return static_cast<uint64_t>(value);
}

template <typename T>
requires (!std::is_floating_point_v<std::remove_reference_t<T>> &&
          requires(T v) { static_cast<size_t>(v); })
constexpr size_t sz(T value) noexcept {
  return static_cast<size_t>(value);
}

template <typename T>
requires (std::is_arithmetic_v<std::remove_reference_t<T>> ||
          std::is_enum_v<std::remove_reference_t<T>>)
constexpr float toFloat(T value) noexcept {
  return static_cast<float>(value);
}

template <typename Enum>
constexpr auto underlying(Enum e) noexcept -> std::underlying_type_t<Enum> {
  return static_cast<std::underlying_type_t<Enum>>(e);
}

template <typename T, typename Index = uint32_t>
void eraseSelected(std::vector<T>& v, const std::vector<Index>& selection)
{
    if (selection.empty()) {
        return;
    }

    size_t write = 0;
    size_t selIdx = 0;
    const size_t selCount = selection.size();

    for (size_t read = 0; read < v.size(); ++read)
    {
        const size_t index = read;
        while (selIdx < selCount && static_cast<size_t>(selection[selIdx]) < index)
        {
            ++selIdx;
        }

        if (selIdx < selCount && static_cast<size_t>(selection[selIdx]) == index)
        {
            continue;
        }

        if (write != read)
        {
            v[write] = std::move(v[read]);
        }
        ++write;
    }

    v.resize(write);
}

}
