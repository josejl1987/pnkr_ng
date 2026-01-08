#pragma once

#include "pnkr/rhi/rhi_types.hpp"
#include "pnkr/rhi/rhi_device.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/rhi/rhi_texture.hpp"
#include "pnkr/rhi/rhi_pipeline.hpp"
#include "pnkr/rhi/rhi_command_buffer.hpp"
#include "pnkr/rhi/rhi_swapchain.hpp"
#include "pnkr/rhi/rhi_sampler.hpp"
#include "pnkr/rhi/rhi_descriptor.hpp"
#include "pnkr/core/Handle.h"
#include "pnkr/core/Pool.hpp"
#include "pnkr/platform/window.hpp"
#include "pnkr/renderer/AssetManager.hpp"
#include "pnkr/renderer/SystemMeshes.hpp"

#include <functional>
#include <memory>
#include <vector>
#include <cstddef>
#include <optional>
#include <filesystem>

#include "renderer_config.hpp"
#include "RHIDeviceContext.hpp"
#include "RHIResourceManager.hpp"
#include "RHISwapchainManager.hpp"
#include "RHIPipelineCache.hpp"
#include "RenderContext.hpp"
#include "RenderDevice.hpp"

namespace pnkr::renderer
{
    struct Vertex;

    struct RHIFrameContext
    {
        rhi::RHICommandList* commandBuffer;
        rhi::RHITexture* backBuffer;
        rhi::RHITexture* depthBuffer;
        uint32_t frameIndex;
        float deltaTime;
    };

    using RHIRecordFunc = std::function<void(const RHIFrameContext&)>;

    struct MeshView {
        rhi::RHIBuffer* vertexBuffer = nullptr;
        rhi::RHIBuffer* indexBuffer = nullptr;
        uint32_t indexCount = 0;
        bool vertexPulling = false;
    };

    class RHIRenderer
    {
    public:
        explicit RHIRenderer(platform::Window& window,
                            const RendererConfig& config = RendererConfig{});
        ~RHIRenderer();

        RHIRenderer(const RHIRenderer&) = delete;
        RHIRenderer& operator=(const RHIRenderer&) = delete;
        RHIRenderer(RHIRenderer&&) = delete;
        RHIRenderer& operator=(RHIRenderer&&) = delete;

        void beginFrame(float deltaTime);
        void drawFrame();
        void endFrame();
        void resize(int width, int height);
        MeshPtr loadNoVertexPulling(std::span<const Vertex> vertices,
                                       std::span<const uint32_t> indices);
        MeshPtr loadVertexPulling(std::span<const Vertex> vertices,
                                       std::span<const uint32_t> indices);

        MeshPtr createMesh(std::span<const struct Vertex> vertices,
                              std::span<const uint32_t> indices, bool enableVertexPulling);

        TexturePtr createTexture(const char* name, const rhi::TextureDescriptor& desc);
        TexturePtr createTexture(const rhi::TextureDescriptor& desc)
        {
            const char* name = desc.debugName.empty() ? "Texture" : desc.debugName.c_str();
            return createTexture(name, desc);
        }

        TexturePtr createTextureView(const char* name, TextureHandle parent, const rhi::TextureViewDescriptor& desc);
        TexturePtr createTextureView(TextureHandle parent, const rhi::TextureViewDescriptor& desc)
        {
            return createTextureView("TextureView", parent, desc);
        }

        void destroyTexture(TextureHandle handle);
        void destroyBuffer(BufferHandle handle);
        void deferDestroyBuffer(BufferHandle handle);
        void destroyMesh(MeshHandle handle);

        void replaceTexture(TextureHandle handle, TextureHandle source);
        bool isValid(TextureHandle handle) const;

        BufferPtr createBuffer(const char* name, const rhi::BufferDescriptor& desc);
        BufferPtr createBuffer(const rhi::BufferDescriptor& desc)
        {
            const char* name = desc.debugName.empty() ? "Buffer" : desc.debugName.c_str();
            return createBuffer(name, desc);
        }

        PipelinePtr createGraphicsPipeline(const rhi::GraphicsPipelineDescriptor& desc);
        PipelinePtr createComputePipeline(const rhi::ComputePipelineDescriptor& desc);

        void hotSwapPipeline(PipelineHandle handle, const rhi::GraphicsPipelineDescriptor& desc);
        void hotSwapPipeline(PipelineHandle handle, const rhi::ComputePipelineDescriptor& desc);

