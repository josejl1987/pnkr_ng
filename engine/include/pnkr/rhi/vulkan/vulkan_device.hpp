#pragma once

#include "pnkr/rhi/rhi_device.hpp"
#include "pnkr/rhi/rhi_descriptor.hpp"
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <memory>
#include <vector>

namespace pnkr::platform { class Window; }

namespace pnkr::renderer::rhi::vulkan
{
    class VulkanRHIPhysicalDevice : public RHIPhysicalDevice
    {
    public:
        explicit VulkanRHIPhysicalDevice(vk::PhysicalDevice physicalDevice, vk::Instance instance);
        ~VulkanRHIPhysicalDevice() override = default;

        // RHIPhysicalDevice interface
        const DeviceCapabilities& capabilities() const override { return m_capabilities; }
        std::vector<QueueFamilyInfo> queueFamilies() const override { return m_queueFamilies; }
        bool supportsPresentation(uint32_t queueFamily) const override;

        // Vulkan-specific accessors
        vk::PhysicalDevice physicalDevice() const { return m_physicalDevice; }
        vk::Instance instance() const { return m_instance; }

    private:
        vk::PhysicalDevice m_physicalDevice;
        vk::Instance m_instance;
        DeviceCapabilities m_capabilities;
        std::vector<QueueFamilyInfo> m_queueFamilies;

        void queryCapabilities();
        void queryQueueFamilies();
    };

    class VulkanRHIDevice : public RHIDevice
    {
    public:
        VulkanRHIDevice(std::unique_ptr<VulkanRHIPhysicalDevice> physicalDevice,
                        const DeviceDescriptor& desc);
        ~VulkanRHIDevice() override;

        // Disable copy
        VulkanRHIDevice(const VulkanRHIDevice&) = delete;
        VulkanRHIDevice& operator=(const VulkanRHIDevice&) = delete;

        // RHIDevice interface - Resource creation
        std::unique_ptr<RHIBuffer> createBuffer(
            uint64_t size,
            BufferUsage usage,
            MemoryUsage memoryUsage) override;

        std::unique_ptr<RHITexture> createTexture(
            const Extent3D& extent,
            Format format,
            TextureUsage usage,
            uint32_t mipLevels = 1,
            uint32_t arrayLayers = 1) override;
        std::unique_ptr<RHITexture>
        createCubemap(const Extent3D& extent, Format format, TextureUsage usage, uint32_t mipLevels) override;

        std::unique_ptr<RHISampler> createSampler(
            Filter minFilter,
            Filter magFilter,
            SamplerAddressMode addressMode) override;

        std::unique_ptr<RHICommandBuffer> createCommandBuffer() override;

        std::unique_ptr<RHIPipeline> createGraphicsPipeline(
            const GraphicsPipelineDescriptor& desc) override;

        std::unique_ptr<RHIPipeline> createComputePipeline(
            const ComputePipelineDescriptor& desc) override;

        std::unique_ptr<RHIDescriptorSetLayout> createDescriptorSetLayout(
            const DescriptorSetLayout& desc) override;
        std::unique_ptr<RHIDescriptorSet> allocateDescriptorSet(
            RHIDescriptorSetLayout* layout) override;

        // Synchronization
        void waitIdle() override;
        void waitForFences(const std::vector<uint64_t>& fenceValues) override;

        // Queue submission
        void submitCommands(
            RHICommandBuffer* commandBuffer,
            const std::vector<uint64_t>& waitSemaphores = {},
            const std::vector<uint64_t>& signalSemaphores = {}) override;

        // Device queries
        const RHIPhysicalDevice& physicalDevice() const override { return *m_physicalDevice; }
        uint32_t graphicsQueueFamily() const override { return m_graphicsQueueFamily; }
        uint32_t computeQueueFamily() const override { return m_computeQueueFamily; }
        uint32_t transferQueueFamily() const override { return m_transferQueueFamily; }

        // Bindless Registration
        BindlessHandle registerBindlessTexture(RHITexture* texture, RHISampler* sampler) override;
        BindlessHandle registerBindlessCubemap(RHITexture* texture, RHISampler* sampler) override;
        BindlessHandle registerBindlessBuffer(RHIBuffer* buffer) override;
        // To bind the global set to a command buffer
        void* getBindlessDescriptorSetNative() override;
        RHIDescriptorSetLayout* getBindlessDescriptorSetLayout() override;

        // Vulkan-specific accessors
        vk::Device device() const { return m_device; }
        vk::Instance instance() const { return m_physicalDevice->instance(); }
        vk::PhysicalDevice vkPhysicalDevice() const { return m_physicalDevice->physicalDevice(); }
        vk::Queue graphicsQueue() const { return m_graphicsQueue; }
        vk::Queue computeQueue() const { return m_computeQueue; }
        vk::Queue transferQueue() const { return m_transferQueue; }
        VmaAllocator allocator() const { return m_allocator; }
        vk::CommandPool commandPool() const { return m_commandPool; }

        // Utility methods
        vk::ShaderModule createShaderModule(const std::vector<uint32_t>& spirvCode);
        void destroyShaderModule(vk::ShaderModule module);

    private:
        void initBindless(); // Called during device creation

        vk::DescriptorPool m_bindlessPool;
        vk::DescriptorSet m_bindlessSet;
        std::unique_ptr<RHIDescriptorSetLayout> m_bindlessLayout;
    
        uint32_t m_textureIndexCounter = 0;
        uint32_t m_bufferIndexCounter = 0;
        uint32_t m_cubemapIndexCounter = 0;
    
        static constexpr uint32_t MAX_BINDLESS_RESOURCES = 100000;

        std::unique_ptr<VulkanRHIPhysicalDevice> m_physicalDevice;
        vk::Device m_device;
        VmaAllocator m_allocator;

        // Queue families and queues
        uint32_t m_graphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;
        uint32_t m_computeQueueFamily = VK_QUEUE_FAMILY_IGNORED;
        uint32_t m_transferQueueFamily = VK_QUEUE_FAMILY_IGNORED;
        vk::Queue m_graphicsQueue;
        vk::Queue m_computeQueue;
        vk::Queue m_transferQueue;

        // Command pool
        vk::CommandPool m_commandPool;

        // Validation
        bool m_validationEnabled = false;
        vk::Semaphore m_timelineSemaphore;
        vk::DescriptorPool m_descriptorPool{};

        // Initialization helpers
        void createLogicalDevice(const DeviceDescriptor& desc);
        void selectQueueFamilies();
        void createAllocator();
        void createCommandPool();
        void createTimelineSemaphore();
        void createDescriptorPool();
    };

} // namespace pnkr::renderer::rhi::vulkan
