#include "pnkr/rhi/vulkan/vulkan_device.hpp"

#include "pnkr/rhi/vulkan/vulkan_buffer.hpp"
#include "pnkr/rhi/vulkan/vulkan_texture.hpp"
#include "pnkr/rhi/vulkan/vulkan_sampler.hpp"
#include "pnkr/rhi/vulkan/vulkan_command_buffer.hpp"
#include "pnkr/rhi/vulkan/vulkan_pipeline.hpp"
#include "pnkr/rhi/vulkan/vulkan_descriptor.hpp"
#include "pnkr/core/logger.hpp"
#include <array>
#include <set>

#include "pnkr/rhi/vulkan/vulkan_utils.hpp"

namespace pnkr::renderer::rhi::vulkan
{
    // VulkanRHIPhysicalDevice Implementation
    VulkanRHIPhysicalDevice::VulkanRHIPhysicalDevice(vk::PhysicalDevice physicalDevice, vk::Instance instance)
        : m_physicalDevice(physicalDevice)
          , m_instance(instance)
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
            m_device.waitIdle();

            if (m_bindlessLayout)
            {
                m_bindlessLayout.reset(); // This triggers the RHI destructor which calls Vulkan destroy
            }
            if (m_bindlessPool)
            {
                m_device.destroyDescriptorPool(m_bindlessPool);
            }

            if (m_timelineSemaphore)
            {
                m_device.destroySemaphore(m_timelineSemaphore);
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

        // Destroy the instance we created in the factory
        if (m_physicalDevice && m_physicalDevice->instance())
        {
            m_physicalDevice->instance().destroy();
        }
    }

