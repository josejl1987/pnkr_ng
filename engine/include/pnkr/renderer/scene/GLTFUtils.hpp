#pragma once

#include "pnkr/renderer/scene/Animation.hpp"
#include "pnkr/renderer/scene/transform.hpp"
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <filesystem>
#include <functional>
#include <glm/glm.hpp>
#include <string_view>
#include <vector>

namespace pnkr::renderer::rhi {
enum class SamplerAddressMode;
}

namespace pnkr::renderer::scene {

std::vector<std::uint8_t> base64Decode(std::string_view in);

std::vector<std::uint8_t>
extractImageBytes(const fastgltf::Asset &gltf, const fastgltf::Image &image,
                  const std::filesystem::path &baseDir);

uint64_t hashImageBytes(const fastgltf::Asset &gltf,
                        const fastgltf::Image &image,
                        const std::filesystem::path &baseDir);

pnkr::renderer::rhi::SamplerAddressMode toAddressMode(fastgltf::Wrap wrap);

Transform toTransform(const fastgltf::Node &node);

template <typename T> uint32_t getTexCoordIndex(const T &info) {
  if constexpr (requires { info.texCoord; }) {
    return static_cast<uint32_t>(info.texCoord);
  }
  return 0;
}

void loadSkins(
    const fastgltf::Asset &gltf, std::vector<Skin> &outSkins,
    const std::function<uint32_t(size_t)> &nodeIndexMapper = [](size_t i) {
      return static_cast<uint32_t>(i);
    });

void loadAnimations(
    const fastgltf::Asset &gltf, std::vector<Animation> &outAnimations,
    const std::function<uint32_t(size_t)> &nodeIndexMapper = [](size_t i) {
      return static_cast<uint32_t>(i);
    });

} // namespace pnkr::renderer::scene
