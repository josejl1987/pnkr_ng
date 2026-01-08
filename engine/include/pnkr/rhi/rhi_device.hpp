#pragma once

#include "rhi_types.hpp"
#include "rhi_texture.hpp"
#include "rhi_command_buffer.hpp"
#include "rhi_buffer.hpp"
#include "rhi_sync.hpp"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <cstddef>
#include <span>

namespace pnkr::renderer
{
    class GPUTimeQueriesManager;
}

namespace pnkr::renderer::rhi
{

    class RHIBuffer;
    class RHITexture;
    class RHIPipeline;
    class RHISwapchain;
    class RHISampler;
    class RHIDescriptorSetLayout;
    class RHIDescriptorSet;
    class BindlessManager;
    class RHIImGui;

    struct DeviceCapabilities
    {
        std::string deviceName;
        uint32_t vendorID;
        uint32_t deviceID;
        bool discreteGPU;

        uint32_t maxImageDimension2D;
        uint32_t maxImageDimension3D;
        uint32_t maxFramebufferWidth;
        uint32_t maxFramebufferHeight;
        uint32_t maxColorAttachments;

        bool geometryShader;
        bool tessellationShader;
        bool samplerAnisotropy;
        bool textureCompressionBC;
        bool bindlessTextures;
        bool drawIndirectCount;
        bool pipelineStatisticsQuery;
        bool rayTracing;
        bool meshShading;

        uint32_t maxColorSampleCount = 1;
        uint32_t maxDepthSampleCount = 1;
        uint32_t maxCombinedSampleCount = 1;
    };

    struct QueueFamilyInfo
    {
        uint32_t familyIndex;
        uint32_t queueCount;
        bool supportsGraphics;
        bool supportsCompute;
        bool supportsTransfer;
    };

    struct DeviceDescriptor
    {
        std::vector<const char*> requiredExtensions;
        std::vector<const char*> optionalExtensions;
        bool enableValidation = false;
        bool enableBindless = false;
        bool enableRayTracing = false;
    };

    class RHIPhysicalDevice
    {
    public:
        virtual ~RHIPhysicalDevice() = default;

        virtual const DeviceCapabilities& capabilities() const = 0;
        virtual std::vector<QueueFamilyInfo> queueFamilies() const = 0;
        virtual bool supportsPresentation(uint32_t queueFamily) const = 0;
    };

    class RHIUploadContext
    {
    public:
        virtual ~RHIUploadContext() = default;
        virtual void uploadTexture(RHITexture* texture, std::span<const std::byte> data,
                                   const TextureSubresource& subresource = {}) = 0;
        virtual void uploadBuffer(RHIBuffer* buffer, std::span<const std::byte> data, uint64_t offset = 0) = 0;
        virtual void flush() = 0;
    };

    struct RHICommandPool
    {
        virtual ~RHICommandPool() = default;
        virtual void reset() = 0;
        virtual void* nativeHandle() = 0;
    };

    enum class CommandPoolFlags
    {
        None = 0,
        Transient = 1 << 0,
        ResetCommandBuffer = 1 << 1,
    };

    struct CommandPoolDescriptor
    {
        uint32_t queueFamilyIndex = 0;
        CommandPoolFlags flags = CommandPoolFlags::ResetCommandBuffer;
    };

    class RHIDevice
    {
    public:
        virtual ~RHIDevice() = default;

        virtual std::unique_ptr<RHIBuffer> createBuffer(const char* name, const BufferDescriptor& desc) = 0;

        std::unique_ptr<RHIBuffer> createBuffer(const BufferDescriptor& desc)
        {
            const char* name = desc.debugName.empty() ? "Buffer" : desc.debugName.c_str();
            return createBuffer(name, desc);
        }

        virtual std::unique_ptr<RHITexture> createTexture(const char* name, const TextureDescriptor& desc) = 0;

        std::unique_ptr<RHITexture> createTexture(const TextureDescriptor& desc)
        {
            const char* name = desc.debugName.empty() ? "Texture" : desc.debugName.c_str();
            return createTexture(name, desc);
        }

        virtual std::unique_ptr<RHITexture> createTextureView(
            const char* name,
            RHITexture* parent,
            const TextureViewDescriptor& desc) = 0;

