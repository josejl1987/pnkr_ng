#pragma once
#include <vector>
#include <string>
#include <variant>
#include <cstddef>
#include "pnkr/rhi/rhi_types.hpp"

namespace pnkr::assets {

    struct TextureSubresourceData {
        std::vector<std::byte> data;
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depth = 1;
        uint32_t mipLevel = 0;
        uint32_t arrayLayer = 0;
    };

    struct TextureAsset {
        std::string debugName;
        renderer::rhi::TextureType type = renderer::rhi::TextureType::Texture2D;
        renderer::rhi::Format format = renderer::rhi::Format::Undefined;
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depth = 1;
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;

        std::vector<TextureSubresourceData> subresources;

        size_t getTotalSizeBytes() const {
            size_t total = 0;
            for (const auto& sub : subresources) {
                total += sub.data.size();
            }
            return total;
        }
    };

    struct MeshAsset {
        std::string name;

        struct Surface {
            uint32_t firstIndex = 0;
            uint32_t indexCount = 0;
            uint32_t materialIndex = 0;
        };

        std::vector<std::byte> vertexData;
        std::vector<std::byte> indexData;

        uint32_t vertexStride = 0;
        uint32_t vertexCount = 0;
        renderer::rhi::Format indexFormat = renderer::rhi::Format::Undefined;

        std::vector<Surface> surfaces;
    };
}
