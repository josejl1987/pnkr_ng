#pragma once

#include "pnkr/core/common.hpp"
#include "pnkr/core/Handle.h"
#include "pnkr/renderer/rhi_renderer.hpp"

#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace pnkr::renderer {

struct FGHandle {
    uint32_t index = std::numeric_limits<uint32_t>::max();

    uint32_t baseMipLevel = 0;
    uint32_t levelCount   = 0xFFFFFFFFu;
    uint32_t baseArrayLayer = 0;
    uint32_t layerCount     = 0xFFFFFFFFu;

    bool isValid() const { return index != std::numeric_limits<uint32_t>::max(); }

    bool operator==(const FGHandle& o) const {
        return index == o.index &&
               baseMipLevel == o.baseMipLevel &&
               levelCount == o.levelCount &&
               baseArrayLayer == o.baseArrayLayer &&
               layerCount == o.layerCount;
    }
};

enum class FGAccess : uint8_t
{
    SampledRead,
    StorageRead,
    StorageWrite,
    ColorAttachmentWrite,
    DepthAttachmentWrite,
    DepthSampledRead,
    TransferSrc,
    TransferDst,
    Present,
    ColorAttachmentRead,
    DepthAttachmentRead,
    
    VertexBufferRead,
    IndexBufferRead,
    UniformBufferRead,
    IndirectBufferRead,
    IndirectBufferWrite
};

struct FGUse {
    FGHandle h;
    FGAccess access;
};

struct FGResourceCreateInfo {
    std::string name;
    rhi::Format format = rhi::Format::Undefined;
    uint32_t width = 0;
    uint32_t height = 0;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    uint32_t mipLevels = 1;
    std::optional<rhi::ClearValue> clearValue;
};

class FrameGraph;
class FrameGraphResources;

struct ResourceEntry {
    std::string name;
    FGResourceCreateInfo info;
    FGHandle producer = {};
    rhi::RHITexture* importedPtr = nullptr;
    rhi::RHIBuffer* importedBufferPtr = nullptr;
    TexturePtr physicalHandle;
    BufferHandle physicalBufferHandle = {};
    rhi::ResourceLayout initialLayout = rhi::ResourceLayout::Undefined;
    rhi::ResourceLayout currentLayout = rhi::ResourceLayout::Undefined;
    std::vector<rhi::ResourceLayout> mipLayouts;

    rhi::ShaderStageFlags lastStages;
    bool lastWasWrite = false;

    bool isImported = false;
    bool isCulled = false;
    bool isBackbuffer = false;
    bool isBuffer = false;
    bool allowStorageImageBindless = false;
    uint32_t refCount = 0;
    uint32_t w = 0, h = 0;

    FGHandle parent = {};
    uint32_t baseMipLevel = 0;
    uint32_t levelCount   = 0xFFFFFFFFu;
    uint32_t baseArrayLayer = 0;
    uint32_t layerCount     = 0xFFFFFFFFu;
};

struct PassEntry {
    std::string name;
    std::shared_ptr<void> data;
    std::function<void(const void*, const FrameGraphResources&, rhi::RHICommandList*)> executor;
    std::vector<FGHandle> creates;
    std::vector<FGUse> reads;
    std::vector<FGUse> writes;
    uint32_t refCount = 0;
    bool isCulled = false;
};

} // namespace pnkr::renderer