        std::unique_ptr<RHITexture> createTextureView(
            RHITexture* parent,
            const TextureViewDescriptor& desc)
        {
            return createTextureView("TextureView", parent, desc);
        }

        virtual std::unique_ptr<RHITexture> createTexture(
            const Extent3D& extent,
            Format format,
            TextureUsageFlags usage,
            uint32_t mipLevels = 1,
            uint32_t arrayLayers = 1) = 0;

        virtual std::unique_ptr<RHITexture> createCubemap(
            const Extent3D& extent,
            Format format,
            TextureUsageFlags usage,
            uint32_t mipLevels = 1) = 0;

        virtual std::unique_ptr<RHISampler> createSampler(
            Filter minFilter,
            Filter magFilter,
            SamplerAddressMode addressMode,
            CompareOp compareOp = CompareOp::None) = 0;

        virtual std::unique_ptr<RHICommandPool> createCommandPool(const CommandPoolDescriptor& desc) = 0;
        virtual std::unique_ptr<RHICommandBuffer> createCommandBuffer(RHICommandPool* pool = nullptr) = 0;
        std::unique_ptr<RHICommandList> createCommandList(RHICommandPool* pool = nullptr)
        {
            return createCommandBuffer(pool);
        }

        virtual std::unique_ptr<RHIPipeline> createGraphicsPipeline(
            const struct GraphicsPipelineDescriptor& desc) = 0;

        virtual std::unique_ptr<RHIPipeline> createComputePipeline(
            const struct ComputePipelineDescriptor& desc) = 0;

        virtual std::unique_ptr<RHIUploadContext> createUploadContext(uint64_t stagingBufferSize = 64 * 1024 * 1024) =
        0;

        virtual std::unique_ptr<RHIDescriptorSetLayout> createDescriptorSetLayout(
            const struct DescriptorSetLayout& desc) = 0;
        virtual std::unique_ptr<RHIDescriptorSet> allocateDescriptorSet(
            RHIDescriptorSetLayout* layout) = 0;

        virtual std::unique_ptr<RHIFence> createFence(bool signaled = false) = 0;
        virtual void waitIdle() = 0;
        virtual void waitForFences(const std::vector<uint64_t>& fenceValues) = 0;

        virtual void waitForFrame(uint64_t frameIndex) = 0;
        virtual uint64_t incrementFrame() = 0;
        virtual uint64_t getCompletedFrame() const = 0;

        virtual void submitCommands(
            RHICommandList* commandBuffer,
            RHIFence* signalFence = nullptr,
            const std::vector<uint64_t>& waitSemaphores = {},
            const std::vector<uint64_t>& signalSemaphores = {},
            RHISwapchain* swapchain = nullptr) = 0;

        virtual void submitComputeCommands(
            RHICommandList* commandBuffer,
            bool waitForPreviousCompute = true,
            bool signalGraphicsQueue = true) = 0;

        virtual uint64_t getLastComputeSemaphoreValue() const = 0;

        virtual void immediateSubmit(std::function<void(RHICommandList*)>&& func) = 0;

        virtual void downloadTexture(
            RHITexture* texture,
            std::span<std::byte> outData,
            const TextureSubresource& subresource = {}) = 0;

        virtual const RHIPhysicalDevice& physicalDevice() const = 0;
        virtual uint32_t graphicsQueueFamily() const = 0;
        virtual uint32_t computeQueueFamily() const = 0;
        virtual uint32_t transferQueueFamily() const = 0;

        virtual uint32_t getMaxUsableSampleCount() const = 0;

        virtual BindlessManager* getBindlessManager() = 0;

        virtual std::unique_ptr<RHIImGui> createImGuiRenderer() = 0;

        virtual ::pnkr::renderer::GPUTimeQueriesManager* gpuProfiler() { return nullptr; }

        virtual void clearPipelineCache() = 0;
        virtual size_t getPipelineCacheSize() const = 0;

        virtual void auditBDA(uint64_t address, const char* context) = 0;

        virtual RHIDescriptorSet* getBindlessDescriptorSet() = 0;
        virtual RHIDescriptorSetLayout* getBindlessDescriptorSetLayout() = 0;
        virtual void* getNativeInstance() const = 0;
    };
}
