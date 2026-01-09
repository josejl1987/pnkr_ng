#pragma once

#include "pnkr/rhi/rhi_device.hpp"
#include "pnkr/rhi/rhi_descriptor.hpp"
#include "pnkr/core/profiler.hpp"
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <memory>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <functional>
#include <string_view>
#include <unordered_map>
#include "rhi/vulkan/VulkanDeletionQueue.hpp"

namespace pnkr::platform { class Window; }

namespace pnkr::renderer::rhi::vulkan
{
    struct VmaAllocatorDeleter {
        void operator()(VmaAllocator allocator) const;
    };
    using UniqueVmaAllocator = std::unique_ptr<struct VmaAllocator_T, VmaAllocatorDeleter>;

    struct VulkanInstanceContext
    {
        vk::Instance instance{};
        vk::DebugUtilsMessengerEXT debugMessenger{};
        bool hasDebugMessenger = false;

        ~VulkanInstanceContext();
    };

    class BindlessDescriptorManager;
    class VulkanGPUTimeQueriesManager;
    class BDARegistry;
    class VulkanRHIPhysicalDevice;
    class VulkanSyncManager;

    struct VulkanQueues
    {
        uint32_t graphicsFamily = VK_QUEUE_FAMILY_IGNORED;
        uint32_t computeFamily = VK_QUEUE_FAMILY_IGNORED;
        uint32_t transferFamily = VK_QUEUE_FAMILY_IGNORED;
        vk::Queue graphics;
        vk::Queue compute;
        vk::Queue transfer;
    };

    struct VulkanDeviceConstructionContext
    {
        std::unique_ptr<VulkanRHIPhysicalDevice> physicalDevice;
        vk::Device device;
        UniqueVmaAllocator allocator;
        VulkanQueues queues;
        vk::PhysicalDeviceFeatures enabledFeatures;
        bool minLodExtensionEnabled = false;
        bool calibratedTimestampsEnabled = false;

        vk::CommandPool commandPool;
        vk::Semaphore frameTimelineSemaphore;
        vk::Semaphore computeTimelineSemaphore;
        vk::DescriptorPool descriptorPool;
        vk::PipelineCache pipelineCache;
        std::unique_ptr<BDARegistry> bdaRegistry;
        std::unique_ptr<BindlessDescriptorManager> bindlessManager;
        std::unique_ptr<VulkanGPUTimeQueriesManager> gpuProfiler;
    };


    class VulkanRHIPhysicalDevice : public RHIPhysicalDevice
    {
    public:
        explicit VulkanRHIPhysicalDevice(vk::PhysicalDevice physicalDevice,
                                         std::shared_ptr<VulkanInstanceContext> instanceContext);
        ~VulkanRHIPhysicalDevice() override = default;

        const DeviceCapabilities& capabilities() const override { return m_capabilities; }
        std::vector<QueueFamilyInfo> queueFamilies() const override { return m_queueFamilies; }
        bool supportsPresentation(uint32_t queueFamily) const override;

        vk::PhysicalDevice physicalDevice() const { return m_physicalDevice; }
        vk::Instance instance() const { return m_instanceContext ? m_instanceContext->instance : vk::Instance{}; }
        bool hasDebugMessenger() const { return m_instanceContext && m_instanceContext->hasDebugMessenger; }

    private:
        vk::PhysicalDevice m_physicalDevice;
        std::shared_ptr<VulkanInstanceContext> m_instanceContext;
        DeviceCapabilities m_capabilities;
        std::vector<QueueFamilyInfo> m_queueFamilies;

        void queryCapabilities();
        void queryQueueFamilies();
    };

    class VulkanRHIDevice;
    class VulkanResourceFactory;

    class VulkanRHICommandPool : public RHICommandPool
    {
    public:
        VulkanRHICommandPool(VulkanRHIDevice* device, const CommandPoolDescriptor& desc);
        ~VulkanRHICommandPool() override;

        void reset() override;
        void* nativeHandle() override { return (void*)(VkCommandPool)m_pool; }
        vk::CommandPool pool() const { return m_pool; }
        uint32_t queueFamilyIndex() const { return m_queueFamilyIndex; }

