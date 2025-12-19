//
// Created by Jose on 12/14/2025.
//

#ifndef PNKR_PIPELINEBUILDER_H
#define PNKR_PIPELINEBUILDER_H
#pragma once

#include "pnkr/core/Handle.h"
#include <vulkan/vulkan.hpp>
#include <string>
#include <filesystem>
#include <vector>
#include "pnkr/renderer/geometry/VertexInputDescription.h"

namespace pnkr::renderer {

class Renderer;

class PipelineBuilder {
public:
    explicit PipelineBuilder(Renderer& renderer);

    // -- Shaders --
    PipelineBuilder& setShaders(const std::filesystem::path& vertSpv, const std::filesystem::path& fragSpv);

    // -- Input Assembly --
    PipelineBuilder& setInputTopology(vk::PrimitiveTopology topology);

    // -- Rasterization --
    PipelineBuilder& setPolygonMode(vk::PolygonMode mode);
    PipelineBuilder& setCullMode(vk::CullModeFlags cullMode, vk::FrontFace frontFace = vk::FrontFace::eCounterClockwise);
    PipelineBuilder& setLineWidth(float width);

    // -- Depth / Stencil --
    PipelineBuilder& enableDepthTest(bool enableWrite, vk::CompareOp op);
    PipelineBuilder& disableDepthTest();

    // -- Blending --
    // Simple preset: Standard Alpha Blending
    PipelineBuilder& enableAlphaBlending();
    // Simple preset: Additive Blending
    PipelineBuilder& enableAdditiveBlending();
    // Default: Opaque (no blending)
    PipelineBuilder& disableBlending();

    // -- Vertex Input --
    // We can accept the raw struct or a helper enum/class later
    // For now, let's keep using the existing VertexInputDescription system
    // but maybe just pass it in.
    // Ideally, we'd have .addVertexAttribute(...) methods, but let's stick to the struct for now.
    // Forward declare VertexInputDescription if possible or include header.

    PipelineBuilder& setPushConstantSize(uint32_t size);
    PipelineBuilder& addDescriptorSetLayout(vk::DescriptorSetLayout layout);
    PipelineBuilder& setVertexInput(const VertexInputDescription& description);
    PipelineBuilder& setRenderingFormats(vk::Format color, vk::Format depth);
    PipelineBuilder& setPushConstantsShaderFlags(vk::ShaderStageFlags pushConstantsShaderStages);
    PipelineBuilder& useBindless();

    [[nodiscard]] PipelineHandle build();

private:
    Renderer& m_renderer;

    // State storage
    std::filesystem::path m_vertPath;
    std::filesystem::path m_fragPath;

    vk::PrimitiveTopology m_topology = vk::PrimitiveTopology::eTriangleList;
    vk::PolygonMode m_polygonMode = vk::PolygonMode::eFill;
    vk::CullModeFlags m_cullMode = vk::CullModeFlagBits::eBack;
    vk::FrontFace m_frontFace = vk::FrontFace::eCounterClockwise;

    vk::Format m_colorFormat;
    vk::Format m_depthFormat;

    float m_lineWidth = 1.0f;

    struct DepthState {
        bool testEnable = true;
        bool writeEnable = true;
        vk::CompareOp compareOp = vk::CompareOp::eLess;
    } m_depthState;

    struct BlendState {
        bool enable = false;
        vk::BlendFactor srcColor = vk::BlendFactor::eOne;
        vk::BlendFactor dstColor = vk::BlendFactor::eZero;
        vk::BlendOp colorOp = vk::BlendOp::eAdd;
        vk::BlendFactor srcAlpha = vk::BlendFactor::eOne;
        vk::BlendFactor dstAlpha = vk::BlendFactor::eZero;
        vk::BlendOp alphaOp = vk::BlendOp::eAdd;
    } m_blendState;

    VertexInputDescription m_vertexInput;
    uint32_t m_pushConstantSize = 0;
    std::vector<vk::DescriptorSetLayout> m_descriptorLayouts;
    vk::ShaderStageFlags m_pushConstantStages;
    bool m_useBindless{};
};

} // namespace pnkr::renderer
#endif //PNKR_PIPELINEBUILDER_H