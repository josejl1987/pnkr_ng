# PNKR Engine

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C++-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.3-red.svg)](https://www.vulkan.org/)

**PNKR** (pronounced "Ponker") is a modern graphics engine built with Vulkan and C++20. It's designed for learning and experimentation, focusing on contemporary rendering techniques like bindless resources, indirect drawing, and FrameGraph-based resource management.

> [!NOTE]
> **Research Project**: PNKR is an active research project. APIs and architecture may change as we explore new ideas. It's not intended for production use yet, but it's a great place to learn about modern graphics programming.

---

## What You Can Do With PNKR

PNKR gives you the building blocks for creating real-time 3D graphics applications. Here's what's ready to use:

### 🎨 Rendering

- **PBR Materials**: Physically based rendering with energy-conserving materials
- **Image-Based Lighting (IBL)**: Beautiful environment lighting with irradiance and prefiltered maps
- **Shadows**: Configurable shadow mapping with bias controls and automatic frustum fitting
- **Post-Processing**:
  - Bloom with threshold, knee, and firefly suppression
  - Screen-space ambient occlusion (SSAO)
  - Auto-exposure with histogram-based adaptation
  - Multiple tone mapping options (Reinhard, Uchimura, Khronos PBR)
- **Order-Independent Transparency**: Weighted blended OIT for transparent objects
- **MSAA**: Multi-sample anti-aliasing support

### 🚀 Performance Features

- **Bindless Resources**: Access textures and buffers via indices instead of descriptor sets
- **Indirect Drawing**: Batch thousands of draw calls into a single GPU dispatch
- **GPU Culling**: Compute-based frustum culling for large scene handling
- **Async Asset Loading**: Non-blocking texture and mesh loading to keep framerates smooth
- **Texture Streaming**: Progressive KTX texture loading with mip management

### 🏗️ Engine Systems

- **FrameGraph**: Automatic resource lifecycle management and barrier resolution
- **Scene Graph**: ECS-based entity-component system for flexible scene composition
- **Animation**: Animated and skinned meshes with blend support
- **Physics**: GPU-based cloth simulation with wind and constraints
- **Virtual File System**: Mount physical paths to virtual locations for portable asset loading

### 🛠️ Tools & Debugging

- **GPU Profiler**: Real-time timing queries, pipeline stats, memory tracking, and bottleneck detection
- **Debug Rendering**: 3D lines, boxes, spheres, frustums, and more
- **Scene Editor**: Full-featured editor with scene graph, material editor, and transform controls (ImGuizmo)
- **ImGui Integration**: Immediate mode UI for controls and visualization
- **Console Variables**: Runtime tweakable parameters with persistence

### 📦 Asset Pipeline

- **glTF 2.0**: Load models with materials, animations, and skins via `fastgltf`
- **KTX Textures**: Streaming texture support with BC7 compression
- **Slang Shaders**: Modern shading language with hot-reloading and caching

---

## Samples

PNKR includes over 15 samples demonstrating different features:

| Sample | Description |
|--------|-------------|
| `scene_editor` | Full-featured 3D scene editor with glTF loading, lighting controls, and material editing |
| `rhiIndirectGLTF` | GPU-driven rendering with glTF models, shadows, and PBR |
| `rhiMillionCubes` | Stress test with indirect drawing of 1,000,000+ instances |
| `rhiSprites` | 2D sprite rendering system |
| `rhiSkybox` | Environment mapping and skybox rendering |
| `rhiGrid` | Infinite grid rendering with fade effect |
| `debug_canvas` | Debug rendering visualization (lines, boxes, frustums) |
| `rhiComputeTexture` | Compute shader for texture processing |
| `rhiComputedMesh` | GPU mesh generation and processing |
| `rhiOffscreenMipRendering` | Offscreen rendering with automatic mip generation |

---

## Quick Start

### Prerequisites

- **C++20 compiler** (MSVC, Clang 16+, or GCC 13+)
- **Vulkan SDK** (1.3+)
- **CMake** (3.25+)
- **vcpkg** for dependencies

### Building

```bash
# Clone the repository
git clone https://github.com/josejl1987/pnkr_ng.git --recursive
cd pnkr_ng

# Configure with CMake (replace [vcpkg_path] with your vcpkg installation)
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[vcpkg_path]/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build --config Release

# Run a sample (Windows)
./build/bin/Release/scene_editor.exe
```

### Running Tests

```bash
# Run all tests
ctest --test-dir build -C Debug --output-on-failure

# Run specific test case
./build/tests/Debug/pnkr_tests --test-case="Vulkan Buffer Operations"

# List all available tests
./build/tests/Debug/pnkr_tests --list-test-cases
```

---

## Architecture Overview

### Vulkan RHI Layer

A thin abstraction over Vulkan that provides:
- Bindless descriptor management (textures, buffers, samplers, storage images)
- Timeline semaphores for multi-queue synchronization
- Pipeline state objects and pipeline caching
- Automatic barrier tracking and layout transitions

### FrameGraph

Declarative render pass system:
- Automatic resource lifetime management
- Transient resource pooling for memory efficiency
- Barrier solver for correct synchronization
- Pass-based execution with flexible dependencies

### Async Loader

Multi-threaded asset streaming:
- Dedicated I/O thread pool for file loading
- Staging buffer management for GPU uploads
- Progress tracking and cancellation support
- Thread-safe resource state machine

### Task System

Built on `enki::TaskScheduler`:
- Parallel-for loops for CPU-side work
- Pinned tasks for specific threads
- Integration with logging system for scoped tasks

---

## Development

### Code Style

- **Naming**: `PascalCase` for types, `camelCase` for functions, `m_camelCase` for members
- **Headers**: `#pragma once`, absolute-style includes from `engine/include`
- **Types**: Fixed-size integers (`uint32_t`), `std::string_view` for read-only strings
- **Error handling**: `pnkr::core::Result<T>` (alias for `std::expected`)

### Formatting & Linting

```bash
# Auto-format all files
cmake --build build --target format

# Run clang-tidy
python run-clang-tidy.py -p build
```

### Vulkan Testing with Lavapipe

For CI and hardware-independent testing:

```bash
# Install lavapipe (Ubuntu/Debian)
sudo apt-get install mesa-vulkan-drivers

# Set environment to use lavapipe
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lavapipe_icd.x86_64.json

# Run Vulkan tests
./build/tests/Debug/pnkr_vulkan_tests
```

---

## Project Structure

```
pnkr/
├── engine/              # Core engine code
│   ├── include/pnkr/    # Public headers
│   └── src/             # Implementations
├── samples/             # Example applications
├── tests/               # Unit tests (doctest)
├── engine/src/renderer/shaders/  # Slang shaders
└── cmake/               # CMake modules and scripts
```

---

## License

PNKR is licensed under the MIT License. See [LICENSE](LICENSE) for details.
