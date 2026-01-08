#include "rhi/vulkan/vulkan_device.hpp"
#include "rhi/vulkan/vulkan_swapchain.hpp"
#include "rhi/vulkan/BindlessDescriptorManager.hpp"
#include "rhi/vulkan/BDARegistry.hpp"
#include "rhi/vulkan/vulkan_gpu_profiler.hpp"
#include "rhi/vulkan/vulkan_buffer.hpp"

#include "pnkr/core/logger.hpp"
#include "pnkr/core/profiler.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/rhi/rhi_factory.hpp"
#include "rhi/vulkan/vulkan_command_buffer.hpp"
#include "rhi/vulkan/vulkan_descriptor.hpp"
#include "rhi/vulkan/vulkan_imgui.hpp"
#include "rhi/vulkan/vulkan_pipeline.hpp"
#include "rhi/vulkan/vulkan_sampler.hpp"
#include "rhi/vulkan/vulkan_texture.hpp"
#include "rhi/vulkan/vulkan_utils.hpp"
#include "vulkan_cast.hpp"
#include "vulkan_sync.hpp"
#include <algorithm>
#include <array>
#include <cpptrace/cpptrace.hpp>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <utility>

#ifdef PNKR_AFTERMATH_ENABLED
#include "rhi/vulkan/AftermathIntegration.hpp"
#endif

namespace pnkr::renderer::rhi::vulkan
{
    void VmaAllocatorDeleter::operator()(VmaAllocator allocator) const
    {
      if (allocator != nullptr) {
        vmaDestroyAllocator(allocator);
      }
    }

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

        m_capabilities.geometryShader = (features.geometryShader != 0U);
        m_capabilities.tessellationShader = (features.tessellationShader != 0U);
        m_capabilities.samplerAnisotropy = (features.samplerAnisotropy != 0U);
        m_capabilities.textureCompressionBC =
            (features.textureCompressionBC != 0U);
        m_capabilities.pipelineStatisticsQuery =
            (features.pipelineStatisticsQuery != 0U);

        vk::PhysicalDeviceVulkan12Features indexingFeatures{};
        vk::PhysicalDeviceFeatures2 features2{};
        features2.pNext = &indexingFeatures;
        m_physicalDevice.getFeatures2(&features2);

        m_capabilities.bindlessTextures =
            (indexingFeatures.descriptorBindingPartiallyBound != 0U);
        m_capabilities.drawIndirectCount =
            (indexingFeatures.drawIndirectCount != 0U);
        m_capabilities.rayTracing = false;
        m_capabilities.meshShading = false;

        // Strict Hardware Capability Check for Bindless Features
        {
            auto checkFeature = [](vk::Bool32 feature, const char* name) {
                if (!feature) {
                    core::Logger::RHI.error("Required Vulkan feature missing: {}", name);
                    return false;
                }
                return true;
            };

            bool supported = true;
            supported &= checkFeature(indexingFeatures.descriptorBindingPartiallyBound, "descriptorBindingPartiallyBound");
            supported &= checkFeature(indexingFeatures.runtimeDescriptorArray, "runtimeDescriptorArray");
            supported &= checkFeature(indexingFeatures.descriptorBindingSampledImageUpdateAfterBind, "descriptorBindingSampledImageUpdateAfterBind");
            supported &= checkFeature(indexingFeatures.descriptorBindingStorageImageUpdateAfterBind, "descriptorBindingStorageImageUpdateAfterBind");
            supported &= checkFeature(indexingFeatures.descriptorBindingStorageBufferUpdateAfterBind, "descriptorBindingStorageBufferUpdateAfterBind");
            supported &= checkFeature(indexingFeatures.shaderSampledImageArrayNonUniformIndexing, "shaderSampledImageArrayNonUniformIndexing");

            if (!supported) {
                throw std::runtime_error("GPU does not support required bindless features (Descriptor Indexing). PNKR Engine require these features to run.");
            }
        }

        auto colorSamples = props.limits.framebufferColorSampleCounts;
        auto depthSamples = props.limits.framebufferDepthSampleCounts;

        auto getHighestBit = [](vk::SampleCountFlags flags) -> uint32_t {
          if (flags & vk::SampleCountFlagBits::e64) {
            return 64;
          }
          if (flags & vk::SampleCountFlagBits::e32) {
            return 32;
          }
          if (flags & vk::SampleCountFlagBits::e16) {
            return 16;
          }
          if (flags & vk::SampleCountFlagBits::e8) {
            return 8;
          }
          if (flags & vk::SampleCountFlagBits::e4) {
            return 4;
          }
          if (flags & vk::SampleCountFlagBits::e2) {
            return 2;
          }
          return 1;
        };

