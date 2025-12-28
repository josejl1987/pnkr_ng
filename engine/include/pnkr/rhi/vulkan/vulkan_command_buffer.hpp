#pragma once

#include "pnkr/rhi/rhi_command_buffer.hpp"
#include <vulkan/vulkan.hpp>

namespace pnkr::renderer::rhi::vulkan
{
    class VulkanRHIDevice;
    class VulkanRHIPipeline;

    class VulkanRHICommandBuffer : public RHICommandBuffer
    {
    public:
        explicit VulkanRHICommandBuffer(VulkanRHIDevice* device);
        ~VulkanRHICommandBuffer() override;

        // Disable copy
        VulkanRHICommandBuffer(const VulkanRHICommandBuffer&) = delete;
        VulkanRHICommandBuffer& operator=(const VulkanRHICommandBuffer&) = delete;

        // RHICommandBuffer interface
        void begin() override;
        void end() override;
        void reset() override;

        void beginRendering(const RenderingInfo& info) override;
        void endRendering() override;

        void bindPipeline(RHIPipeline* pipeline) override;

        // Dynamic State Overrides
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

        void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;

        void pushConstants(RHIPipeline* pipeline, ShaderStage stages,
                          uint32_t offset, uint32_t size, const void* data) override;

        void bindDescriptorSet(RHIPipeline* pipeline, uint32_t setIndex,
                              RHIDescriptorSet* descriptorSet) override;

        void setViewport(const Viewport& viewport) override;
        void setScissor(const Rect2D& scissor) override;
        void setDepthBias(float constantFactor, float clamp, float slopeFactor) override;

        void pipelineBarrier(
            ShaderStage srcStage,
            ShaderStage dstStage,
            const std::vector<RHIMemoryBarrier>& barriers) override;

        void copyBuffer(RHIBuffer* src, RHIBuffer* dst,
                       uint64_t srcOffset, uint64_t dstOffset, uint64_t size) override;
        void copyBufferToTexture(RHIBuffer* src, RHITexture* dst,
                                const BufferTextureCopyRegion& region) override;
        void copyTextureToBuffer(RHITexture* src, RHIBuffer* dst,
                                const BufferTextureCopyRegion& region) override;
        void copyTexture(RHITexture* src, RHITexture* dst,
                        const TextureCopyRegion& region) override;

        void beginDebugLabel(const char* name, float r, float g, float b, float a) override;
        void endDebugLabel() override;
        void insertDebugLabel(const char* name, float r, float g, float b, float a) override;

        void* nativeHandle() const override {
            return static_cast<VkCommandBuffer>(m_commandBuffer);
        }

        // Vulkan-specific accessors
        vk::CommandBuffer commandBuffer() const { return m_commandBuffer; }
        bool isRecording() const { return m_recording; }

        // Implicit conversion operators for cleaner Vulkan API usage
        operator vk::CommandBuffer() const { return m_commandBuffer; }
        operator VkCommandBuffer() const { return m_commandBuffer; }

    private:
        VulkanRHIDevice* m_device;
        vk::CommandBuffer m_commandBuffer;
        bool m_recording = false;
        bool m_inRendering = false;

        // Cached pipeline for descriptor set binding
        VulkanRHIPipeline* m_boundPipeline = nullptr;
    };

} // namespace pnkr::renderer::rhi::vulkan
