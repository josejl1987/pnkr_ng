#include "pnkr/renderer/debug/LineCanvas3D.hpp"
#include "pnkr/rhi/rhi_device.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/rhi/rhi_pipeline.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/renderer/passes/RenderPassUtils.hpp"
#include "pnkr/renderer/gpu_shared/LineCanvasShared.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/constants.hpp>

using namespace pnkr::util;

namespace pnkr::renderer::debug
{
    namespace
    {
    uint32_t packUnorm4x8LineCanvas(const glm::vec3 &color) {
      auto toByte = [](float v) -> uint32_t {
        v = std::clamp(v, 0.0F, 1.0F);
        return static_cast<uint32_t>(std::lround(v * 255.0F));
      };

      uint32_t r = toByte(color.r);
      uint32_t g = toByte(color.g);
      uint32_t b = toByte(color.b);
      uint32_t a = 255U;
      return r | (g << 8U) | (b << 16U) | (a << 24U);
    }
    }

    LineCanvas3D::LineCanvas3D() = default;
    LineCanvas3D::~LineCanvas3D() = default;

    void LineCanvas3D::initialize(RHIRenderer* renderer)
    {
        if (m_initialized || (renderer == nullptr))
        {
            return;
        }

        m_renderer = renderer;

        m_vertices.reserve(m_maxVertices);

        createPipeline();
        createVertexBuffer();

        m_initialized = true;
    }

    void LineCanvas3D::beginFrame()
    {
        if (!m_initialized)
        {
            return;
        }

        m_frameActive = true;
        m_vertices.clear();
    }

    void LineCanvas3D::endFrame()
    {
        if (!m_initialized || !m_frameActive)
        {
            return;
        }

        m_frameActive = false;
    }

    void LineCanvas3D::line(const glm::vec3& start, const glm::vec3& end, const glm::vec3& color)
    {

        if (!m_frameActive || m_vertices.size() + 2 > m_maxVertices)
        {
            return;
        }

        m_vertices.push_back(
            {.position = start, .color = packUnorm4x8LineCanvas(color)});
        m_vertices.push_back(
            {.position = end, .color = packUnorm4x8LineCanvas(color)});
    }

    void LineCanvas3D::box(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color)
    {
        utils::box(*this, min, max, color);
    }

    void LineCanvas3D::box(const glm::mat4& transform, const glm::vec3& size, const glm::vec3& color)
    {
        utils::box(*this, transform, size, color);
    }

    void LineCanvas3D::plane(const glm::vec3& o, const glm::vec3& v1, const glm::vec3& v2,
                             int n1, int n2, const glm::vec3& color)
    {
        utils::plane(*this, o, v1, v2, n1, n2, color);
    }

    void LineCanvas3D::frustum(const glm::mat4& viewProj, const glm::vec3& color)
    {
        utils::frustum(*this, viewProj, color);
    }

    void LineCanvas3D::circle(const glm::vec3& center, float radius, const glm::vec3& color, int segments)
    {
        utils::circle(*this, center, radius, glm::vec3(0, 1, 0), color, segments);
    }

    void LineCanvas3D::sphere(const glm::vec3& center, float radius, const glm::vec3& color, int segments)
    {
        utils::sphere(*this, center, radius, color, segments);
    }

    void LineCanvas3D::render(const RHIFrameContext& ctx, const glm::mat4& viewProj)
    {
        if (!m_initialized || m_vertices.empty())
        {
            return;
        }

        m_currentFrameIndex = ctx.frameIndex % kMaxFrames;

        uint64_t currentSize = m_vertices.size() * sizeof(gpu::LineVertex);

        if (!m_vertexBuffer || (m_maxVertices * sizeof(gpu::LineVertex) < currentSize))
        {

            while (m_maxVertices * sizeof(gpu::LineVertex) < currentSize)
            {
                m_maxVertices *= 2;
            }
            createVertexBuffer();
        }

        uploadVertexData();

        ctx.commandBuffer->bindPipeline(m_renderer->getPipeline(m_pipeline));

        uint64_t frameOffset = m_currentFrameIndex * (m_maxVertices * sizeof(gpu::LineVertex));
        ctx.commandBuffer->bindVertexBuffer(0, m_vertexBuffer.get(), frameOffset);

        gpu::LineCanvasConstants pc{};
        pc.viewProj = viewProj;
        ctx.commandBuffer->pushConstants(rhi::ShaderStage::Vertex, pc);

        ctx.commandBuffer->draw(static_cast<uint32_t>(m_vertices.size()), 1, 0, 0);
    }