    private:
        VulkanRHIDevice* m_device;
        vk::CommandPool m_pool;
        uint32_t m_queueFamilyIndex;
    };

    class VulkanRHIDevice : public RHIDevice
    {
    public:
        using TrackedVulkanObject = VulkanDeletionQueue::TrackedVulkanObject;

        static std::unique_ptr<VulkanRHIDevice> create(std::unique_ptr<VulkanRHIPhysicalDevice> physicalDevice,
                                                       const DeviceDescriptor& desc);

        explicit VulkanRHIDevice(VulkanDeviceConstructionContext&& ctx);

        ~VulkanRHIDevice() override;

        PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT getPhysicalDeviceCalibrateableTimeDomainsEXT = nullptr;
        PFN_vkGetCalibratedTimestampsEXT getCalibratedTimestampsEXT = nullptr;

        VulkanRHIDevice(const VulkanRHIDevice&) = delete;
        VulkanRHIDevice& operator=(const VulkanRHIDevice&) = delete;

        std::unique_ptr<RHIBuffer> createBuffer(const char* name, const BufferDescriptor& desc) override;
        std::unique_ptr<RHITexture> createTexture(const char* name, const TextureDescriptor& desc) override;
        std::unique_ptr<RHITexture> createTextureView(const char* name, RHITexture* parent, const TextureViewDescriptor& desc) override;
        std::unique_ptr<RHITexture> createTexture(const Extent3D& extent, Format format, TextureUsageFlags usage, uint32_t mipLevels = 1, uint32_t arrayLayers = 1) override;
        std::unique_ptr<RHITexture> createCubemap(const Extent3D& extent, Format format, TextureUsageFlags usage, uint32_t mipLevels) override;
        std::unique_ptr<RHISampler> createSampler(Filter minFilter, Filter magFilter, SamplerAddressMode addressMode, CompareOp compareOp = CompareOp::None) override;

        std::unique_ptr<RHICommandPool> createCommandPool(const CommandPoolDescriptor& desc) override;
        std::unique_ptr<RHICommandBuffer> createCommandBuffer(RHICommandPool* pool = nullptr) override;

        std::unique_ptr<RHIPipeline> createGraphicsPipeline(const GraphicsPipelineDescriptor& desc) override;
        std::unique_ptr<RHIPipeline> createComputePipeline(const ComputePipelineDescriptor& desc) override;
        std::unique_ptr<RHIUploadContext> createUploadContext(uint64_t stagingBufferSize = 64 * 1024 * 1024) override;
        RHIUploadContext* uploadContext();
        uint32_t getMaxUsableSampleCount() const override;
        std::unique_ptr<RHIDescriptorSetLayout> createDescriptorSetLayout(const DescriptorSetLayout& desc) override;
        std::unique_ptr<RHIDescriptorSet> allocateDescriptorSet(RHIDescriptorSetLayout* layout) override;

        std::unique_ptr<RHIFence> createFence(bool signaled = false) override;
        void waitIdle() override;
        void waitForFences(const std::vector<uint64_t>& fenceValues) override;
        void waitForFrame(uint64_t frameIndex) override;
        uint64_t incrementFrame() override;
        uint64_t getCompletedFrame() const override;
        uint64_t getCurrentFrame() const;

        void submitCommands(
            RHICommandList* commandBuffer,
            RHIFence* signalFence = nullptr,
            const std::vector<uint64_t>& waitSemaphores = {},
            const std::vector<uint64_t>& signalSemaphores = {},
            RHISwapchain* swapchain = nullptr) override;

        void submitComputeCommands(
            RHICommandList* commandBuffer,
            bool waitForPreviousCompute = true,
            bool signalGraphicsQueue = true) override;

        uint64_t getLastComputeSemaphoreValue() const override;

        void immediateSubmit(std::function<void(RHICommandList*)>&& func) override;

        void downloadTexture(RHITexture* texture, std::span<std::byte> outData, const TextureSubresource& subresource = {}) override;

