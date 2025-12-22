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
#include "pnkr/platform/window.hpp"

#include <functional>
#include <memory>
#include <vector>
#include <filesystem>

#include "renderer_config.hpp"

namespace pnkr::renderer
{
    struct Vertex;

    // Frame context for recording commands
    struct RHIFrameContext
    {
        rhi::RHICommandBuffer* commandBuffer;
        uint32_t frameIndex;
        float deltaTime;
    };

    using RHIRecordFunc = std::function<void(const RHIFrameContext&)>;

    class RHIRenderer
    {
    public:
        explicit RHIRenderer(platform::Window& window,
                            const RendererConfig& config = RendererConfig{});
        ~RHIRenderer();

        // Disable copy/move
        RHIRenderer(const RHIRenderer&) = delete;
        RHIRenderer& operator=(const RHIRenderer&) = delete;
        RHIRenderer(RHIRenderer&&) = delete;
        RHIRenderer& operator=(RHIRenderer&&) = delete;

        // Frame management
        void beginFrame(float deltaTime);
        void drawFrame();
        void endFrame();
        void resize(int width, int height);
        MeshHandle loadNoVertexPulling(const std::vector<Vertex>& vertices,
                                       const std::vector<uint32_t>& indices);
        MeshHandle loadVertexPulling(const std::vector<Vertex>& vertices,
                                       const std::vector<uint32_t>& indices);

        // Resource creation
        MeshHandle createMesh(const std::vector<struct Vertex>& vertices,
                              const std::vector<uint32_t>& indices, bool enableVertexPulling);

        TextureHandle createTexture(const unsigned char* data,
                                   int width, int height, int channels,
                                   bool srgb = true);

        TextureHandle loadTexture(const std::filesystem::path& filepath,
                                 bool srgb = true);

        TextureHandle loadTextureKTX(const std::filesystem::path& filepath);

        TextureHandle createCubemap(const std::vector<std::filesystem::path>& faces,
                                   bool srgb = true);

        BufferHandle createBuffer(const rhi::BufferDescriptor& desc);

        PipelineHandle createGraphicsPipeline(const rhi::GraphicsPipelineDescriptor& desc);
        PipelineHandle createComputePipeline(const rhi::ComputePipelineDescriptor& desc);

        // Command recording
        void setRecordFunc(const RHIRecordFunc& callback);
        void setComputeRecordFunc(const RHIRecordFunc& callback) { m_computeRecordCallback = callback; }

        // Drawing commands (to be used within record callback)
        void bindPipeline(rhi::RHICommandBuffer* cmd, PipelineHandle handle);
        void bindComputePipeline(rhi::RHICommandBuffer* cmd, PipelineHandle handle)
        {
            bindPipeline(cmd, handle); // Pipelines know their bind point
        }
        void bindMesh(rhi::RHICommandBuffer* cmd, MeshHandle handle);
        void drawMesh(rhi::RHICommandBuffer* cmd, MeshHandle handle);
        void drawMeshInstanced(rhi::RHICommandBuffer* cmd, MeshHandle handle, uint32_t instanceCount);
        void drawMeshBaseInstance(rhi::RHICommandBuffer* cmd, MeshHandle handle, uint32_t firstInstance);
        void bindDescriptorSet(rhi::RHICommandBuffer* cmd,
                               PipelineHandle handle,
                               uint32_t setIndex,
                               rhi::RHIDescriptorSet* descriptorSet);

        // Push constants
        template <typename T>
        void pushConstants(rhi::RHICommandBuffer* cmd,
                          PipelineHandle pipe,
                          rhi::ShaderStage stages,
                          const T& data,
                          uint32_t offset = 0)
        {
            static_assert(std::is_trivially_copyable_v<T>,
                         "pushConstants<T>: T must be trivially copyable");
            
            auto* pipeline = getPipeline(pipe);
            cmd->pushConstants(pipeline, stages, offset, sizeof(T), &data);
        }

        // Texture/descriptor access
        [[nodiscard]] rhi::RHITexture* getTexture(TextureHandle handle) const;
        [[nodiscard]] uint32_t getTextureBindlessIndex(TextureHandle handle) const;
        [[nodiscard]] uint32_t getBindlessSamplerIndex(rhi::SamplerAddressMode addressMode) const;

        // Buffer access
        [[nodiscard]] rhi::RHIBuffer* getBuffer(BufferHandle handle) const;
        [[nodiscard]] uint32_t getBufferBindlessIndex(BufferHandle handle) const;

