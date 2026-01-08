
#include "pnkr/renderer/framegraph/FrameGraph.hpp"
#include "pnkr/renderer/RenderResourceManager.h"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/rhi/rhi_device.hpp"
#include "rhi/vulkan/vulkan_utils.hpp"
#include "pnkr/renderer/framegraph/BarrierSolver.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

namespace pnkr::renderer {

    static std::string phaseForPassName(std::string_view n) {
        auto has = [&](const char* s) { return n.find(s) != std::string_view::npos; };
        if (has("Shadow")) {
          return "Shadow";
        }
        if (has("Culling")) {
          return "Culling";
        }
        if (has("Geometry") || has("Opaque") || has("Transparent") ||
            has("OIT")) {
          return "Geometry";
        }
        if (has("SSAO")) {
          return "SSAO";
        }
        if (has("Transmission")) {
          return "Transmission";
        }
        if (has("Post")) {
          return "PostProcess";
        }
        if (has("UI") || has("ImGui")) {
          return "UI";
        }
        return "Misc";
    }

    FrameGraphBuilder::FrameGraphBuilder(FrameGraph& graph, FGHandle passNode)
        : m_graph(graph), m_passNode(passNode) {}

    FGHandle FrameGraphBuilder::create(const std::string& name, const FGResourceCreateInfo& info) {
        FGHandle h = m_graph.createResourceNode(name, info);
        m_graph.m_passes[m_passNode.index].creates.push_back(h);
        m_graph.m_resources[h.index].producer = m_passNode;
        return h;
    }

    FGHandle FrameGraphBuilder::import(const std::string& name, rhi::RHITexture* texture, rhi::ResourceLayout currentLayout, bool isBackbuffer, bool allowStorageImageBindless) {
        return m_graph.import(name, texture, currentLayout, isBackbuffer, allowStorageImageBindless);
    }

    FGHandle FrameGraphBuilder::importBuffer(const std::string& name, rhi::RHIBuffer* buffer, rhi::ResourceLayout currentLayout) {
        return m_graph.importBuffer(name, buffer, currentLayout);
    }

    FGHandle FrameGraphBuilder::read(FGHandle handle, FGAccess access) {
        if (!handle.isValid() || handle.index >= m_graph.m_resources.size()) {
          core::Logger::Render.error(
              "Pass '{}' attempted to read invalid resource index {}",
              m_graph.m_passes[m_passNode.index].name, handle.index);
          return handle;
        }
        m_graph.m_passes[m_passNode.index].reads.push_back({ .h = handle, .access = access });
        return handle;
    }

    FGHandle FrameGraphBuilder::write(FGHandle handle, FGAccess access) {
        if (!handle.isValid() || handle.index >= m_graph.m_resources.size()) {
          core::Logger::Render.error(
              "Pass '{}' attempted to write invalid resource index {}",
              m_graph.m_passes[m_passNode.index].name, handle.index);
          return handle;
        }
        m_graph.m_passes[m_passNode.index].writes.push_back({ .h = handle, .access = access });
        return handle;
    }

    FGHandle FrameGraphBuilder::view(FGHandle handle, uint32_t baseMip, uint32_t mipCount, uint32_t baseLayer, uint32_t layerCount) {
        FGHandle v = handle;
        v.baseMipLevel = baseMip;
        v.levelCount = mipCount;
        v.baseArrayLayer = baseLayer;
        v.layerCount = layerCount;

        if (!handle.isValid()) {
          return {};
        }

        FGResourceCreateInfo info = m_graph.m_resources[handle.index].info;
        info.name = m_graph.m_resources[handle.index].name + "/view";

        FGHandle h = m_graph.createResourceNode(info.name, info);

        auto& parentRes = m_graph.m_resources[handle.index];
        auto& res = m_graph.m_resources[h.index];

        res.parent = handle;
        res.isImported = parentRes.isImported;
        res.importedPtr = parentRes.importedPtr;
        res.physicalHandle = parentRes.physicalHandle;
        res.isBackbuffer = parentRes.isBackbuffer;
        res.allowStorageImageBindless = parentRes.allowStorageImageBindless;

        res.initialLayout = parentRes.initialLayout;
        res.currentLayout = parentRes.currentLayout;

        res.baseMipLevel = baseMip;
        res.levelCount = mipCount;
        res.baseArrayLayer = baseLayer;
        res.layerCount = layerCount;
        res.producer = m_passNode;

        m_graph.m_passes[m_passNode.index].creates.push_back(h);

        h.baseMipLevel = baseMip;
        h.levelCount = mipCount;
        h.baseArrayLayer = baseLayer;
        h.layerCount = layerCount;
        return h;
    }

