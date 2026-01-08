#include "rhi/vulkan/vulkan_pipeline.hpp"

#include "pnkr/core/common.hpp"
#include "pnkr/core/logger.hpp"
#include "rhi/vulkan/vulkan_descriptor.hpp"
#include "rhi/vulkan/vulkan_device.hpp"
#include "rhi/vulkan/vulkan_utils.hpp"
#include <algorithm>
#include <cpptrace/cpptrace.hpp>
#include <unordered_map>

using namespace pnkr::util;

namespace pnkr::renderer::rhi::vulkan
{

    VulkanRHIPipeline::VulkanRHIPipeline(VulkanRHIDevice* device,
                                         const GraphicsPipelineDescriptor& desc)
        : m_device(device)
          , m_bindPoint(PipelineBindPoint::Graphics)
    {
        createGraphicsPipeline(desc);
    }

    VulkanRHIPipeline::VulkanRHIPipeline(VulkanRHIDevice* device,
                                         const ComputePipelineDescriptor& desc)
        : m_device(device)
          , m_bindPoint(PipelineBindPoint::Compute)
    {
        createComputePipeline(desc);
    }

    VulkanRHIPipeline::~VulkanRHIPipeline()
    {
        if (m_pipeline)
        {
            m_device->untrackObject(u64(static_cast<VkPipeline>(m_pipeline)));
            m_device->device().destroyPipeline(m_pipeline);
        }

        if (m_pipelineLayout)
        {
            m_device->untrackObject(u64(static_cast<VkPipelineLayout>(m_pipelineLayout)));
            m_device->device().destroyPipelineLayout(m_pipelineLayout);
        }

        cleanupShaderModules();
    }