        void setRecordFunc(const RHIRecordFunc& callback);
        void setComputeRecordFunc(const RHIRecordFunc& callback) { m_computeRecordCallback = callback; }
        void setUseDefaultRenderPass(bool enabled) { m_useDefaultRenderPass = enabled; }
        [[nodiscard]] bool useDefaultRenderPass() const { return m_useDefaultRenderPass; }

        [[nodiscard]] std::optional<MeshView> getMeshView(MeshHandle handle) const;
        [[nodiscard]] rhi::RHITexture* getTexture(TextureHandle handle) const;
        [[nodiscard]] rhi::TextureBindlessHandle getTextureBindlessIndex(TextureHandle handle) const;
        [[nodiscard]] rhi::TextureBindlessHandle getStorageImageBindlessIndex(TextureHandle handle);
        void updateTextureBindlessDescriptor(TextureHandle handle);
        [[nodiscard]] rhi::TextureBindlessHandle
        getStorageImageBindlessIndex(rhi::RHITexture *texture) const;

        [[nodiscard]] rhi::SamplerBindlessHandle getBindlessSamplerIndex(rhi::SamplerAddressMode addressMode) const;

        [[nodiscard]] rhi::SamplerBindlessHandle getBindlessSamplerIndex(rhi::Filter filter,
                                                                          rhi::SamplerAddressMode addressMode) const;
        [[nodiscard]] rhi::SamplerBindlessHandle getShadowSamplerIndex() const { return m_shadowSamplerIndex; }

        [[nodiscard]] rhi::RHIBuffer* getBuffer(BufferHandle handle) const;
        [[nodiscard]] rhi::BufferBindlessHandle getBufferBindlessIndex(BufferHandle handle) const;
        [[nodiscard]] uint64_t getBufferDeviceAddress(BufferHandle handle) const;

        [[nodiscard]] uint32_t getMeshIndexCount(MeshHandle handle) const;
        uint64_t getMeshVertexBufferAddress(MeshHandle handle) const;

        rhi::Format getDrawColorFormat() const;
        rhi::Format getDrawDepthFormat() const;
        rhi::Format getSwapchainColorFormat() const;

        [[nodiscard]] uint32_t getFrameIndex() const { return m_frameIndex; }
        rhi::RHIDevice* device() const { return m_renderDevice->device(); }
        rhi::RHISwapchain* getSwapchain() const { return m_renderDevice->swapchain(); }
        RenderDevice* renderDevice() const { return m_renderDevice.get(); }
        rhi::RHITexture* getBackbuffer() const { return m_backbuffer; }
        rhi::RHITexture* getBackbufferTexture() const { return m_backbuffer; }
        rhi::RHITexture* getDepthTexture() const { return m_depthTarget.get(); }

        void setVsync(bool enabled);
        [[nodiscard]] bool isVsyncEnabled() const { return m_vsync; }

        AssetManager* assets() const { return m_assets.get(); }

        RHIResourceManager* resourceManager() const { return m_resourceManager.get(); }
        RHIPipelineCache* pipelineCache() const { return m_pipelineCache.get(); }

        void setBindlessEnabled(bool enabled);
        rhi::RHIPipeline* pipeline(PipelineHandle handle);
        bool isBindlessEnabled() const noexcept { return m_useBindless; }
        bool hasBindlessSupport() const noexcept { return m_bindlessSupported; }
        bool checkDrawIndirectCountSupport() const;

        [[nodiscard]] rhi::RHIPipeline* getPipeline(PipelineHandle handle);

        void setGlobalIBL(TextureHandle irradiance, TextureHandle prefilter, TextureHandle brdfLut);
        [[nodiscard]] rhi::RHIDescriptorSet* getGlobalLightingDescriptorSet() const { return m_globalLightingSet.get(); }
        [[nodiscard]] rhi::RHIDescriptorSetLayout* getGlobalLightingDescriptorSetLayout() const { return m_globalLightingLayout.get(); }
        [[nodiscard]] TextureHandle getWhiteTexture() const { return m_whiteTexture; }
        [[nodiscard]] TextureHandle getBlackTexture() const { return m_blackTexture; }
        [[nodiscard]] TextureHandle getFlatNormalTexture() const { return m_flatNormalTexture; }