    const FGResourceCreateInfo& FrameGraphBuilder::getResourceInfo(FGHandle handle) const {
        return m_graph.m_resources[handle.index].info;
    }

    FrameGraphResources::FrameGraphResources(FrameGraph& graph, FGHandle passNode)
        : m_graph(graph), m_passNode(passNode) {}

    rhi::RHITexture* FrameGraphResources::getTexture(FGHandle handle) const {
        return m_graph.getTexture(handle);
    }

    rhi::ResourceLayout FrameGraphResources::getTextureLayout(FGHandle handle) const {
        if (!handle.isValid() || handle.index >= m_graph.m_resources.size()) {
            return rhi::ResourceLayout::Undefined;
        }
        const ResourceEntry& entry = m_graph.m_resources[handle.index];
        const ResourceEntry& rootEntry = entry.parent.isValid() ? m_graph.m_resources[entry.parent.index] : entry;

        // If it's a subresource view, use the specific mip layout from the root
        if (!rootEntry.mipLayouts.empty() && handle.baseMipLevel < rootEntry.mipLayouts.size()) {
            return rootEntry.mipLayouts[handle.baseMipLevel];
        }
        return rootEntry.currentLayout; // Fallback to general if not mip-specific or for buffers
    }

    rhi::RHIBuffer* FrameGraphResources::getBuffer(FGHandle handle) const {
        return m_graph.getBuffer(handle);
    }

    const FGResourceCreateInfo& FrameGraphResources::getResourceInfo(FGHandle handle) const {
        return m_graph.m_resources[handle.index].info;
    }

    rhi::TextureBindlessHandle FrameGraphResources::getTextureIndex(FGHandle handle) const {
        if (!handle.isValid() || handle.index >= m_graph.m_resources.size()) {
          return rhi::TextureBindlessHandle::Invalid;
        }

        const auto& res = m_graph.m_resources[handle.index];
        if (res.isImported || res.isBackbuffer) {
          auto* tex = getTexture(handle);
          return tex ? tex->getBindlessHandle()
                     : rhi::TextureBindlessHandle::Invalid;
        }

        if (!res.physicalHandle.isValid()) {
          return rhi::TextureBindlessHandle::Invalid;
        }

        return m_graph.m_renderer->getTextureBindlessIndex(
            res.physicalHandle.handle());
    }

    rhi::TextureBindlessHandle FrameGraphResources::getStorageImageIndex(FGHandle handle) const {
        return m_graph.getStorageImageIndex(handle);
    }

    FrameGraph::FrameGraph(RHIRenderer* renderer)
        : m_renderer(renderer) {
        m_resourcePool = std::make_unique<FrameGraphResourcePool>(renderer);
    }

    FrameGraph::~FrameGraph() {
        // Resource pool handles cleanup
    }

    void FrameGraph::setResourceManager(RenderResourceManager* manager) {
        m_resourceMgr = manager;
        if (m_resourcePool) {
            m_resourcePool->setResourceManager(manager);
        }
    }

    void FrameGraph::beginFrame(uint32_t viewportWidth, uint32_t viewportHeight) {
        m_width = viewportWidth;
        m_height = viewportHeight;
        m_passes.clear();
        m_resources.clear();
        m_executionOrder.clear();
        m_resourceMap.clear();
        m_frameCounter++;

        if (m_resourcePool) {
            m_resourcePool->beginFrame(viewportWidth, viewportHeight, m_frameCounter);
        }
    }

    rhi::TextureBindlessHandle FrameGraph::getStorageImageIndex(FGHandle handle) {
        if (!handle.isValid()) return rhi::TextureBindlessHandle::Invalid;
        
        rhi::RHITexture* rootTex = getTexture(handle);
        if (m_resourcePool) {
            return m_resourcePool->getStorageImageIndex(handle, m_resources, rootTex, m_frameCounter);
        }
        return rhi::TextureBindlessHandle::Invalid;
    }

    FGHandle FrameGraph::createPassNode(const std::string& name, std::shared_ptr<void> data, std::function<void(const void*, const FrameGraphResources&, rhi::RHICommandList*)>&& exec) {
        FGHandle h{ .index = static_cast<uint32_t>(m_passes.size()) };
        m_passes.push_back({.name = name,
                            .data = std::move(data),
                            .executor = std::move(exec),
                            .creates = {},
                            .reads = {},
                            .writes = {},
                            .refCount = 0,
                            .isCulled = false});
        return h;
    }