        m_capabilities.maxColorSampleCount = getHighestBit(colorSamples);
        m_capabilities.maxDepthSampleCount = getHighestBit(depthSamples);
        m_capabilities.maxCombinedSampleCount = std::min(m_capabilities.maxColorSampleCount, m_capabilities.maxDepthSampleCount);
    }

    void VulkanRHIPhysicalDevice::queryQueueFamilies()
    {
        auto queueFamilyProps = m_physicalDevice.getQueueFamilyProperties();

        for (const auto& queueFamilyProperty : queueFamilyProps)
        {
            QueueFamilyInfo info{};
            info.familyIndex = static_cast<uint32_t>(&queueFamilyProperty - &queueFamilyProps[0]);
            info.queueCount = queueFamilyProperty.queueCount;
            info.supportsGraphics = (queueFamilyProperty.queueFlags & vk::QueueFlagBits::eGraphics) != vk::QueueFlags{};
            info.supportsCompute = (queueFamilyProperty.queueFlags & vk::QueueFlagBits::eCompute) != vk::QueueFlags{};
            info.supportsTransfer = (queueFamilyProperty.queueFlags & vk::QueueFlagBits::eTransfer) != vk::QueueFlags{};

            m_queueFamilies.push_back(info);
        }
    }

    bool VulkanRHIPhysicalDevice::supportsPresentation(uint32_t queueFamily) const
    {

        return queueFamily < m_queueFamilies.size();
    }

    namespace
    {
        class VulkanDeviceBuilder
        {
        public:
          VulkanDeviceBuilder(std::unique_ptr<VulkanRHIPhysicalDevice> pd,
                              DeviceDescriptor desc)
              : m_physicalDevice(std::move(pd)), m_desc(std::move(desc)) {}

          void selectQueues() {
            auto queueFamilies = m_physicalDevice->queueFamilies();
            uint32_t graphicsFamily = VK_QUEUE_FAMILY_IGNORED;
            uint32_t computeFamily = VK_QUEUE_FAMILY_IGNORED;
            uint32_t transferFamily = VK_QUEUE_FAMILY_IGNORED;

            for (const auto &family : queueFamilies) {
              if (family.supportsGraphics &&
                  graphicsFamily == VK_QUEUE_FAMILY_IGNORED) {
                graphicsFamily = family.familyIndex;
              }
              if (family.supportsCompute && !family.supportsGraphics &&
                  computeFamily == VK_QUEUE_FAMILY_IGNORED) {
                computeFamily = family.familyIndex;
              }
              if (family.supportsTransfer && !family.supportsGraphics &&
                  !family.supportsCompute &&
                  transferFamily == VK_QUEUE_FAMILY_IGNORED) {
                transferFamily = family.familyIndex;
              }
            }

            for (const auto &family : queueFamilies) {
              if (family.supportsCompute &&
                  computeFamily == VK_QUEUE_FAMILY_IGNORED) {
                computeFamily = family.familyIndex;
              }
              if (family.supportsTransfer &&
                  transferFamily == VK_QUEUE_FAMILY_IGNORED) {
                transferFamily = family.familyIndex;
              }
            }

            PNKR_ASSERT(graphicsFamily != VK_QUEUE_FAMILY_IGNORED, "No graphics queue family found");

            m_queues.graphicsFamily = graphicsFamily;
            m_queues.computeFamily = computeFamily != VK_QUEUE_FAMILY_IGNORED
                                         ? computeFamily
                                         : graphicsFamily;
            m_queues.transferFamily = transferFamily != VK_QUEUE_FAMILY_IGNORED
                                          ? transferFamily
                                          : graphicsFamily;
          }

            void createLogicalDevice()
            {
                std::set uniqueQueueFamilies = { m_queues.graphicsFamily };
                if (m_queues.computeFamily != VK_QUEUE_FAMILY_IGNORED) {
                  uniqueQueueFamilies.insert(m_queues.computeFamily);
                }
                if (m_queues.transferFamily != VK_QUEUE_FAMILY_IGNORED) {
                  uniqueQueueFamilies.insert(m_queues.transferFamily);
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

                vk::PhysicalDeviceFeatures featuresCore{};
                featuresCore.samplerAnisotropy = VK_TRUE;
                featuresCore.shaderStorageImageWriteWithoutFormat = VK_TRUE;
                featuresCore.shaderStorageImageMultisample = VK_TRUE;
                featuresCore.fillModeNonSolid = VK_TRUE;
                featuresCore.geometryShader = VK_TRUE;
                featuresCore.shaderInt64 = VK_TRUE;
                featuresCore.shaderFloat64 = VK_TRUE;
                featuresCore.multiDrawIndirect = VK_TRUE;
                featuresCore.fragmentStoresAndAtomics = VK_TRUE;
                featuresCore.drawIndirectFirstInstance = VK_TRUE;
                featuresCore.sampleRateShading = VK_TRUE;
                featuresCore.shaderInt16 = VK_TRUE;
                featuresCore.independentBlend = VK_TRUE;
                featuresCore.pipelineStatisticsQuery = m_physicalDevice->capabilities().pipelineStatisticsQuery ? VK_TRUE : VK_FALSE;
                m_enabledFeatures = featuresCore;

                vk::PhysicalDeviceVulkan12Features features12{};
                features12.drawIndirectCount = m_physicalDevice->capabilities().drawIndirectCount ? VK_TRUE : VK_FALSE;
                features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
                features12.bufferDeviceAddress = VK_TRUE;
                features12.scalarBlockLayout = VK_TRUE;
                features12.shaderFloat16 = VK_TRUE;
                features12.timelineSemaphore = VK_TRUE;
                features12.descriptorIndexing = VK_TRUE;
                features12.runtimeDescriptorArray = VK_TRUE;
                features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
                features12.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
                features12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
                features12.descriptorBindingPartiallyBound = VK_TRUE;
                features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
                features12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
                features12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;

                vk::PhysicalDeviceVulkan13Features features13{};
                features13.dynamicRendering = VK_TRUE;
                features13.synchronization2 = VK_TRUE;
                features13.maintenance4 = VK_TRUE;
                features13.shaderDemoteToHelperInvocation = VK_TRUE;

                vk::PhysicalDeviceVulkan11Features features11{};
                features11.shaderDrawParameters = VK_TRUE;
                features11.storageBuffer16BitAccess = VK_TRUE;

                vk::PhysicalDeviceComputeShaderDerivativesFeaturesKHR derivativeFeatures{};
                derivativeFeatures.computeDerivativeGroupQuads = VK_TRUE;
                derivativeFeatures.computeDerivativeGroupLinear = VK_TRUE;

                vk::PhysicalDeviceAddressBindingReportFeaturesEXT bindingReportFeatures{};
                bindingReportFeatures.reportAddressBinding = VK_TRUE;

                vk::DeviceDiagnosticsConfigCreateInfoNV aftermathDiagInfo{};
                bool enableAftermath = false;

#ifdef PNKR_AFTERMATH_ENABLED
                AftermathIntegration::initialize();
                enableAftermath = AftermathIntegration::isEnabled();
                #endif

                vk::PhysicalDeviceImageViewMinLodFeaturesEXT minLodFeatures{};
                minLodFeatures.sType = vk::StructureType::ePhysicalDeviceImageViewMinLodFeaturesEXT;
                minLodFeatures.minLod = VK_TRUE;

                features13.pNext = &features12;
                features12.pNext = &features11;
                features11.pNext = &derivativeFeatures;
                derivativeFeatures.pNext = &bindingReportFeatures;
                bindingReportFeatures.pNext = nullptr;

                std::vector<const char*> extensions = m_desc.requiredExtensions;
                auto availExts = m_physicalDevice->physicalDevice().enumerateDeviceExtensionProperties();
                auto hasExt = [&](const char *name) {
                  return std::ranges::any_of(
                      availExts, [&](const vk::ExtensionProperties &e) {
                        return std::strcmp(e.extensionName, name) == 0;
                      });
                };

                bool instanceHasDebugUtils = m_physicalDevice->hasDebugMessenger();
                if (instanceHasDebugUtils && hasExt("VK_EXT_device_address_binding_report")) {
                    extensions.push_back("VK_EXT_device_address_binding_report");
                } else {
                    bindingReportFeatures.reportAddressBinding = VK_FALSE;
                }

                if (enableAftermath && hasExt(VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME) && hasExt("VK_NV_device_diagnostic_checkpoints")) {
                    extensions.push_back(VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME);
                    extensions.push_back("VK_NV_device_diagnostic_checkpoints");
                    aftermathDiagInfo.flags = vk::DeviceDiagnosticsConfigFlagBitsNV::eEnableShaderDebugInfo | vk::DeviceDiagnosticsConfigFlagBitsNV::eEnableResourceTracking | vk::DeviceDiagnosticsConfigFlagBitsNV::eEnableAutomaticCheckpoints;
                    aftermathDiagInfo.pNext = bindingReportFeatures.pNext;
                    bindingReportFeatures.pNext = &aftermathDiagInfo;
                } else if (enableAftermath) {
                    core::Logger::RHI.warn("NVIDIA Aftermath is enabled in build, but required Vulkan extensions are missing: {} or {}", VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME, "VK_NV_device_diagnostic_checkpoints");
                }

                if (hasExt("VK_KHR_compute_shader_derivatives")) {
                    extensions.push_back("VK_KHR_compute_shader_derivatives");
                } else {
                    derivativeFeatures.computeDerivativeGroupQuads = VK_FALSE;
                    derivativeFeatures.computeDerivativeGroupLinear = VK_FALSE;
                }

                if (hasExt("VK_EXT_device_fault")) {
                  extensions.push_back("VK_EXT_device_fault");
                }
                if (hasExt("VK_NV_device_diagnostic_checkpoints")) {
                  extensions.push_back("VK_NV_device_diagnostic_checkpoints");
                }
                if (hasExt("VK_GOOGLE_hlsl_functionality1")) {
                  extensions.push_back("VK_GOOGLE_hlsl_functionality1");
                }
                if (hasExt("VK_GOOGLE_user_type")) {
                  extensions.push_back("VK_GOOGLE_user_type");
                }
                if (hasExt(VK_KHR_RELAXED_BLOCK_LAYOUT_EXTENSION_NAME)) {
                  extensions.push_back(
                      VK_KHR_RELAXED_BLOCK_LAYOUT_EXTENSION_NAME);
                }

                const bool enableMinLodExt = hasExt(VK_EXT_IMAGE_VIEW_MIN_LOD_EXTENSION_NAME);
                if (enableMinLodExt) {
                    extensions.push_back(VK_EXT_IMAGE_VIEW_MIN_LOD_EXTENSION_NAME);
                    m_minLodExtensionEnabled = true;
                }

                vk::DeviceCreateInfo createInfo{};
                createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
                createInfo.pQueueCreateInfos = queueCreateInfos.data();
                createInfo.pEnabledFeatures = &featuresCore;
                createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
                createInfo.ppEnabledExtensionNames = extensions.data();

                void* pNextChain = &features13;
                if (enableMinLodExt) {
                    minLodFeatures.pNext = pNextChain;
                    pNextChain = &minLodFeatures;
                }
                createInfo.pNext = pNextChain;

                m_device = m_physicalDevice->physicalDevice().createDevice(createInfo);

                m_queues.graphics = m_device.getQueue(m_queues.graphicsFamily, 0);
                m_queues.compute = m_device.getQueue(m_queues.computeFamily, 0);
                m_queues.transfer = m_device.getQueue(m_queues.transferFamily, 0);
            }

            void initVMA()
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

                VmaAllocator allocator = nullptr;
                auto result = static_cast<vk::Result>(vmaCreateAllocator(&allocatorInfo, &allocator));
                if (result != vk::Result::eSuccess)
                {
                    throw cpptrace::runtime_error("Failed to create VMA allocator: " + vk::to_string(result));
                }
                m_allocator.reset(allocator);
            }

            void createCommandPool()
            {
                vk::CommandPoolCreateInfo poolInfo{};
                poolInfo.queueFamilyIndex = m_queues.graphicsFamily;
                poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

                m_commandPool = m_device.createCommandPool(poolInfo);
            }

            void createTimelineSemaphore()
            {
                vk::SemaphoreTypeCreateInfo typeInfo{};
                typeInfo.semaphoreType = vk::SemaphoreType::eTimeline;
                typeInfo.initialValue = 0;

                vk::SemaphoreCreateInfo createInfo{};
                createInfo.pNext = &typeInfo;

                m_frameTimelineSemaphore = m_device.createSemaphore(createInfo);
                m_computeTimelineSemaphore = m_device.createSemaphore(createInfo);
            }

            void createDescriptorPool()
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

            void initBindless()
            {
                m_bindlessManager = std::make_unique<BindlessDescriptorManager>(m_device, m_physicalDevice->physicalDevice());
            }

            void initGpuProfiler()
            {
                m_gpuProfiler = std::make_unique<VulkanGPUTimeQueriesManager>();
                auto props = m_physicalDevice->physicalDevice().getProperties();
                auto timestampPeriod =
                    static_cast<double>(props.limits.timestampPeriod);
                const auto queueFamilyProps = m_physicalDevice->physicalDevice().getQueueFamilyProperties();
                const uint32_t validBits = (m_queues.graphicsFamily < queueFamilyProps.size())
                    ? queueFamilyProps[m_queues.graphicsFamily].timestampValidBits
                    : 0;
                core::Logger::RHI.trace("GPU profiler: timestampPeriod={} ns, timestampValidBits={}",
                                   timestampPeriod, validBits);
                if (validBits == 0) {
                    core::Logger::RHI.warn("GPU profiler: timestamp queries are not supported on this queue family");
                }

                m_gpuProfiler->init(m_device, 4096, timestampPeriod, m_physicalDevice->capabilities().pipelineStatisticsQuery);
            }

            void initPipelineCache()
            {
                vk::PipelineCacheCreateInfo cacheInfo{};
                const std::string cachePath = "pnkr_pipeline_cache.bin";

                std::vector<uint8_t> cacheData;
                std::ifstream cacheFile(cachePath, std::ios::binary | std::ios::ate);
                if (cacheFile.is_open()) {
                    size_t fileSize = cacheFile.tellg();
                    cacheFile.seekg(0);
                    cacheData.resize(fileSize);

                    cacheFile.read(reinterpret_cast<char*>(cacheData.data()), fileSize);
                    cacheFile.close();
                    core::Logger::RHI.info("Loaded pipeline cache: {} bytes", fileSize);

                    cacheInfo.initialDataSize = cacheData.size();
                    cacheInfo.pInitialData = cacheData.data();
                }

                m_pipelineCache = m_device.createPipelineCache(cacheInfo);
                if (!m_pipelineCache) {
                    core::Logger::RHI.warn("Failed to create pipeline cache, using null cache");
                }
            }

            void initBDARegistry()
            {
                m_bdaRegistry = std::make_unique<BDARegistry>();
            }

            VulkanDeviceConstructionContext finalize()
            {
                VulkanDeviceConstructionContext ctx;
                ctx.physicalDevice = std::move(m_physicalDevice);
                ctx.device = m_device;
                ctx.allocator = std::move(m_allocator);
                ctx.queues = m_queues;
                ctx.enabledFeatures = m_enabledFeatures;
                ctx.minLodExtensionEnabled = m_minLodExtensionEnabled;

                ctx.commandPool = m_commandPool;
                ctx.frameTimelineSemaphore = m_frameTimelineSemaphore;
                ctx.computeTimelineSemaphore = m_computeTimelineSemaphore;
                ctx.descriptorPool = m_descriptorPool;
                ctx.pipelineCache = m_pipelineCache;
                ctx.bdaRegistry = std::move(m_bdaRegistry);
                ctx.bindlessManager = std::move(m_bindlessManager);
                ctx.gpuProfiler = std::move(m_gpuProfiler);

                return ctx;
            }

        private:
            std::unique_ptr<VulkanRHIPhysicalDevice> m_physicalDevice;
            DeviceDescriptor m_desc;
            vk::Device m_device;
            UniqueVmaAllocator m_allocator;
            VulkanQueues m_queues;
            vk::PhysicalDeviceFeatures m_enabledFeatures;
            bool m_minLodExtensionEnabled = false;

            vk::CommandPool m_commandPool;
            vk::Semaphore m_frameTimelineSemaphore;
            vk::Semaphore m_computeTimelineSemaphore;
            vk::DescriptorPool m_descriptorPool;
            vk::PipelineCache m_pipelineCache;
            std::unique_ptr<BDARegistry> m_bdaRegistry;
            std::unique_ptr<BindlessDescriptorManager> m_bindlessManager;
            std::unique_ptr<VulkanGPUTimeQueriesManager> m_gpuProfiler;
        };
    }

    std::unique_ptr<VulkanRHIDevice> VulkanRHIDevice::create(std::unique_ptr<VulkanRHIPhysicalDevice> physicalDevice,
                                                            const DeviceDescriptor& desc)
    {
        VulkanDeviceBuilder builder(std::move(physicalDevice), desc);
        builder.selectQueues();
        builder.createLogicalDevice();
        builder.initVMA();
        builder.createCommandPool();
        builder.createTimelineSemaphore();
        builder.createDescriptorPool();
        builder.initBindless();
        builder.initGpuProfiler();
        builder.initPipelineCache();
        builder.initBDARegistry();

        return std::make_unique<VulkanRHIDevice>(builder.finalize());
    }

    VulkanRHIDevice::VulkanRHIDevice(VulkanDeviceConstructionContext&& ctx)
        : m_physicalDevice(std::move(ctx.physicalDevice))
        , m_device(ctx.device)
        , m_allocator(std::move(ctx.allocator))
        , m_bdaRegistry(std::move(ctx.bdaRegistry))
        , m_bindlessManager(std::move(ctx.bindlessManager))
        , m_gpuProfiler(std::move(ctx.gpuProfiler))
        , m_graphicsQueueFamily(ctx.queues.graphicsFamily)
        , m_computeQueueFamily(ctx.queues.computeFamily)
        , m_transferQueueFamily(ctx.queues.transferFamily)
        , m_graphicsQueue(ctx.queues.graphics)
        , m_computeQueue(ctx.queues.compute)
        , m_transferQueue(ctx.queues.transfer)
        , m_commandPool(ctx.commandPool)
        , m_minLodExtensionEnabled(ctx.minLodExtensionEnabled)
        , m_enabledFeatures(ctx.enabledFeatures)
        , m_frameTimelineSemaphore(ctx.frameTimelineSemaphore)
        , m_computeTimelineSemaphore(ctx.computeTimelineSemaphore)
        , m_descriptorPool(ctx.descriptorPool)
        , m_pipelineCache(ctx.pipelineCache)
    {
        RHIFactory::registerDebugDevice(this);

        if (m_bindlessManager)
        {
            m_bindlessManager->init(this);
        }

        trackObject(vk::ObjectType::eCommandPool,
                    pnkr::util::u64(static_cast<VkCommandPool>(m_commandPool)),
                    "DeviceCommandPool");
        trackObject(vk::ObjectType::eSemaphore,
                    pnkr::util::u64(static_cast<VkSemaphore>(m_frameTimelineSemaphore)),
                    "FrameTimelineSemaphore");
        trackObject(vk::ObjectType::eSemaphore,
                    pnkr::util::u64(static_cast<VkSemaphore>(m_computeTimelineSemaphore)),
                    "ComputeTimelineSemaphore");
        trackObject(vk::ObjectType::eDescriptorPool,
                    pnkr::util::u64(static_cast<VkDescriptorPool>(m_descriptorPool)),
                    "DescriptorPool");
        trackObject(vk::ObjectType::ePipelineCache,
                    pnkr::util::u64(static_cast<VkPipelineCache>(m_pipelineCache)),
                    "PipelineCache");

        core::Logger::RHI.info("Vulkan RHI Device created: {}", m_physicalDevice->capabilities().deviceName);
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
            waitIdle();

            if (m_bindlessManager)
            {
                m_bindlessManager.reset();
            }

            if (m_gpuProfiler)
            {
                m_gpuProfiler->shutdown(m_device);
                m_gpuProfiler = nullptr;
            }

            if (m_frameTimelineSemaphore)
            {
                untrackObject(pnkr::util::u64(
                    static_cast<VkSemaphore>(m_frameTimelineSemaphore)));
                m_device.destroySemaphore(m_frameTimelineSemaphore);
            }
            if (m_computeTimelineSemaphore)
            {
                untrackObject(pnkr::util::u64(
                    static_cast<VkSemaphore>(m_computeTimelineSemaphore)));
                m_device.destroySemaphore(m_computeTimelineSemaphore);
            }

            if (m_commandPool)
            {
                untrackObject(
                    pnkr::util::u64(static_cast<VkCommandPool>(m_commandPool)));
                m_device.destroyCommandPool(m_commandPool);
            }

            if (m_descriptorPool)
            {
                untrackObject(pnkr::util::u64(
                    static_cast<VkDescriptorPool>(m_descriptorPool)));
                m_device.destroyDescriptorPool(m_descriptorPool);
            }

            if (m_pipelineCache)
            {
                savePipelineCache();
                untrackObject(pnkr::util::u64(
                    static_cast<VkPipelineCache>(m_pipelineCache)));
                m_device.destroyPipelineCache(m_pipelineCache);
            }

            while (true)
            {
                std::function<void()> fn;
                {
                  std::scoped_lock lock(m_deletionMutex);
                  if (m_deletionQueue.empty()) {
                    break;
                  }
                    fn = std::move(m_deletionQueue.front().deleteFn);
                    m_deletionQueue.pop_front();
                }
                if (fn) {
                  fn();
                }
            }

            m_allocator.reset();
            m_device.destroy();
        }

        RHIFactory::registerDebugDevice(nullptr);