    void LineCanvas3D::createPipeline()
    {
        using namespace passes::utils;

        std::vector<rhi::VertexInputBinding> bindings = {
            {.binding = 0, .stride = sizeof(gpu::LineVertex), .inputRate = rhi::VertexInputRate::Vertex}
        };

        std::vector<rhi::VertexInputAttribute> attributes = {
            {
                .location = 0, .binding = 0, .format = rhi::Format::R32G32B32_SFLOAT,
                .offset = offsetof(gpu::LineVertex, position), .semantic = rhi::VertexSemantic::Position
            },
            {
                .location = 1, .binding = 0, .format = rhi::Format::R32_UINT,
                .offset = offsetof(gpu::LineVertex, color), .semantic = rhi::VertexSemantic::Color
            }
        };

        std::vector<rhi::PushConstantRange> pushConstants = {
            {.stages = rhi::ShaderStage::Vertex, .offset = 0, .size = sizeof(gpu::LineCanvasConstants)}
        };

        auto shaders = loadGraphicsShaders("shaders/linecanvas.vert.spv", "shaders/linecanvas.frag.spv", "LineCanvas3D");
        if (!shaders.success)
        {
            return;
        }

        rhi::GraphicsPipelineDescriptor desc{};
        desc.shaders = {
            {.stage = rhi::ShaderStage::Vertex, .spirvCode = shaders.vertex->code(), .entryPoint = shaders.vertex->reflection().entryPoint},
            {.stage = rhi::ShaderStage::Fragment, .spirvCode = shaders.fragment->code(), .entryPoint = shaders.fragment->reflection().entryPoint}
        };
        desc.vertexBindings = bindings;
        desc.vertexAttributes = attributes;
        desc.topology = rhi::PrimitiveTopology::LineList;

        desc.rasterization.polygonMode = rhi::PolygonMode::Fill;
        desc.rasterization.cullMode = rhi::CullMode::None;
        desc.rasterization.lineWidth = 1.0F;

        desc.depthStencil.depthTestEnable = true;
        desc.depthStencil.depthWriteEnable = false;
        desc.depthStencil.depthCompareOp = rhi::CompareOp::LessOrEqual;

        desc.blend.attachments = {{.blendEnable = false}};

        desc.colorFormats = {m_renderer->getDrawColorFormat()};
        desc.depthFormat = m_renderer->getDrawDepthFormat();

        desc.dynamicStates = {rhi::DynamicState::Viewport, rhi::DynamicState::Scissor};
        desc.pushConstants = pushConstants;
        desc.debugName = "LineCanvas3D Pipeline";

        m_pipeline = m_renderer->createGraphicsPipeline(desc);
    }

    void LineCanvas3D::createVertexBuffer()
    {

        if (m_vertexBuffer)
        {
            m_vertexBuffer.reset();
        }

        m_vertexBuffer = m_renderer->device()->createBuffer({
            .size = u64(m_maxVertices * kMaxFrames) * sizeof(gpu::LineVertex),
            .usage = rhi::BufferUsage::VertexBuffer,
            .memoryUsage = rhi::MemoryUsage::CPUToGPU,
            .debugName = "LineCanvas3D_VertexBuffer"
        });
    }

    void LineCanvas3D::uploadVertexData()
    {
        if (m_vertices.empty())
        {
            return;
        }

        uint64_t frameOffset = m_currentFrameIndex * (m_maxVertices * sizeof(gpu::LineVertex));
        uint64_t uploadSize = m_vertices.size() * sizeof(gpu::LineVertex);

        m_vertexBuffer->uploadData(std::span<const std::byte>(reinterpret_cast<const std::byte*>(m_vertices.data()), uploadSize), frameOffset);
    }
}
