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

    /**
     * @brief Interface for the Render Hardware Interface (RHI) device.
     * 
     * The RHIDevice is responsible for resource creation, command submission, and synchronization.
     */
    class RHIDevice
    {
    public:
        virtual ~RHIDevice() = default;

        /**
         * @brief Creates a GPU buffer.
         * @param name Debug name for the buffer.
         * @param desc Descriptor defining buffer size, usage, and memory properties.
         * @return Unique pointer to the created RHIBuffer.
         */
        virtual std::unique_ptr<RHIBuffer> createBuffer(const char* name, const BufferDescriptor& desc) = 0;

        std::unique_ptr<RHIBuffer> createBuffer(const BufferDescriptor& desc)
        {
            const char* name = desc.debugName.empty() ? "Buffer" : desc.debugName.c_str();
            return createBuffer(name, desc);
        }

        /**
         * @brief Creates a GPU texture.
         * @param name Debug name for the texture.
         * @param desc Descriptor defining texture dimensions, format, and usage.
         * @return Unique pointer to the created RHITexture.
         */
        virtual std::unique_ptr<RHITexture> createTexture(const char* name, const TextureDescriptor& desc) = 0;

        std::unique_ptr<RHITexture> createTexture(const TextureDescriptor& desc)
        {
            const char* name = desc.debugName.empty() ? "Texture" : desc.debugName.c_str();
            return createTexture(name, desc);
        }

        /**
         * @brief Creates a texture view from an existing texture.
         * @param name Debug name for the view.
         * @param parent The parent texture to create a view from.
         * @param desc Descriptor defining the view's subresource range and format.
         * @return Unique pointer to the created RHITexture (view).
         */
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

        /**
         * @brief Creates a GPU texture with basic parameters.
         */
        virtual std::unique_ptr<RHITexture> createTexture(
            const Extent3D& extent,
            Format format,
            TextureUsageFlags usage,
            uint32_t mipLevels = 1,
            uint32_t arrayLayers = 1) = 0;

        /**
         * @brief Creates a cubemap texture.
         */
        virtual std::unique_ptr<RHITexture> createCubemap(
            const Extent3D& extent,
            Format format,
            TextureUsageFlags usage,
            uint32_t mipLevels = 1) = 0;

        /**
         * @brief Creates a sampler for texture sampling.
         */
        virtual std::unique_ptr<RHISampler> createSampler(
            Filter minFilter,
            Filter magFilter,
            SamplerAddressMode addressMode,
            CompareOp compareOp = CompareOp::None) = 0;

        /**
         * @brief Creates a command pool for allocating command buffers.
         */
        virtual std::unique_ptr<RHICommandPool> createCommandPool(const CommandPoolDescriptor& desc) = 0;

        /**
         * @brief Creates a command buffer for recording GPU commands.
         */
        virtual std::unique_ptr<RHICommandBuffer> createCommandBuffer(RHICommandPool* pool = nullptr) = 0;

        std::unique_ptr<RHICommandList> createCommandList(RHICommandPool* pool = nullptr)
        {
            return createCommandBuffer(pool);
        }

        /**
         * @brief Creates a graphics pipeline state object.
         */
        virtual std::unique_ptr<RHIPipeline> createGraphicsPipeline(
            const struct GraphicsPipelineDescriptor& desc) = 0;

        /**
         * @brief Creates a compute pipeline state object.
         */
        virtual std::unique_ptr<RHIPipeline> createComputePipeline(
            const struct ComputePipelineDescriptor& desc) = 0;

        /**
         * @brief Creates a context for uploading data to the GPU.
         */
        virtual std::unique_ptr<RHIUploadContext> createUploadContext(uint64_t stagingBufferSize = 64 * 1024 * 1024) =
        0;

        /**
         * @brief Creates a descriptor set layout defining the binding structure for shaders.
         */
        virtual std::unique_ptr<RHIDescriptorSetLayout> createDescriptorSetLayout(
            const struct DescriptorSetLayout& desc) = 0;

        /**
         * @brief Allocates a descriptor set from a layout.
         */
        virtual std::unique_ptr<RHIDescriptorSet> allocateDescriptorSet(
            RHIDescriptorSetLayout* layout) = 0;

        /**
         * @brief Creates a GPU fence for CPU-GPU synchronization.
         */
        virtual std::unique_ptr<RHIFence> createFence(bool signaled = false) = 0;

        /**
         * @brief Waits until the GPU is idle.
         */
        virtual void waitIdle() = 0;

        /**
         * @brief Waits for a set of fences to be signaled.
         */
        virtual void waitForFences(const std::vector<uint64_t>& fenceValues) = 0;

        /**
         * @brief Waits for a specific frame to complete.
         */
        virtual void waitForFrame(uint64_t frameIndex) = 0;

        /**
         * @brief Increments the frame counter.
         */
        virtual uint64_t incrementFrame() = 0;

        /**
         * @brief Returns the index of the last completed frame.
         */
        virtual uint64_t getCompletedFrame() const = 0;

        /**
         * @brief Submits a command list to the graphics queue.
         */
        virtual void submitCommands(
            RHICommandList* commandBuffer,
            RHIFence* signalFence = nullptr,
            const std::vector<uint64_t>& waitSemaphores = {},
            const std::vector<uint64_t>& signalSemaphores = {},
            RHISwapchain* swapchain = nullptr) = 0;

        /**
         * @brief Submits a command list to the compute queue.
         */
        virtual void submitComputeCommands(
            RHICommandList* commandBuffer,
            bool waitForPreviousCompute = true,
            bool signalGraphicsQueue = true) = 0;

        /**
         * @brief Returns the value of the last signaled compute semaphore.
         */
        virtual uint64_t getLastComputeSemaphoreValue() const = 0;

        /**
         * @brief Executes a function immediately by submitting a temporary command buffer.
         */
        virtual void immediateSubmit(std::function<void(RHICommandList*)>&& func) = 0;

        /**
         * @brief Downloads texture data from the GPU to the CPU.
         */
        virtual void downloadTexture(
            RHITexture* texture,
            std::span<std::byte> outData,
            const TextureSubresource& subresource = {}) = 0;

        /**
         * @brief Returns the physical device information.
         */
        virtual const RHIPhysicalDevice& physicalDevice() const = 0;

        virtual uint32_t graphicsQueueFamily() const = 0;
        virtual uint32_t computeQueueFamily() const = 0;
        virtual uint32_t transferQueueFamily() const = 0;

        /**
         * @brief Returns the maximum usable MSAA sample count.
         */
        virtual uint32_t getMaxUsableSampleCount() const = 0;

        /**
         * @brief Returns the bindless resource manager.
         */
        virtual BindlessManager* getBindlessManager() = 0;

        /**
         * @brief Creates an ImGui renderer for this device.
         */
        virtual std::unique_ptr<RHIImGui> createImGuiRenderer() = 0;

        /**
         * @brief Returns the GPU time queries manager for profiling.
         */
        virtual ::pnkr::renderer::GPUTimeQueriesManager* gpuProfiler() { return nullptr; }

        /**
         * @brief Clears the internal pipeline cache.
         */
        virtual void clearPipelineCache() = 0;

        /**
         * @brief Returns the current size of the pipeline cache in bytes.
         */
        virtual size_t getPipelineCacheSize() const = 0;

        /**
         * @brief Audits a Buffer Device Address (BDA) for debugging.
         */
        virtual void auditBDA(uint64_t address, const char* context) = 0;

        /**
         * @brief Returns the global bindless descriptor set.
         */
        virtual RHIDescriptorSet* getBindlessDescriptorSet() = 0;

        /**
         * @brief Returns the global bindless descriptor set layout.
         */
        virtual RHIDescriptorSetLayout* getBindlessDescriptorSetLayout() = 0;

        /**
         * @brief Returns the native API instance handle (e.g. VkInstance).
         */
        virtual void* getNativeInstance() const = 0;
    };
}
