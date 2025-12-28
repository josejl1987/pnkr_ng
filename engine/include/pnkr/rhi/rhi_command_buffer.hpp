#pragma once

#include "rhi_types.hpp"
#include <vector>

#include "rhi_texture.hpp"
#include "rhi_descriptor.hpp"

namespace pnkr::renderer::rhi
{
    // Forward declarations
    class RHIBuffer;
    class RHITexture;
    class RHIPipeline;

    // Memory barrier - renamed to avoid Windows macro conflict
    struct RHIMemoryBarrier
    {
        RHIBuffer* buffer = nullptr;
        RHITexture* texture = nullptr;
        ShaderStage srcAccessStage;
        ShaderStage dstAccessStage;
        // For textures
        ResourceLayout oldLayout = ResourceLayout::Undefined;
        ResourceLayout newLayout = ResourceLayout::Undefined;
    };

    // Buffer to texture copy region
    struct BufferTextureCopyRegion
    {
        uint64_t bufferOffset = 0;
        uint32_t bufferRowLength = 0;
        uint32_t bufferImageHeight = 0;

        TextureSubresource textureSubresource;

        struct {
            int32_t x = 0;
            int32_t y = 0;
            int32_t z = 0;
        } textureOffset;

        Extent3D textureExtent;
    };

    struct TextureCopyRegion
    {
        TextureSubresource srcSubresource;
        TextureSubresource dstSubresource;

        struct {
            int32_t x = 0;
            int32_t y = 0;
            int32_t z = 0;
        } srcOffset;

        struct {
            int32_t x = 0;
            int32_t y = 0;
            int32_t z = 0;
        } dstOffset;

        Extent3D extent;
    };

    // Rendering attachment
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

    // Dynamic rendering info
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

        // Command buffer lifecycle
        virtual void begin() = 0;
        virtual void end() = 0;
        virtual void reset() = 0;

        // Rendering commands
        virtual void beginRendering(const RenderingInfo& info) = 0;
        virtual void endRendering() = 0;

        // Pipeline binding
        virtual void bindPipeline(RHIPipeline* pipeline) = 0;

        // Vertex/index buffers
        virtual void bindVertexBuffer(uint32_t binding, RHIBuffer* buffer, uint64_t offset = 0) = 0;
        virtual void bindIndexBuffer(RHIBuffer* buffer, uint64_t offset = 0, bool use16Bit = false) = 0;

        // Draw commands
        virtual void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
                         uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;
        virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                                uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                                uint32_t firstInstance = 0) = 0;

        virtual void drawIndexedIndirect(RHIBuffer* buffer, uint64_t offset, uint32_t drawCount, uint32_t stride) = 0;

        // Compute dispatch
        virtual void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;

        // Push constants
        virtual void pushConstants(RHIPipeline* pipeline, ShaderStage stages,
                                  uint32_t offset, uint32_t size, const void* data) = 0;

        // Descriptor sets
        virtual void bindDescriptorSet(RHIPipeline* pipeline, uint32_t setIndex,
                                      RHIDescriptorSet* descriptorSet) = 0;

        // Dynamic state
        virtual void setViewport(const Viewport& viewport) = 0;
        virtual void setScissor(const Rect2D& scissor) = 0;
        virtual void setDepthBias(float constantFactor, float clamp, float slopeFactor) = 0;

        // Extended Dynamic State
        virtual void setCullMode(CullMode mode) = 0;
        virtual void setDepthTestEnable(bool enable) = 0;
        virtual void setDepthWriteEnable(bool enable) = 0;
        virtual void setDepthCompareOp(CompareOp op) = 0;
        virtual void setPrimitiveTopology(PrimitiveTopology topology) = 0;

        virtual void pipelineBarrier(
            ShaderStage srcStage,
            ShaderStage dstStage,
            const std::vector<RHIMemoryBarrier>& barriers) = 0;

        // Copy operations
        virtual void copyBuffer(RHIBuffer* src, RHIBuffer* dst,
                               uint64_t srcOffset, uint64_t dstOffset, uint64_t size) = 0;
        virtual void copyBufferToTexture(RHIBuffer* src, RHITexture* dst,
                                        const BufferTextureCopyRegion& region) = 0;
        virtual void copyTextureToBuffer(RHITexture* src, RHIBuffer* dst,
                                        const BufferTextureCopyRegion& region) = 0;
        virtual void copyTexture(RHITexture* src, RHITexture* dst,
                                const TextureCopyRegion& region) = 0;

        // Debug Groups (RenderDoc / Nsight)
        virtual void beginDebugLabel(const char* name, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f) = 0;
        virtual void endDebugLabel() = 0;
        virtual void insertDebugLabel(const char* name, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f) = 0;

        // Backend handle
        virtual void* nativeHandle() const = 0;
    };

} // namespace pnkr::renderer::rhi