        [[nodiscard]] uint32_t getMeshIndexCount(MeshHandle handle) const;
        uint64_t getMeshVertexBufferAddress(MeshHandle handle) const;

        // Format queries
        rhi::Format getDrawColorFormat() const;
        rhi::Format getDrawDepthFormat() const;
        rhi::Format getSwapchainColorFormat() const;

        // Device access
        rhi::RHIDevice* device() const { return m_device.get(); }
        rhi::RHITexture* getBackbuffer() const { return m_backbuffer; }
        rhi::RHITexture* getBackbufferTexture() const { return m_backbuffer; }
        rhi::RHITexture* getDepthTexture() const { return m_depthTarget.get(); }

        void setVsync(bool enabled);

        // Bindless support
        void setBindlessEnabled(bool enabled);
        rhi::RHIPipeline* pipeline(PipelineHandle handle);
        bool isBindlessEnabled() const noexcept { return m_useBindless; }
        bool hasBindlessSupport() const noexcept { return m_bindlessSupported; }

        // Pipeline access
        [[nodiscard]] rhi::RHIPipeline* getPipeline(PipelineHandle handle);

        // Global Lighting
        void setGlobalIBL(TextureHandle irradiance, TextureHandle prefilter, TextureHandle brdfLut);
        [[nodiscard]] rhi::RHIDescriptorSet* getGlobalLightingDescriptorSet() const { return m_globalLightingSet.get(); }
        [[nodiscard]] rhi::RHIDescriptorSetLayout* getGlobalLightingDescriptorSetLayout() const { return m_globalLightingLayout.get(); }

    private:
        // Window reference
        platform::Window& m_window;

        // RHI objects
        std::unique_ptr<rhi::RHIDevice> m_device;
        std::unique_ptr<rhi::RHISwapchain> m_swapchain;
        std::vector<std::unique_ptr<rhi::RHICommandBuffer>> m_commandBuffers;
        rhi::RHICommandBuffer* m_activeCommandBuffer = nullptr;
        rhi::SwapchainFrame m_currentFrame{};
        std::unique_ptr<rhi::RHISampler> m_defaultSampler;
        std::unique_ptr<rhi::RHISampler> m_repeatSampler;
        std::unique_ptr<rhi::RHISampler> m_clampSampler;
        std::unique_ptr<rhi::RHISampler> m_mirrorSampler;
        uint32_t m_repeatSamplerIndex = 0xFFFFFFFF;
        uint32_t m_clampSamplerIndex = 0xFFFFFFFF;
        uint32_t m_mirrorSamplerIndex = 0xFFFFFFFF;

        // Resources
        struct MeshData {
            std::unique_ptr<rhi::RHIBuffer> m_vertexBuffer;
            std::unique_ptr<rhi::RHIBuffer> m_indexBuffer;
            uint32_t m_vertexCount;
            uint32_t m_indexCount;
            bool m_vertexPulling;
        };
        std::vector<MeshData> m_meshes;

        struct TextureData {
            std::unique_ptr<rhi::RHITexture> texture;
            uint32_t bindlessIndex;
        };
        std::vector<TextureData> m_textures;

        struct BufferData {
            std::unique_ptr<rhi::RHIBuffer> buffer;
            uint32_t bindlessIndex = 0xFFFFFFFF;
        };
        std::vector<BufferData> m_buffers;

        std::vector<std::unique_ptr<rhi::RHIPipeline>> m_pipelines;

        // Render targets
        // Backbuffer is owned by the swapchain; depth is device-owned.
        rhi::RHITexture* m_backbuffer = nullptr;
        std::unique_ptr<rhi::RHITexture> m_depthTarget;
        rhi::ResourceLayout m_depthLayout = rhi::ResourceLayout::Undefined;

        // Frame state
        RHIRecordFunc m_recordCallback;
        RHIRecordFunc m_computeRecordCallback;
        bool m_frameInProgress = false;
        float m_deltaTime = 0.0f;
        uint32_t m_frameIndex = 0;

        // Config
        bool m_bindlessSupported = false;
        bool m_useBindless = false;
        TextureHandle m_whiteTexture{INVALID_TEXTURE_HANDLE};

        // Global lighting
        std::unique_ptr<rhi::RHIDescriptorSetLayout> m_globalLightingLayout;
        std::unique_ptr<rhi::RHIDescriptorSet> m_globalLightingSet;

        // Helper methods
        void createRenderTargets();
        void createDefaultResources();
        TextureHandle createWhiteTexture();
        void uploadToBuffer(rhi::RHIBuffer* target, const void* data, uint64_t size);
    };

} // namespace pnkr::renderer