    void VulkanRHIDevice::createLogicalDevice(const DeviceDescriptor& desc)
    {
        // 1. Queue Families
        auto queueFamilies = m_physicalDevice->queueFamilies();
        uint32_t graphicsFamily = VK_QUEUE_FAMILY_IGNORED;
        uint32_t computeFamily = VK_QUEUE_FAMILY_IGNORED;

        for (const auto& family : queueFamilies)
        {
            if (family.supportsGraphics && graphicsFamily == VK_QUEUE_FAMILY_IGNORED)
            {
                graphicsFamily = family.familyIndex;
            }
            if (family.supportsCompute && computeFamily == VK_QUEUE_FAMILY_IGNORED)
            {
                computeFamily = family.familyIndex;
            }
        }

        if (graphicsFamily == VK_QUEUE_FAMILY_IGNORED)
        {
            throw std::runtime_error("No graphics queue family found");
        }

        std::set<uint32_t> uniqueQueueFamilies = {graphicsFamily};
        if (computeFamily != VK_QUEUE_FAMILY_IGNORED)
        {
            uniqueQueueFamilies.insert(computeFamily);
        }

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
        // Vulkan 1.2 features (Bindless)
        vk::PhysicalDeviceVulkan12Features features12{};
        features12.runtimeDescriptorArray = VK_TRUE;
        features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        features12.descriptorBindingPartiallyBound = VK_TRUE;
        features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
        features12.bufferDeviceAddress = VK_TRUE;
        features12.descriptorIndexing = VK_TRUE;
        features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        features12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
        // Vulkan 1.3 features (Dynamic Rendering, Sync2)
        vk::PhysicalDeviceVulkan13Features features13{};
        features13.dynamicRendering = VK_TRUE;
        features13.synchronization2 = VK_TRUE;
        features12.timelineSemaphore = VK_TRUE;
        // Chain features: 13 -> 12 -> Core
        features13.pNext = &features12;

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
        m_transferQueueFamily = graphicsFamily;

        core::Logger::info("Logical device created. GraphicsQ={}, ComputeQ={}", m_graphicsQueueFamily,
                           m_computeQueueFamily);
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
        funcs.vkGetPhysicalDeviceMemoryProperties2KHR = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceMemoryProperties2;
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
            throw std::runtime_error("Failed to create VMA allocator: " + vk::to_string(result));
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

        m_timelineSemaphore = m_device.createSemaphore(createInfo);
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
        if (desc.data && desc.memoryUsage == MemoryUsage::GPUOnly) {
            finalDesc.usage |= BufferUsage::TransferDst;
        }

        auto buffer = std::make_unique<VulkanRHIBuffer>(m_device, m_allocator, finalDesc);

        if (desc.data) {
            if (desc.memoryUsage == MemoryUsage::CPUToGPU || desc.memoryUsage == MemoryUsage::CPUOnly) {
                // Direct map and copy
                buffer->uploadData(desc.data, desc.size);
            } else {
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

        return std::make_unique<VulkanRHITexture>(this, desc);
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
        desc.arrayLayers = 6;  // 6 faces for cubemap
        desc.type = TextureType::TextureCube;  // Specify cubemap type

        return std::make_unique<VulkanRHITexture>(this, desc);
    }

    std::unique_ptr<RHISampler> VulkanRHIDevice::createSampler(
        Filter minFilter,
        Filter magFilter,
        SamplerAddressMode addressMode)
    {
        return std::make_unique<VulkanRHISampler>(this, minFilter, magFilter, addressMode);
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
        if (fenceValues.empty()) { return;
}

        std::vector<vk::Semaphore> semaphores(fenceValues.size(), m_timelineSemaphore);

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

        std::vector<vk::Semaphore> waitSems(waitSemaphores.size(), m_timelineSemaphore);
        std::vector<vk::PipelineStageFlags> waitStages(waitSemaphores.size(), vk::PipelineStageFlagBits::eAllCommands);
        std::vector<vk::Semaphore> signalSems(signalSemaphores.size(), m_timelineSemaphore);

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
      // 1. Create Descriptor Set Layout
      std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};

      // Binding 0: Combined Image Samplers (Matches bindless.glsl set=1 binding=0)
      bindings[0].binding = 0;
      bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
      bindings[0].descriptorCount = MAX_BINDLESS_RESOURCES;
      bindings[0].stageFlags = vk::ShaderStageFlagBits::eAll;

      // Binding 1: Storage Buffers
      bindings[1].binding = 1;
      bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
      bindings[1].descriptorCount = MAX_BINDLESS_RESOURCES;
      bindings[1].stageFlags = vk::ShaderStageFlagBits::eAll;

      // Binding 2: Cubemap Samplers
      bindings[2].binding = 2;
      bindings[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
      bindings[2].descriptorCount = MAX_BINDLESS_RESOURCES;
      bindings[2].stageFlags = vk::ShaderStageFlagBits::eAll;

      std::array<vk::DescriptorBindingFlags, 3> bindingFlags{};
      bindingFlags[0] = vk::DescriptorBindingFlagBits::ePartiallyBound |
                        vk::DescriptorBindingFlagBits::eUpdateAfterBind;
      bindingFlags[1] = vk::DescriptorBindingFlagBits::ePartiallyBound |
                        vk::DescriptorBindingFlagBits::eUpdateAfterBind;
      bindingFlags[2] = vk::DescriptorBindingFlagBits::ePartiallyBound |
                        vk::DescriptorBindingFlagBits::eUpdateAfterBind;

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
      layoutDesc.bindings.push_back({.binding=0, .type=DescriptorType::CombinedImageSampler, .count=MAX_BINDLESS_RESOURCES, .stages=ShaderStage::All});
      layoutDesc.bindings.push_back({.binding=1, .type=DescriptorType::StorageBuffer, .count=MAX_BINDLESS_RESOURCES, .stages=ShaderStage::All});
      layoutDesc.bindings.push_back({.binding=2, .type=DescriptorType::CombinedImageSampler, .count=MAX_BINDLESS_RESOURCES, .stages=ShaderStage::All});

      m_bindlessLayout = std::make_unique<VulkanRHIDescriptorSetLayout>(this, vkLayout, layoutDesc);

      // 2. Create Descriptor Pool
      std::array<vk::DescriptorPoolSize, 3> poolSizes{};
      poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
      poolSizes[0].descriptorCount = MAX_BINDLESS_RESOURCES * 2; // *2 for textures + cubemaps
      poolSizes[1].type = vk::DescriptorType::eStorageBuffer;
      poolSizes[1].descriptorCount = MAX_BINDLESS_RESOURCES;
      poolSizes[2].type = vk::DescriptorType::eCombinedImageSampler;
      poolSizes[2].descriptorCount = MAX_BINDLESS_RESOURCES;

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
    }

    BindlessHandle VulkanRHIDevice::registerBindlessTexture(RHITexture* texture,
                                                            RHISampler* sampler)
    {
      auto* vkTex = dynamic_cast<VulkanRHITexture*>(texture);
      auto* vkSamp = dynamic_cast<VulkanRHISampler*>(sampler);

      uint32_t index = m_textureIndexCounter++;

      vk::DescriptorImageInfo imageInfo{};
      imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
      imageInfo.imageView = vkTex->imageView();
      imageInfo.sampler = vkSamp->sampler();

      vk::WriteDescriptorSet write{};
      write.dstSet = m_bindlessSet;
      write.dstBinding = 0; // Binding 0 is Textures
      write.dstArrayElement = index;
      write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
      write.descriptorCount = 1;
      write.pImageInfo = &imageInfo;

      m_device.updateDescriptorSets(write, nullptr);

      return { index };
    }

    BindlessHandle VulkanRHIDevice::registerBindlessCubemap(RHITexture* texture, RHISampler* sampler)
    {
      auto* vkTex = dynamic_cast<VulkanRHITexture*>(texture);
      auto* vkSamp = dynamic_cast<VulkanRHISampler*>(sampler);

      uint32_t index = m_cubemapIndexCounter++;

      vk::DescriptorImageInfo imageInfo{};
      imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
      imageInfo.imageView = vkTex->imageView();
      imageInfo.sampler = vkSamp->sampler();

      vk::WriteDescriptorSet write{};
      write.dstSet = m_bindlessSet;
      write.dstBinding = 2; // Binding 2 is Cubemaps
      write.dstArrayElement = index;
      write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
      write.descriptorCount = 1;
      write.pImageInfo = &imageInfo;

      m_device.updateDescriptorSets(write, nullptr);

      return { index };
    }

    BindlessHandle VulkanRHIDevice::registerBindlessBuffer(RHIBuffer* buffer)
    {
      auto* vkBuf = dynamic_cast<VulkanRHIBuffer*>(buffer);
      uint32_t index = m_bufferIndexCounter++;

      vk::DescriptorBufferInfo bufferInfo{};
      bufferInfo.buffer = vkBuf->buffer();
      bufferInfo.offset = 0;
      bufferInfo.range = VK_WHOLE_SIZE;

      vk::WriteDescriptorSet write{};
      write.dstSet = m_bindlessSet;
      write.dstBinding = 1; // Binding 1 is Buffers
      write.dstArrayElement = index;
      write.descriptorType = vk::DescriptorType::eStorageBuffer;
      write.descriptorCount = 1;
      write.pBufferInfo = &bufferInfo;

      m_device.updateDescriptorSets(write, nullptr);

      return { index };
    }

        void* VulkanRHIDevice::getBindlessDescriptorSetNative()

        {

          return m_bindlessSet;

        }

    

        RHIDescriptorSetLayout* VulkanRHIDevice::getBindlessDescriptorSetLayout()

        {

            return m_bindlessLayout.get();

        }

    } // namespace pnkr::renderer::rhi::vulkan

    