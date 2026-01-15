# PNKR Engine - Agent Guidelines

This document provides essential information for autonomous agents working on the PNKR Engine codebase.

## 🛠 Build, Test, and Lint Commands

### Environment Setup
The project uses **CMake (3.25+)** and **vcpkg** for dependency management.
Target C++ Standard: **C++20/23**.

### Build Commands
- **Configure**:
  ```bash
  cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[vcpkg_path]/scripts/buildsystems/vcpkg.cmake
  ```
- **Build (All)**:
  ```bash
  cmake --build build --config Debug
  ```
- **Build Single Target**:
  ```bash
  cmake --build build --target pnkr_engine --config Debug
  ```

### Test Commands
- **Run All Tests**:
  ```bash
  ctest --test-dir build -C Debug --output-on-failure
  ```
- **Run Single Test Case** (using doctest):
  ```bash
  ./build/tests/Debug/pnkr_tests --test-case="YourTestCaseName"
  ```
- **List All Tests**:
  ```bash
  ./build/tests/Debug/pnkr_tests --list-test-cases
  ```

### Lint and Format
- **Auto-format**:
  ```bash
  cmake --build build --target format
  ```
- **Manual format**: Use `clang-format -i <file>` following the project's `.clang-format`.
- **Clang-Tidy**:
  ```bash
  python run-clang-tidy.py -p build
  ```

---

## 🎨 Code Style Guidelines

### Naming Conventions
- **Namespaces**: `pnkr::subsystem` (e.g., `pnkr::renderer`, `pnkr::rhi`, `pnkr::core`).
- **Classes/Structs**: `PascalCase` (e.g., `AsyncLoaderStagingManager`).
- **Methods/Functions**: `camelCase` (e.g., `waitForPages`).
- **Variables**: `camelCase` (e.g., `maxBatchToWaitFor`).
- **Member Variables**: `m_camelCase` (e.g., `m_ringBufferMapped`).
- **Constants**: `kPascalCase` (e.g., `kRingBufferSize`).
- **Enums**: `PascalCase` for type and values (e.g., `LogLevel::Error`).
- **Files**: `.hpp` for headers, `.cpp` for implementations. Names should match the primary class or purpose (e.g., `FrameGraph.hpp`).

### Includes and Imports
- **Header Guards**: Use `#pragma once`.
- **Order**:
  1. Main header for the file (e.g., `AsyncLoaderStagingManager.hpp`).
  2. Project headers (e.g., `pnkr/core/logger.hpp`).
  3. Third-party headers (e.g., `quill/Logger.h`).
  4. Standard library headers (e.g., `<vector>`).
- **Paths**: Use absolute-style paths from `engine/include` (e.g., `#include "pnkr/core/common.hpp"`).

### Types and Safety
- **Fixed-size Integers**: Prefer `uint32_t`, `uint64_t`, `int32_t`, etc., from `<cstdint>`.
- **Strings**: Use `std::string_view` for read-only parameters, `std::string` for ownership.
- **Smart Pointers**: Use `std::unique_ptr` for ownership; `std::shared_ptr` only when necessary. Avoid raw `new`/`delete`.
- **Error Handling**: 
  - Use `pnkr::core::Result<T>` (alias for `std::expected<T, std::string>`) for recoverable errors.
  - Use `pnkr::core::Logger` (e.g., `core::Logger::Renderer.error(...)`).
  - Use `assert` for developer-only invariant checks.
- **Thread Safety**: Use `std::atomic` for simple states, `std::mutex` or `std::shared_mutex` for complex data. Prefer lock-free structures where performance is critical.

---

## 🏛 Architecture & Systems

### 🖼 FrameGraph
The FrameGraph manages rendering passes and resource lifetimes. 
- **Resources**: Declared as handles (e.g., `FGTextureHandle`).
- **Passes**: Implement `IRenderPass` or use the lambda-based setup.
- **Transience**: Most textures in the FrameGraph are transient and reused across frames.

### 🔗 Bindless RHI
The engine uses a modern Vulkan RHI with a bindless architecture.
- **Descriptors**: Most resources (textures, buffers) are accessed via indices in global descriptor sets.
- **Indirect Drawing**: Prefer `drawIndexedIndirect` for batching multiple meshes into a single call.

### 🧵 Task System
Heavy tasks (loading, culling, physics) should be offloaded to the `TaskSystem`.
- Uses `enkits` as the underlying scheduler.
- Example: `TaskSystem::execute([]() { /* job */ });`

### 📦 Asset Management
- **VFS**: The Virtual File System handles path resolution (e.g., `vfs://shaders/`).
- **Async Loading**: Use `AsyncLoader` for non-blocking texture/mesh loading.

### 🎆 Shaders
Shaders are written in **Slang** and located in `engine/src/renderer/shaders/`.
- Shared headers between C++ and Slang often reside in `shared/` directories.
- Hot-reloading is supported for rapid iteration.

---

## 🛠 Development Tips & Common Pitfalls

- **Vulkan Synchronization**: When working with the RHI directly, ensure proper barriers and image layout transitions. The FrameGraph handles most of this automatically.
- **Memory Aliasing**: The FrameGraph resource pool aliases memory for transient resources. Never hold onto a `FGTextureHandle` across frames.
- **Thread Safety in ECS**: Component access is generally not thread-safe unless through specific parallel systems.
- **VFS Paths**: Always use the `vfs://` prefix when loading assets to ensure cross-platform path resolution.
- **Performance Profiling**: Use the built-in profiler (`pnkr/core/profiler.hpp`) to instrument expensive sections. View results in the ImGui profiler window.

---

## 🤖 Agent Workflow

1. **Analyze**: Use `grep` and `glob` to find relevant files. Read existing headers before implementation.
2. **Context**: Understand the `pnkr::` namespace hierarchy and how the current task fits into the FrameGraph or ECS.
3. **Plan**: Draft a concise plan. If modifying the RHI, be mindful of Vulkan synchronization.
4. **Implement**: 
   - Follow the naming conventions strictly (especially `m_` prefix for members).
   - Use `Result<T>` for functions that can fail.
   - Keep implementations in `.cpp` unless it's a template or header-only utility.
5. **Verify**:
   - Build using `cmake --build build`.
   - Run relevant tests using `./build/tests/Debug/pnkr_tests --test-case="..."`.
   - If UI changes, verify with the `triangle` or `scene_editor` samples.

---

## 📜 Repository Structure
- `engine/`: Core engine code.
  - `include/pnkr/`: Public headers.
  - `src/`: Private implementations.
- `samples/`: Example applications.
- `tests/`: Unit tests using doctest.
- `cmake/`: CMake build scripts and modules.
- `vcpkg.json`: Dependency manifest.
