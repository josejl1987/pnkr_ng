#include "pnkr/renderer/vulkan/vulkan_pipeline.hpp"
#include "pnkr/core/logger.hpp"

#include <fstream>
#include <stdexcept>
#include <utility>

#include "pnkr/renderer/vulkan/PushConstants.h"

namespace pnkr::renderer
{
    namespace
    {
        std::vector<std::uint32_t> ReadSpirvU32(const char* path)
        {
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            if (!file.is_open())
            {
                core::Logger::error("[VulkanPipeline] Failed to open SPIR-V file: {}",
                                    path);
                throw std::runtime_error("Failed to open SPIR-V file");
            }

            const std::streamsize byteSize = file.tellg();
            if (byteSize <= 0 || (byteSize % 4) != 0)
            {
                core::Logger::error(
                    "[VulkanPipeline] Invalid SPIR-V size ({} bytes): {}", byteSize, path);
                throw std::runtime_error("Invalid SPIR-V file size");
            }

            std::vector<std::uint32_t> words(static_cast<size_t>(byteSize / 4));
            file.seekg(0);
            file.read(reinterpret_cast<char*>(words.data()), byteSize);

            if (!file)
            {
                core::Logger::error("[VulkanPipeline] Failed to read SPIR-V file: {}",
                                    path);
                throw std::runtime_error("Failed to read SPIR-V file");
            }

            return words;
        }

        vk::ShaderModule CreateShaderModule(vk::Device device, const char* path)
        {
            const auto code = ReadSpirvU32(path);

            vk::ShaderModuleCreateInfo smci{};
            smci.codeSize = code.size() * sizeof(std::uint32_t);
            smci.pCode = code.data();

            vk::ShaderModule module{};
            try
            {
                module = device.createShaderModule(smci);
            }
            catch (const vk::SystemError& e)
            {
                core::Logger::error(
                    "[VulkanPipeline] createShaderModule failed for {}: {}", path,
                    e.what());
                throw;
            }
            return module;
        }
    } // namespace

    VulkanPipeline::VulkanPipeline(vk::Device device,
                                   const Config& config)
        : m_config(config), m_device(device)
    {
        if (!m_device)
        {
            throw std::runtime_error("[VulkanPipeline] device is null");
        }

        m_vertexInput = config.m_vertexInput;

        core::Logger::info(
            "[VulkanPipeline] Creating pipeline (dynamic rendering), format={}",
            vk::to_string(m_colorFormat));

        createShaderModules(config);
        createPipelineLayout();
        createGraphicsPipeline(config);

        core::Logger::info("[VulkanPipeline] Pipeline created.");
    }

    void VulkanPipeline::reset() noexcept
    {
        if (!m_device)
            return;

        if (m_pipeline)
        {
            m_device.destroyPipeline(m_pipeline);
            m_pipeline = nullptr;
        }
        if (m_layout)
        {
            m_device.destroyPipelineLayout(m_layout);
            m_layout = nullptr;
        }
        if (m_frag)
        {
            m_device.destroyShaderModule(m_frag);
            m_frag = nullptr;
        }
        if (m_vert)
        {
            m_device.destroyShaderModule(m_vert);
            m_vert = nullptr;
        }
    }

    VulkanPipeline::~VulkanPipeline() { reset(); }

    VulkanPipeline::VulkanPipeline(VulkanPipeline&& other) noexcept
    {
        *this = std::move(other);
    }

    VulkanPipeline& VulkanPipeline::operator=(VulkanPipeline&& other) noexcept
    {
        if (this == &other)
            return *this;

        reset();

        m_device = std::exchange(other.m_device, {});
        m_colorFormat = std::exchange(other.m_colorFormat, vk::Format::eUndefined);
        m_vert = std::exchange(other.m_vert, {});
        m_frag = std::exchange(other.m_frag, {});
        m_layout = std::exchange(other.m_layout, {});
        m_pipeline = std::exchange(other.m_pipeline, {});

        return *this;
    }

    void VulkanPipeline::createShaderModules(const Config& config)
    {
        m_vert = CreateShaderModule(m_device, config.m_vertSpvPath.string().c_str());
        m_frag = CreateShaderModule(m_device, config.m_fragSpvPath.string().c_str());
    }

    void VulkanPipeline::createPipelineLayout()
    {
        // Push constant range
        vk::PushConstantRange pushConstantRange{};
        if (m_config.m_pushConstantSize > 0)
        {
            pushConstantRange.stageFlags = m_config.m_pushConstantStages;
            pushConstantRange.offset = 0;
            pushConstantRange.size = m_config.m_pushConstantSize;
        }

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(m_config.m_descriptorSetLayouts.size());
        layoutInfo.pSetLayouts = m_config.m_descriptorSetLayouts.data();

        if (m_config.m_pushConstantSize > 0)
        {
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushConstantRange;
        }

        m_layout = m_device.createPipelineLayout(layoutInfo);
    }


