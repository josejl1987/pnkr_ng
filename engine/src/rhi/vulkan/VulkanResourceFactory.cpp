#include "rhi/vulkan/VulkanResourceFactory.hpp"
#include "rhi/vulkan/vulkan_device.hpp"
#include "rhi/vulkan/vulkan_buffer.hpp"
#include "rhi/vulkan/vulkan_texture.hpp"
#include "rhi/vulkan/vulkan_utils.hpp"
#include "rhi/vulkan/vulkan_command_buffer.hpp"
#include "rhi/vulkan/vulkan_descriptor.hpp"
#include "rhi/vulkan/vulkan_pipeline.hpp"
#include "rhi/vulkan/vulkan_sampler.hpp"
#include "rhi/vulkan/vulkan_sync.hpp"
#include "vulkan_cast.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/profiler.hpp"
#include "pnkr/core/common.hpp"

#include <format>

namespace pnkr::renderer::rhi::vulkan
{
    VulkanResourceFactory::VulkanResourceFactory(VulkanRHIDevice& device)
        : m_device(device)
    {
    }

    std::unique_ptr<RHIBuffer> VulkanResourceFactory::createBuffer(const char* name, const BufferDescriptor& desc)
    {
        PNKR_LOG_SCOPE(std::format("RHI::CreateBuffer[{}]", name ? name : "Unnamed"));
        PNKR_PROFILE_FUNCTION();

        if ((name == nullptr) || name[0] == '\0') {
            core::Logger::RHI.error("createBuffer: name is required for all buffers");
            name = "UnnamedBuffer";
        }

        BufferDescriptor finalDesc = desc;
        finalDesc.debugName = name;

        if (finalDesc.data != nullptr && finalDesc.memoryUsage == MemoryUsage::GPUOnly)
        {
            finalDesc.usage |= BufferUsage::TransferDst;
        }

        auto buf = std::make_unique<VulkanRHIBuffer>(&m_device, finalDesc);
        core::Logger::RHI.trace("Created buffer: {} ({} bytes)", name, finalDesc.size);

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

                m_device.immediateSubmit([&](RHICommandList* cmd) {
                    cmd->copyBuffer(staging.get(), buf.get(), 0, 0, finalDesc.size);
                });
            }
        }

#ifdef TRACY_ENABLE
        TracyAllocN(buf->nativeHandle(), desc.size, name);
#endif

        return buf;
    }

    std::unique_ptr<RHITexture> VulkanResourceFactory::createTexture(const char* name, const TextureDescriptor& desc)
    {
        PNKR_LOG_SCOPE(std::format("RHI::CreateTexture[{}]", name ? name : "Unnamed"));

        if ((name == nullptr) || name[0] == '\0') {
            core::Logger::RHI.error("createTexture: name is required for all textures");
            name = "UnnamedTexture";
        }

        TextureDescriptor finalDesc = desc;
        finalDesc.debugName = name;

        auto tex = std::make_unique<VulkanRHITexture>(&m_device, finalDesc);
        core::Logger::RHI.trace("Created texture: {} ({}x{} {})", name, desc.extent.width, desc.extent.height, static_cast<uint32_t>(desc.format));
        return tex;
    }

    std::unique_ptr<RHITexture> VulkanResourceFactory::createTextureView(
        const char* name,
        RHITexture* parent,
        const TextureViewDescriptor& desc)
    {
        if ((name == nullptr) || name[0] == '\0') {
            core::Logger::RHI.error("createTextureView: name is required for all texture views");
            name = "UnnamedTextureView";
        }

        PNKR_LOG_SCOPE(std::format("RHI::CreateTextureView[{}]", name ? name : "Unnamed"));
        auto* vkParent = rhi_cast<VulkanRHITexture>(parent);
        if (vkParent == nullptr) {
            return nullptr;
        }

        core::Logger::RHI.trace("Created texture view: {} from parent", name);
        return std::make_unique<VulkanRHITexture>(&m_device, vkParent, desc);
    }


    std::unique_ptr<RHISampler> VulkanResourceFactory::createSampler(
        Filter minFilter,
        Filter magFilter,
        SamplerAddressMode addressMode,
        CompareOp compareOp)
    {
        return std::make_unique<VulkanRHISampler>(&m_device, minFilter, magFilter, addressMode, compareOp);
    }

    std::unique_ptr<RHICommandBuffer> VulkanResourceFactory::createCommandBuffer(RHICommandPool* pool)
    {
        return std::make_unique<VulkanRHICommandBuffer>(&m_device, rhi_cast<VulkanRHICommandPool>(pool));
    }

    std::unique_ptr<RHICommandPool> VulkanResourceFactory::createCommandPool(const CommandPoolDescriptor& desc)
    {
        return std::make_unique<VulkanRHICommandPool>(&m_device, desc);
    }

    std::unique_ptr<RHIPipeline> VulkanResourceFactory::createGraphicsPipeline(
        const GraphicsPipelineDescriptor& desc)
    {
        return std::make_unique<VulkanRHIPipeline>(&m_device, desc);
    }

    std::unique_ptr<RHIPipeline> VulkanResourceFactory::createComputePipeline(
        const ComputePipelineDescriptor& desc)
    {
        return std::make_unique<VulkanRHIPipeline>(&m_device, desc);
    }

    std::unique_ptr<RHIDescriptorSetLayout> VulkanResourceFactory::createDescriptorSetLayout(
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

        vk::DescriptorSetLayout layout = m_device.device().createDescriptorSetLayout(layoutInfo);
        return std::make_unique<VulkanRHIDescriptorSetLayout>(&m_device, layout, desc);
    }

    std::unique_ptr<RHIDescriptorSet> VulkanResourceFactory::allocateDescriptorSet(RHIDescriptorSetLayout* layout)
    {
        auto* vkLayout = rhi_cast<VulkanRHIDescriptorSetLayout>(layout);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = m_device.descriptorPool();
        vk::DescriptorSetLayout layoutHandle = vkLayout->layout();
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layoutHandle;

        auto sets = m_device.device().allocateDescriptorSets(allocInfo);
        return std::make_unique<VulkanRHIDescriptorSet>(&m_device, vkLayout, sets[0]);
    }

    std::unique_ptr<RHIFence> VulkanResourceFactory::createFence(bool signaled)
    {
        return std::make_unique<VulkanRHIFence>(&m_device, signaled);
    }

    vk::ShaderModule VulkanResourceFactory::createShaderModule(const std::vector<uint32_t>& spirvCode)
    {
        vk::ShaderModuleCreateInfo createInfo{};
        createInfo.codeSize = spirvCode.size() * sizeof(uint32_t);
        createInfo.pCode = spirvCode.data();

        auto module = m_device.device().createShaderModule(createInfo);
        m_device.trackObject(vk::ObjectType::eShaderModule,
                    ::pnkr::util::u64(static_cast<VkShaderModule>(module)),
                    "ShaderModule");
        return module;
    }

    void VulkanResourceFactory::destroyShaderModule(vk::ShaderModule module)
    {
        m_device.untrackObject(::pnkr::util::u64(static_cast<VkShaderModule>(module)));
        m_device.device().destroyShaderModule(module);
    }
}
