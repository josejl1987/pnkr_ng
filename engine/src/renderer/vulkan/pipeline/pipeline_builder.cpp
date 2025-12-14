//
// Created by Jose on 12/14/2025.
//

#include "pnkr/renderer/vulkan/pipeline/PipelineBuilder.h"
#include "pnkr/renderer/renderer.hpp"
#include "pnkr/renderer/vulkan/pipeline/PipelineConfig.h" // Reuse existing config struct internally if needed

namespace pnkr::renderer
{
    PipelineBuilder::PipelineBuilder(Renderer& renderer) : m_renderer(renderer)
    {
        m_colorFormat = m_renderer.getDrawColorFormat();
        m_depthFormat = m_renderer.getDrawDepthFormat();
    }

    PipelineBuilder& PipelineBuilder::setRenderingFormats(vk::Format color, vk::Format depth)
    {
        m_colorFormat = color;
        m_depthFormat = depth;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setPushConstantsShaderFlags(vk::ShaderStageFlags pushConstantsShaderStages)
    {
        m_pushConstantStages = pushConstantsShaderStages;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setShaders(const std::filesystem::path& vertSpv,
                                                 const std::filesystem::path& fragSpv)
    {
        m_vertPath = vertSpv;
        m_fragPath = fragSpv;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setInputTopology(vk::PrimitiveTopology topology)
    {
        m_topology = topology;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setPolygonMode(vk::PolygonMode mode)
    {
        m_polygonMode = mode;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setCullMode(vk::CullModeFlags cullMode, vk::FrontFace frontFace)
    {
        m_cullMode = cullMode;
        m_frontFace = frontFace;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setLineWidth(float width)
    {
        m_lineWidth = width;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::enableDepthTest(bool enableWrite, vk::CompareOp op)
    {
        m_depthState.testEnable = true;
        m_depthState.writeEnable = enableWrite;
        m_depthState.compareOp = op;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::disableDepthTest()
    {
        m_depthState.testEnable = false;
        m_depthState.writeEnable = false;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::enableAlphaBlending()
    {
        m_blendState.enable = true;
        m_blendState.srcColor = vk::BlendFactor::eSrcAlpha;
        m_blendState.dstColor = vk::BlendFactor::eOneMinusSrcAlpha;
        m_blendState.colorOp = vk::BlendOp::eAdd;
        m_blendState.srcAlpha = vk::BlendFactor::eOne;
        m_blendState.dstAlpha = vk::BlendFactor::eZero;
        m_blendState.alphaOp = vk::BlendOp::eAdd;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::enableAdditiveBlending()
    {
        m_blendState.enable = true;
        m_blendState.srcColor = vk::BlendFactor::eOne;
        m_blendState.dstColor = vk::BlendFactor::eOne;
        m_blendState.colorOp = vk::BlendOp::eAdd;
        m_blendState.srcAlpha = vk::BlendFactor::eOne;
        m_blendState.dstAlpha = vk::BlendFactor::eZero;
        m_blendState.alphaOp = vk::BlendOp::eAdd;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::disableBlending()
    {
        m_blendState.enable = false;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setPushConstantSize(uint32_t size)
    {
        m_pushConstantSize = size;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::addDescriptorSetLayout(vk::DescriptorSetLayout layout) {
        m_descriptorLayouts.push_back(layout);
        return *this;
    }

    PipelineHandle PipelineBuilder::build()
    {
        // Convert Builder state to the Config struct the Renderer expects

        PipelineConfig cfg{};
        cfg.m_vertSpvPath = m_vertPath;
        cfg.m_fragSpvPath = m_fragPath;

        // Hardcoded vertex input for now (standard mesh), or expose setter
        cfg.m_vertexInput = VertexInputDescription::VertexInputCube();

        cfg.m_cullMode = m_cullMode;
        cfg.m_frontFace = m_frontFace;

        cfg.m_colorFormat = m_colorFormat;
        cfg.m_depthFormat = m_depthFormat;

        cfg.m_depth.testEnable = m_depthState.testEnable;
        cfg.m_depth.writeEnable = m_depthState.writeEnable;
        cfg.m_depth.compareOp = m_depthState.compareOp;
        cfg.m_descriptorSetLayouts = m_descriptorLayouts;

        cfg.m_pushConstantSize = m_pushConstantSize; // Add this to Config too
        cfg.m_pushConstantStages = m_pushConstantStages;
        return m_renderer.createPipeline(cfg);
    }
} // namespace pnkr::renderer