    void VulkanRHIPipeline::createGraphicsPipeline(const GraphicsPipelineDescriptor& desc)
    {

        std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;

        for (const auto& shaderDesc : desc.shaders)
        {
            vk::ShaderModule module = createShaderModule(shaderDesc);

            vk::PipelineShaderStageCreateInfo stageInfo{};
            vk::ShaderStageFlags flags = VulkanUtils::toVkShaderStage(shaderDesc.stage);

            if (flags & vk::ShaderStageFlagBits::eVertex) {
                stageInfo.stage = vk::ShaderStageFlagBits::eVertex;
            } else if (flags & vk::ShaderStageFlagBits::eFragment) {
                stageInfo.stage = vk::ShaderStageFlagBits::eFragment;
            } else if (flags & vk::ShaderStageFlagBits::eCompute) {
                stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
            } else if (flags & vk::ShaderStageFlagBits::eGeometry) {
                stageInfo.stage = vk::ShaderStageFlagBits::eGeometry;
            } else if (flags & vk::ShaderStageFlagBits::eTessellationControl) {
                stageInfo.stage = vk::ShaderStageFlagBits::eTessellationControl;
            } else if (flags & vk::ShaderStageFlagBits::eTessellationEvaluation) {
                stageInfo.stage = vk::ShaderStageFlagBits::eTessellationEvaluation;
            }
            stageInfo.module = module;
            stageInfo.pName = shaderDesc.entryPoint.c_str();

            shaderStages.push_back(stageInfo);
        }

        std::vector<vk::VertexInputBindingDescription> bindingDescs;
        for (const auto& binding : desc.vertexBindings)
        {
            vk::VertexInputBindingDescription bindingDesc{};
            bindingDesc.binding = binding.binding;
            bindingDesc.stride = binding.stride;
            bindingDesc.inputRate = binding.inputRate == VertexInputRate::Vertex
                                        ? vk::VertexInputRate::eVertex
                                        : vk::VertexInputRate::eInstance;
            bindingDescs.push_back(bindingDesc);
        }

        std::vector<vk::VertexInputAttributeDescription> attributeDescs;
        for (const auto& attribute : desc.vertexAttributes)
        {
            vk::VertexInputAttributeDescription attrDesc{};
            attrDesc.location = attribute.location;
            attrDesc.binding = attribute.binding;
            attrDesc.format = VulkanUtils::toVkFormat(attribute.format);
            attrDesc.offset = attribute.offset;
            attributeDescs.push_back(attrDesc);
        }

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.vertexBindingDescriptionCount = u32(bindingDescs.size());
        vertexInputInfo.pVertexBindingDescriptions = bindingDescs.data();
        vertexInputInfo.vertexAttributeDescriptionCount = u32(attributeDescs.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescs.data();

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = VulkanUtils::toVkTopology(desc.topology);
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        vk::PipelineTessellationStateCreateInfo tessellationInfo{};
        if (desc.topology == PrimitiveTopology::PatchList) {
            tessellationInfo.patchControlPoints = desc.patchControlPoints;
        }

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VulkanUtils::toVkPolygonMode(desc.rasterization.polygonMode);
        rasterizer.cullMode = VulkanUtils::toVkCullMode(desc.rasterization.cullMode);
        rasterizer.frontFace = desc.rasterization.frontFaceCCW
                                   ? vk::FrontFace::eCounterClockwise
                                   : vk::FrontFace::eClockwise;
        rasterizer.depthBiasEnable = desc.rasterization.depthBiasEnable ? VK_TRUE : VK_FALSE;
        rasterizer.lineWidth = desc.rasterization.lineWidth;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.rasterizationSamples = VulkanUtils::toVkSampleCount(desc.multisample.rasterizationSamples);
        multisampling.sampleShadingEnable = desc.multisample.sampleShadingEnable ? VK_TRUE : VK_FALSE;
        multisampling.minSampleShading = desc.multisample.minSampleShading;
        multisampling.pSampleMask = nullptr;
        multisampling.alphaToCoverageEnable = VK_FALSE;
        multisampling.alphaToOneEnable = VK_FALSE;

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable =
            static_cast<vk::Bool32>(desc.depthStencil.depthTestEnable);
        depthStencil.depthWriteEnable =
            static_cast<vk::Bool32>(desc.depthStencil.depthWriteEnable);
        depthStencil.depthCompareOp = VulkanUtils::toVkCompareOp(desc.depthStencil.depthCompareOp);
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable =
            static_cast<vk::Bool32>(desc.depthStencil.stencilTestEnable);

        std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;
        for (const auto& attachment : desc.blend.attachments)
        {
            vk::PipelineColorBlendAttachmentState blendAttachment{};
            blendAttachment.blendEnable =
                static_cast<vk::Bool32>(attachment.blendEnable);
            blendAttachment.srcColorBlendFactor = VulkanUtils::toVkBlendFactor(attachment.srcColorBlendFactor);
            blendAttachment.dstColorBlendFactor = VulkanUtils::toVkBlendFactor(attachment.dstColorBlendFactor);
            blendAttachment.colorBlendOp = VulkanUtils::toVkBlendOp(attachment.colorBlendOp);
            blendAttachment.srcAlphaBlendFactor = VulkanUtils::toVkBlendFactor(attachment.srcAlphaBlendFactor);
            blendAttachment.dstAlphaBlendFactor = VulkanUtils::toVkBlendFactor(attachment.dstAlphaBlendFactor);
            blendAttachment.alphaBlendOp = VulkanUtils::toVkBlendOp(attachment.alphaBlendOp);
            blendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
                vk::ColorComponentFlagBits::eG |
                vk::ColorComponentFlagBits::eB |
                vk::ColorComponentFlagBits::eA;
            colorBlendAttachments.push_back(blendAttachment);
        }

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = u32(colorBlendAttachments.size());
        colorBlending.pAttachments = colorBlendAttachments.data();

        std::vector<vk::DynamicState> dynamicStates;
        dynamicStates.reserve(desc.dynamicStates.size());
        for (auto state : desc.dynamicStates) {
          dynamicStates.push_back(VulkanUtils::toVkDynamicState(state));
        }

        std::ranges::sort(dynamicStates);
        auto uniqueRange = std::ranges::unique(dynamicStates);
        dynamicStates.erase(uniqueRange.begin(), uniqueRange.end());

        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.dynamicStateCount = u32(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        createDescriptorSetLayouts(desc.descriptorSets);

        createPipelineLayout(desc.pushConstants);

        std::vector<vk::Format> colorFormats;
        colorFormats.reserve(desc.colorFormats.size());
        for (auto format : desc.colorFormats) {
          colorFormats.push_back(VulkanUtils::toVkFormat(format));
        }

        vk::PipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.colorAttachmentCount = u32(colorFormats.size());
        renderingInfo.pColorAttachmentFormats = colorFormats.data();
        renderingInfo.depthAttachmentFormat = VulkanUtils::toVkFormat(desc.depthFormat);

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = u32(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        if (desc.topology == PrimitiveTopology::PatchList) {
            pipelineInfo.pTessellationState = &tessellationInfo;
        }
        pipelineInfo.layout = m_pipelineLayout;
        pipelineInfo.renderPass = nullptr;
        pipelineInfo.subpass = 0;
        pipelineInfo.pNext = &renderingInfo;

        auto result = m_device->device().createGraphicsPipeline(m_device->getPipelineCache(), pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            core::Logger::RHI.error("Failed to create graphics pipeline: {}", vk::to_string(result.result));
            throw cpptrace::runtime_error("Graphics pipeline creation failed");
        }

        m_pipeline = result.value;

        VulkanUtils::setDebugName(m_device->device(), vk::ObjectType::ePipeline, reinterpret_cast<uint64_t>(static_cast<VkPipeline>(m_pipeline)), desc.debugName);
        m_device->trackObject(vk::ObjectType::ePipeline,
                              u64(static_cast<VkPipeline>(m_pipeline)),
                              desc.debugName);
    }

    void VulkanRHIPipeline::createComputePipeline(const ComputePipelineDescriptor& desc)
    {

        vk::ShaderModule module = createShaderModule(desc.shader);

        vk::PipelineShaderStageCreateInfo shaderStage{};
        shaderStage.stage = vk::ShaderStageFlagBits::eCompute;
        shaderStage.module = module;
        shaderStage.pName = desc.shader.entryPoint.c_str();

        createDescriptorSetLayouts(desc.descriptorSets);

        createPipelineLayout(desc.pushConstants);

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = shaderStage;
        pipelineInfo.layout = m_pipelineLayout;

        auto result = m_device->device().createComputePipeline(m_device->getPipelineCache(), pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            core::Logger::RHI.error("Failed to create compute pipeline: {}", vk::to_string(result.result));
            throw cpptrace::runtime_error("Compute pipeline creation failed");
        }

        m_pipeline = result.value;

        VulkanUtils::setDebugName(m_device->device(), vk::ObjectType::ePipeline, reinterpret_cast<uint64_t>(static_cast<VkPipeline>(m_pipeline)), desc.debugName);
        m_device->trackObject(vk::ObjectType::ePipeline,
                              u64(static_cast<VkPipeline>(m_pipeline)),
                              desc.debugName);
    }

    void VulkanRHIPipeline::createDescriptorSetLayouts(const std::vector<DescriptorSetLayout>& layouts)
    {
        const bool hasBindless = (m_device->getBindlessDescriptorSetLayout() != nullptr);
        const size_t requiredSetCount =
            hasBindless ? std::max<size_t>(layouts.size(), 2U) : layouts.size();

        auto createEmptySetLayout = [&]() -> vk::DescriptorSetLayout {
            vk::DescriptorSetLayoutCreateInfo info{};
            info.bindingCount = 0;
            info.pBindings = nullptr;
            return m_device->device().createDescriptorSetLayout(info);
        };

        const VulkanRHIDescriptorSetLayout* vkBindlessLayoutObj = nullptr;
        const DescriptorSetLayout* bindlessDescPtr = nullptr;
        if (hasBindless) {
            vkBindlessLayoutObj = dynamic_cast<VulkanRHIDescriptorSetLayout*>(m_device->getBindlessDescriptorSetLayout());
            bindlessDescPtr = &vkBindlessLayoutObj->description();
        }

        for (size_t i = 0; i < requiredSetCount; ++i)
        {
            const bool hasIncoming = (i < layouts.size());
            const DescriptorSetLayout emptyIncoming{};
            const DescriptorSetLayout& setLayout = hasIncoming ? layouts[i] : emptyIncoming;

            if (i == 1 && hasBindless)
            {

                if (hasIncoming && bindlessDescPtr != nullptr && !setLayout.bindings.empty())
                {
                    std::unordered_map<uint32_t, DescriptorType> expected;
                    expected.reserve(bindlessDescPtr->bindings.size());
                    for (const auto& b : bindlessDescPtr->bindings) {
                        expected[b.binding] = b.type;
                    }

                    for (const auto& b : setLayout.bindings)
                    {
                        auto it = expected.find(b.binding);
                        if (it == expected.end())
                        {
                            core::Logger::RHI.error(
                                "[Bindless ABI] Shader declared set=1 binding={} (type={}) but device bindless schema has no such binding.",
                                b.binding, (int)b.type);
                            continue;
                        }
                        if (it->second != b.type)
                        {
                            core::Logger::RHI.error(
                                "[Bindless ABI] Shader declared set=1 binding={} type={} but device expects type={}. "
                                "This often indicates combined image samplers (sampler2D/samplerCube) instead of separate image+sampler per bindless.glsl.",
                                b.binding, (int)b.type, (int)it->second);
                        }
                    }
                }

                m_descriptorSetLayouts.push_back(
                    std::make_unique<VulkanRHIDescriptorSetLayout>(
                        m_device,
                        vkBindlessLayoutObj->layout(),
                        *bindlessDescPtr,
                        false
                    )
                );
                continue;
            }

            std::vector<vk::DescriptorSetLayoutBinding> bindings;
            bindings.reserve(setLayout.bindings.size());

            for (const auto& binding : setLayout.bindings)
            {
                vk::DescriptorSetLayoutBinding vkBinding{};
                vkBinding.binding = binding.binding;
                vkBinding.descriptorType = VulkanUtils::toVkDescriptorType(binding.type);
                vkBinding.descriptorCount = binding.count;
                vkBinding.stageFlags = VulkanUtils::toVkShaderStage(binding.stages);
                bindings.push_back(vkBinding);
            }

            vk::DescriptorSetLayout layout{};
            if (bindings.empty())
            {
                layout = createEmptySetLayout();
            }
            else
            {
                vk::DescriptorSetLayoutCreateInfo layoutInfo{};
                layoutInfo.bindingCount = u32(bindings.size());
                layoutInfo.pBindings = bindings.data();
                layout = m_device->device().createDescriptorSetLayout(layoutInfo);
            }

            m_descriptorSetLayouts.push_back(
                std::make_unique<VulkanRHIDescriptorSetLayout>(m_device, layout, setLayout));
        }
    }

    void VulkanRHIPipeline::createPipelineLayout(const std::vector<PushConstantRange>& pushConstants)
    {

        std::vector<vk::PushConstantRange> vkPushConstants;
        for (const auto& range : pushConstants)
        {
            vk::PushConstantRange vkRange{};
            vkRange.stageFlags = VulkanUtils::toVkShaderStage(range.stages);
            vkRange.offset = range.offset;
            vkRange.size = range.size;

            vkPushConstants.push_back(vkRange);
        }

        vk::PipelineLayoutCreateInfo layoutInfo{};
        std::vector<vk::DescriptorSetLayout> vkLayouts;
        vkLayouts.reserve(m_descriptorSetLayouts.size());
        for (const auto& layout : m_descriptorSetLayouts)
        {
            vkLayouts.push_back(layout->layout());
        }

        layoutInfo.setLayoutCount = u32(vkLayouts.size());
        layoutInfo.pSetLayouts = vkLayouts.data();
        layoutInfo.pushConstantRangeCount = u32(vkPushConstants.size());
        layoutInfo.pPushConstantRanges = vkPushConstants.data();

        m_pipelineLayout = m_device->device().createPipelineLayout(layoutInfo);
        m_device->trackObject(vk::ObjectType::ePipelineLayout,
                              u64(static_cast<VkPipelineLayout>(m_pipelineLayout)),
                              "PipelineLayout");
    }

    vk::ShaderModule VulkanRHIPipeline::createShaderModule(const ShaderModuleDescriptor& desc)
    {
        vk::ShaderModuleCreateInfo createInfo{};
        createInfo.codeSize = desc.spirvCode.size() * sizeof(uint32_t);
        createInfo.pCode = desc.spirvCode.data();

        vk::ShaderModule module = m_device->device().createShaderModule(createInfo);
        m_shaderModules.push_back(module);
        m_device->trackObject(vk::ObjectType::eShaderModule,
                              u64(static_cast<VkShaderModule>(module)),
                              "ShaderModule");

        return module;
    }

    void VulkanRHIPipeline::cleanupShaderModules()
    {
        for (auto module : m_shaderModules)
        {
            m_device->untrackObject(u64(static_cast<VkShaderModule>(module)));
            m_device->device().destroyShaderModule(module);
        }
        m_shaderModules.clear();
    }

    RHIDescriptorSetLayout* VulkanRHIPipeline::descriptorSetLayout(uint32_t setIndex) const
    {
        if (setIndex >= m_descriptorSetLayouts.size())
        {
            return nullptr;
        }
        return m_descriptorSetLayouts[setIndex].get();
    }

    uint32_t VulkanRHIPipeline::descriptorSetLayoutCount() const
    {
        return u32(m_descriptorSetLayouts.size());
    }
}

