#include "pnkr/rhi/vulkan/vulkan_device.hpp"

#include "pnkr/rhi/vulkan/vulkan_buffer.hpp"
#include "pnkr/rhi/vulkan/vulkan_texture.hpp"
#include "pnkr/rhi/vulkan/vulkan_sampler.hpp"
#include "pnkr/rhi/vulkan/vulkan_command_buffer.hpp"
#include "pnkr/rhi/vulkan/vulkan_pipeline.hpp"
#include "pnkr/rhi/vulkan/vulkan_descriptor.hpp"
#include "pnkr/core/logger.hpp"
#include <array>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <cpptrace/cpptrace.hpp>

#include "pnkr/rhi/vulkan/vulkan_utils.hpp"

namespace pnkr::renderer::rhi::vulkan
{
    VulkanInstanceContext::~VulkanInstanceContext()
    {
        if (hasDebugMessenger && instance)
        {
            instance.destroyDebugUtilsMessengerEXT(debugMessenger);
        }
        if (instance)
        {
            instance.destroy();
        }
    }

    // VulkanRHIPhysicalDevice Implementation
    VulkanRHIPhysicalDevice::VulkanRHIPhysicalDevice(vk::PhysicalDevice physicalDevice,
                                                     std::shared_ptr<VulkanInstanceContext> instanceContext)
        : m_physicalDevice(physicalDevice)
          , m_instanceContext(std::move(instanceContext))
    {
        queryCapabilities();
        queryQueueFamilies();
    }


    void VulkanRHIPhysicalDevice::queryCapabilities()
    {
        auto props = m_physicalDevice.getProperties();
        auto features = m_physicalDevice.getFeatures();

        m_capabilities.deviceName = std::string(props.deviceName.data());
        m_capabilities.vendorID = props.vendorID;
        m_capabilities.deviceID = props.deviceID;
        m_capabilities.discreteGPU = (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu);

        m_capabilities.maxImageDimension2D = props.limits.maxImageDimension2D;
        m_capabilities.maxImageDimension3D = props.limits.maxImageDimension3D;
        m_capabilities.maxFramebufferWidth = props.limits.maxFramebufferWidth;
        m_capabilities.maxFramebufferHeight = props.limits.maxFramebufferHeight;
        m_capabilities.maxColorAttachments = props.limits.maxColorAttachments;

        m_capabilities.geometryShader = (features.geometryShader != 0u);
        m_capabilities.tessellationShader = (features.tessellationShader != 0u);
        m_capabilities.samplerAnisotropy = (features.samplerAnisotropy != 0u);
        m_capabilities.textureCompressionBC = (features.textureCompressionBC != 0u);

        // Check for bindless support (descriptor indexing)
        vk::PhysicalDeviceDescriptorIndexingFeatures indexingFeatures;
        vk::PhysicalDeviceFeatures2 features2;
        features2.pNext = &indexingFeatures;
        m_physicalDevice.getFeatures2(&features2);
        m_capabilities.bindlessTextures = (indexingFeatures.descriptorBindingPartiallyBound != 0u);

        // Check for ray tracing
        m_capabilities.rayTracing = false;
        m_capabilities.meshShading = false;
    }

    void VulkanRHIPhysicalDevice::queryQueueFamilies()
    {
        auto queueFamilyProps = m_physicalDevice.getQueueFamilyProperties();

        for (uint32_t i = 0; i < queueFamilyProps.size(); ++i)
        {
            QueueFamilyInfo info{};
            info.familyIndex = i;
            info.queueCount = queueFamilyProps[i].queueCount;
            info.supportsGraphics = (queueFamilyProps[i].queueFlags & vk::QueueFlagBits::eGraphics) != vk::QueueFlags{};
            info.supportsCompute = (queueFamilyProps[i].queueFlags & vk::QueueFlagBits::eCompute) != vk::QueueFlags{};
            info.supportsTransfer = (queueFamilyProps[i].queueFlags & vk::QueueFlagBits::eTransfer) != vk::QueueFlags{};

            m_queueFamilies.push_back(info);
        }
    }

    bool VulkanRHIPhysicalDevice::supportsPresentation(uint32_t queueFamily) const
    {
        // TODO: Check presentation support with surface (requires passing surface to physical device)
        return queueFamily < m_queueFamilies.size();
    }

    // VulkanRHIDevice Implementation
    VulkanRHIDevice::VulkanRHIDevice(std::unique_ptr<VulkanRHIPhysicalDevice> physicalDevice,
                                     const DeviceDescriptor& desc)
        : m_physicalDevice(std::move(physicalDevice))
    {
        createLogicalDevice(desc);
        selectQueueFamilies();
        createAllocator();
        createCommandPool();
        createTimelineSemaphore();
        createDescriptorPool();
        initBindless();
        (void)m_validationEnabled;
        core::Logger::info("Vulkan RHI Device created: {}", m_physicalDevice->capabilities().deviceName);
    }

    VulkanRHIDevice::~VulkanRHIDevice()
    {
        if (m_device)
        {
            if (m_uploadContext)
            {
                m_uploadContext->flush();
                m_uploadContext.reset();
            }
            m_device.waitIdle();

            if (m_bindlessSetWrapper)
            {
                m_bindlessSetWrapper.reset();
            }

            if (m_bindlessLayout)
            {
                m_bindlessLayout.reset(); // This triggers the RHI destructor which calls Vulkan destroy
            }
            if (m_bindlessPool)
            {
                m_device.destroyDescriptorPool(m_bindlessPool);
            }

            if (m_frameTimelineSemaphore)
            {
                m_device.destroySemaphore(m_frameTimelineSemaphore);
            }

            if (m_commandPool)
            {
                m_device.destroyCommandPool(m_commandPool);
            }

            if (m_descriptorPool)
            {
                m_device.destroyDescriptorPool(m_descriptorPool);
            }

            if (m_allocator != nullptr)
            {
                vmaDestroyAllocator(m_allocator);
            }

            m_device.destroy();
        }

    }