        SystemMeshes& getSystemMeshes() { return m_systemMeshes; }
        const SystemMeshes& getSystemMeshes() const { return m_systemMeshes; }

    private:

        platform::Window& m_window;

        std::unique_ptr<RenderDevice> m_renderDevice;
        std::unique_ptr<RHIResourceManager> m_resourceManager;
        std::unique_ptr<RHIPipelineCache> m_pipelineCache;
        std::unique_ptr<RenderContext> m_renderContext;

        std::unique_ptr<AssetManager> m_assets;
        SystemMeshes m_systemMeshes;

        rhi::RHICommandList* m_activeCommandBuffer = nullptr;
        rhi::SwapchainFrame m_currentFrame{};

        std::unique_ptr<rhi::RHISampler> m_defaultSampler;
        std::unique_ptr<rhi::RHISampler> m_repeatSampler;
        std::unique_ptr<rhi::RHISampler> m_clampSampler;
        std::unique_ptr<rhi::RHISampler> m_mirrorSampler;
        std::unique_ptr<rhi::RHISampler> m_shadowSampler;

        std::unique_ptr<rhi::RHISampler> m_repeatSamplerNearest;
        std::unique_ptr<rhi::RHISampler> m_clampSamplerNearest;
        std::unique_ptr<rhi::RHISampler> m_mirrorSamplerNearest;

        rhi::SamplerBindlessHandle m_repeatSamplerIndex;
        rhi::SamplerBindlessHandle m_clampSamplerIndex;
        rhi::SamplerBindlessHandle m_mirrorSamplerIndex;
        rhi::SamplerBindlessHandle m_shadowSamplerIndex;

        rhi::SamplerBindlessHandle m_repeatSamplerNearestIndex;
        rhi::SamplerBindlessHandle m_clampSamplerNearestIndex;
        rhi::SamplerBindlessHandle m_mirrorSamplerNearestIndex;

        rhi::RHITexture* m_backbuffer = nullptr;
        std::unique_ptr<rhi::RHITexture> m_depthTarget;
        rhi::ResourceLayout m_depthLayout = rhi::ResourceLayout::Undefined;

        RHIRecordFunc m_recordCallback;
        RHIRecordFunc m_computeRecordCallback;
        bool m_frameInProgress = false;
        float m_deltaTime = 0.0f;
        uint32_t m_frameIndex = 0;
        std::vector<uint64_t> m_frameSlotRetireValues;
        std::vector<uint32_t> m_frameSlotFrameIndices;

        bool m_bindlessSupported = false;
        bool m_useBindless = false;
        bool m_useDefaultRenderPass = true;
        bool m_vsync = true;
        TextureHandle m_whiteTexture = INVALID_TEXTURE_HANDLE;
        TextureHandle m_blackTexture = INVALID_TEXTURE_HANDLE;
        TextureHandle m_flatNormalTexture = INVALID_TEXTURE_HANDLE;

        std::unique_ptr<rhi::RHIDescriptorSetLayout> m_globalLightingLayout;
        std::unique_ptr<rhi::RHIDescriptorSet> m_globalLightingSet;

        BufferPtr m_persistentStagingBuffer;
        uint8_t* m_persistentStagingMapped = nullptr;
        uint64_t m_persistentStagingCapacity = 0;

        void createRenderTargets();
        void createDefaultResources();
        void createPersistentStagingBuffer(uint64_t size);
        void destroyPersistentStagingBuffer();
        TextureHandle createWhiteTexture();
        TextureHandle createBlackTexture();
        TextureHandle createFlatNormalTexture();
        void uploadToBuffer(rhi::RHIBuffer* target, std::span<const std::byte> data, uint64_t offset = 0);

        void prepareRenderPass(const RHIFrameContext& context);
        void executeRenderPass(const RHIFrameContext& context);
        void updateMemoryStatistics();

    };

    struct ScopedDebugGroup
    {
        rhi::RHICommandList* cmd;
        ScopedDebugGroup(rhi::RHICommandList* c, const char* name, float r = 1.0f, float g = 1.0f, float b = 1.0f)
            : cmd(c)
        {
            if (cmd) cmd->beginDebugLabel(name, r, g, b, 1.0f);
        }
        ~ScopedDebugGroup()
        {
            if (cmd) cmd->endDebugLabel();
        }
    };

}