    void VulkanPipeline::createGraphicsPipeline(
        const PipelineConfig& config)
    {
        // Shader stages
        vk::PipelineShaderStageCreateInfo stages[2]{};

        stages[0].stage = vk::ShaderStageFlagBits::eVertex;
        stages[0].module = m_vert;
        stages[0].pName = "main";

        stages[1].stage = vk::ShaderStageFlagBits::eFragment;
        stages[1].module = m_frag;
        stages[1].pName = "main";

        // Input Assembly
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = config.m_topology;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // Dynamic viewport/scissor: pipeline does not bake extent
        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        vk::DynamicState dynStates[] = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };
        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynStates;

        vk::PipelineRasterizationStateCreateInfo raster{};
        raster.depthClampEnable = VK_FALSE;
        raster.rasterizerDiscardEnable = VK_FALSE;
        raster.polygonMode = vk::PolygonMode::eFill;
        raster.cullMode = config.m_cullMode;
        // NOTE: Vulkan screen-space Y is inverted vs OpenGL
        raster.frontFace = config.m_frontFace;
        raster.depthBiasEnable = VK_FALSE;
        raster.lineWidth = 1.0f;

        vk::PipelineMultisampleStateCreateInfo msaa{};
        msaa.rasterizationSamples = vk::SampleCountFlagBits::e1;
        msaa.sampleShadingEnable = VK_FALSE;

        vk::PipelineColorBlendAttachmentState blendAttach{};
        blendAttach.colorWriteMask =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

        blendAttach.blendEnable = config.m_blend.enable ? VK_TRUE : VK_FALSE;
        if (config.m_blend.enable) {
            blendAttach.srcColorBlendFactor = config.m_blend.srcColor;
            blendAttach.dstColorBlendFactor = config.m_blend.dstColor;
            blendAttach.colorBlendOp = config.m_blend.colorOp;
            blendAttach.srcAlphaBlendFactor = config.m_blend.srcAlpha;
            blendAttach.dstAlphaBlendFactor = config.m_blend.dstAlpha;
            blendAttach.alphaBlendOp = config.m_blend.alphaOp;
        }

        vk::PipelineColorBlendStateCreateInfo blend{};
        blend.logicOpEnable = VK_FALSE;
        blend.attachmentCount = 1;
        blend.pAttachments = &blendAttach;

        // Dynamic Rendering: declare attachment formats at pipeline creation time.
        vk::PipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &config.m_colorFormat;
        const bool haveDepthFormat =
            (m_config.m_depthFormat != vk::Format::eUndefined);
        renderingInfo.depthAttachmentFormat =
            haveDepthFormat ? m_config.m_depthFormat : vk::Format::eUndefined;
        renderingInfo.stencilAttachmentFormat = vk::Format::eUndefined;

        vk::GraphicsPipelineCreateInfo gpci{};
        vk::PipelineDepthStencilStateCreateInfo depth{};

        // Vertex Input

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo = {};
        vertexInputInfo.pVertexBindingDescriptions = m_vertexInput.m_bindings.data();
        vertexInputInfo.vertexBindingDescriptionCount = (uint32_t)m_vertexInput.m_bindings.size();
        vertexInputInfo.pVertexAttributeDescriptions = m_vertexInput.m_attributes.data();
        vertexInputInfo.vertexAttributeDescriptionCount = (uint32_t)m_vertexInput.m_attributes.size();

        gpci.pVertexInputState = &vertexInputInfo;

        gpci.pNext = &renderingInfo; // critical for dynamic rendering
        gpci.stageCount = 2;
        gpci.pStages = stages;
        gpci.pInputAssemblyState = &inputAssembly;
        gpci.pViewportState = &viewportState;
        gpci.pRasterizationState = &raster;
        gpci.pMultisampleState = &msaa;
        gpci.pColorBlendState = &blend;
        gpci.pDynamicState = &dynamicState;
        gpci.layout = m_layout;
        gpci.subpass = 0;


        if (haveDepthFormat)
        {
            depth.depthTestEnable = m_config.m_depth.testEnable ? VK_TRUE : VK_FALSE;
            depth.depthWriteEnable = m_config.m_depth.writeEnable ? VK_TRUE : VK_FALSE;
            depth.depthCompareOp = m_config.m_depth.compareOp;
            depth.depthBoundsTestEnable = VK_FALSE;
            depth.stencilTestEnable = VK_FALSE;
            gpci.pDepthStencilState = &depth;
        }
        else
        {
            gpci.pDepthStencilState = nullptr;
        }
        try
        {
            auto result = m_device.createGraphicsPipeline(nullptr, gpci);
            if (result.result != vk::Result::eSuccess)
            {
                core::Logger::error(
                    "[VulkanPipeline] createGraphicsPipeline failed: {}",
                    vk::to_string(result.result));
                throw std::runtime_error("createGraphicsPipeline failed");
            }
            m_pipeline = result.value;
        }
        catch (const vk::SystemError& e)
        {
            core::Logger::error(
                "[VulkanPipeline] createGraphicsPipeline threw: {}", e.what());
            throw;
        }
    }
} // namespace pnkr::renderer