    void VulkanRHIDevice::createLogicalDevice(const DeviceDescriptor& desc)
    {
        // 1. Queue Families
        auto queueFamilies = m_physicalDevice->queueFamilies();
        uint32_t graphicsFamily = VK_QUEUE_FAMILY_IGNORED;
        uint32_t computeFamily = VK_QUEUE_FAMILY_IGNORED;
        uint32_t transferFamily = VK_QUEUE_FAMILY_IGNORED;

        // First pass: find dedicated queues
        for (const auto& family : queueFamilies)
        {
            if (family.supportsGraphics && graphicsFamily == VK_QUEUE_FAMILY_IGNORED)
            {
                graphicsFamily = family.familyIndex;
            }
            if (family.supportsCompute && !family.supportsGraphics && computeFamily == VK_QUEUE_FAMILY_IGNORED)
            {
                computeFamily = family.familyIndex;
            }
            if (family.supportsTransfer && !family.supportsGraphics && !family.supportsCompute && transferFamily == VK_QUEUE_FAMILY_IGNORED)
            {
                transferFamily = family.familyIndex;
            }
        }

        // Second pass: fill in missing queues with anything that supports them
        for (const auto& family : queueFamilies)
        {
            if (family.supportsCompute && computeFamily == VK_QUEUE_FAMILY_IGNORED)
            {
                computeFamily = family.familyIndex;
            }
            if (family.supportsTransfer && transferFamily == VK_QUEUE_FAMILY_IGNORED)
            {
                transferFamily = family.familyIndex;
            }
        }

        if (graphicsFamily == VK_QUEUE_FAMILY_IGNORED)
        {
            throw cpptrace::runtime_error("No graphics queue family found");
        }

        std::set<uint32_t> uniqueQueueFamilies = {graphicsFamily};
        if (computeFamily != VK_QUEUE_FAMILY_IGNORED) uniqueQueueFamilies.insert(computeFamily);
        if (transferFamily != VK_QUEUE_FAMILY_IGNORED) uniqueQueueFamilies.insert(transferFamily);

        std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
        float queuePriority = 1.0F;

        for (uint32_t queueFamily : uniqueQueueFamilies)
        {
            vk::DeviceQueueCreateInfo queueInfo{};
            queueInfo.queueFamilyIndex = queueFamily;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueInfo);
        }

        // 2. Features
        // Core features
        vk::PhysicalDeviceFeatures featuresCore{};
        featuresCore.samplerAnisotropy = VK_TRUE;
        featuresCore.shaderStorageImageWriteWithoutFormat = VK_TRUE;
        featuresCore.fillModeNonSolid = VK_TRUE;
        featuresCore.geometryShader = VK_TRUE;
        featuresCore.shaderInt64 = VK_TRUE;
        featuresCore.shaderFloat64 = VK_TRUE;
        featuresCore.multiDrawIndirect = VK_TRUE; // Enable MultiDraw
        
        // FIX: Enable Sample Rate Shading for MSAA
        featuresCore.sampleRateShading = VK_TRUE; 

        // Vulkan 1.2 features (core) - Promoted features go here
        vk::PhysicalDeviceVulkan12Features features12{};

        // Original 1.2 features
        features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
        features12.bufferDeviceAddress = VK_TRUE;
        features12.scalarBlockLayout = VK_TRUE;
        features12.shaderFloat16 = VK_TRUE;
        features12.timelineSemaphore = VK_TRUE; // From later in original code

        // Descriptor Indexing (Promoted from EXT_descriptor_indexing)
        features12.descriptorIndexing = VK_TRUE;
        features12.runtimeDescriptorArray = VK_TRUE;
        features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        features12.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
        features12.descriptorBindingPartiallyBound = VK_TRUE;
        features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        features12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
        features12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;

        // Vulkan 1.3 features (Dynamic Rendering, Sync2)
        vk::PhysicalDeviceVulkan13Features features13{};
        features13.dynamicRendering = VK_TRUE;
        features13.synchronization2 = VK_TRUE;
        features13.maintenance4 = VK_TRUE;

        // Vulkan 1.1 features
        vk::PhysicalDeviceVulkan11Features features11{};
        features11.shaderDrawParameters = VK_TRUE;
        // [FIX] Combine 16-bit storage into features11 to avoid struct conflict
        features11.storageBuffer16BitAccess = VK_TRUE;

        // Chain features: 13 -> 12 -> 11 -> Core
        features13.pNext = &features12;
        features12.pNext = &features11;
        features11.pNext = nullptr;

        // 3. Create Info
        vk::DeviceCreateInfo createInfo{};
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &featuresCore;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(desc.requiredExtensions.size());
        createInfo.ppEnabledExtensionNames = desc.requiredExtensions.data();

        createInfo.pNext = &features13;

        m_device = m_physicalDevice->physicalDevice().createDevice(createInfo);

        m_graphicsQueueFamily = graphicsFamily;
        m_computeQueueFamily = computeFamily != VK_QUEUE_FAMILY_IGNORED ? computeFamily : graphicsFamily;
        m_transferQueueFamily = transferFamily != VK_QUEUE_FAMILY_IGNORED ? transferFamily : graphicsFamily;

