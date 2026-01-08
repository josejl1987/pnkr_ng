#include "pnkr/renderer/framegraph/BarrierSolver.hpp"
#include "pnkr/renderer/framegraph/FrameGraph.hpp"
#include "pnkr/core/logger.hpp"
#include <algorithm>

namespace pnkr::renderer {

    rhi::ResourceLayout BarrierSolver::accessToLayout(FGAccess a) {
        using RL = rhi::ResourceLayout;
        switch (a) {
            case FGAccess::SampledRead:         return RL::ShaderReadOnly;
            case FGAccess::DepthSampledRead:    return RL::DepthStencilReadOnly;
            case FGAccess::StorageRead:         return RL::General;
            case FGAccess::StorageWrite:        return RL::General;
            case FGAccess::ColorAttachmentWrite:return RL::ColorAttachment;
            case FGAccess::DepthAttachmentWrite:return RL::DepthStencilAttachment;
            case FGAccess::TransferSrc:         return RL::TransferSrc;
            case FGAccess::TransferDst:         return RL::TransferDst;
            case FGAccess::Present:             return RL::Present;
            case FGAccess::ColorAttachmentRead: return RL::ColorAttachment;
            case FGAccess::DepthAttachmentRead: return RL::DepthStencilAttachment;
            case FGAccess::VertexBufferRead:    return RL::VertexBufferRead;
            case FGAccess::IndexBufferRead:     return RL::IndexBufferRead;
            case FGAccess::UniformBufferRead:   return RL::UniformBufferRead;
            case FGAccess::IndirectBufferRead:  return RL::IndirectBufferRead;
            case FGAccess::IndirectBufferWrite: return RL::General;
        }
        return RL::General;
    }

    rhi::ShaderStageFlags BarrierSolver::accessToStage(FGAccess a) {
        using SS = rhi::ShaderStage;
        switch (a) {
            case FGAccess::SampledRead:         return SS::Vertex | SS::Fragment | SS::Compute;
            case FGAccess::DepthSampledRead:    return SS::Vertex | SS::Fragment | SS::Compute;
            case FGAccess::StorageRead:         return SS::Compute | SS::Fragment;
            case FGAccess::StorageWrite:        return SS::Compute | SS::Fragment;
            case FGAccess::ColorAttachmentWrite:return SS::RenderTarget;
            case FGAccess::ColorAttachmentRead: return SS::RenderTarget;
            case FGAccess::DepthAttachmentWrite:return SS::DepthStencilAttachment;
            case FGAccess::DepthAttachmentRead: return SS::DepthStencilAttachment;
            case FGAccess::TransferSrc:         return SS::Transfer;
            case FGAccess::TransferDst:         return SS::Transfer;
            case FGAccess::Present:             return SS::All;
            case FGAccess::VertexBufferRead:    return SS::Vertex;
            case FGAccess::IndexBufferRead:     return SS::Vertex;
            case FGAccess::UniformBufferRead:   return SS::Vertex | SS::Fragment | SS::Compute;
            case FGAccess::IndirectBufferRead:  return SS::DrawIndirect;
            case FGAccess::IndirectBufferWrite: return SS::Compute;
        }
        return SS::None;
    }

    int BarrierSolver::accessPriority(FGAccess a) {
        switch (a) {
            case FGAccess::Present: return 100;
            case FGAccess::TransferDst: return 60;
            case FGAccess::StorageWrite: return 50;
            case FGAccess::IndirectBufferWrite: return 50;
            case FGAccess::DepthAttachmentWrite: return 45;
            case FGAccess::ColorAttachmentWrite: return 40;
            case FGAccess::DepthAttachmentRead: return 39;
            case FGAccess::ColorAttachmentRead: return 39;
            case FGAccess::TransferSrc: return 30;
            case FGAccess::StorageRead: return 20;
            case FGAccess::SampledRead: return 10;
            case FGAccess::DepthSampledRead: return 10;
            case FGAccess::VertexBufferRead: return 5;
            case FGAccess::IndexBufferRead: return 5;
            case FGAccess::UniformBufferRead: return 15;
            case FGAccess::IndirectBufferRead: return 25;
        }
        return 0;
    }