    FGHandle FrameGraph::createResourceNode(const std::string& name, const FGResourceCreateInfo& info) {
        FGHandle h{ .index = static_cast<uint32_t>(m_resources.size()) };

        uint32_t w = info.width;
        uint32_t hPx = info.height;
        if (info.scaleX > 0.0F) {
          w = static_cast<uint32_t>(m_width * info.scaleX);
        }
        if (info.scaleY > 0.0F) {
          hPx = static_cast<uint32_t>(m_height * info.scaleY);
        }
        w = std::max(1U, w);
        hPx = std::max(1U, hPx);

        ResourceEntry entry{.name = name,
                            .info = info,
                            .producer = {},
                            .importedPtr = nullptr,
                            .importedBufferPtr = nullptr,
                            .physicalHandle = {},
                            .physicalBufferHandle = {},
                            .initialLayout = rhi::ResourceLayout::Undefined,
                            .currentLayout = rhi::ResourceLayout::Undefined,
                            .mipLayouts = {},
                            .lastStages = {},
                            .lastWasWrite = false,
                            .isImported = false,
                            .isCulled = false,
                            .isBackbuffer = false,
                            .isBuffer = false,
                            .allowStorageImageBindless = true,
                            .refCount = 0,
                            .w = w,
                            .h = hPx,
                            .parent = {},
                            .baseMipLevel = 0,
                            .levelCount = BINDLESS_INVALID_ID,
                            .baseArrayLayer = 0,
                            .layerCount = BINDLESS_INVALID_ID};

        entry.mipLayouts.assign(std::max(1U, info.mipLevels),
                                rhi::ResourceLayout::Undefined);
        m_resources.push_back(std::move(entry));
        m_resourceMap[name] = h;
        return h;
    }

    FGHandle FrameGraph::createBufferNode(const std::string& name, rhi::RHIBuffer* buffer) {
      FGHandle h = createResourceNode(name, {.name = name,
                                             .format = rhi::Format::Undefined,
                                             .width = 0,
                                             .height = 0,
                                             .scaleX = 1.0F,
                                             .scaleY = 1.0F,
                                             .mipLevels = 1,
                                             .clearValue = std::nullopt});
      auto &res = m_resources[h.index];
      res.importedBufferPtr = buffer;
      res.isImported = true;
      res.isBuffer = true;
      return h;
    }

    FGHandle FrameGraph::import(const std::string& name, rhi::RHITexture* texture, rhi::ResourceLayout currentLayout, bool isBackbuffer, bool allowStorageImageBindless) {
      FGHandle h = createResourceNode(
          name, {.name = name,
                 .format = texture->format(),
                 .width = texture->extent().width,
                 .height = texture->extent().height,
                 .scaleX = 1.0F,
                 .scaleY = 1.0F,
                 .mipLevels = std::max(1U, texture->mipLevels()),
                 .clearValue = std::nullopt});
      auto &res = m_resources[h.index];
      res.importedPtr = texture;
      res.isImported = true;
      res.isBackbuffer = isBackbuffer;
      res.allowStorageImageBindless = allowStorageImageBindless;

      auto it = m_importedLayoutCache.find(texture);
      rhi::ResourceLayout startLayout =
          (it != m_importedLayoutCache.end()) ? it->second : currentLayout;
      if (isBackbuffer) {
        startLayout = rhi::ResourceLayout::Undefined;
      }

        res.initialLayout = startLayout;
        res.currentLayout = startLayout;
        res.mipLayouts.assign(res.info.mipLevels, startLayout);
        return h;
    }

    FGHandle FrameGraph::importBuffer(const std::string& name, rhi::RHIBuffer* buffer, rhi::ResourceLayout currentLayout) {
      FGHandle h = createResourceNode(name, {.name = name,
                                             .format = rhi::Format::Undefined,
                                             .width = 0,
                                             .height = 0,
                                             .scaleX = 1.0F,
                                             .scaleY = 1.0F,
                                             .mipLevels = 1,
                                             .clearValue = std::nullopt});
      auto &res = m_resources[h.index];
      res.importedBufferPtr = buffer;
      res.isImported = true;
      res.isBuffer = true;
      res.initialLayout = currentLayout;
      res.currentLayout = currentLayout;
      return h;
    }

