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

#include "pnkr/renderer/framegraph/FGTypes.hpp"
#include "pnkr/renderer/framegraph/BarrierSolver.hpp"
#include "pnkr/renderer/framegraph/FrameGraphResourcePool.hpp"

namespace pnkr::renderer {

class RenderResourceManager;

class FrameGraph;

class FrameGraphBuilder {
public:
    FrameGraphBuilder(FrameGraph& graph, FGHandle passNode);

    FGHandle create(const std::string& name, const FGResourceCreateInfo& info);

    FGHandle import(const std::string& name, rhi::RHITexture* texture, rhi::ResourceLayout currentLayout = rhi::ResourceLayout::Undefined,
                    bool isBackbuffer = false, bool allowStorageImageBindless = true);
    
    FGHandle importBuffer(const std::string& name, rhi::RHIBuffer* buffer, rhi::ResourceLayout currentLayout = rhi::ResourceLayout::Undefined);

    FGHandle read(FGHandle handle, FGAccess access = FGAccess::SampledRead);
    FGHandle write(FGHandle handle, FGAccess access = FGAccess::ColorAttachmentWrite);

    // Creates a view of an existing resource
    FGHandle view(FGHandle handle, uint32_t baseMip, uint32_t mipCount, uint32_t baseArrayLayer, uint32_t layerCount);

    const FGResourceCreateInfo& getResourceInfo(FGHandle handle) const;

private:
    FrameGraph& m_graph;
    FGHandle m_passNode;
};

class FrameGraphResources {
public:
    FrameGraphResources(FrameGraph& graph, FGHandle passNode);

    rhi::RHITexture* getTexture(FGHandle handle) const;
    rhi::RHIBuffer* getBuffer(FGHandle handle) const;
    const FGResourceCreateInfo& getResourceInfo(FGHandle handle) const;
    rhi::ResourceLayout getTextureLayout(FGHandle handle) const;

    // Helper to get bindless index. Returns Invalid if not applicable (e.g. backbuffer without storage)
    rhi::TextureBindlessHandle getTextureIndex(FGHandle handle) const;
    rhi::TextureBindlessHandle getStorageImageIndex(FGHandle handle) const;

private:
    FrameGraph& m_graph;
    FGHandle m_passNode;
};

class FrameGraph {
public:
    FrameGraph(RHIRenderer* renderer);
    ~FrameGraph();

    void setResourceManager(RenderResourceManager* manager);

    void beginFrame(uint32_t viewportWidth, uint32_t viewportHeight);

    // Creates a pass node. The executor will be called during execute()
    template<typename T>
    FGHandle addPass(const std::string& name, std::function<void(FrameGraphBuilder&, T&)>&& setup, std::function<void(const T&, const FrameGraphResources&, rhi::RHICommandList*)>&& exec) {
        auto data = std::make_shared<T>();
        FGHandle passNode = createPassNode(name, data, 
            [exec = std::move(exec)](const void* d, const FrameGraphResources& r, rhi::RHICommandList* c) {
                exec(*static_cast<const T*>(d), r, c);
            });
        
        FrameGraphBuilder builder(*this, passNode);
        setup(builder, *data);
        return passNode;
    }

    FGHandle import(const std::string& name, rhi::RHITexture* texture, rhi::ResourceLayout currentLayout, bool isBackbuffer, bool allowStorageImageBindless);
    FGHandle importBuffer(const std::string& name, rhi::RHIBuffer* buffer, rhi::ResourceLayout currentLayout);

    void compile();
    void execute(rhi::RHICommandList* cmd);

    rhi::ResourceLayout getFinalLayout(rhi::RHITexture* texture) const {
        if (auto it = m_importedLayoutCache.find(texture); it != m_importedLayoutCache.end()) {
            return it->second;
        }
        return rhi::ResourceLayout::Undefined;
    }
    
    // Internal use by builder/resources
    FGHandle createResourceNode(const std::string& name, const FGResourceCreateInfo& info);
    FGHandle createBufferNode(const std::string& name, rhi::RHIBuffer* buffer);
    FGHandle createPassNode(const std::string& name, std::shared_ptr<void> data, std::function<void(const void*, const FrameGraphResources&, rhi::RHICommandList*)>&& exec);

    FGHandle getResourceHandle(const std::string& name) const;
    rhi::RHITexture* getTexture(FGHandle handle);
    rhi::RHIBuffer* getBuffer(FGHandle handle);
    rhi::TextureBindlessHandle getStorageImageIndex(FGHandle handle);
    
    friend class FrameGraphBuilder;
    friend class FrameGraphResources;
    friend class BarrierSolver;

private:
    RHIRenderer* m_renderer = nullptr;
    RenderResourceManager* m_resourceMgr = nullptr;

    std::vector<PassEntry> m_passes;
    std::vector<ResourceEntry> m_resources;
    std::vector<FGHandle> m_executionOrder;
    std::unordered_map<std::string, FGHandle> m_resourceMap;
    
    // Extracted components
    std::unique_ptr<FrameGraphResourcePool> m_resourcePool;
    std::unordered_map<rhi::RHITexture*, rhi::ResourceLayout> m_importedLayoutCache;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_frameCounter = 0;
};

} // namespace pnkr::renderer
