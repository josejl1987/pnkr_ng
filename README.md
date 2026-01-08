# PNKR Engine

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C++-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.3-red.svg)](https://www.vulkan.org/)

**PNKR** (pronounced "Ponker") is a modern, high-performance graphics engine built from the ground up using **Vulkan** and **C++20**. It focuses on modern rendering techniques, including bindless resource management, indirect drawing, and an efficient FrameGraph-based architecture.

> [!WARNING]
> **Research Work in Progress**: This project is currently a research-oriented work-in-progress. The API and architecture are highly experimental and subject to radical changes. It is not intended for production use.

---

## 🚀 Key Features

### 🎨 Rendering
- **Modern Vulkan Backend**: Leverages Bindless textures and Indirect rendering for high throughput.
- **FrameGraph Architecture**: Modular and efficient management of rendering passes and resource lifetimes.
- **PBR (Physically Based Rendering)**: Energy-conserving materials with support for IBL (Image Based Lighting).
- **Advanced Post-Processing**:
  - Auto-exposure & Bloom.
  - SSAO (Screen Space Ambient Occlusion).
  - High-quality tone mapping.
- **Shadows**: Robust shadow mapping system.
- **Frustum Culling**: GPU-driven culling for efficient rendering of complex scenes.
- **glTF Support**: Seamless loading of glTF assets via `fastgltf`, including `KHR_texture_transform` support.

### 🏗️ Core Engine
- **ECS (Entity Component System)**: Data-oriented design for high-performance entity management.
- **Async Asset Loading**: Threaded loading of textures and meshes to prevent frame stutters.
- **Slang Shaders**: Modern shader programming with Slang, allowing for flexible and powerful GPU logic.
- **Cross-Platform Foundation**: Built on SDL3 for robust windowing and input handling.
- **Developer Tools**: Integrated ImGui for real-time debugging and scene inspection.

---

## 📂 Repository Structure

```text
pnkr/
├── engine/         # Core engine source code and headers
│   ├── include/    # Public headers
│   └── src/        # Implementation files
├── samples/        # Example applications and demonstrations
├── shaders/        # Global shader source files (Slang)
├── third-party/    # External dependencies (fetched via CPM or Git)
├── tools/          # Helper scripts and development tools
└── tests/          # Unit and integration tests
```

---

## 🛠️ Getting Started

### Prerequisites
- **C++20 Compiler** (MSVC, Clang 16+, or GCC 13+)
- **Vulkan SDK** (1.3+)
- **CMake** (3.25+)
- **VCPKG** (Recommended for dependency management)

### Building the Project

1. **Clone the repository**:
   ```bash
   git clone https://github.com/josejl1987/pnkr_ng.git --recursive
   cd pnkr_ng
   ```

2. **Configure with CMake**:
   ```bash
   cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[path_to_vcpkg]/scripts/buildsystems/vcpkg.cmake
   ```

3. **Build**:
   ```bash
   cmake --build build --config Release
   ```

---

## 📜 License
PNKR is licensed under the MIT License. See [LICENSE](LICENSE) for details. (Note: Check the root for actual license file).