        core::Logger::info("Logical device created. GraphicsQ={}, ComputeQ={}, TransferQ={}", 
                           m_graphicsQueueFamily, m_computeQueueFamily, m_transferQueueFamily);
    }

    void VulkanRHIDevice::selectQueueFamilies()
    {
        m_graphicsQueue = m_device.getQueue(m_graphicsQueueFamily, 0);
        m_computeQueue = m_device.getQueue(m_computeQueueFamily, 0);
        m_transferQueue = m_device.getQueue(m_transferQueueFamily, 0);
    }

    void VulkanRHIDevice::createAllocator()
    {
        VmaVulkanFunctions funcs{};
        funcs.vkGetPhysicalDeviceProperties = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceProperties;
        funcs.vkGetPhysicalDeviceMemoryProperties = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceMemoryProperties;
        funcs.vkAllocateMemory = VULKAN_HPP_DEFAULT_DISPATCHER.vkAllocateMemory;
        funcs.vkFreeMemory = VULKAN_HPP_DEFAULT_DISPATCHER.vkFreeMemory;
        funcs.vkMapMemory = VULKAN_HPP_DEFAULT_DISPATCHER.vkMapMemory;
        funcs.vkUnmapMemory = VULKAN_HPP_DEFAULT_DISPATCHER.vkUnmapMemory;
        funcs.vkFlushMappedMemoryRanges = VULKAN_HPP_DEFAULT_DISPATCHER.vkFlushMappedMemoryRanges;
        funcs.vkInvalidateMappedMemoryRanges = VULKAN_HPP_DEFAULT_DISPATCHER.vkInvalidateMappedMemoryRanges;
        funcs.vkBindBufferMemory = VULKAN_HPP_DEFAULT_DISPATCHER.vkBindBufferMemory;
        funcs.vkBindImageMemory = VULKAN_HPP_DEFAULT_DISPATCHER.vkBindImageMemory;
        funcs.vkGetBufferMemoryRequirements = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetBufferMemoryRequirements;
        funcs.vkGetImageMemoryRequirements = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetImageMemoryRequirements;
        funcs.vkCreateBuffer = VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateBuffer;
        funcs.vkDestroyBuffer = VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroyBuffer;
        funcs.vkCreateImage = VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateImage;
        funcs.vkDestroyImage = VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroyImage;
        funcs.vkCmdCopyBuffer = VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdCopyBuffer;


        funcs.vkGetBufferMemoryRequirements2KHR = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetBufferMemoryRequirements2;
        funcs.vkGetImageMemoryRequirements2KHR = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetImageMemoryRequirements2;
        funcs.vkBindBufferMemory2KHR = VULKAN_HPP_DEFAULT_DISPATCHER.vkBindBufferMemory2;
        funcs.vkBindImageMemory2KHR = VULKAN_HPP_DEFAULT_DISPATCHER.vkBindImageMemory2;
        funcs.vkGetPhysicalDeviceMemoryProperties2KHR = VULKAN_HPP_DEFAULT_DISPATCHER.
            vkGetPhysicalDeviceMemoryProperties2;
        funcs.vkGetDeviceBufferMemoryRequirements = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceBufferMemoryRequirements;

        VmaAllocatorCreateInfo allocatorInfo{};
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
        allocatorInfo.physicalDevice = m_physicalDevice->physicalDevice();
        allocatorInfo.device = m_device;
        allocatorInfo.instance = m_physicalDevice->instance();
        allocatorInfo.pVulkanFunctions = &funcs;
        allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

        auto result = static_cast<vk::Result>(vmaCreateAllocator(&allocatorInfo, &m_allocator));
        if (result != vk::Result::eSuccess)
        {
            throw cpptrace::runtime_error("Failed to create VMA allocator: " + vk::to_string(result));
        }
    }

    void VulkanRHIDevice::createCommandPool()
    {
        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.queueFamilyIndex = m_graphicsQueueFamily;
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

        m_commandPool = m_device.createCommandPool(poolInfo);
    }

    void VulkanRHIDevice::createTimelineSemaphore()
    {
        vk::SemaphoreTypeCreateInfo typeInfo{};
        typeInfo.semaphoreType = vk::SemaphoreType::eTimeline;
        typeInfo.initialValue = 0;

        vk::SemaphoreCreateInfo createInfo{};
        createInfo.pNext = &typeInfo;

        m_frameTimelineSemaphore = m_device.createSemaphore(createInfo);
    }

    void VulkanRHIDevice::createDescriptorPool()
    {
        std::array<vk::DescriptorPoolSize, 4> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[0].descriptorCount = 1024;
        poolSizes[1].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[1].descriptorCount = 1024;
        poolSizes[2].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[2].descriptorCount = 2048;
        poolSizes[3].type = vk::DescriptorType::eStorageImage;
        poolSizes[3].descriptorCount = 256;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 2048;
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;

        m_descriptorPool = m_device.createDescriptorPool(poolInfo);
    }

    std::unique_ptr<RHIBuffer> VulkanRHIDevice::createBuffer(const BufferDescriptor& desc)
    {
        BufferDescriptor finalDesc = desc;

        // If we have data to upload to a GPU-only buffer, we need TransferDst usage
        if (desc.data && desc.memoryUsage == MemoryUsage::GPUOnly)
        {
            finalDesc.usage |= BufferUsage::TransferDst;
        }

        auto buffer = std::make_unique<VulkanRHIBuffer>(this, finalDesc);

        if (desc.data)
        {
            if (desc.memoryUsage == MemoryUsage::CPUToGPU || desc.memoryUsage == MemoryUsage::CPUOnly)
            {
                // Direct map and copy
                buffer->uploadData(desc.data, desc.size);
            }
            else
            {
                // GPUOnly: Requires staging buffer
                auto staging = createBuffer({
                    .size = desc.size,
                    .usage = BufferUsage::TransferSrc,
                    .memoryUsage = MemoryUsage::CPUToGPU
                });
                staging->uploadData(desc.data, desc.size);

                auto cmd = createCommandBuffer();
                cmd->begin();
                cmd->copyBuffer(staging.get(), buffer.get(), 0, 0, desc.size);
                cmd->end();

                submitCommands(cmd.get());
                waitIdle(); // Ensure data is there before returning the handle
            }
        }

        return buffer;
    }

    std::unique_ptr<RHITexture> VulkanRHIDevice::createTexture(const TextureDescriptor& desc)
    {
        return std::make_unique<VulkanRHITexture>(this, desc);
    }

    std::unique_ptr<RHITexture> VulkanRHIDevice::createTextureView(
        RHITexture* parent,
        const TextureViewDescriptor& desc)
    {
        auto* vkParent = dynamic_cast<VulkanRHITexture*>(parent);
        if (!vkParent) return nullptr;
        return std::make_unique<VulkanRHITexture>(this, vkParent, desc);
    }

    std::unique_ptr<RHITexture> VulkanRHIDevice::createTexture(
        const Extent3D& extent,
        Format format,
        TextureUsage usage,
        uint32_t mipLevels,
        uint32_t arrayLayers)
    {
        TextureDescriptor desc{};
        desc.extent = extent;
        desc.format = format;
        desc.usage = usage;
        desc.mipLevels = mipLevels;
        desc.arrayLayers = arrayLayers;

        return createTexture(desc);
    }

    std::unique_ptr<RHITexture> VulkanRHIDevice::createCubemap(
        const Extent3D& extent,
        Format format,
        TextureUsage usage,
        uint32_t mipLevels)
    {
        TextureDescriptor desc{};
        desc.extent = extent;
        desc.format = format;
        desc.usage = usage;
        desc.mipLevels = mipLevels;
        desc.arrayLayers = 6; // 6 faces for cubemap
        desc.type = TextureType::TextureCube; // Specify cubemap type

        return std::make_unique<VulkanRHITexture>(this, desc);
    }

    std::unique_ptr<RHISampler> VulkanRHIDevice::createSampler(
        Filter minFilter,
        Filter magFilter,
        SamplerAddressMode addressMode,
        CompareOp compareOp)
    {
        return std::make_unique<VulkanRHISampler>(this, minFilter, magFilter, addressMode, compareOp);
    }

    std::unique_ptr<RHICommandBuffer> VulkanRHIDevice::createCommandBuffer()
    {
        return std::make_unique<VulkanRHICommandBuffer>(this);
    }

    std::unique_ptr<RHIPipeline> VulkanRHIDevice::createGraphicsPipeline(
        const GraphicsPipelineDescriptor& desc)
    {
        return std::make_unique<VulkanRHIPipeline>(this, desc);
    }

    std::unique_ptr<RHIPipeline> VulkanRHIDevice::createComputePipeline(
        const ComputePipelineDescriptor& desc)
    {
        return std::make_unique<VulkanRHIPipeline>(this, desc);
    }

    std::unique_ptr<RHIDescriptorSetLayout> VulkanRHIDevice::createDescriptorSetLayout(
        const DescriptorSetLayout& desc)
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings;
        bindings.reserve(desc.bindings.size());

        for (const auto& binding : desc.bindings)
        {
            vk::DescriptorSetLayoutBinding vkBinding{};
            vkBinding.binding = binding.binding;
            vkBinding.descriptorType = VulkanUtils::toVkDescriptorType(binding.type);
            vkBinding.descriptorCount = binding.count;
            vkBinding.stageFlags = VulkanUtils::toVkShaderStage(binding.stages);
            bindings.push_back(vkBinding);
        }

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        vk::DescriptorSetLayout layout = m_device.createDescriptorSetLayout(layoutInfo);
        return std::make_unique<VulkanRHIDescriptorSetLayout>(this, layout, desc);
    }

    std::unique_ptr<RHIDescriptorSet> VulkanRHIDevice::allocateDescriptorSet(
        RHIDescriptorSetLayout* layout)
    {
        auto* vkLayout = dynamic_cast<VulkanRHIDescriptorSetLayout*>(layout);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = m_descriptorPool;
        vk::DescriptorSetLayout layoutHandle = vkLayout->layout();
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layoutHandle;

        auto sets = m_device.allocateDescriptorSets(allocInfo);
        return std::make_unique<VulkanRHIDescriptorSet>(this, vkLayout, sets[0]);
    }

    void VulkanRHIDevice::waitIdle()
    {
        m_device.waitIdle();
    }

    void VulkanRHIDevice::waitForFences(const std::vector<uint64_t>& fenceValues)
    {
        if (fenceValues.empty())
        {
            return;
        }

        std::vector<vk::Semaphore> semaphores(fenceValues.size(), m_frameTimelineSemaphore);

        vk::SemaphoreWaitInfo waitInfo{};
        waitInfo.semaphoreCount = static_cast<uint32_t>(fenceValues.size());
        waitInfo.pSemaphores = semaphores.data();
        waitInfo.pValues = fenceValues.data();

        auto result = m_device.waitSemaphores(waitInfo, UINT64_MAX);
        if (result != vk::Result::eSuccess)
        {
            core::Logger::error("Failed to wait for fences: {}", vk::to_string(result));
        }
    }

    void VulkanRHIDevice::submitCommands(
        RHICommandBuffer* commandBuffer,
        const std::vector<uint64_t>& waitSemaphores,
        const std::vector<uint64_t>& signalSemaphores)
    {
        auto* vkCmdBuffer = dynamic_cast<VulkanRHICommandBuffer*>(commandBuffer);

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        auto cmdBuf = vkCmdBuffer->commandBuffer();
        submitInfo.pCommandBuffers = &cmdBuf;

        // Timeline Semaphore Synchronization
        vk::TimelineSemaphoreSubmitInfo timelineInfo{};

        std::vector<vk::Semaphore> waitSems(waitSemaphores.size(), m_frameTimelineSemaphore);
        std::vector<vk::PipelineStageFlags> waitStages(waitSemaphores.size(), vk::PipelineStageFlagBits::eAllCommands);
        std::vector<vk::Semaphore> signalSems(signalSemaphores.size(), m_frameTimelineSemaphore);

        if (!waitSemaphores.empty())
        {
            timelineInfo.waitSemaphoreValueCount = static_cast<uint32_t>(waitSemaphores.size());
            timelineInfo.pWaitSemaphoreValues = waitSemaphores.data();

            submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSems.size());
            submitInfo.pWaitSemaphores = waitSems.data();
            submitInfo.pWaitDstStageMask = waitStages.data();
        }

        if (!signalSemaphores.empty())
        {
            timelineInfo.signalSemaphoreValueCount = static_cast<uint32_t>(signalSemaphores.size());
            timelineInfo.pSignalSemaphoreValues = signalSemaphores.data();

            submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSems.size());
            submitInfo.pSignalSemaphores = signalSems.data();
        }

        submitInfo.pNext = &timelineInfo;

        m_graphicsQueue.submit(submitInfo, nullptr);
    }

    void VulkanRHIDevice::waitForTimelineValue(uint64_t value)
    {
        if (value == 0)
        {
            return;
        }

        vk::SemaphoreWaitInfo waitInfo{};
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &m_frameTimelineSemaphore;
        waitInfo.pValues = &value;

        // NOTE:
        // Timing out here and continuing will cause reuse of per-frame semaphores/command buffers
        // while still pending, triggering validation errors and eventually DEVICE_LOST.
        const auto result = m_device.waitSemaphores(waitInfo, UINT64_MAX);
        if (result != vk::Result::eSuccess)
        {
            core::Logger::critical("Failed to wait for timeline value {}: {}", value, vk::to_string(result));
            throw std::runtime_error("VulkanRHIDevice::waitForTimelineValue failed");
        }
    }

    void VulkanRHIDevice::submitCommands(
        RHICommandBuffer* commandBuffer,
        const std::vector<vk::Semaphore>& waitSemaphores,
        const std::vector<vk::PipelineStageFlags>& waitStages,
        const std::vector<vk::Semaphore>& signalSemaphores,
        uint64_t signalTimelineValue)
    {
        auto* vkCmdBuffer = dynamic_cast<VulkanRHICommandBuffer*>(commandBuffer);

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        auto cmdBuf = vkCmdBuffer->commandBuffer();
        submitInfo.pCommandBuffers = &cmdBuf;

        std::vector<vk::PipelineStageFlags> stages = waitStages;
        if (!waitSemaphores.empty() && stages.size() != waitSemaphores.size())
        {
            stages.assign(waitSemaphores.size(), vk::PipelineStageFlagBits::eAllCommands);
        }

        if (!waitSemaphores.empty())
        {
            submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
            submitInfo.pWaitSemaphores = waitSemaphores.data();
            submitInfo.pWaitDstStageMask = stages.data();
        }

        std::vector<vk::Semaphore> allSignalSemaphores = signalSemaphores;
        allSignalSemaphores.push_back(m_frameTimelineSemaphore);

        std::vector<uint64_t> signalValues(signalSemaphores.size(), 0);
        signalValues.push_back(signalTimelineValue);

        std::vector<uint64_t> waitValues(waitSemaphores.size(), 0);

        vk::TimelineSemaphoreSubmitInfo timelineInfo{};
        timelineInfo.waitSemaphoreValueCount = static_cast<uint32_t>(waitValues.size());
        timelineInfo.pWaitSemaphoreValues = waitValues.data();
        timelineInfo.signalSemaphoreValueCount = static_cast<uint32_t>(signalValues.size());
        timelineInfo.pSignalSemaphoreValues = signalValues.data();

        submitInfo.signalSemaphoreCount = static_cast<uint32_t>(allSignalSemaphores.size());
        submitInfo.pSignalSemaphores = allSignalSemaphores.data();
        submitInfo.pNext = &timelineInfo;

        m_graphicsQueue.submit(submitInfo, nullptr);
    }


    void VulkanRHIDevice::immediateSubmit(std::function<void(RHICommandBuffer*)>&& func)
    {
        auto cmd = createCommandBuffer();
        cmd->begin();
        func(cmd.get());
        cmd->end();
        submitCommands(cmd.get());
        waitIdle();
    }

    void VulkanRHIDevice::downloadTexture(
        RHITexture* texture,
        void* outData,
        uint64_t dataSize,
        const TextureSubresource& subresource)
    {
        auto* vkTex = dynamic_cast<VulkanRHITexture*>(texture);

        // 1. Create staging buffer
        auto stagingBuffer = createBuffer({
            .size = dataSize,
            .usage = BufferUsage::TransferDst,
            .memoryUsage = MemoryUsage::GPUToCPU,
            .debugName = "TextureDownloadStaging"
        });

        // 2. Immediate submit copy command
        immediateSubmit([&](RHICommandBuffer* cmd)
        {
            auto* vkCmd = dynamic_cast<VulkanRHICommandBuffer*>(cmd);

            // Transition image to transfer src
            vkTex->transitionLayout(vk::ImageLayout::eTransferSrcOptimal, vkCmd->commandBuffer());

            // Copy image to buffer
            vk::BufferImageCopy copyRegion{};
            copyRegion.bufferOffset = 0;
            copyRegion.bufferRowLength = 0;
            copyRegion.bufferImageHeight = 0;

            vk::Format fmt = VulkanUtils::toVkFormat(vkTex->format());
            if (fmt == vk::Format::eD16Unorm || fmt == vk::Format::eD32Sfloat ||
                fmt == vk::Format::eD24UnormS8Uint || fmt == vk::Format::eD32SfloatS8Uint)
            {
                copyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eDepth;
            }
            else
            {
                copyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            }

            copyRegion.imageSubresource.mipLevel = subresource.mipLevel;
            copyRegion.imageSubresource.baseArrayLayer = subresource.arrayLayer;
            copyRegion.imageSubresource.layerCount = 1;

            copyRegion.imageOffset = vk::Offset3D{0, 0, 0};
            copyRegion.imageExtent = VulkanUtils::toVkExtent3D(vkTex->extent());

            // Adjust extent for mip level
            copyRegion.imageExtent.width = std::max(1u, copyRegion.imageExtent.width >> subresource.mipLevel);
            copyRegion.imageExtent.height = std::max(1u, copyRegion.imageExtent.height >> subresource.mipLevel);

            vkCmd->commandBuffer().copyImageToBuffer(
                vkTex->image(),
                vk::ImageLayout::eTransferSrcOptimal,
                static_cast<VulkanRHIBuffer*>(stagingBuffer.get())->buffer(),
                copyRegion
            );

            // Transition back to shader read (or whatever it was)
            vkTex->transitionLayout(vk::ImageLayout::eShaderReadOnlyOptimal, vkCmd->commandBuffer());
        });

        // 3. Map and copy data to output
        void* mappedData = stagingBuffer->map();
        std::memcpy(outData, mappedData, dataSize);
        stagingBuffer->unmap();
    }

    vk::ShaderModule VulkanRHIDevice::createShaderModule(const std::vector<uint32_t>& spirvCode)
    {
        vk::ShaderModuleCreateInfo createInfo{};
        createInfo.codeSize = spirvCode.size() * sizeof(uint32_t);
        createInfo.pCode = spirvCode.data();

        return m_device.createShaderModule(createInfo);
    }

    void VulkanRHIDevice::destroyShaderModule(vk::ShaderModule module)
    {
        m_device.destroyShaderModule(module);
    }

    void VulkanRHIDevice::initBindless()
    {
        // Query hardware limits for bindless resources
        auto props = m_physicalDevice->physicalDevice().getProperties();
        uint32_t maxSampledImages = std::min(MAX_BINDLESS_RESOURCES, props.limits.maxPerStageDescriptorSampledImages);
        uint32_t maxStorageImages = std::min(MAX_BINDLESS_RESOURCES, props.limits.maxPerStageDescriptorStorageImages);
        uint32_t maxStorageBuffers = std::min(MAX_BINDLESS_RESOURCES, props.limits.maxPerStageDescriptorStorageBuffers);
        uint32_t maxSamplers = std::min(MAX_SAMPLERS, props.limits.maxPerStageDescriptorSamplers);

        m_textureManager.init(maxSampledImages);
        m_samplerManager.init(maxSamplers);
        m_shadowTextureManager.init(maxSampledImages);
        m_shadowSamplerManager.init(maxSamplers);
        m_bufferManager.init(maxStorageBuffers);
        m_cubemapManager.init(maxSampledImages);
        m_storageImageManager.init(maxStorageImages);

        // 1. Create Descriptor Set Layout
        std::array<vk::DescriptorSetLayoutBinding, 8> bindings{};

        // Binding 0: Sampled 2D Images (Matches bindless.glsl set=1 binding=0)
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eSampledImage;
        bindings[0].descriptorCount = maxSampledImages;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eAll;

        // Binding 1: Samplers
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eSampler;
        bindings[1].descriptorCount = maxSamplers;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eAll;

        // Binding 2: Sampled Cubemap Images
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eSampledImage;
        bindings[2].descriptorCount = maxSampledImages;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eAll;

        // Binding 3: Storage Buffers
        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[3].descriptorCount = maxStorageBuffers;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eAll;

        // Binding 4: Storage Images
        bindings[4].binding = 4;
        bindings[4].descriptorType = vk::DescriptorType::eStorageImage;
        bindings[4].descriptorCount = maxStorageImages;
        bindings[4].stageFlags = vk::ShaderStageFlagBits::eAll;


        // Binding 5: 3D textures
        bindings[5].binding = 5;
        bindings[5].descriptorType = vk::DescriptorType::eSampledImage;
        bindings[5].descriptorCount = maxSampledImages;
        bindings[5].stageFlags = vk::ShaderStageFlagBits::eAll;


        // Binding 6: Shadow samplers
        bindings[6].binding = 6;
        bindings[6].descriptorType = vk::DescriptorType::eSampler;
        bindings[6].descriptorCount = maxSamplers;
        bindings[6].stageFlags = vk::ShaderStageFlagBits::eAll;

        // Binding 7: Shadow textures

        bindings[7].binding = 7;
        bindings[7].descriptorType = vk::DescriptorType::eSampledImage;
        bindings[7].descriptorCount = maxSampledImages;
        bindings[7].stageFlags = vk::ShaderStageFlagBits::eAll;

        std::array<vk::DescriptorBindingFlags, 8> bindingFlags{};
        for (auto& binding : bindingFlags)
        {
            binding = vk::DescriptorBindingFlagBits::ePartiallyBound |
                vk::DescriptorBindingFlagBits::eUpdateAfterBind;
        }

        vk::DescriptorSetLayoutBindingFlagsCreateInfo extendedInfo{};
        extendedInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
        extendedInfo.pBindingFlags = bindingFlags.data();

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.pNext = &extendedInfo;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        layoutInfo.flags =
            vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;

        vk::DescriptorSetLayout vkLayout = m_device.createDescriptorSetLayout(layoutInfo);

        DescriptorSetLayout layoutDesc;
        layoutDesc.bindings.push_back({
            .binding = 0, .type = DescriptorType::SampledImage, .count = maxSampledImages,
            .stages = ShaderStage::All
        });
        layoutDesc.bindings.push_back({
            .binding = 1, .type = DescriptorType::Sampler, .count = maxSamplers, .stages = ShaderStage::All
        });
        layoutDesc.bindings.push_back({
            .binding = 2, .type = DescriptorType::SampledImage, .count = maxSampledImages,
            .stages = ShaderStage::All
        });
        layoutDesc.bindings.push_back({
            .binding = 3, .type = DescriptorType::StorageBuffer, .count = maxStorageBuffers,
            .stages = ShaderStage::All
        });
        layoutDesc.bindings.push_back({
            .binding = 4, .type = DescriptorType::StorageImage, .count = maxStorageImages,
            .stages = ShaderStage::All
        });
        layoutDesc.bindings.push_back({
            .binding = 5, .type = DescriptorType::SampledImage, .count = maxSampledImages,
            .stages = ShaderStage::All
        });
        layoutDesc.bindings.push_back({
            .binding = 6, .type = DescriptorType::Sampler, .count = maxSamplers,
            .stages = ShaderStage::All
        });
        layoutDesc.bindings.push_back({
            .binding = 7, .type = DescriptorType::SampledImage, .count = maxSampledImages,
            .stages = ShaderStage::All
        });

        m_bindlessLayout = std::make_unique<VulkanRHIDescriptorSetLayout>(this, vkLayout, layoutDesc);

        // 2. Create Descriptor Pool
        std::array<vk::DescriptorPoolSize, 4> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eSampledImage;
        poolSizes[0].descriptorCount = maxSampledImages * 5; 
        poolSizes[1].type = vk::DescriptorType::eSampler;
        poolSizes[1].descriptorCount = maxSamplers * 2; 
        poolSizes[2].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[2].descriptorCount = maxStorageBuffers;
        poolSizes[3].type = vk::DescriptorType::eStorageImage;
        poolSizes[3].descriptorCount = maxStorageImages;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
        poolInfo.maxSets = 1; // Only one global bindless set
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        m_bindlessPool = m_device.createDescriptorPool(poolInfo);

        // 3. Allocate Descriptor Set
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = m_bindlessPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &vkLayout;

        m_bindlessSet = m_device.allocateDescriptorSets(allocInfo)[0];

        m_bindlessSetWrapper = std::make_unique<VulkanRHIDescriptorSet>(
            this,
            static_cast<VulkanRHIDescriptorSetLayout*>(m_bindlessLayout.get()),
            m_bindlessSet
        );
    }

    BindlessHandle VulkanRHIDevice::registerBindlessTexture(RHITexture* texture,
                                                            RHISampler* sampler)
    {
        auto imageHandle = registerBindlessTexture2D(texture);
        if (sampler != nullptr && imageHandle.isValid())
        {
            auto* vkSamp = dynamic_cast<VulkanRHISampler*>(sampler);
            vk::DescriptorImageInfo imageInfo{};
            imageInfo.sampler = vkSamp->sampler();

            vk::WriteDescriptorSet write{};
            write.dstSet = m_bindlessSet;
            write.dstBinding = 1;
            write.dstArrayElement = imageHandle.index;
            write.descriptorType = vk::DescriptorType::eSampler;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfo;

            m_device.updateDescriptorSets(write, nullptr);
        }

        return imageHandle;
    }

    BindlessHandle VulkanRHIDevice::registerBindlessCubemap(RHITexture* texture, RHISampler* sampler)
    {
        auto imageHandle = registerBindlessCubemapImage(texture);
        if (sampler != nullptr && imageHandle.isValid())
        {
            auto* vkSamp = dynamic_cast<VulkanRHISampler*>(sampler);
            vk::DescriptorImageInfo imageInfo{};
            imageInfo.sampler = vkSamp->sampler();

            vk::WriteDescriptorSet write{};
            write.dstSet = m_bindlessSet;
            write.dstBinding = 1;
            write.dstArrayElement = imageHandle.index;
            write.descriptorType = vk::DescriptorType::eSampler;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfo;

            m_device.updateDescriptorSets(write, nullptr);
        }

        return imageHandle;
    }

    BindlessHandle VulkanRHIDevice::registerBindlessTexture2D(RHITexture* texture)
    {
        auto* vkTex = dynamic_cast<VulkanRHITexture*>(texture);
        uint32_t index = m_textureManager.allocate();
        if (index == 0xFFFFFFFF) return {index};

        vk::DescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfo.imageView = vkTex->imageView();

        vk::WriteDescriptorSet write{};
        write.dstSet = m_bindlessSet;
        write.dstBinding = 0;
        write.dstArrayElement = index;
        write.descriptorType = vk::DescriptorType::eSampledImage;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        m_device.updateDescriptorSets(write, nullptr);

        return {index};
    }

    BindlessHandle VulkanRHIDevice::registerBindlessCubemapImage(RHITexture* texture)
    {
        auto* vkTex = dynamic_cast<VulkanRHITexture*>(texture);
        uint32_t index = m_cubemapManager.allocate();
        if (index == 0xFFFFFFFF) return {index};

        vk::DescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfo.imageView = vkTex->imageView();

        vk::WriteDescriptorSet write{};
        write.dstSet = m_bindlessSet;
        write.dstBinding = 2;
        write.dstArrayElement = index;
        write.descriptorType = vk::DescriptorType::eSampledImage;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        m_device.updateDescriptorSets(write, nullptr);

        return {index};
    }

    BindlessHandle VulkanRHIDevice::registerBindlessSampler(RHISampler* sampler)
    {
        auto* vkSamp = dynamic_cast<VulkanRHISampler*>(sampler);
        uint32_t index = m_samplerManager.allocate();
        if (index == 0xFFFFFFFF) return {index};

        vk::DescriptorImageInfo imageInfo{};
        imageInfo.sampler = vkSamp->sampler();

        vk::WriteDescriptorSet write{};
        write.dstSet = m_bindlessSet;
        write.dstBinding = 1;
        write.dstArrayElement = index;
        write.descriptorType = vk::DescriptorType::eSampler;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        m_device.updateDescriptorSets(write, nullptr);

        return {index};
    }

    BindlessHandle VulkanRHIDevice::registerBindlessShadowSampler(RHISampler* sampler)
    {
        auto* vkSamp = dynamic_cast<VulkanRHISampler*>(sampler);
        uint32_t index = m_shadowSamplerManager.allocate();
        if (index == 0xFFFFFFFF) return {index};

        vk::DescriptorImageInfo imageInfo{};
        imageInfo.sampler = vkSamp->sampler();

        vk::WriteDescriptorSet write{};
        write.dstSet = m_bindlessSet;
        write.dstBinding = 6;
        write.dstArrayElement = index;
        write.descriptorType = vk::DescriptorType::eSampler;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        m_device.updateDescriptorSets(write, nullptr);

        return {index};
    }

    BindlessHandle VulkanRHIDevice::registerBindlessBuffer(RHIBuffer* buffer)
    {
        auto* vkBuf = dynamic_cast<VulkanRHIBuffer*>(buffer);
        uint32_t index = m_bufferManager.allocate();
        if (index == 0xFFFFFFFF) return {index};

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = vkBuf->buffer();
        bufferInfo.offset = 0;
        bufferInfo.range = VK_WHOLE_SIZE;

        vk::WriteDescriptorSet write{};
        write.dstSet = m_bindlessSet;
        write.dstBinding = 3; // Binding 3 is Buffers
        write.dstArrayElement = index;
        write.descriptorType = vk::DescriptorType::eStorageBuffer;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;

        m_device.updateDescriptorSets(write, nullptr);

        return {index};
    }

    BindlessHandle VulkanRHIDevice::registerBindlessStorageImage(RHITexture* texture)
    {
        auto* vkTex = dynamic_cast<VulkanRHITexture*>(texture);
        uint32_t index = m_storageImageManager.allocate();
        if (index == 0xFFFFFFFF) return {index};

        vk::DescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = vk::ImageLayout::eGeneral;
        imageInfo.imageView = vkTex->imageView();
        imageInfo.sampler = nullptr;

        vk::WriteDescriptorSet write{};
        write.dstSet = m_bindlessSet;
        write.dstBinding = 4; // Binding 4 is Storage Images
        write.dstArrayElement = index;
        write.descriptorType = vk::DescriptorType::eStorageImage;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        m_device.updateDescriptorSets(write, nullptr);

        return {index};
    }

    BindlessHandle VulkanRHIDevice::registerBindlessShadowTexture2D(RHITexture* texture)
    {
        auto* vkTex = dynamic_cast<VulkanRHITexture*>(texture);
        uint32_t index = m_shadowTextureManager.allocate();
        if (index == 0xFFFFFFFF) return {index};

        vk::DescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfo.imageView = vkTex->imageView();

        vk::WriteDescriptorSet write{};
        write.dstSet = m_bindlessSet;
        write.dstBinding = 7;
        write.dstArrayElement = index;
        write.descriptorType = vk::DescriptorType::eSampledImage;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        m_device.updateDescriptorSets(write, nullptr);

        return {index};
    }

    void VulkanRHIDevice::releaseBindlessTexture(BindlessHandle handle) {
        m_textureManager.free(handle.index);
    }
    void VulkanRHIDevice::releaseBindlessCubemap(BindlessHandle handle) {
        m_cubemapManager.free(handle.index);
    }
    void VulkanRHIDevice::releaseBindlessSampler(BindlessHandle handle) {
        m_samplerManager.free(handle.index);
    }
    void VulkanRHIDevice::releaseBindlessShadowSampler(BindlessHandle handle) {
        m_shadowSamplerManager.free(handle.index);
    }
    void VulkanRHIDevice::releaseBindlessStorageImage(BindlessHandle handle) {
        m_storageImageManager.free(handle.index);
    }
    void VulkanRHIDevice::releaseBindlessBuffer(BindlessHandle handle) {
        m_bufferManager.free(handle.index);
    }
    void VulkanRHIDevice::releaseBindlessShadowTexture2D(BindlessHandle handle) {
        m_shadowTextureManager.free(handle.index);
    }

    class VulkanRHIUploadContext : public RHIUploadContext
    {
    public:
        VulkanRHIUploadContext(VulkanRHIDevice* device, uint64_t stagingSize)
            : m_device(device), m_stagingSize(stagingSize)
        {
            m_stagingBuffer = m_device->createBuffer({
                .size = m_stagingSize,
                .usage = BufferUsage::TransferSrc,
                .memoryUsage = MemoryUsage::CPUToGPU,
                .debugName = "UploadStagingBuffer"
            });
            m_mappedPtr = m_stagingBuffer->map();
            
            vk::CommandPoolCreateInfo poolInfo{};
            poolInfo.queueFamilyIndex = m_device->graphicsQueueFamily();
            poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
            m_pool = m_device->device().createCommandPool(poolInfo);

            vk::CommandBufferAllocateInfo allocInfo{};
            allocInfo.commandPool = m_pool;
            allocInfo.level = vk::CommandBufferLevel::ePrimary;
            allocInfo.commandBufferCount = 1;
            m_cmd = m_device->device().allocateCommandBuffers(allocInfo)[0];

            vk::FenceCreateInfo fenceInfo{};
            m_fence = m_device->device().createFence(fenceInfo);

            m_cmd.begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        }

        ~VulkanRHIUploadContext() override
        {
            m_device->device().destroyFence(m_fence);
            m_device->device().destroyCommandPool(m_pool);
        }

        void uploadTexture(RHITexture* texture, const void* data, uint64_t size, const TextureSubresource& subresource) override
        {
            if (m_currentOffset + size > m_stagingSize) {
                flush();
            }

            std::memcpy(static_cast<uint8_t*>(m_mappedPtr) + m_currentOffset, data, size);

            auto* vkTex = static_cast<VulkanRHITexture*>(texture);
            
            // Layout transition to TransferDst
            vkTex->transitionLayout(vk::ImageLayout::eTransferDstOptimal, m_cmd);

            vk::BufferImageCopy region{};
            region.bufferOffset = m_currentOffset;
            region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            region.imageSubresource.mipLevel = subresource.mipLevel;
            region.imageSubresource.baseArrayLayer = subresource.arrayLayer;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = VulkanUtils::toVkExtent3D(vkTex->extent());
            region.imageExtent.width = std::max(1u, region.imageExtent.width >> subresource.mipLevel);
            region.imageExtent.height = std::max(1u, region.imageExtent.height >> subresource.mipLevel);
            region.imageExtent.depth = std::max(1u, region.imageExtent.depth >> subresource.mipLevel);

            m_cmd.copyBufferToImage(
                static_cast<VulkanRHIBuffer*>(m_stagingBuffer.get())->buffer(),
                vkTex->image(),
                vk::ImageLayout::eTransferDstOptimal,
                1, &region
            );

            // Transition to ShaderRead
            vkTex->transitionLayout(vk::ImageLayout::eShaderReadOnlyOptimal, m_cmd);

            m_currentOffset = (m_currentOffset + size + 15) & ~15; // 16-byte alignment
        }

        void uploadBuffer(RHIBuffer* buffer, const void* data, uint64_t size, uint64_t offset) override
        {
            if (m_currentOffset + size > m_stagingSize) {
                flush();
            }

            std::memcpy(static_cast<uint8_t*>(m_mappedPtr) + m_currentOffset, data, size);

            auto* vkBuf = static_cast<VulkanRHIBuffer*>(buffer);
            vk::BufferCopy region{};
            region.srcOffset = m_currentOffset;
            region.dstOffset = offset;
            region.size = size;

            m_cmd.copyBuffer(
                static_cast<VulkanRHIBuffer*>(m_stagingBuffer.get())->buffer(),
                vkBuf->buffer(),
                1, &region
            );

            m_currentOffset = (m_currentOffset + size + 15) & ~15; // 16-byte alignment
        }

        void flush() override
        {
            if (m_currentOffset == 0) return;

            m_cmd.end();

            vk::SubmitInfo submitInfo{};
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &m_cmd;

            m_device->graphicsQueue().submit(submitInfo, m_fence);
            
            auto result = m_device->device().waitForFences(1, &m_fence, VK_TRUE, UINT64_MAX);
            if (result != vk::Result::eSuccess) {
                core::Logger::error("Failed to wait for upload fence");
            }
            
            if (m_device->device().resetFences(1, &m_fence) != vk::Result::eSuccess) {
                core::Logger::error("Failed to reset upload fence");
            }

            m_currentOffset = 0;
            m_cmd.reset();
            m_cmd.begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        }

    private:
        VulkanRHIDevice* m_device;
        uint64_t m_stagingSize;
        uint64_t m_currentOffset = 0;
        std::unique_ptr<RHIBuffer> m_stagingBuffer;
        void* m_mappedPtr = nullptr;
        vk::CommandPool m_pool;
        vk::CommandBuffer m_cmd;
        vk::Fence m_fence;
    };

    std::unique_ptr<RHIUploadContext> VulkanRHIDevice::createUploadContext(uint64_t stagingBufferSize)
    {
        return std::make_unique<VulkanRHIUploadContext>(this, stagingBufferSize);
    }

    RHIUploadContext* VulkanRHIDevice::uploadContext()
    {
        if (!m_uploadContext)
        {
            m_uploadContext = createUploadContext();
        }
        return m_uploadContext.get();
    }

    RHIDescriptorSet* VulkanRHIDevice::getBindlessDescriptorSet()
    {
        return m_bindlessSetWrapper.get();
    }

    RHIDescriptorSetLayout* VulkanRHIDevice::getBindlessDescriptorSetLayout()

    {
        return m_bindlessLayout.get();
    }
} // namespace pnkr::renderer::rhi::vulkan