#ifdef PNKR_AFTERMATH_ENABLED
        AftermathIntegration::shutdown();
#endif
    }

    void VulkanRHIDevice::savePipelineCache()
    {
        size_t cacheSize = 0;
        auto result = m_device.getPipelineCacheData(m_pipelineCache, &cacheSize, nullptr);
        if (result == vk::Result::eSuccess && cacheSize > 0)
        {
            std::vector<uint8_t> cacheData(cacheSize);
            result = m_device.getPipelineCacheData(m_pipelineCache, &cacheSize, cacheData.data());
            if (result == vk::Result::eSuccess)
            {
                std::ofstream cacheFile("pnkr_pipeline_cache.bin", std::ios::binary);
                if (cacheFile.is_open())
                {

                    cacheFile.write(reinterpret_cast<const char*>(cacheData.data()), cacheSize);
                    cacheFile.close();
                    core::Logger::RHI.info("Saved pipeline cache: {} bytes", cacheSize);
                }
            }
        }
    }

    std::unique_ptr<RHIBuffer> VulkanRHIDevice::createBuffer(const char* name, const BufferDescriptor& desc)
    {
        PNKR_LOG_SCOPE(std::format("RHI::CreateBuffer[{}]", name ? name : "Unnamed"));
        PNKR_PROFILE_FUNCTION();

        if ((name == nullptr) || name[0] == '\0') {
          core::Logger::RHI.error(
              "createBuffer: name is required for all buffers");
          name = "UnnamedBuffer";
        }

        BufferDescriptor finalDesc = desc;
        finalDesc.debugName = name;

        if (finalDesc.data != nullptr && finalDesc.memoryUsage == MemoryUsage::GPUOnly)
        {
            finalDesc.usage |= BufferUsage::TransferDst;
        }

        auto buf = std::make_unique<VulkanRHIBuffer>(this, finalDesc);
        core::Logger::RHI.info("Created buffer: {} ({} bytes)", name, finalDesc.size);

        if (finalDesc.data != nullptr)
        {
            if (finalDesc.memoryUsage == MemoryUsage::CPUToGPU || finalDesc.memoryUsage == MemoryUsage::CPUOnly)
            {
                buf->uploadData(std::span<const std::byte>(reinterpret_cast<const std::byte*>(finalDesc.data), finalDesc.size));
            }
            else
            {

                auto staging = createBuffer("StagingBuffer", {
                    .size = finalDesc.size,
                    .usage = BufferUsage::TransferSrc,
                    .memoryUsage = MemoryUsage::CPUToGPU,
                    .debugName = "StagingBuffer"
                });
                staging->uploadData(std::span<const std::byte>(reinterpret_cast<const std::byte*>(finalDesc.data), finalDesc.size));

                immediateSubmit([&](RHICommandList* cmd) {
                    cmd->copyBuffer(staging.get(), buf.get(), 0, 0, finalDesc.size);
                });
            }
        }

#ifdef TRACY_ENABLE
        TracyAllocN(buf->nativeHandle(), desc.size, name);
#endif

        return buf;
    }

    std::unique_ptr<RHITexture> VulkanRHIDevice::createTexture(const char* name, const TextureDescriptor& desc)
    {
        PNKR_LOG_SCOPE(std::format("RHI::CreateTexture[{}]", name ? name : "Unnamed"));

        if ((name == nullptr) || name[0] == '\0') {
          core::Logger::RHI.error(
              "createTexture: name is required for all textures");
          name = "UnnamedTexture";
        }

        TextureDescriptor finalDesc = desc;
        finalDesc.debugName = name;

        auto tex = std::make_unique<VulkanRHITexture>(this, finalDesc);
        core::Logger::RHI.info("Created texture: {} ({}x{} {})", name, desc.extent.width, desc.extent.height, static_cast<uint32_t>(desc.format));
        return tex;
    }

    std::unique_ptr<RHITexture> VulkanRHIDevice::createTextureView(
        const char* name,
        RHITexture* parent,
        const TextureViewDescriptor& desc)
    {

      if ((name == nullptr) || name[0] == '\0') {
        core::Logger::RHI.error(
            "createTextureView: name is required for all texture views");
        name = "UnnamedTextureView";
      }

        PNKR_LOG_SCOPE(std::format("RHI::CreateTextureView[{}]", name ? name : "Unnamed"));
        auto* vkParent = rhi_cast<VulkanRHITexture>(parent);
        if (vkParent == nullptr) {
          return nullptr;
        }

        core::Logger::RHI.info("Created texture view: {} from parent", name);
        return std::make_unique<VulkanRHITexture>(this, vkParent, desc);
    }

    std::unique_ptr<RHITexture> VulkanRHIDevice::createTexture(
        const Extent3D& extent,
        Format format,
        TextureUsageFlags usage,
        uint32_t mipLevels,
        uint32_t arrayLayers)
    {
        TextureDescriptor desc{};
        desc.extent = extent;
        desc.format = format;
        desc.usage = usage;
        desc.mipLevels = mipLevels;
        desc.arrayLayers = arrayLayers;

        return createTexture("UnnamedTexture", desc);
    }

    std::unique_ptr<RHITexture> VulkanRHIDevice::createCubemap(
        const Extent3D& extent,
        Format format,
        TextureUsageFlags usage,
        uint32_t mipLevels)
    {
        PNKR_LOG_SCOPE("RHI::CreateCubemap");
        TextureDescriptor desc{};
        desc.extent = extent;
        desc.format = format;
        desc.usage = usage;
        desc.mipLevels = mipLevels;
        desc.arrayLayers = 6;
        desc.type = TextureType::TextureCube;

        core::Logger::RHI.info("Created cubemap: {}x{}", extent.width, extent.height);
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

    std::unique_ptr<RHICommandBuffer> VulkanRHIDevice::createCommandBuffer(RHICommandPool* pool)
    {
      return std::make_unique<VulkanRHICommandBuffer>(
this, rhi_cast<VulkanRHICommandPool>(pool));
    }

    std::unique_ptr<RHICommandPool> VulkanRHIDevice::createCommandPool(const CommandPoolDescriptor& desc)
    {
        return std::make_unique<VulkanRHICommandPool>(this, desc);
    }

    VulkanRHICommandPool::VulkanRHICommandPool(VulkanRHIDevice* device, const CommandPoolDescriptor& desc)
        : m_device(device)
        , m_queueFamilyIndex(desc.queueFamilyIndex)
    {
        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.queueFamilyIndex = desc.queueFamilyIndex;

        if (desc.flags == CommandPoolFlags::Transient) {
          poolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient;
        } else if (desc.flags == CommandPoolFlags::ResetCommandBuffer) {
          poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        }

        m_pool = device->device().createCommandPool(poolInfo);
        m_device->trackObject(vk::ObjectType::eCommandPool,
                              pnkr::util::u64(static_cast<VkCommandPool>(m_pool)),
                              "CommandPool");
    }

    VulkanRHICommandPool::~VulkanRHICommandPool()
    {
         if (m_pool)
         {
             m_device->untrackObject(
                 pnkr::util::u64(static_cast<VkCommandPool>(m_pool)));
             m_device->device().destroyCommandPool(m_pool);
         }
    }

    void VulkanRHICommandPool::reset()
    {
        if (m_pool)
        {
            m_device->device().resetCommandPool(m_pool, vk::CommandPoolResetFlagBits::eReleaseResources);
        }
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
        std::vector<vk::DescriptorBindingFlags> bindingFlags;
        bindings.reserve(desc.bindings.size());
        bindingFlags.reserve(desc.bindings.size());

        bool hasUpdateAfterBind = false;

        for (const auto& binding : desc.bindings)
        {
            vk::DescriptorSetLayoutBinding vkBinding{};
            vkBinding.binding = binding.binding;
            vkBinding.descriptorType = VulkanUtils::toVkDescriptorType(binding.type);
            vkBinding.descriptorCount = binding.count;
            vkBinding.stageFlags = VulkanUtils::toVkShaderStage(binding.stages);
            bindings.push_back(vkBinding);

            vk::DescriptorBindingFlags flags = {};
            if (binding.flags.has(DescriptorBindingFlags::UpdateAfterBind)) {
                flags |= vk::DescriptorBindingFlagBits::eUpdateAfterBind;
                hasUpdateAfterBind = true;
            }
            if (binding.flags.has(DescriptorBindingFlags::PartiallyBound)) {
                flags |= vk::DescriptorBindingFlagBits::ePartiallyBound;
            }
            if (binding.flags.has(DescriptorBindingFlags::VariableDescriptorCount)) {
                flags |= vk::DescriptorBindingFlagBits::eVariableDescriptorCount;
            }
            bindingFlags.push_back(flags);
        }

        vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
        flagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
        flagsInfo.pBindingFlags = bindingFlags.data();

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.pNext = &flagsInfo;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (hasUpdateAfterBind) {
            layoutInfo.flags |= vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
        }

        vk::DescriptorSetLayout layout = m_device.createDescriptorSetLayout(layoutInfo);
        return std::make_unique<VulkanRHIDescriptorSetLayout>(this, layout, desc);
    }

    std::unique_ptr<RHIDescriptorSet> VulkanRHIDevice::allocateDescriptorSet(
        RHIDescriptorSetLayout* layout)
    {
        auto* vkLayout = rhi_cast<VulkanRHIDescriptorSetLayout>(layout);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = m_descriptorPool;
        vk::DescriptorSetLayout layoutHandle = vkLayout->layout();
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layoutHandle;

        auto sets = m_device.allocateDescriptorSets(allocInfo);
        return std::make_unique<VulkanRHIDescriptorSet>(this, vkLayout, sets[0]);
    }

    std::unique_ptr<RHIFence> VulkanRHIDevice::createFence(bool signaled)
    {
        return std::make_unique<VulkanRHIFence>(this, signaled);
    }

    void VulkanRHIDevice::waitIdle()
    {
        std::scoped_lock lock(m_queueMutex);
        m_device.waitIdle();
        processDeletionQueue();
    }

    void VulkanRHIDevice::waitForFences(const std::vector<uint64_t>& fenceValues)
    {
        if (fenceValues.empty())
        {
            return;
        }

        std::vector semaphores(fenceValues.size(), m_frameTimelineSemaphore);

        vk::SemaphoreWaitInfo waitInfo{};
        waitInfo.sType = vk::StructureType::eSemaphoreWaitInfo;
        waitInfo.semaphoreCount = static_cast<uint32_t>(fenceValues.size());
        waitInfo.pSemaphores = semaphores.data();
        waitInfo.pValues = fenceValues.data();

        auto result = m_device.waitSemaphores(waitInfo, UINT64_MAX);
        if (result != vk::Result::eSuccess)
        {
            core::Logger::RHI.error("Failed to wait for fences: {}", vk::to_string(result));
        }
    }

    void VulkanRHIDevice::submitCommands(
        RHICommandList* commandBuffer,
        RHIFence* signalFence,
        const std::vector<uint64_t>& waitSemaphores,
        const std::vector<uint64_t>& signalSemaphores,
        RHISwapchain* swapchain)
    {
        auto* vkCmdBuffer = rhi_cast<VulkanRHICommandBuffer>(commandBuffer);

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        auto cmdBuf = vkCmdBuffer->commandBuffer();
        submitInfo.pCommandBuffers = &cmdBuf;

        vk::TimelineSemaphoreSubmitInfo timelineInfo{};

        std::vector<vk::Semaphore> waitSems;
        std::vector<vk::PipelineStageFlags> waitStages;
        std::vector<uint64_t> waitValues;

        for (auto val : waitSemaphores)
        {
            waitSems.push_back(m_frameTimelineSemaphore);
            waitStages.emplace_back(vk::PipelineStageFlagBits::eAllCommands);
            waitValues.push_back(val);
        }

        std::vector<vk::Semaphore> signalSems;
        std::vector<uint64_t> signalValues;

        for (auto val : signalSemaphores)
        {
            signalSems.push_back(m_frameTimelineSemaphore);
            signalValues.push_back(val);
        }

        if (swapchain != nullptr) {
          auto *vkSwapchain = rhi_cast<VulkanRHISwapchain>(swapchain);
          if (vkSwapchain != nullptr) {

            waitSems.push_back(vkSwapchain->getCurrentAcquireSemaphore());
            waitStages.emplace_back(
                vk::PipelineStageFlagBits::eColorAttachmentOutput);
            waitValues.push_back(0);

            signalSems.push_back(
                vkSwapchain->getCurrentRenderFinishedSemaphore());
            signalValues.push_back(0);
          }
        }

        if (!waitSems.empty())
        {
            submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSems.size());
            submitInfo.pWaitSemaphores = waitSems.data();
            submitInfo.pWaitDstStageMask = waitStages.data();
        }

        if (!signalSems.empty())
        {
            submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSems.size());
            submitInfo.pSignalSemaphores = signalSems.data();
        }

        timelineInfo.waitSemaphoreValueCount = static_cast<uint32_t>(waitValues.size());
        timelineInfo.pWaitSemaphoreValues = waitValues.data();
        timelineInfo.signalSemaphoreValueCount = static_cast<uint32_t>(signalValues.size());
        timelineInfo.pSignalSemaphoreValues = signalValues.data();

        for (size_t i = 0; i < signalSems.size(); ++i) {
            if (signalSems[i] == m_frameTimelineSemaphore) {
                uint64_t current = 0;
                if (m_device.getSemaphoreCounterValue(m_frameTimelineSemaphore, &current) == vk::Result::eSuccess) {
                    if (current == UINT64_MAX) {
                        core::Logger::RHI.error("Timeline semaphore has reached UINT64_MAX! Likely device loss.");
                    } else if (signalValues[i] <= current) {
                        core::Logger::RHI.error("Timeline semaphore signal value {} is not greater than current value {}!", signalValues[i], current);
                    }
                }
            }
        }

        submitInfo.pNext = &timelineInfo;

        vk::Fence fenceHandle{};
        if (signalFence != nullptr)
        {
            auto* vkFence = rhi_cast<VulkanRHIFence>(signalFence);
            fenceHandle = vk::Fence(static_cast<VkFence>(vkFence->nativeHandle()));
        }

        vk::Queue queue = m_graphicsQueue;
        const uint32_t family = vkCmdBuffer->getQueueFamilyIndex();

        if (family == m_computeQueueFamily)
        {
            queue = m_computeQueue;
        }
        else if (family == m_transferQueueFamily)
        {
            queue = m_transferQueue;
        }

        queueSubmit(queue, submitInfo, fenceHandle);
    }

    void VulkanRHIDevice::queueSubmit(vk::Queue queue, const vk::SubmitInfo& submitInfo, vk::Fence fence)
    {
      std::scoped_lock lock(m_queueMutex);
      queue.submit(submitInfo, fence);
    }

    std::unique_lock<std::mutex> VulkanRHIDevice::acquireQueueLock()
    {
        return std::unique_lock<std::mutex>(m_queueMutex);
    }

    uint64_t VulkanRHIDevice::getCompletedFrame() const
    {
        uint64_t completed = 0;
        const auto res = m_device.getSemaphoreCounterValue(m_frameTimelineSemaphore, &completed);
        if (res != vk::Result::eSuccess)
        {
            core::Logger::RHI.critical("getSemaphoreCounterValue failed: {}", vk::to_string(res));
            throw std::runtime_error("VulkanRHIDevice::getCompletedFrame failed");
        }

        if (completed == UINT64_MAX) {
            core::Logger::RHI.warn("getCompletedFrame returned UINT64_MAX. Semantic value for 'Device Lost'.");
        }

        return completed;
    }

    void VulkanRHIDevice::waitForFrame(uint64_t frameIndex)
    {
        if (frameIndex == 0)
        {
            return;
        }

        const uint64_t completed = getCompletedFrame();
        if (completed >= frameIndex)
        {
            return;
        }
        vk::SemaphoreWaitInfo waitInfo{};
        waitInfo.sType = vk::StructureType::eSemaphoreWaitInfo;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &m_frameTimelineSemaphore;
        waitInfo.pValues = &frameIndex;

        const auto result = m_device.waitSemaphores(waitInfo, UINT64_MAX);
        if (result != vk::Result::eSuccess)
        {
            core::Logger::RHI.critical("Failed to wait for frame value {}: {}", frameIndex, vk::to_string(result));
            throw std::runtime_error("VulkanRHIDevice::waitForFrame failed");
        }
    }

    uint64_t VulkanRHIDevice::incrementFrame()
    {
        processDeletionQueue();
        if (m_bindlessManager)
        {
            m_bindlessManager->update(getCompletedFrame());
        }
        return ++m_frameCounter;
    }

    void VulkanRHIDevice::immediateSubmit(std::function<void(RHICommandList*)>&& func)
    {
        auto cmd = createCommandList();
        cmd->begin();
        func(cmd.get());
        cmd->end();
        submitCommands(cmd.get());
        waitIdle();
    }

    void VulkanRHIDevice::downloadTexture(
        RHITexture* texture,
        std::span<std::byte> outData,
        const TextureSubresource& subresource)
    {
        uint64_t dataSize = outData.size_bytes();
        auto* vkTex = rhi_cast<VulkanRHITexture>(texture);

        auto stagingBuffer = createBuffer("TextureDownloadStaging",{
            .size = dataSize,
            .usage = BufferUsage::TransferDst,
            .memoryUsage = MemoryUsage::GPUToCPU,
            .debugName = "TextureDownloadStaging"
        });

        immediateSubmit([&](RHICommandList* cmd)
        {
            auto* vkCmd = rhi_cast<VulkanRHICommandBuffer>(cmd);

            vkTex->transitionLayout(static_cast<VkImageLayout>(vk::ImageLayout::eTransferSrcOptimal), vkCmd->commandBuffer());

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

            copyRegion.imageExtent.width = std::max(
                1U, copyRegion.imageExtent.width >> subresource.mipLevel);
            copyRegion.imageExtent.height = std::max(
                1U, copyRegion.imageExtent.height >> subresource.mipLevel);

            vkCmd->commandBuffer().copyImageToBuffer(
                vk::Image(vkTex->imageHandle()), vk::ImageLayout::eTransferSrcOptimal,
                rhi_cast<VulkanRHIBuffer>(stagingBuffer.get())->buffer(),
                copyRegion);

            vkTex->transitionLayout(static_cast<VkImageLayout>(vk::ImageLayout::eShaderReadOnlyOptimal), vkCmd->commandBuffer());
        });

        void* mappedData = stagingBuffer->map();
        std::memcpy(outData.data(), mappedData, dataSize);
        stagingBuffer->unmap();
    }

    vk::ShaderModule VulkanRHIDevice::createShaderModule(const std::vector<uint32_t>& spirvCode)
    {
        vk::ShaderModuleCreateInfo createInfo{};
        createInfo.codeSize = spirvCode.size() * sizeof(uint32_t);
        createInfo.pCode = spirvCode.data();

        auto module = m_device.createShaderModule(createInfo);
        trackObject(vk::ObjectType::eShaderModule,
                    pnkr::util::u64(static_cast<VkShaderModule>(module)),
                    "ShaderModule");
        return module;
    }

    void VulkanRHIDevice::destroyShaderModule(vk::ShaderModule module)
    {
        untrackObject(pnkr::util::u64(static_cast<VkShaderModule>(module)));
        m_device.destroyShaderModule(module);
    }

    std::unique_ptr<RHIImGui> VulkanRHIDevice::createImGuiRenderer()
    {
        return std::make_unique<VulkanRHIImGui>(this);
    }

    class VulkanRHIUploadContext : public RHIUploadContext
    {
    public:
      VulkanRHIUploadContext(VulkanRHIDevice *device, uint64_t stagingSize)
          : m_device(device), m_stagingSize(stagingSize)
             {
        m_stagingBuffer = m_device->createBuffer(
            "UploadStagingBuffer", {.size = m_stagingSize,
                                    .usage = BufferUsage::TransferSrc,
                                    .memoryUsage = MemoryUsage::CPUToGPU,
                                    .debugName = "UploadStagingBuffer"});
          m_mappedPtr = m_stagingBuffer->map();
        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.queueFamilyIndex = m_device->graphicsQueueFamily();
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        m_pool = m_device->device().createCommandPool(poolInfo);
        m_device->trackObject(vk::ObjectType::eCommandPool,
                              pnkr::util::u64(static_cast<VkCommandPool>(m_pool)),
                              "UploadCommandPool");

        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo.commandPool = m_pool;
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandBufferCount = 1;
        m_cmd = m_device->device().allocateCommandBuffers(allocInfo)[0];
        m_device->trackObject(vk::ObjectType::eCommandBuffer,
                              pnkr::util::u64(static_cast<VkCommandBuffer>(m_cmd)),
                              "UploadCommandBuffer");

        vk::FenceCreateInfo fenceInfo{};
        m_fence = m_device->device().createFence(fenceInfo);
        m_device->trackObject(vk::ObjectType::eFence,
                              pnkr::util::u64(static_cast<VkFence>(m_fence)),
                              "UploadFence");

        m_cmd.begin(vk::CommandBufferBeginInfo{
            vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
      }

        ~VulkanRHIUploadContext() override
        {
            m_device->untrackObject(
                pnkr::util::u64(static_cast<VkFence>(m_fence)));
            m_device->device().destroyFence(m_fence);
            m_device->untrackObject(
                pnkr::util::u64(static_cast<VkCommandBuffer>(m_cmd)));
            m_device->untrackObject(
                pnkr::util::u64(static_cast<VkCommandPool>(m_pool)));
            m_device->device().destroyCommandPool(m_pool);
        }

        void uploadTexture(RHITexture* texture, std::span<const std::byte> data, const TextureSubresource& subresource) override
        {
            uint64_t size = data.size_bytes();
            if (m_currentOffset + size > m_stagingSize) {
                flush();
            }

            m_stagingBuffer->uploadData(data, m_currentOffset);

            auto *vkTex = rhi_cast<VulkanRHITexture>(texture);

            vkTex->transitionLayout(static_cast<VkImageLayout>(vk::ImageLayout::eTransferDstOptimal), m_cmd);

            vk::BufferImageCopy region{};
            region.bufferOffset = m_currentOffset;
            region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            region.imageSubresource.mipLevel = subresource.mipLevel;
            region.imageSubresource.baseArrayLayer = subresource.arrayLayer;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = VulkanUtils::toVkExtent3D(vkTex->extent());
            region.imageExtent.width =
                std::max(1U, region.imageExtent.width >> subresource.mipLevel);
            region.imageExtent.height =
                std::max(1U, region.imageExtent.height >> subresource.mipLevel);
            region.imageExtent.depth =
                std::max(1U, region.imageExtent.depth >> subresource.mipLevel);

            m_cmd.copyBufferToImage(
                rhi_cast<VulkanRHIBuffer>(m_stagingBuffer.get())
                    ->buffer(),
                vk::Image(vkTex->imageHandle()), vk::ImageLayout::eTransferDstOptimal, 1,
                &region);

            vkTex->transitionLayout(static_cast<VkImageLayout>(vk::ImageLayout::eShaderReadOnlyOptimal), m_cmd);

            m_currentOffset = (m_currentOffset + size + 15) & ~15;
        }

        void uploadBuffer(RHIBuffer* buffer, std::span<const std::byte> data, uint64_t offset) override
        {
            uint64_t size = data.size_bytes();
            if (m_currentOffset + size > m_stagingSize) {
                flush();
            }

            m_stagingBuffer->uploadData(data, m_currentOffset);

            auto *vkBuf = rhi_cast<VulkanRHIBuffer>(buffer);
            vk::BufferCopy region{};
            region.srcOffset = m_currentOffset;
            region.dstOffset = offset;
            region.size = size;

            m_cmd.copyBuffer(
                rhi_cast<VulkanRHIBuffer>(m_stagingBuffer.get())
                    ->buffer(),
                vkBuf->buffer(), 1, &region);

            m_currentOffset = (m_currentOffset + size + 15) & ~15;
        }

        void flush() override
        {
          if (m_currentOffset == 0) {
            return;
          }

            m_cmd.end();

            vk::SubmitInfo submitInfo{};
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &m_cmd;

            m_device->queueSubmit(m_device->graphicsQueue(), submitInfo, m_fence);

            auto result = m_device->device().waitForFences(1, &m_fence, VK_TRUE, UINT64_MAX);
            if (result != vk::Result::eSuccess) {
                core::Logger::RHI.error("Failed to wait for upload fence");
            }

            if (m_device->device().resetFences(1, &m_fence) != vk::Result::eSuccess) {
                core::Logger::RHI.error("Failed to reset upload fence");
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

    uint32_t VulkanRHIDevice::getMaxUsableSampleCount() const
    {
        return m_physicalDevice->capabilities().maxCombinedSampleCount;
    }

    BindlessManager* VulkanRHIDevice::getBindlessManager()
    {
        return m_bindlessManager.get();
    }

    GPUTimeQueriesManager* VulkanRHIDevice::gpuProfiler()
    {
        return m_gpuProfiler.get();
    }

    void VulkanRHIDevice::clearPipelineCache()
    {
        if (!m_device) {
            return;
        }

        waitIdle();

        if (m_pipelineCache) {
            m_device.destroyPipelineCache(m_pipelineCache);
            m_pipelineCache = nullptr;
        }

        std::error_code ec;
        std::filesystem::remove("pnkr_pipeline_cache.bin", ec);

        vk::PipelineCacheCreateInfo cacheInfo{};
        m_pipelineCache = m_device.createPipelineCache(cacheInfo);
        if (!m_pipelineCache) {
            core::Logger::RHI.warn("Failed to recreate pipeline cache, using null cache");
            return;
        }

        core::Logger::RHI.info("Vulkan Pipeline Cache cleared.");
    }

    size_t VulkanRHIDevice::getPipelineCacheSize() const
    {
        if (!m_pipelineCache) {
            return 0;
        }

        size_t size = 0;
        if (m_device.getPipelineCacheData(m_pipelineCache, &size, nullptr) != vk::Result::eSuccess) {
            return 0;
        }
        return size;
    }

    RHIDescriptorSet* VulkanRHIDevice::getBindlessDescriptorSet()
    {
        return m_bindlessManager->getDescriptorSet();
    }

    RHIDescriptorSetLayout* VulkanRHIDevice::getBindlessDescriptorSetLayout()
    {
        return m_bindlessManager->getDescriptorSetLayout();
    }

    void VulkanRHIDevice::enqueueDeletion(std::function<void()>&& deleteFn)
    {
      std::scoped_lock lock(m_deletionMutex);
      m_deletionQueue.push_back(
          {.frameIndex = m_frameCounter, .deleteFn = std::move(deleteFn)});
    }

    void VulkanRHIDevice::processDeletionQueue()
    {
        uint64_t completedFrame = getCompletedFrame();

        while (true)
        {
            std::function<void()> fn;
            {
              std::scoped_lock lock(m_deletionMutex);
              if (m_deletionQueue.empty() ||
                  m_deletionQueue.front().frameIndex > completedFrame) {
                break;
              }
                fn = std::move(m_deletionQueue.front().deleteFn);
                m_deletionQueue.pop_front();
            }
            if (fn) {
              fn();
            }
        }
    }

    void VulkanRHIDevice::trackObject(vk::ObjectType type, uint64_t handle, std::string_view name)
    {
        if (handle == 0) {
            return;
        }

        TrackedVulkanObject tracked{};
        tracked.type = type;
        tracked.name = std::string(name);
        tracked.trace = cpptrace::generate_trace(2).to_string();

        core::Logger::RHI.trace("Tracking Object: Handle={:#x}, Type={}, Name='{}'", handle, vk::to_string(type), name);

        std::scoped_lock lock(m_objectTraceMutex);
        m_objectTraces[handle] = std::move(tracked);
    }

    void VulkanRHIDevice::untrackObject(uint64_t handle)
    {
        if (handle == 0) {
            return;
        }

        core::Logger::RHI.trace("Untracking Object: Handle={:#x}", handle);

        std::scoped_lock lock(m_objectTraceMutex);
        m_objectTraces.erase(handle);
    }

    bool VulkanRHIDevice::tryGetObjectTrace(uint64_t handle, TrackedVulkanObject& out) const
    {
        if (handle == 0) {
            return false;
        }

        std::scoped_lock lock(m_objectTraceMutex);
        auto it = m_objectTraces.find(handle);
        if (it == m_objectTraces.end()) {
            return false;
        }
        out = it->second;
        return true;
    }

    void VulkanRHIDevice::setCheckpoint(vk::CommandBuffer cmd, const char* name)
    {
        if (VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdSetCheckpointNV)
        {
            VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdSetCheckpointNV(cmd, (void*)name);
        }
    }

    void VulkanRHIDevice::auditBDA([[maybe_unused]] uint64_t address, [[maybe_unused]] const char* context)
    {
#ifdef PNKR_DEBUG
      if (address == 0) {
        return;
      }

        if (m_bdaRegistry)
        {
          const auto *info = m_bdaRegistry->findAllocation(address);
          if (info == nullptr) {
            core::Logger::RHI.critical(
                "BDA FAULT: Address {} ({}) not found in registry!", address,
                context);
          } else if (!info->isAlive) {
            core::Logger::RHI.critical(
                "BDA FAULT: Address {} ({}) used after free! Freed at frame {}",
                address, context, info->frameFreed);
          }
        }
#endif
    }

    void VulkanRHIDevice::submitComputeCommands(
        RHICommandList* commandBuffer,
        bool waitForPreviousCompute,
        bool signalGraphicsQueue)
    {
        auto* vkCmdBuffer = rhi_cast<VulkanRHICommandBuffer>(commandBuffer);

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        auto cmdBuf = vkCmdBuffer->commandBuffer();
        submitInfo.pCommandBuffers = &cmdBuf;

        vk::TimelineSemaphoreSubmitInfo timelineInfo{};

        std::vector<vk::Semaphore> waitSems;
        std::vector<vk::PipelineStageFlags> waitStages;
        std::vector<uint64_t> waitValues;

        std::vector<vk::Semaphore> signalSems;
        std::vector<uint64_t> signalValues;

        if (waitForPreviousCompute)
        {
            uint64_t lastValue = m_computeSemaphoreValue.load();
            if (lastValue > 0)
            {
                waitSems.push_back(m_computeTimelineSemaphore);
                waitStages.emplace_back(
                    vk::PipelineStageFlagBits::eComputeShader);
                waitValues.push_back(lastValue);
            }
        }

        submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSems.size());
        submitInfo.pWaitSemaphores = waitSems.data();
        submitInfo.pWaitDstStageMask = waitStages.data();
        timelineInfo.waitSemaphoreValueCount = static_cast<uint32_t>(waitValues.size());
        timelineInfo.pWaitSemaphoreValues = waitValues.data();

        uint64_t nextValue = m_computeSemaphoreValue.fetch_add(1) + 1;
        signalSems.push_back(m_computeTimelineSemaphore);
        signalValues.push_back(nextValue);

        if (signalGraphicsQueue)
        {
        }

        submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSems.size());
        submitInfo.pSignalSemaphores = signalSems.data();
        timelineInfo.signalSemaphoreValueCount = static_cast<uint32_t>(signalValues.size());
        timelineInfo.pSignalSemaphoreValues = signalValues.data();

        submitInfo.pNext = &timelineInfo;

        {
             queueSubmit(m_computeQueue, submitInfo, nullptr);
        }
    }

    void VulkanRHIDevice::reportGpuFault()
    {
#ifdef PNKR_AFTERMATH_ENABLED
      if (AftermathIntegration::isEnabled()) {
        GFSDK_Aftermath_CrashDump_Status status =
            GFSDK_Aftermath_CrashDump_Status_Unknown;
        GFSDK_Aftermath_GetCrashDumpStatus(&status);

        if (status != GFSDK_Aftermath_CrashDump_Status_NotStarted) {
          core::Logger::RHI.critical(
              "Aftermath: Waiting for crash dump generation...");

          auto start = std::chrono::steady_clock::now();
          while (status !=
                     GFSDK_Aftermath_CrashDump_Status_CollectingDataFailed &&
                 status != GFSDK_Aftermath_CrashDump_Status_Finished) {
            GFSDK_Aftermath_GetCrashDumpStatus(&status);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            if (std::chrono::steady_clock::now() - start >
                std::chrono::seconds(3)) {
              break;
            }
          }

          if (status == GFSDK_Aftermath_CrashDump_Status_Finished) {
            core::Logger::RHI.critical("Aftermath: Crash dump finished.");
          } else {
            core::Logger::RHI.critical(
                "Aftermath: Crash dump failed or timed out.");
          }
        }
      }
#endif

        auto getFaultInfo = (PFN_vkGetDeviceFaultInfoEXT)m_device.getProcAddr("vkGetDeviceFaultInfoEXT");
        if (getFaultInfo == nullptr) {
          core::Logger::RHI.warn(
              "VK_EXT_device_fault is enabled but function pointer not found.");
          return;
        }

        std::vector<VkDeviceFaultAddressInfoEXT> addressInfos(8);
        std::vector<VkDeviceFaultVendorInfoEXT> vendorInfos(8);

        VkDeviceFaultInfoEXT faultInfo = {};
        faultInfo.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT;
        faultInfo.pAddressInfos = addressInfos.data();
        faultInfo.pVendorInfos = vendorInfos.data();

        VkResult res = getFaultInfo(static_cast<VkDevice>(m_device), nullptr, &faultInfo);

        if (res != VK_SUCCESS)
        {
            core::Logger::RHI.critical("GPU Hang detected, but GetDeviceFaultInfoEXT failed with {}", static_cast<int>(res));
            return;
        }

        core::Logger::RHI.critical("=== GPU FAULT DETECTED - VK_ERROR_DEVICE_LOST ===");

        core::Logger::RHI.critical("Description: {}", faultInfo.description[0] != '\0' ? faultInfo.description : "No description provided");

        uint32_t faultCount = 0;
        for (uint32_t i = 0; i < 8; ++i)
        {
            const auto& addrInfo = addressInfos[i];
            if (addrInfo.reportedAddress == 0) {
              continue;
            }

            faultCount++;
            uint64_t faultedAddr = addrInfo.reportedAddress;

            core::Logger::RHI.critical("  [{:#x}] Address: {:#x} | Precision: {}",
                i, faultedAddr, static_cast<int>(addrInfo.addressPrecision));

            if (m_bdaRegistry)
            {
              const auto *alloc = m_bdaRegistry->findAllocation(faultedAddr);
              if (alloc != nullptr) {
                core::Logger::RHI.critical(
                    "    MATCH: Buffer '{}' <{:#x} - {:#x}> ({} bytes)",
                    alloc->debugName, alloc->baseAddress,
                    alloc->baseAddress + alloc->size, alloc->size);
                core::Logger::RHI.critical(
                    "    Status: Created at frame {}, Alive: {}",
                    alloc->frameCreated, alloc->isAlive ? "YES" : "NO (FREED)");

                if (!alloc->isAlive) {
                  core::Logger::RHI.critical(
                      "    *** USE-AFTER-FREE DETECTED ***");
                  core::Logger::RHI.critical("    Buffer was freed at frame {}",
                                             alloc->frameFreed);
                }
              } else {
                core::Logger::RHI.critical(
                    "    UNRESOLVED: Address not found in BDA registry!");
              }
            }

        }

        if (faultCount == 0)
        {
            core::Logger::RHI.critical("No faulted addresses provided by driver");
        }

        if (m_bdaRegistry)
        {
            core::Logger::RHI.critical("=== Current Live BDA Allocations ===");
            auto snapshot = m_bdaRegistry->snapshot();
            int liveCount = 0;

            for (const auto& range : snapshot)
            {
                if (range.alive)
                {
                    liveCount++;
                    core::Logger::RHI.critical("  Live[{}]: <{:#x} - {:#x}> ({} bytes) | Seq: {}",
                        liveCount, range.base, range.base + range.size, range.size, range.sequence);

                    for (const auto& obj : range.objects)
                    {
                        if (!obj.name.empty())
                        {
                            core::Logger::RHI.critical("    -> Object: '{}' | Type: {} | Handle: {:#x}",
                                obj.name, static_cast<int>(obj.type), obj.handle);
                        }
                    }
                }
            }

            if (liveCount == 0)
            {
                core::Logger::RHI.critical("No live BDA allocations found");
            }
        }

        core::Logger::RHI.critical("=== END GPU FAULT REPORT ===");
    }
}

