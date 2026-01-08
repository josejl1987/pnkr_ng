#pragma once

#include "rhi_types.hpp"
#include <vector>
#include <stdexcept>

#include "rhi_texture.hpp"
#include "rhi_descriptor.hpp"
#include <span>
namespace pnkr::renderer::rhi
{

    class RHIBuffer;
    class RHITexture;
    class RHIPipeline;

    struct RHIMemoryBarrier
    {
        RHIBuffer* buffer = nullptr;
        RHITexture* texture = nullptr;
        ShaderStageFlags srcAccessStage;
        ShaderStageFlags dstAccessStage;

        ResourceLayout oldLayout = ResourceLayout::Undefined;
        ResourceLayout newLayout = ResourceLayout::Undefined;

        uint32_t baseMipLevel = 0;
        uint32_t levelCount   = 0xFFFFFFFFu;
        uint32_t baseArrayLayer = 0;
        uint32_t layerCount     = 0xFFFFFFFFu;

        uint32_t srcQueueFamilyIndex = kQueueFamilyIgnored;
        uint32_t dstQueueFamilyIndex = kQueueFamilyIgnored;
    };

    struct TextureBlitRegion
    {
        TextureSubresource srcSubresource;
        TextureSubresource dstSubresource;

        struct { int32_t x, y, z; } srcOffsets[2];
        struct { int32_t x, y, z; } dstOffsets[2];
    };

    struct RenderingAttachment
    {
        RHITexture* texture;
        RHITexture* resolveTexture = nullptr;
        LoadOp loadOp;
        StoreOp storeOp;
        ClearValue clearValue;
        uint32_t mipLevel = 0;
        uint32_t arrayLayer = 0;
    };

    struct RenderingInfo
    {
        Rect2D renderArea;
        std::vector<RenderingAttachment> colorAttachments;
        RenderingAttachment* depthAttachment = nullptr;
        RenderingAttachment* stencilAttachment = nullptr;
    };

    class RHICommandBuffer
    {
    public:
        virtual ~RHICommandBuffer() = default;

        virtual void setProfilingContext(void* ctx) { (void)ctx; }
        virtual void* getProfilingContext() const { return nullptr; }

        virtual void resolveTexture(RHITexture* src, ResourceLayout srcLayout,
                                    RHITexture* dst, ResourceLayout dstLayout,
                                    const TextureCopyRegion& region) = 0;

        virtual void begin() = 0;
        virtual void end() = 0;
        virtual void reset() = 0;

        virtual void beginRendering(const RenderingInfo& info) = 0;
        virtual void endRendering() = 0;

        virtual void bindPipeline(RHIPipeline* pipeline) = 0;

        virtual void bindVertexBuffer(uint32_t binding, RHIBuffer* buffer, uint64_t offset = 0) = 0;
        virtual void bindIndexBuffer(RHIBuffer* buffer, uint64_t offset = 0, bool use16Bit = false) = 0;

        virtual void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
                         uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;
        virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                                uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                                uint32_t firstInstance = 0) = 0;

        virtual void drawIndexedIndirect(RHIBuffer* buffer, uint64_t offset, uint32_t drawCount, uint32_t stride) = 0;

        virtual void drawIndexedIndirectCount(RHIBuffer* buffer, uint64_t offset,
                                              RHIBuffer* countBuffer, uint64_t countBufferOffset,
                                              uint32_t maxDrawCount, uint32_t stride) = 0;

        virtual void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;

        virtual void pushConstants(RHIPipeline* pipeline, ShaderStageFlags stages,
                                  uint32_t offset, uint32_t size, const void* data) = 0;

        virtual void bindDescriptorSet(RHIPipeline* pipeline, uint32_t setIndex,
                                      RHIDescriptorSet* descriptorSet) = 0;

        void bindDescriptorSet(uint32_t setIndex, RHIDescriptorSet* descriptorSet)
        {
            auto* pipeline = boundPipeline();
            if (!pipeline)
            {
                throw std::runtime_error("RHICommandBuffer::bindDescriptorSet requires a bound pipeline");
            }
            bindDescriptorSet(pipeline, setIndex, descriptorSet);
        }

        template <typename T>
        void pushConstants(ShaderStageFlags stages, const T& data, uint32_t offset = 0)
        {
            static_assert(std::is_trivially_copyable_v<T>,
                          "pushConstants<T>: T must be trivially copyable");
            pushConstantsInternal(stages, offset, sizeof(T), &data);
        }

        virtual void setViewport(const Viewport& viewport) = 0;
        virtual void setScissor(const Rect2D& scissor) = 0;
        virtual void setDepthBias(float constantFactor, float clamp, float slopeFactor) = 0;

        virtual void setCullMode(CullMode mode) = 0;
        virtual void setDepthTestEnable(bool enable) = 0;
        virtual void setDepthWriteEnable(bool enable) = 0;
        virtual void setDepthCompareOp(CompareOp op) = 0;
        virtual void setPrimitiveTopology(PrimitiveTopology topology) = 0;

        virtual void pipelineBarrier(
            ShaderStageFlags srcStage,
            ShaderStageFlags dstStage,
            const std::vector<RHIMemoryBarrier>& barriers) = 0;

        virtual void copyBuffer(RHIBuffer* src, RHIBuffer* dst,
                               uint64_t srcOffset, uint64_t dstOffset, uint64_t size) = 0;
        virtual void fillBuffer(RHIBuffer* buffer, uint64_t offset, uint64_t size, uint32_t data) = 0;
        virtual void copyBufferToTexture(RHIBuffer* src, RHITexture* dst,
                                        const BufferTextureCopyRegion& region) = 0;
        virtual void copyBufferToTexture(RHIBuffer* src, RHITexture* dst,
                                        std::span<const BufferTextureCopyRegion> regions) = 0;
        virtual void copyTextureToBuffer(RHITexture* src, RHIBuffer* dst,
                                        const BufferTextureCopyRegion& region) = 0;
        virtual void copyTexture(RHITexture* src, RHITexture* dst,
                                const TextureCopyRegion& region) = 0;
        virtual void blitTexture(RHITexture* src, RHITexture* dst,
                                const TextureBlitRegion& region, Filter filter = Filter::Linear) = 0;
        virtual void clearImage(RHITexture* texture, const ClearValue& clearValue,
                               ResourceLayout layout = ResourceLayout::General) = 0;

        virtual void beginDebugLabel(const char* name, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f) = 0;
        virtual void endDebugLabel() = 0;
        virtual void insertDebugLabel(const char* name, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f) = 0;

        virtual void pushGPUMarker(const char* name) = 0;
        virtual void popGPUMarker() = 0;

        virtual void setCheckpoint(const char* name) { (void)name;}

        virtual void setFrameIndex(uint32_t frameIndex) { (void)frameIndex; }

        virtual void* nativeHandle() const = 0;

    protected:
        virtual RHIPipeline* boundPipeline() const = 0;
        virtual void pushConstantsInternal(ShaderStageFlags stages,
                                           uint32_t offset,
                                           uint32_t size,
                                           const void* data) = 0;
    };

}
