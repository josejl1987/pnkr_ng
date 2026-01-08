#pragma once

#include "pnkr/rhi/rhi_command_buffer.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>

#include "pnkr/renderer/profiling/gpu_profiler.hpp"

#ifdef TRACY_ENABLE
namespace tracy
{
    class VkCtxScope;
}
#endif

namespace pnkr::renderer::rhi::vulkan
{
    class VulkanRHIDevice;
    class VulkanRHIPipeline;

    class VulkanRHICommandBuffer : public RHICommandBuffer
    {
    public:
        explicit VulkanRHICommandBuffer(VulkanRHIDevice* device, class VulkanRHICommandPool* pool = nullptr);
        ~VulkanRHICommandBuffer() override;

        VulkanRHICommandBuffer(const VulkanRHICommandBuffer&) = delete;
        VulkanRHICommandBuffer& operator=(const VulkanRHICommandBuffer&) = delete;

        void begin() override;
        void end() override;
        void reset() override;

        void setProfilingContext(void* ctx) override { m_profilingCtx = ctx; }
        void* getProfilingContext() const override { return m_profilingCtx; }

        void beginRendering(const RenderingInfo& info) override;
        void endRendering() override;

        void bindPipeline(RHIPipeline* pipeline) override;

        void setCullMode(CullMode mode) override;
        void setDepthTestEnable(bool enabled) override;
        void setDepthWriteEnable(bool enabled) override;
        void setDepthCompareOp(CompareOp op) override;
        void setPrimitiveTopology(PrimitiveTopology topology) override;

        void bindVertexBuffer(uint32_t binding, RHIBuffer* buffer, uint64_t offset = 0) override;
        void bindIndexBuffer(RHIBuffer* buffer, uint64_t offset = 0, bool use16Bit = false) override;

        void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
                 uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;
        void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                        uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                        uint32_t firstInstance = 0) override;

        void drawIndexedIndirect(RHIBuffer* buffer, uint64_t offset, uint32_t drawCount, uint32_t stride) override;

        void drawIndexedIndirectCount(RHIBuffer* buffer, uint64_t offset,
                                      RHIBuffer* countBuffer, uint64_t countBufferOffset,
                                      uint32_t maxDrawCount, uint32_t stride) override;

        void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;

        void pushConstants(RHIPipeline* pipeline, ShaderStageFlags stages,
                          uint32_t offset, uint32_t size, const void* data) override;

        void bindDescriptorSet(RHIPipeline* pipeline, uint32_t setIndex,
                              RHIDescriptorSet* descriptorSet) override;

        void setViewport(const Viewport& viewport) override;
        void setScissor(const Rect2D& scissor) override;
        void setDepthBias(float constantFactor, float clamp, float slopeFactor) override;

        void resolveTexture(RHITexture* src, ResourceLayout srcLayout,
                            RHITexture* dst, ResourceLayout dstLayout,
                            const TextureCopyRegion& region) override;

        void pipelineBarrier(
            ShaderStageFlags srcStage,
            ShaderStageFlags dstStage,
            const std::vector<RHIMemoryBarrier>& barriers) override;

        void copyBuffer(RHIBuffer* src, RHIBuffer* dst,
                       uint64_t srcOffset, uint64_t dstOffset, uint64_t size) override;
        void fillBuffer(RHIBuffer* buffer, uint64_t offset, uint64_t size, uint32_t data) override;
        void copyBufferToTexture(RHIBuffer* src, RHITexture* dst,
                                const BufferTextureCopyRegion& region) override;
        void copyTextureToBuffer(RHITexture* src, RHIBuffer* dst,
                                const BufferTextureCopyRegion& region) override;
        void copyBufferToTexture(RHIBuffer* src, RHITexture* dst,
                                                   std::span<const rhi::BufferTextureCopyRegion> regions) override;
        void copyTexture(RHITexture* src, RHITexture* dst,
                        const TextureCopyRegion& region) override;
        void blitTexture(RHITexture* src, RHITexture* dst, const TextureBlitRegion& region, Filter filter) override;
        void clearImage(RHITexture* texture, const ClearValue& clearValue,
                        ResourceLayout layout) override;

        void beginDebugLabel(const char* name, float r, float g, float b, float a) override;
        void endDebugLabel() override;
        void insertDebugLabel(const char* name, float r, float g, float b, float a) override;

        void pushGPUMarker(const char* name) override;
                void popGPUMarker() override;

                void setCheckpoint(const char* name) override;

                void setFrameIndex(uint32_t frameIndex) override { m_currentFrameIndex = frameIndex; }

        void* nativeHandle() const override {
            return static_cast<VkCommandBuffer>(m_commandBuffer);
        }

        vk::CommandBuffer commandBuffer() const { return m_commandBuffer; }
        bool isRecording() const { return m_recording; }

        operator vk::CommandBuffer() const { return m_commandBuffer; }
        operator VkCommandBuffer() const { return m_commandBuffer; }

        uint32_t getQueueFamilyIndex() const { return m_queueFamilyIndex; }

    private:
        VulkanRHIDevice* m_device;
        vk::CommandPool m_pool;
        vk::CommandBuffer m_commandBuffer;
        uint32_t m_queueFamilyIndex = 0;
        bool m_recording = false;
        bool m_inRendering = false;
        uint32_t m_currentFrameIndex = 0;
        void* m_profilingCtx = nullptr;
        ::pnkr::renderer::GPUDrawCallStatistics m_drawCallStats;

        RHIPipeline* m_boundPipeline = nullptr;
        std::vector<uint32_t> m_markerStack;

#ifdef TRACY_ENABLE
        std::vector<std::unique_ptr<tracy::VkCtxScope>> m_tracyZoneStack;
#endif

        RHIPipeline* boundPipeline() const override { return m_boundPipeline; }
        void pushConstantsInternal(ShaderStageFlags stages,
                                   uint32_t offset,
                                   uint32_t size,
                                   const void* data) override;
    };

}
