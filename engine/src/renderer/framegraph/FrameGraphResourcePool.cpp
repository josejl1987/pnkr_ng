#include "pnkr/renderer/framegraph/FrameGraphResourcePool.hpp"
#include "pnkr/renderer/RenderResourceManager.h"
#include "pnkr/core/logger.hpp"
#include "pnkr/rhi/rhi_device.hpp"
#include "pnkr/rhi/BindlessManager.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"

namespace pnkr::renderer {

FrameGraphResourcePool::FrameGraphResourcePool(RHIRenderer* renderer)
    : m_renderer(renderer) {}

FrameGraphResourcePool::~FrameGraphResourcePool() {
    shutdown();
}

void FrameGraphResourcePool::shutdown() {
    // Release view handles first
    if (m_renderer && m_renderer->device() && m_renderer->device()->getBindlessManager()) {
        rhi::BindlessManager* bm = m_renderer->device()->getBindlessManager();
        for (auto& view : m_storageViews) {
            if (view.second.viewOwned) {
                rhi::TextureBindlessHandle handle = view.second.viewOwned->getStorageImageHandle();
                if (handle.isValid()) {
                    bm->releaseStorageImage(handle);
                }
            }
        }
    }
    m_storageViews.clear();

    // Release pooled textures
    if (m_renderer) {
        for (auto& pt : m_texturePool) {
            if (pt.handle.isValid()) {
                m_renderer->destroyTexture(pt.handle);
            }
        }
    }
    m_texturePool.clear();
}

void FrameGraphResourcePool::setResourceManager(RenderResourceManager* manager) {
    m_resourceMgr = manager;
}

void FrameGraphResourcePool::beginFrame(uint32_t /*viewportWidth*/, uint32_t /*viewportHeight*/, uint32_t frameCounter) {
    for (auto &pt : m_texturePool) {
        pt.inUse = false;
    }
    gcStorageViews(frameCounter);
}

void FrameGraphResourcePool::gcStorageViews(uint32_t currentFrame) {
    rhi::BindlessManager* bm = (m_renderer && m_renderer->device()) ? m_renderer->device()->getBindlessManager() : nullptr;
    
    for (auto it = m_storageViews.begin(); it != m_storageViews.end(); ) {
        if (currentFrame - it->second.lastUsedFrame > kStorageViewTTLFrames) {
            if (it->second.viewOwned) {
                rhi::TextureBindlessHandle handle = it->second.viewOwned->getStorageImageHandle();
                if (handle.isValid() && bm) {
                    bm->releaseStorageImage(handle);
                }
            }
            it = m_storageViews.erase(it);
        } else {
            ++it;
        }
    }
}

void FrameGraphResourcePool::allocateResources(
    const std::vector<FGHandle>& executionOrder,
    const std::vector<PassEntry>& passes,
    std::vector<ResourceEntry>& resources
) {
    for (FGHandle passHandle : executionOrder) {
        const PassEntry& pass = passes[passHandle.index];
        for (FGHandle resHandle : pass.creates) {
            ResourceEntry& res = resources[resHandle.index];
            if (res.isImported || res.parent.isValid()) {
                continue;
            }

            bool found = false;
            for (auto& pooled : m_texturePool) {
                if (!pooled.inUse && pooled.format == res.info.format &&
                    pooled.w == res.w && pooled.h == res.h &&
                    pooled.mips == std::max(1U, res.info.mipLevels)) {

                    pooled.inUse = true;
                    // Re-wrap the raw handle into a TexturePtr.
                    // IMPORTANT: TexturePtr ctor usually takes (RenderResourceManager*, TextureHandle).
                    // We need to ensure we have m_resourceMgr set.
                    if (m_resourceMgr) {
                         res.physicalHandle = TexturePtr(m_renderer->resourceManager(), pooled.handle);
                    } else {
                        // Fallback or error? FrameGraph usually has manager set.
                         core::Logger::Render.error("FrameGraphResourcePool: ResourceManager not set during allocation!");
                    }
                    found = true;
                    break;
                }
            }

            if (!found) {
                rhi::TextureDescriptor desc{
                    .type = rhi::TextureType::Texture2D,
                    .extent = {.width = res.w, .height = res.h, .depth = 1},
                    .format = res.info.format,
                    .usage = rhi::TextureUsage::Sampled |
                             rhi::TextureUsage::Storage |
                             rhi::TextureUsage::TransferSrc |
                             rhi::TextureUsage::TransferDst,
                    .mipLevels = std::max(1U, res.info.mipLevels),
                    .arrayLayers = 1,
                    .sampleCount = 1,
                    .debugName = res.name,
                };

                bool isDepth =
                    (res.info.format == rhi::Format::D16_UNORM ||
                     res.info.format == rhi::Format::D32_SFLOAT ||
                     res.info.format == rhi::Format::D24_UNORM_S8_UINT);
                if (isDepth) {
                    desc.usage |= rhi::TextureUsage::DepthStencilAttachment;
                } else {
                    desc.usage |= rhi::TextureUsage::ColorAttachment;
                }

                if (m_renderer) {
                    res.physicalHandle = m_renderer->createTexture(res.name.c_str(), desc);
                    if (res.physicalHandle.isValid()) {
                         m_texturePool.push_back({
                             .handle = res.physicalHandle.handle(),
                             .format = res.info.format,
                             .w = res.w,
                             .h = res.h,
                             .mips = desc.mipLevels,
                             .inUse = true
                         });
                    }
                }
            }
        }
    }
}

rhi::TextureBindlessHandle FrameGraphResourcePool::getStorageImageIndex(
    FGHandle handle,
    const std::vector<ResourceEntry>& resources,
    rhi::RHITexture* rootTex,
    uint32_t frameCounter
) {
    if (!handle.isValid() || handle.index >= resources.size() || !rootTex) {
        return rhi::TextureBindlessHandle::Invalid;
    }

    const ResourceEntry& res = resources[handle.index];
    if ((res.isBackbuffer || res.isImported) && !res.allowStorageImageBindless) {
        return rhi::TextureBindlessHandle::Invalid;
    }

    rhi::RHIDevice *device = ((m_renderer != nullptr) ? m_renderer->device() : nullptr);
    if (device == nullptr) {
        return rhi::TextureBindlessHandle::Invalid;
    }

    const uint32_t texMips = std::max(1U, rootTex->mipLevels());
    const uint32_t texLayers = std::max(1U, rootTex->arrayLayers());

    const uint32_t baseMip = handle.baseMipLevel;
    const uint32_t baseLayer = handle.baseArrayLayer;

    const uint32_t reqMipCount = (handle.levelCount == BINDLESS_INVALID_ID) ? (texMips - baseMip) : handle.levelCount;
    const uint32_t reqLayerCount = (handle.layerCount == BINDLESS_INVALID_ID) ? (texLayers - baseLayer) : handle.layerCount;

    const uint32_t mipCount = (baseMip < texMips) ? std::min(reqMipCount, texMips - baseMip) : 0U;
    const uint32_t layerCount = (baseLayer < texLayers) ? std::min(reqLayerCount, texLayers - baseLayer) : 0U;

    if (mipCount == 0 || layerCount == 0) {
        return rhi::TextureBindlessHandle::Invalid;
    }

    rhi::Format fmt = res.info.format;
    StorageViewKey key{ .root = rootTex, .baseMip = baseMip, .mipCount = mipCount, .baseArrayLayer = baseLayer, .layerCount = layerCount, .format = fmt };

    if (auto it = m_storageViews.find(key); it != m_storageViews.end()) {
        it->second.lastUsedFrame = frameCounter;
        return it->second.view->getStorageImageHandle();
    }

    CachedStorageView entry{};
    entry.lastUsedFrame = frameCounter;

    const bool fullView = (baseMip == 0) && (mipCount == texMips) && (baseLayer == 0) && (layerCount == texLayers);
    if (fullView) {
        entry.view = rootTex;
    } else {
        rhi::TextureViewDescriptor vd{ .mipLevel = baseMip, .mipCount = mipCount, .arrayLayer = baseLayer, .layerCount = layerCount, .format = fmt, .debugName = res.name + "_View" };
        entry.viewOwned = device->createTextureView(vd.debugName.c_str(), rootTex, vd);
        entry.view = entry.viewOwned.get();
    }

    if (entry.view == nullptr) {
        return rhi::TextureBindlessHandle::Invalid;
    }

    rhi::TextureBindlessHandle idx = entry.view->getStorageImageHandle();
    if (!idx.isValid()) {
        idx = device->getBindlessManager()->registerStorageImage(entry.view);
        entry.view->setStorageImageHandle(idx);
    }

    m_storageViews.emplace(key, std::move(entry));
    return idx;
}

} // namespace pnkr::renderer
