#pragma once

/**
 * @file common.hpp
 * @brief Common utilities and macros for PNKR engine
 */

#include <utility>
#include <algorithm>
#include <vector>
#include <cpptrace/cpptrace.hpp>

#include "profiler.hpp"
#include "logger.hpp"

// ============================================================================
// Assertion Macros (Debug-only)
// ============================================================================

#ifdef DEBUG
#define PNKR_ASSERT(condition, message)                                        \
  do {                                                                         \
    if (!(condition)) {                                                        \
        std::string trace = cpptrace::generate_trace().to_string();            \
        pnkr::core::Logger::critical("ASSERTION FAILED: {}\nStack Trace:\n{}", \
            message, trace);                                                   \
      throw cpptrace::runtime_error(                                           \
          "ASSERTION FAILED: " + std::string(message) +                        \
          "\nFile: " __FILE__ "\nLine: " + std::to_string(__LINE__));          \
    }                                                                          \
  } while (0)
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
template <typename Func> class ScopeGuard {
public:
  explicit ScopeGuard(Func &&f) : m_func(std::move(f)) {}

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

template <typename Func> [[nodiscard]] auto makeScopeGuard(Func &&func) {
  return ScopeGuard<Func>(std::forward<Func>(func));
}

// ============================================================================
// Cast Helper Functions (to reduce static_cast noise)
// ============================================================================

/**
 * @brief Helper function to convert any type to uint32_t cleanly
 * Reduces visual noise from static_cast<uint32_t>(...)
 */
template <typename T>
constexpr uint32_t u32(T value) noexcept {
  return static_cast<uint32_t>(value);
}

/**
 * @brief Helper function to convert any type to uint64_t cleanly
 * Reduces visual noise from static_cast<uint64_t>(...)
 */
template <typename T>
constexpr uint64_t u64(T value) noexcept {
  if constexpr (std::is_pointer_v<T>) {
    return reinterpret_cast<uint64_t>(value);
  } else {
    return static_cast<uint64_t>(value);
  }
}

/**
 * @brief Helper function to convert any type to size_t cleanly
 * Reduces visual noise from static_cast<size_t>(...)
 */
template <typename T>
constexpr size_t sz(T value) noexcept {
  return static_cast<size_t>(value);
}

/**
 * @brief Helper function to convert any type to float cleanly
 * Useful for integer-to-float conversions in math-heavy code
 */
template <typename T>
constexpr float toFloat(T value) noexcept {
  return static_cast<float>(value);
}

/**
 * @brief Helper function to get underlying value of enum cleanly
 * Alternative to std::to_underlying (C++23) for older compilers
 */
template <typename Enum>
constexpr auto underlying(Enum e) noexcept -> std::underlying_type_t<Enum> {
  return static_cast<std::underlying_type_t<Enum>>(e);
}

/**
 * @brief Helper function to get VkCommandBuffer from RHICommandBuffer cleanly
 * Reduces cast noise when working with base class pointers
 */
inline VkCommandBuffer getVkCommandBuffer(void* nativeHandle) {
  return static_cast<VkCommandBuffer>(nativeHandle);
}

/**
 * @brief Tracy GPU profiling helpers to eliminate cast noise
 */
template<typename Cmd>
inline void tracyGpuCollect(TracyContext ctx, Cmd* cmd) {
#ifdef TRACY_ENABLE
  PNKR_PROFILE_GPU_COLLECT(ctx, static_cast<VkCommandBuffer>(cmd->nativeHandle()));
#endif
}

template<typename Cmd>
inline void tracyGpuZone(TracyContext ctx, Cmd* cmd, const char* name) {
#ifdef TRACY_ENABLE
  PNKR_PROFILE_GPU_ZONE(ctx, static_cast<VkCommandBuffer>(cmd->nativeHandle()), name);
#endif
}

/**
 * @brief Helper to get VkImage from RHI texture base class
 */
inline VkImage getVkImageFromRHI(void* nativeHandle) {
  return static_cast<VkImage>(nativeHandle);
}

/**
 * @brief Helper to get VkBuffer from RHI buffer base class
 */
inline VkBuffer getVkBufferFromRHI(void* nativeHandle) {
  return static_cast<VkBuffer>(nativeHandle);
}

/**
 * @brief Removes items from a vector based on a selection of indices.
 * The selection vector must be sorted.
 */
template <typename T, typename Index = uint32_t>
void eraseSelected(std::vector<T>& v, const std::vector<Index>& selection)
{
    // stable_partition moves elements to be erased to the end
    // we use binary_search to check if an element's original index is in the selection
    // Note: We use pointer arithmetic to deduce the original index.
    // This assumes 'v' has not been reallocated during this process (it hasn't).
    const T* basePtr = v.data();
    
    auto it = std::stable_partition(v.begin(), v.end(),
        [&selection, basePtr](const T& item) {
            // Return true if we want to KEEP the item (i.e., NOT in selection)
            // Index calculation:
            const auto index = static_cast<Index>(&item - basePtr);
            return !std::binary_search(selection.begin(), selection.end(), index);
        }
    );
    
    v.erase(it, v.end());
}

} // namespace pnkr::util
