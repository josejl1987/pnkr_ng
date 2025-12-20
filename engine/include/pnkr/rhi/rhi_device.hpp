#pragma once

#include "rhi_types.hpp"
#include <memory>
#include <string>
#include <vector>

namespace pnkr::renderer::rhi
{
    // Forward declarations
    class RHIBuffer;
    class RHITexture;
    class RHIPipeline;
    class RHISwapchain;
    class RHICommandBuffer;
    class RHISampler;
    class RHIDescriptorSetLayout;
    class RHIDescriptorSet;

    // Forward declaration for BufferDescriptor
    struct BufferDescriptor;

    // Physical device capabilities
    struct DeviceCapabilities
    {
        std::string deviceName;
        uint32_t vendorID;
        uint32_t deviceID;
        bool discreteGPU;

        // Limits
        uint32_t maxImageDimension2D;
        uint32_t maxImageDimension3D;
        uint32_t maxFramebufferWidth;
        uint32_t maxFramebufferHeight;
        uint32_t maxColorAttachments;

        // Features
        bool geometryShader;
        bool tessellationShader;
        bool samplerAnisotropy;
        bool textureCompressionBC;
        bool bindlessTextures;
        bool rayTracing;
        bool meshShading;
    };

    // Queue family information
    struct QueueFamilyInfo
    {
        uint32_t familyIndex;
        uint32_t queueCount;
        bool supportsGraphics;
        bool supportsCompute;
        bool supportsTransfer;
    };

    // Device creation descriptor
    struct DeviceDescriptor
    {
        std::vector<const char*> requiredExtensions;
        std::vector<const char*> optionalExtensions;
        bool enableValidation = false;
        bool enableBindless = false;
        bool enableRayTracing = false;
    };

    // Abstract physical device
    class RHIPhysicalDevice
    {
    public:
        virtual ~RHIPhysicalDevice() = default;

        virtual const DeviceCapabilities& capabilities() const = 0;
        virtual std::vector<QueueFamilyInfo> queueFamilies() const = 0;
        virtual bool supportsPresentation(uint32_t queueFamily) const = 0;
    };

    // Abstract logical device
    class RHIDevice
    {
    public:
        virtual ~RHIDevice() = default;

        // Resource creation
        virtual std::unique_ptr<RHIBuffer> createBuffer(const BufferDescriptor& desc) = 0;

        virtual std::unique_ptr<RHITexture> createTexture(
            const Extent3D& extent,
            Format format,
            TextureUsage usage,
            uint32_t mipLevels = 1,
            uint32_t arrayLayers = 1) = 0;

        virtual std::unique_ptr<RHITexture> createCubemap(
            const Extent3D& extent,
            Format format,
            TextureUsage usage,
            uint32_t mipLevels = 1) = 0;

        virtual std::unique_ptr<RHISampler> createSampler(
            Filter minFilter,
            Filter magFilter,
            SamplerAddressMode addressMode) = 0;

        virtual std::unique_ptr<RHICommandBuffer> createCommandBuffer() = 0;

        virtual std::unique_ptr<RHIPipeline> createGraphicsPipeline(
            const struct GraphicsPipelineDescriptor& desc) = 0;

        virtual std::unique_ptr<RHIPipeline> createComputePipeline(
            const struct ComputePipelineDescriptor& desc) = 0;

        // Descriptor sets/layouts
        virtual std::unique_ptr<RHIDescriptorSetLayout> createDescriptorSetLayout(
            const struct DescriptorSetLayout& desc) = 0;
        virtual std::unique_ptr<RHIDescriptorSet> allocateDescriptorSet(
            RHIDescriptorSetLayout* layout) = 0;

        // Synchronization
        virtual void waitIdle() = 0;
        virtual void waitForFences(const std::vector<uint64_t>& fenceValues) = 0;

        // Queue submission
        virtual void submitCommands(
            RHICommandBuffer* commandBuffer,
            const std::vector<uint64_t>& waitSemaphores = {},
            const std::vector<uint64_t>& signalSemaphores = {}) = 0;

        // Device queries
        virtual const RHIPhysicalDevice& physicalDevice() const = 0;
        virtual uint32_t graphicsQueueFamily() const = 0;
        virtual uint32_t computeQueueFamily() const = 0;
        virtual uint32_t transferQueueFamily() const = 0;

        // Bindless Registration
        virtual BindlessHandle registerBindlessTexture(RHITexture* texture, RHISampler* sampler) = 0;
        virtual BindlessHandle registerBindlessCubemap(RHITexture* texture, RHISampler* sampler) = 0;
        virtual BindlessHandle registerBindlessStorageImage(RHITexture* texture) = 0;
        virtual BindlessHandle registerBindlessBuffer(RHIBuffer* buffer) = 0;

        // To bind the global set to a command buffer
        virtual void* getBindlessDescriptorSetNative() = 0; 
        virtual RHIDescriptorSetLayout* getBindlessDescriptorSetLayout() = 0;
    };

} // namespace pnkr::renderer::rhi