    void BarrierSolver::solveBarriers(
        const PassEntry& pass,
        std::vector<ResourceEntry>& resources,
        FrameGraph& frameGraph,
        std::vector<rhi::RHIMemoryBarrier>& outBarriers
    ) {
        
        static auto clampCount = [](uint32_t base, uint32_t count,
                                uint32_t total) -> uint32_t {
            if (base >= total) {
                return 0U;
            }
            if (count == BINDLESS_INVALID_ID) {
                return total - base;
            }
            return std::min(count, total - base);
        };

        std::vector<DesiredAccess> desired(resources.size());

        auto addUse = [&](const FGUse& u) {
            if (u.h.index >= desired.size()) return;
            auto& d = desired[u.h.index];
            const int p = accessPriority(u.access);
            if (!d.used || p > d.priority) {
                d.used = true;
                d.priority = p;
                d.bestAccess = u.access;
                d.view = u.h;
                d.stages = accessToStage(u.access);
            } else {
                d.stages = d.stages | accessToStage(u.access);
            }
        };

        for (const auto &u : pass.reads) {
            addUse(u);
        }
        for (const auto &u : pass.writes) {
            addUse(u);
        }

        for (uint32_t i = 0; i < (uint32_t)resources.size(); ++i) {
            if (!desired[i].used) {
                continue;
            }
            ResourceEntry& res = resources[i];

            if (res.isBuffer) {
                rhi::RHIBuffer* buf = frameGraph.getBuffer(FGHandle{ .index = i });
                if (buf == nullptr) {
                    continue;
                }

                const rhi::ResourceLayout target = accessToLayout(desired[i].bestAccess);
                uint32_t rootIdx = i;
                while (resources[rootIdx].parent.isValid()) {
                    rootIdx = resources[rootIdx].parent.index;
                }
                ResourceEntry& rootRes = resources[rootIdx];

                const rhi::ResourceLayout old = rootRes.currentLayout;
                bool needsBarrier = (old != target);
                if (!needsBarrier &&
                    (rootRes.lastWasWrite ||
                        desired[i].bestAccess == FGAccess::StorageWrite ||
                        desired[i].bestAccess == FGAccess::IndirectBufferWrite ||
                        desired[i].bestAccess == FGAccess::TransferDst)) {
                    needsBarrier = true;
                }

                if (needsBarrier) {
                    outBarriers.push_back(
                        {.buffer = buf,
                            .srcAccessStage =
                                (rootRes.lastStages == rhi::ShaderStage::None)
                                    ? rhi::ShaderStage::All
                                    : rootRes.lastStages,
                            .dstAccessStage = desired[i].stages,
                            .oldLayout = old,
                            .newLayout = target});
                    rootRes.currentLayout = target;
                }
                continue;
            }

            rhi::RHITexture* tex = frameGraph.getTexture(FGHandle{ .index = i });
            if (tex == nullptr) {
                continue;
            }

            const rhi::ResourceLayout target = accessToLayout(desired[i].bestAccess);
            uint32_t rootIdx = i;
            while (resources[rootIdx].parent.isValid()) {
                rootIdx = resources[rootIdx].parent.index;
            }
            ResourceEntry& rootRes = resources[rootIdx];

            const uint32_t texMips = std::max(1U, tex->mipLevels());
            const uint32_t baseMip = desired[i].view.baseMipLevel;
            const uint32_t mipCount = clampCount(baseMip, desired[i].view.levelCount, texMips);
            const uint32_t baseLayer = desired[i].view.baseArrayLayer;
            const uint32_t layerCount = clampCount(baseLayer, desired[i].view.layerCount, std::max(1U, tex->arrayLayers()));

            if (mipCount == 0 || layerCount == 0) {
                continue;
            }

            for (uint32_t l = 0; l < layerCount; ++l) {
                for (uint32_t m = 0; m < mipCount; ++m) {
                     if ((baseMip + m) >= rootRes.mipLayouts.size()) {
                         continue;
                     }

                     const rhi::ResourceLayout old = rootRes.mipLayouts[baseMip + m];
                     bool needsBarrier = (old != target);
                     if (!needsBarrier && (rootRes.lastWasWrite || 
                         desired[i].bestAccess == FGAccess::StorageWrite || 
                         desired[i].bestAccess == FGAccess::ColorAttachmentWrite ||
                         desired[i].bestAccess == FGAccess::DepthAttachmentWrite ||
                         desired[i].bestAccess == FGAccess::TransferDst)) {
                         needsBarrier = true;
                     }

                     if (needsBarrier) {
                        outBarriers.push_back(rhi::RHIMemoryBarrier{
                            .buffer = nullptr,
                            .texture = tex,
                            .srcAccessStage = (rootRes.lastStages == rhi::ShaderStage::None) ? rhi::ShaderStage::All : rootRes.lastStages,
                            .dstAccessStage = desired[i].stages,
                            .oldLayout = old,
                            .newLayout = target,
                            .baseMipLevel = baseMip + m,
                            .levelCount = 1,
                            .baseArrayLayer = baseLayer + l,
                            .layerCount = 1,
                            .srcQueueFamilyIndex = rhi::kQueueFamilyIgnored,
                            .dstQueueFamilyIndex = rhi::kQueueFamilyIgnored
                        });
                        rootRes.mipLayouts[baseMip + m] = target;
                     }
                }
            }
        }
        
        // Update last state for root resources
        for (uint32_t i = 0; i < (uint32_t)resources.size(); ++i) {
             if (!desired[i].used) continue;
             uint32_t rootIdx = i;
             while (resources[rootIdx].parent.isValid()) {
                rootIdx = resources[rootIdx].parent.index;
             }
             resources[rootIdx].lastStages = desired[i].stages;
             resources[rootIdx].lastWasWrite = (desired[i].bestAccess == FGAccess::StorageWrite || 
                                                desired[i].bestAccess == FGAccess::ColorAttachmentWrite || 
                                                desired[i].bestAccess == FGAccess::DepthAttachmentWrite || 
                                                desired[i].bestAccess == FGAccess::TransferDst ||
                                                desired[i].bestAccess == FGAccess::IndirectBufferWrite);
        }
    }

} // namespace pnkr::renderer