    void FrameGraph::compile() {
        PNKR_LOG_SCOPE("FrameGraphCompile");
        std::vector<uint32_t> resourceRef(m_resources.size(), 0);

        for (auto& pass : m_passes) {
            pass.refCount = 0;
            for (auto& u : pass.writes) {
              if (u.h.index >= m_resources.size()) {
                core::Logger::Render.error(
                    "Pass '{}' references invalid resource index {}", pass.name,
                    u.h.index);
                continue;
              }
              if (m_resources[u.h.index].isImported) {
                pass.refCount = 1;
              }
            }
        }

        bool changed = true;
        while (changed) {
            changed = false;
            std::ranges::fill(resourceRef, 0);

            for (const auto& pass : m_passes) {
                if (pass.refCount > 0) {
                  for (const auto &u : pass.reads) {
                    if (u.h.index >= resourceRef.size()) {
                      core::Logger::Render.error(
                          "Pass '{}' references invalid resource index {}",
                          pass.name, u.h.index);
                      continue;
                    }
                    resourceRef[u.h.index]++;
                  }
                }
            }

            for (auto& pass : m_passes) {
                if (pass.refCount == 0) {
                    bool needed = false;
                    for (auto h : pass.creates) {
                      if (h.index >= resourceRef.size()) {
                        core::Logger::Render.error(
                            "Pass '{}' references invalid resource index {}",
                            pass.name, h.index);
                        continue;
                      }
                      if (resourceRef[h.index] > 0) {
                        needed = true;
                      }
                    }
                    for (auto &u : pass.writes) {
                      if (u.h.index >= resourceRef.size()) {
                        core::Logger::Render.error(
                            "Pass '{}' references invalid resource index {}",
                            pass.name, u.h.index);
                        continue;
                      }
                      if (resourceRef[u.h.index] > 0) {
                        needed = true;
                      }
                    }

                    if (needed) {
                        pass.refCount = 1;
                        changed = true;
                    }
                }
            }
        }

        for (uint32_t i = 0; i < m_passes.size(); ++i) {
            if (m_passes[i].refCount > 0) {
                m_executionOrder.push_back({ .index = i });
            } else {
                m_passes[i].isCulled = true;
            }
        }

        if (m_resourcePool) {
            m_resourcePool->allocateResources(m_executionOrder, m_passes, m_resources);
        }
    }

    rhi::RHITexture* FrameGraph::getTexture(FGHandle handle) {
      if (handle.index >= m_resources.size()) {
        return nullptr;
      }
        FGHandle h = handle;
        while (m_resources[h.index].parent.isValid()) {
            h = m_resources[h.index].parent;
        }
        auto& r = m_resources[h.index];
        if (r.isImported) {
          return r.importedPtr;
        }
        return m_renderer->getTexture(r.physicalHandle);
    }

    rhi::RHIBuffer* FrameGraph::getBuffer(FGHandle handle) {
      if (!handle.isValid() || handle.index >= m_resources.size()) {
        return nullptr;
      }
        auto& res = m_resources[handle.index];
        if (res.isImported) {
          return res.importedBufferPtr;
        }
        return nullptr;
    }

    FGHandle FrameGraph::getResourceHandle(const std::string& name) const {
        if (auto it = m_resourceMap.find(name); it != m_resourceMap.end()) {
            return it->second;
        }
        return {};
    }

    void FrameGraph::execute(rhi::RHICommandList* cmd) {
        PNKR_LOG_SCOPE("FrameGraphExecute");
        cmd->beginDebugLabel("FrameGraph", 0.25F, 0.25F, 0.25F, 1.0F);

        for (auto& res : m_resources) {
          if (!res.isImported) {
            res.currentLayout = rhi::ResourceLayout::Undefined;
          }
            res.lastStages = {};
            res.lastWasWrite = false;
        }

        std::string currentPhase;
        bool phaseOpen = false;

        for (auto passHandle : m_executionOrder) {
            PassEntry& pass = m_passes[passHandle.index];

            const std::string phase = phaseForPassName(pass.name);
            if (!phaseOpen || phase != currentPhase) {
              if (phaseOpen) {
                cmd->endDebugLabel();
              }
                currentPhase = phase;
                phaseOpen = true;
                cmd->beginDebugLabel(currentPhase.c_str(), 0.35F, 0.35F, 0.35F,
                                     1.0F);
            }

            cmd->beginDebugLabel(pass.name.c_str(), 0.5F, 0.5F, 0.5F, 1.0F);

            std::vector<rhi::RHIMemoryBarrier> barriers;
            BarrierSolver::solveBarriers(pass, m_resources, *this, barriers);

            if (!barriers.empty()) {
                rhi::ShaderStageFlags srcStages = rhi::ShaderStage::None;
                rhi::ShaderStageFlags dstStages = rhi::ShaderStage::None;
                for (const auto& b : barriers) {
                    srcStages |= b.srcAccessStage;
                    dstStages |= b.dstAccessStage;
                }
                cmd->pipelineBarrier(srcStages, dstStages, barriers);
            }

            FrameGraphResources res(*this, passHandle);
            pass.executor(pass.data.get(), res, cmd);

            cmd->endDebugLabel(); // Pass
        }

        if (phaseOpen) {
          cmd->endDebugLabel(); // Phase
        }

        cmd->endDebugLabel(); // FrameGraph

        for (auto& res : m_resources) {
          if (res.isImported && res.importedPtr) {
            m_importedLayoutCache[res.importedPtr] = res.currentLayout;
          }
        }
    }

} // namespace pnkr::renderer
