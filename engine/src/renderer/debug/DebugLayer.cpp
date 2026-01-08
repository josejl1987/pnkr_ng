#include "pnkr/renderer/debug/DebugLayer.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/rhi/rhi_device.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/rhi/rhi_pipeline.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/common.hpp"
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
    uint32_t packUnorm4x8DebugLayer(const glm::vec3 &color) {
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

    DebugLayer::DebugLayer() = default;
    DebugLayer::~DebugLayer() = default;

    void DebugLayer::initialize(RHIRenderer* renderer)
    {
        if (m_initialized || (renderer == nullptr))
        {
            return;
        }

        m_renderer = renderer;

        m_verticesPending.reserve(m_maxVertices);
        m_verticesRender.reserve(m_maxVertices);

        createPipeline();
        allocateBuffer(m_maxVertices);

        m_initialized = true;
    }

    bool DebugLayer::hasCapacity(size_t additionalVertices)
    {
        if (m_verticesPending.size() + additionalVertices > m_maxVertices)
        {
            static bool warned = false;
            if (!warned)
            {
                core::Logger::Render.warn("[DebugLayer] Max vertex capacity reached!");
                warned = true;
            }
            return false;
        }
        return true;
    }

    gpu::LineVertex* DebugLayer::appendVertices(size_t count)
    {
      if (!hasCapacity(count)) {
        return nullptr;
      }
        const size_t start = m_verticesPending.size();
        m_verticesPending.resize(start + count);
        return m_verticesPending.data() + start;
    }

    void DebugLayer::clear()
    {
        m_verticesPending.clear();
        m_verticesRender.clear();
    }

    void DebugLayer::line(const glm::vec3& start, const glm::vec3& end, const glm::vec3& color)
    {
        gpu::LineVertex* v = appendVertices(2);
        if (v == nullptr) {
          return;
        }
        uint32_t packed = packUnorm4x8DebugLayer(color);
        v[0] = {.position = start, .color = packed};
        v[1] = {.position = end, .color = packed};
    }

    void DebugLayer::box(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color)
    {
        utils::box(*this, min, max, color);
    }

    void DebugLayer::box(const glm::mat4& transform, const glm::vec3& size, const glm::vec3& color)
    {
        utils::box(*this, transform, size, color);
    }

    void DebugLayer::plane(const glm::vec3& origin, const glm::vec3& v1, const glm::vec3& v2,
                           int segments1, int segments2, const glm::vec3& color)
    {
        utils::plane(*this, origin, v1, v2, segments1, segments2, color);
    }

    void DebugLayer::frustum(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& color)
    {
        m_lastFrustumCorners.clear();
        utils::frustum(*this, view, proj, color, &m_lastFrustumCorners);
    }

    void DebugLayer::circle(const glm::vec3& center, float radius, const glm::vec3& normal, const glm::vec3& color,
                            int segments)
    {
        utils::circle(*this, center, radius, normal, color, segments);
    }

    void DebugLayer::sphere(const glm::vec3& center, float radius, const glm::vec3& color, int segments)
    {
        utils::sphere(*this, center, radius, color, segments);
    }

    void DebugLayer::render(const RHIFrameContext& ctx, const glm::mat4& viewProj)
    {
        if (!m_initialized)
        {
            return;
        }

        if (!m_verticesPending.empty())
        {
            m_verticesRender.swap(m_verticesPending);
            m_verticesPending.clear();
        }

        if (m_verticesRender.empty())
        {
            return;
        }

        uint32_t frameSlot = ctx.frameIndex % kMaxFrames;
        uint64_t offset = u64(frameSlot * m_maxVertices) * sizeof(gpu::LineVertex);

        m_vertexBuffer->uploadData(std::span<const std::byte>(reinterpret_cast<const std::byte*>(m_verticesRender.data()), m_verticesRender.size() * sizeof(gpu::LineVertex)), offset);

        auto* cmd = ctx.commandBuffer;

        cmd->setDepthTestEnable(m_depthTestEnabled);
        cmd->setDepthWriteEnable(false);
        cmd->setDepthCompareOp(rhi::CompareOp::LessOrEqual);

        cmd->bindPipeline(m_renderer->getPipeline(m_pipeline));

        cmd->bindVertexBuffer(0, m_vertexBuffer.get(), offset);

        gpu::LineCanvasConstants pc{};
        pc.viewProj = viewProj;
        cmd->pushConstants(rhi::ShaderStage::Vertex, pc);

        cmd->draw(static_cast<uint32_t>(m_verticesRender.size()));

        cmd->setDepthTestEnable(true);
        cmd->setDepthWriteEnable(true);

        m_verticesRender.clear();
    }

    void DebugLayer::createPipeline()
    {
        using namespace passes::utils;

        auto shaders = loadGraphicsShaders("shaders/linecanvas.vert.spv", "shaders/linecanvas.frag.spv", "DebugLayer");
        if (!shaders.success)
        {
            return;
        }

        rhi::GraphicsPipelineDescriptor desc{};
        desc.shaders = {
            {.stage = rhi::ShaderStage::Vertex, .spirvCode = shaders.vertex->code(), .entryPoint = shaders.vertex->reflection().entryPoint},
            {.stage = rhi::ShaderStage::Fragment, .spirvCode = shaders.fragment->code(), .entryPoint = shaders.fragment->reflection().entryPoint}
        };

        desc.vertexBindings = {{.binding = 0, .stride = sizeof(gpu::LineVertex), .inputRate = rhi::VertexInputRate::Vertex}};
        desc.vertexAttributes = {
            {
                .location = 0, .binding = 0, .format = rhi::Format::R32G32B32_SFLOAT,
                .offset = offsetof(gpu::LineVertex, position), .semantic = rhi::VertexSemantic::Position
            },
            {
                .location = 1, .binding = 0, .format = rhi::Format::R32_UINT,
                .offset = offsetof(gpu::LineVertex, color), .semantic = rhi::VertexSemantic::Color
            }
        };

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

        desc.pushConstants = {{.stages = rhi::ShaderStage::Vertex, .offset = 0, .size = sizeof(gpu::LineCanvasConstants)}};

        desc.dynamicStates = {
            rhi::DynamicState::Viewport, rhi::DynamicState::Scissor,
            rhi::DynamicState::DepthTestEnable, rhi::DynamicState::DepthWriteEnable, rhi::DynamicState::DepthCompareOp
        };
        desc.debugName = "DebugLayerPipeline";

        m_pipeline = m_renderer->createGraphicsPipeline(desc);
    }

    void DebugLayer::allocateBuffer(uint64_t vertexCount)
    {

        m_vertexBuffer = m_renderer->device()->createBuffer({
            .size = vertexCount * kMaxFrames * sizeof(gpu::LineVertex),
            .usage = rhi::BufferUsage::VertexBuffer,
            .memoryUsage = rhi::MemoryUsage::CPUToGPU,
            .debugName = "DebugLayer_VertexBuffer"
        });
    }
}
