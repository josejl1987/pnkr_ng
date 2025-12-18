#include "pnkr/rhi/vulkan/vulkan_pipeline.hpp"
#include "pnkr/rhi/vulkan/vulkan_device.hpp"
#include "pnkr/rhi/vulkan/vulkan_utils.hpp"
#include "pnkr/rhi/vulkan/vulkan_descriptor.hpp"
#include "pnkr/core/logger.hpp"

namespace pnkr::renderer::rhi::vulkan
{
    // Graphics pipeline constructor
    VulkanRHIPipeline::VulkanRHIPipeline(VulkanRHIDevice* device,
                                         const GraphicsPipelineDescriptor& desc)
        : m_device(device)
          , m_bindPoint(PipelineBindPoint::Graphics)
    {
        createGraphicsPipeline(desc);
    }

    // Compute pipeline constructor
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
            m_device->device().destroyPipeline(m_pipeline);
        }

        if (m_pipelineLayout)
        {
            m_device->device().destroyPipelineLayout(m_pipelineLayout);
        }

        cleanupShaderModules();
    }

    void VulkanRHIPipeline::createGraphicsPipeline(const GraphicsPipelineDescriptor& desc)
    {
        // Create shader modules
        std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;

        for (const auto& shaderDesc : desc.shaders)
        {
            vk::ShaderModule module = createShaderModule(shaderDesc);

            vk::PipelineShaderStageCreateInfo stageInfo{};
            vk::ShaderStageFlags flags = VulkanUtils::toVkShaderStage(shaderDesc.stage);
            // Get the first bit set
            if (flags & vk::ShaderStageFlagBits::eVertex)
                stageInfo.stage = vk::ShaderStageFlagBits::eVertex;
            else if (flags & vk::ShaderStageFlagBits::eFragment)
                stageInfo.stage = vk::ShaderStageFlagBits::eFragment;
            else if (flags & vk::ShaderStageFlagBits::eCompute)
                stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
            else if (flags & vk::ShaderStageFlagBits::eGeometry)
                stageInfo.stage = vk::ShaderStageFlagBits::eGeometry;
            stageInfo.module = module;
            stageInfo.pName = shaderDesc.entryPoint.c_str();

            shaderStages.push_back(stageInfo);
        }

        // Vertex input state
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
        vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescs.size());
        vertexInputInfo.pVertexBindingDescriptions = bindingDescs.data();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescs.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescs.data();

        // Input assembly state
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = VulkanUtils::toVkTopology(desc.topology);
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // Viewport state (dynamic)
        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        // Rasterization state
        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VulkanUtils::toVkPolygonMode(desc.rasterization.polygonMode);
        rasterizer.cullMode = VulkanUtils::toVkCullMode(desc.rasterization.cullMode);
        rasterizer.frontFace = desc.rasterization.frontFaceCCW
                                   ? vk::FrontFace::eCounterClockwise
                                   : vk::FrontFace::eClockwise;
        rasterizer.depthBiasEnable = VK_FALSE;
        rasterizer.lineWidth = desc.rasterization.lineWidth;

        // Multisample state
        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        // Depth/stencil state
        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = desc.depthStencil.depthTestEnable;
        depthStencil.depthWriteEnable = desc.depthStencil.depthWriteEnable;
        depthStencil.depthCompareOp = VulkanUtils::toVkCompareOp(desc.depthStencil.depthCompareOp);
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = desc.depthStencil.stencilTestEnable;

        // Color blend state
        std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;
        for (const auto& attachment : desc.blend.attachments)
        {
            vk::PipelineColorBlendAttachmentState blendAttachment{};
            blendAttachment.blendEnable = attachment.blendEnable;
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
        colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
        colorBlending.pAttachments = colorBlendAttachments.data();

        // Dynamic state
        std::vector dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };

        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        // Create descriptor set layouts
        createDescriptorSetLayouts(desc.descriptorSets);

        // Create pipeline layout
        createPipelineLayout(desc.pushConstants);

        // Dynamic rendering (Vulkan 1.3)
        std::vector<vk::Format> colorFormats;
        for (auto format : desc.colorFormats)
        {
            colorFormats.push_back(VulkanUtils::toVkFormat(format));
        }

        vk::PipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
        renderingInfo.pColorAttachmentFormats = colorFormats.data();
        renderingInfo.depthAttachmentFormat = VulkanUtils::toVkFormat(desc.depthFormat);

        // Create graphics pipeline
        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_pipelineLayout;
        pipelineInfo.renderPass = nullptr; // Using dynamic rendering
        pipelineInfo.subpass = 0;
        pipelineInfo.pNext = &renderingInfo;

        auto result = m_device->device().createGraphicsPipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            core::Logger::error("Failed to create graphics pipeline: {}", vk::to_string(result.result));
            throw std::runtime_error("Graphics pipeline creation failed");
        }

        m_pipeline = result.value;

        // Set debug name
        if (desc.debugName)
        {
            vk::DebugUtilsObjectNameInfoEXT nameInfo{};
            nameInfo.objectType = vk::ObjectType::ePipeline;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(static_cast<VkPipeline>(m_pipeline));
            nameInfo.pObjectName = desc.debugName;


            m_device->device().setDebugUtilsObjectNameEXT(nameInfo);
        }
    }

    void VulkanRHIPipeline::createComputePipeline(const ComputePipelineDescriptor& desc)
    {
        // Create shader module
        vk::ShaderModule module = createShaderModule(desc.shader);

        vk::PipelineShaderStageCreateInfo shaderStage{};
        shaderStage.stage = vk::ShaderStageFlagBits::eCompute;
        shaderStage.module = module;
        shaderStage.pName = desc.shader.entryPoint.c_str();

        // Create descriptor set layouts
        createDescriptorSetLayouts(desc.descriptorSets);

        // Create pipeline layout
        createPipelineLayout(desc.pushConstants);

        // Create compute pipeline
        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = shaderStage;
        pipelineInfo.layout = m_pipelineLayout;

        auto result = m_device->device().createComputePipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            core::Logger::error("Failed to create compute pipeline: {}", vk::to_string(result.result));
            throw std::runtime_error("Compute pipeline creation failed");
        }

        m_pipeline = result.value;

        // Set debug name
        if (desc.debugName)
        {
            vk::DebugUtilsObjectNameInfoEXT nameInfo{};
            nameInfo.objectType = vk::ObjectType::ePipeline;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(static_cast<VkPipeline>(m_pipeline));
            nameInfo.pObjectName = desc.debugName;


            m_device->device().setDebugUtilsObjectNameEXT(nameInfo);
        }
    }

    void VulkanRHIPipeline::createDescriptorSetLayouts(const std::vector<DescriptorSetLayout>& layouts)
    {
        for (size_t i = 0; i < layouts.size(); ++i)
        {
            const auto& setLayout = layouts[i];

            // FIX: If this is Set 1 (Bindless), use the global layout from the device
            // This avoids mismatch errors where Reflection thinks size is 1 but actual set is 200k.
            // Also ensures UpdateAfterBind flags are present.
            if (i == 1 && m_device->getBindlessDescriptorSetLayout())
            {
                auto* bindlessLayout = static_cast<VulkanRHIDescriptorSetLayout*>(m_device->getBindlessDescriptorSetLayout());
                m_descriptorSetLayouts.push_back(
                    std::make_unique<VulkanRHIDescriptorSetLayout>(
                        m_device, bindlessLayout->layout(), setLayout, false /* don't own */)
                );
                continue;
            }

            std::vector<vk::DescriptorSetLayoutBinding> bindings;

            for (const auto& binding : setLayout.bindings)
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

            vk::DescriptorSetLayout layout = m_device->device().createDescriptorSetLayout(layoutInfo);
            m_descriptorSetLayouts.push_back(
                std::make_unique<VulkanRHIDescriptorSetLayout>(m_device, layout, setLayout));
        }
    }

    void VulkanRHIPipeline::createPipelineLayout(const std::vector<PushConstantRange>& pushConstants)
    {
        // Convert push constant ranges
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

        layoutInfo.setLayoutCount = static_cast<uint32_t>(vkLayouts.size());
        layoutInfo.pSetLayouts = vkLayouts.data();
        layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(vkPushConstants.size());
        layoutInfo.pPushConstantRanges = vkPushConstants.data();

        m_pipelineLayout = m_device->device().createPipelineLayout(layoutInfo);
    }

    vk::ShaderModule VulkanRHIPipeline::createShaderModule(const ShaderModuleDescriptor& desc)
    {
        vk::ShaderModuleCreateInfo createInfo{};
        createInfo.codeSize = desc.spirvCode.size() * sizeof(uint32_t);
        createInfo.pCode = desc.spirvCode.data();

        vk::ShaderModule module = m_device->device().createShaderModule(createInfo);
        m_shaderModules.push_back(module);

        return module;
    }

    void VulkanRHIPipeline::cleanupShaderModules()
    {
        for (auto module : m_shaderModules)
        {
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
        return static_cast<uint32_t>(m_descriptorSetLayouts.size());
    }
} // namespace pnkr::renderer::rhi::vulkan