        const RHIPhysicalDevice& physicalDevice() const override { return *m_physicalDevice; }
        uint32_t graphicsQueueFamily() const override { return m_graphicsQueueFamily; }
        uint32_t computeQueueFamily() const override { return m_computeQueueFamily; }
        uint32_t transferQueueFamily() const override { return m_transferQueueFamily; }

        BindlessManager* getBindlessManager() override;

        std::unique_ptr<RHIImGui> createImGuiRenderer() override;

        ::pnkr::renderer::GPUTimeQueriesManager* gpuProfiler() override;

        void clearPipelineCache() override;
        size_t getPipelineCacheSize() const override;

        RHIDescriptorSet* getBindlessDescriptorSet() override;
        RHIDescriptorSetLayout* getBindlessDescriptorSetLayout() override;

        void* getNativeInstance() const override { return (void*)(VkInstance)instance(); }

        vk::Semaphore getTimelineSemaphore() const;
        vk::Semaphore getComputeTimelineSemaphore() const;

        vk::PipelineCache getPipelineCache() const { return m_pipelineCache; }

        vk::Device device() const { return m_device; }
        vk::Instance instance() const { return m_physicalDevice->instance(); }
        vk::PhysicalDevice vkPhysicalDevice() const { return m_physicalDevice->physicalDevice(); }
        vk::Queue graphicsQueue() const;
        vk::Queue computeQueue() const;
        vk::Queue transferQueue() const;
        VmaAllocator allocator() const { return m_allocator.get(); }
        vk::CommandPool commandPool() const { return m_commandPool; }
        vk::DescriptorPool descriptorPool() const { return m_descriptorPool; }
        const vk::PhysicalDeviceFeatures& enabledFeatures() const { return m_enabledFeatures; }
        bool isMinLodExtensionEnabled() const { return m_minLodExtensionEnabled; }

        vk::ShaderModule createShaderModule(const std::vector<uint32_t>& spirvCode);
        void destroyShaderModule(vk::ShaderModule module);

        BDARegistry* getBDARegistry() const { return m_bdaRegistry.get(); }

        void enqueueDeletion(std::function<void()>&& deleteFn);
        void processDeletionQueue();

        static void setCheckpoint(vk::CommandBuffer cmd, const char *name);
        void auditBDA(uint64_t address, const char* context) override;
        void reportGpuFault();

        void queueSubmit(vk::Queue queue, const vk::SubmitInfo& submitInfo, vk::Fence fence);

        std::unique_lock<PNKR_MUTEX> acquireQueueLock();

        void trackObject(vk::ObjectType type, uint64_t handle, std::string_view name = {});
        void untrackObject(uint64_t handle);
        bool tryGetObjectTrace(uint64_t handle, TrackedVulkanObject& out) const;

    private:
        std::unique_ptr<VulkanRHIPhysicalDevice> m_physicalDevice;
        vk::Device m_device;
        UniqueVmaAllocator m_allocator;
        std::unique_ptr<BDARegistry> m_bdaRegistry;

        
        std::unique_ptr<VulkanDeletionQueue> m_deletionQueueMgr;

        vk::DebugUtilsMessengerEXT m_debugMessenger;

        std::unique_ptr<BindlessDescriptorManager> m_bindlessManager;
        std::unique_ptr<VulkanGPUTimeQueriesManager> m_gpuProfiler;

        uint32_t m_graphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;
        uint32_t m_computeQueueFamily = VK_QUEUE_FAMILY_IGNORED;
        uint32_t m_transferQueueFamily = VK_QUEUE_FAMILY_IGNORED;

        vk::CommandPool m_commandPool;

        bool m_minLodExtensionEnabled = false;
        vk::PhysicalDeviceFeatures m_enabledFeatures;
        vk::DescriptorPool m_descriptorPool{};
        vk::PipelineCache m_pipelineCache{};
        std::unique_ptr<RHIUploadContext> m_uploadContext;
        std::unique_ptr<VulkanResourceFactory> m_resourceFactory;
        std::unique_ptr<VulkanSyncManager> m_syncManager;

        void savePipelineCache();
    };

}
