#pragma once

#include "pnkr/renderer/framegraph/FGTypes.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/core/common.hpp"

#include <vector>
#include <unordered_map>
#include <memory>

namespace pnkr::renderer {

class RenderResourceManager;
class FrameGraph;

class FrameGraphResourcePool {
public:
    FrameGraphResourcePool(RHIRenderer* renderer);
    ~FrameGraphResourcePool();

    void setResourceManager(RenderResourceManager* manager);
    void beginFrame(uint32_t viewportWidth, uint32_t viewportHeight, uint32_t frameCounter);

    // Allocates physical resources for the resources that need them in the current frame
    void allocateResources(
        const std::vector<FGHandle>& executionOrder,
        const std::vector<PassEntry>& passes,
        std::vector<ResourceEntry>& resources
    );

    // Gets or creates a bindless handle for a storage image view of a resource
    rhi::TextureBindlessHandle getStorageImageIndex(
        FGHandle handle,
        const std::vector<ResourceEntry>& resources,
        rhi::RHITexture* rootTexture,
        uint32_t frameCounter
    );
    
    // Cleanup resources
    void shutdown();

private:
    struct PooledTexture {
        TextureHandle handle; 
        rhi::Format format = rhi::Format::Undefined;
        uint32_t w = 0;
        uint32_t h = 0;
        uint32_t mips = 1;
        bool inUse = false;
    };

    struct CachedStorageView {
        std::shared_ptr<rhi::RHITexture> viewOwned;
        rhi::RHITexture* view = nullptr;
        uint32_t lastUsedFrame = 0;
    };

    struct StorageViewKey {
        rhi::RHITexture* root = nullptr;
        uint32_t baseMip = 0;
        uint32_t mipCount = 0;
        uint32_t baseArrayLayer = 0;
        uint32_t layerCount = 0;
        rhi::Format format = rhi::Format::Undefined;

        bool operator==(const StorageViewKey& o) const {
            return root == o.root && baseMip == o.baseMip && mipCount == o.mipCount &&
                   baseArrayLayer == o.baseArrayLayer && layerCount == o.layerCount &&
                   format == o.format;
        }
    };

    struct StorageViewKeyHash {
        std::size_t operator()(const StorageViewKey& k) const {
            std::size_t h = 0;
            auto hash_combine = [&](std::size_t v) {
                h ^= v + 0x9e3779b9 + (h << 6) + (h >> 2);
            };
            hash_combine(std::hash<void*>{}(k.root));
            hash_combine(std::hash<uint32_t>{}(k.baseMip));
            hash_combine(std::hash<uint32_t>{}(k.mipCount));
            hash_combine(std::hash<uint32_t>{}(k.baseArrayLayer));
            hash_combine(std::hash<uint32_t>{}(k.layerCount));
            hash_combine(static_cast<std::size_t>(k.format));
            return h;
        }
    };

    RHIRenderer* m_renderer = nullptr;
    RenderResourceManager* m_resourceMgr = nullptr;

    std::vector<PooledTexture> m_texturePool;
    std::unordered_map<StorageViewKey, CachedStorageView, StorageViewKeyHash> m_storageViews;

    static constexpr uint32_t kStorageViewTTLFrames = 60;

    void gcStorageViews(uint32_t currentFrame);
};

} // namespace pnkr::renderer
