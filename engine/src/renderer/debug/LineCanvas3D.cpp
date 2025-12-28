#include "pnkr/renderer/debug/LineCanvas3D.hpp"
#include "pnkr/rhi/rhi_device.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/rhi/rhi_pipeline.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/core/logger.hpp"

#include <algorithm>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/constants.hpp>

using namespace pnkr::util;

namespace pnkr::renderer::debug
{
    LineCanvas3D::LineCanvas3D() = default;
    LineCanvas3D::~LineCanvas3D() = default;

    void LineCanvas3D::initialize(pnkr::renderer::RHIRenderer* renderer)
    {
        if (m_initialized || (renderer == nullptr))
        {
            return;
        }

        m_renderer = renderer;

        // Reserve vertex storage
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
        // Safety check to prevent buffer overflow before upload
        if (!m_frameActive || m_vertices.size() + 2 > m_maxVertices)
        {
            return;
        }

        m_vertices.push_back({.position = start, .color = color});
        m_vertices.push_back({.position = end, .color = color});
    }

    void LineCanvas3D::box(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color)
    {
        // Axis Aligned Box
        glm::vec3 pts[8] = {
            glm::vec3(max.x, max.y, max.z), glm::vec3(max.x, max.y, min.z),
            glm::vec3(max.x, min.y, max.z), glm::vec3(max.x, min.y, min.z),
            glm::vec3(min.x, max.y, max.z), glm::vec3(min.x, max.y, min.z),
            glm::vec3(min.x, min.y, max.z), glm::vec3(min.x, min.y, min.z),
        };

        // Connect edges
        line(pts[0], pts[1], color);
        line(pts[2], pts[3], color);
        line(pts[4], pts[5], color);
        line(pts[6], pts[7], color);

        line(pts[0], pts[2], color);
        line(pts[1], pts[3], color);
        line(pts[4], pts[6], color);
        line(pts[5], pts[7], color);

        line(pts[0], pts[4], color);
        line(pts[1], pts[5], color);
        line(pts[2], pts[6], color);
        line(pts[3], pts[7], color);
    }

    void LineCanvas3D::box(const glm::mat4& transform, const glm::vec3& size, const glm::vec3& color)
    {
        // OBB (Oriented Bounding Box) logic adapted from reference
        glm::vec3 halfSize = size * 0.5f;

        glm::vec3 pts[8] = {
            glm::vec3(+halfSize.x, +halfSize.y, +halfSize.z), glm::vec3(+halfSize.x, +halfSize.y, -halfSize.z),
            glm::vec3(+halfSize.x, -halfSize.y, +halfSize.z), glm::vec3(+halfSize.x, -halfSize.y, -halfSize.z),
            glm::vec3(-halfSize.x, +halfSize.y, +halfSize.z), glm::vec3(-halfSize.x, +halfSize.y, -halfSize.z),
            glm::vec3(-halfSize.x, -halfSize.y, +halfSize.z), glm::vec3(-halfSize.x, -halfSize.y, -halfSize.z),
        };

        for (auto& p : pts)
        {
            p = glm::vec3(transform * glm::vec4(p, 1.f));
        }

        // Connect edges
        line(pts[0], pts[1], color);
        line(pts[2], pts[3], color);
        line(pts[4], pts[5], color);
        line(pts[6], pts[7], color);

        line(pts[0], pts[2], color);
        line(pts[1], pts[3], color);
        line(pts[4], pts[6], color);
        line(pts[5], pts[7], color);

        line(pts[0], pts[4], color);
        line(pts[1], pts[5], color);
        line(pts[2], pts[6], color);
        line(pts[3], pts[7], color);
    }

    void LineCanvas3D::plane(const glm::vec3& o, const glm::vec3& v1, const glm::vec3& v2,
                             int n1, int n2, const glm::vec3& color)
    {
        // Reference implementation logic for centered grid
        // We treat v1 and v2 as the full extent vectors in this adaptation
        // to match common C++ usage, assuming s1/s2 scaling is 1.0 relative to input vectors.

        // Draw Outline
        line(o - 0.5f * v1 - 0.5f * v2, o - 0.5f * v1 + 0.5f * v2, color);
        line(o + 0.5f * v1 - 0.5f * v2, o + 0.5f * v1 + 0.5f * v2, color);
        line(o - 0.5f * v1 + 0.5f * v2, o + 0.5f * v1 + 0.5f * v2, color);
        line(o - 0.5f * v1 - 0.5f * v2, o + 0.5f * v1 - 0.5f * v2, color);

        // Inner lines along V1 direction
        for (int i = 1; i < n1; i++)
        {
            float t = ((float)i - (float)n1 / 2.0f) / (float)n1;
            // Adjustment: in reference s1 was separate, here we assume |v1| is size
            const glm::vec3 o1 = o + t * v1;
            line(o1 - 0.5f * v2, o1 + 0.5f * v2, color);
        }

        // Inner lines along V2 direction
        for (int i = 1; i < n2; i++)
        {
            float t = ((float)i - (float)n2 / 2.0f) / (float)n2;
            const glm::vec3 o2 = o + t * v2;
            line(o2 - 0.5f * v1, o2 + 0.5f * v1, color);
        }
    }

    void LineCanvas3D::frustum(const glm::mat4& viewProj, const glm::vec3& color)
    {
        const glm::vec3 corners[] = {
            glm::vec3(-1, -1, -1), glm::vec3(+1, -1, -1), glm::vec3(+1, +1, -1), glm::vec3(-1, +1, -1),
            glm::vec3(-1, -1, +1), glm::vec3(+1, -1, +1), glm::vec3(+1, +1, +1), glm::vec3(-1, +1, +1)
        };

        glm::vec3 pp[8];

        for (int i = 0; i < 8; i++)
        {
            glm::vec4 q = glm::inverse(viewProj)* glm::vec4(corners[i], 1.0f);
            pp[i] = glm::vec3(q.x / q.w, q.y / q.w, q.z / q.w);
        }
        line(pp[0], pp[4], color);
        line(pp[1], pp[5], color);
        line(pp[2], pp[6], color);
        line(pp[3], pp[7], color);
        // near
        line(pp[0], pp[1], color);
        line(pp[1], pp[2], color);
        line(pp[2], pp[3], color);
        line(pp[3], pp[0], color);
        // x
        line(pp[0], pp[2], color);
        line(pp[1], pp[3], color);
        // far
        line(pp[4], pp[5], color);
        line(pp[5], pp[6], color);
        line(pp[6], pp[7], color);
        line(pp[7], pp[4], color);
        // x
        line(pp[4], pp[6], color);
        line(pp[5], pp[7], color);

        const glm::vec3 gridColor = color * 0.7f;
        const int gridLines = 100;

        // bottom
        {
            glm::vec3 p1 = pp[0];
            glm::vec3 p2 = pp[1];
            const glm::vec3 s1 = (pp[4] - pp[0]) / float(gridLines);
            const glm::vec3 s2 = (pp[5] - pp[1]) / float(gridLines);
            for (int i = 0; i != gridLines; i++, p1 += s1, p2 += s2)
                line(p1, p2, gridColor);
        }
        // top
        {
            glm::vec3 p1 = pp[2];
            glm::vec3 p2 = pp[3];
            const glm::vec3 s1 = (pp[6] - pp[2]) / float(gridLines);
            const glm::vec3 s2 = (pp[7] - pp[3]) / float(gridLines);
            for (int i = 0; i != gridLines; i++, p1 += s1, p2 += s2)
                line(p1, p2, gridColor);
        }
        // left
        {
            glm::vec3 p1 = pp[0];
            glm::vec3 p2 = pp[3];
            const glm::vec3 s1 = (pp[4] - pp[0]) / float(gridLines);
            const glm::vec3 s2 = (pp[7] - pp[3]) / float(gridLines);
            for (int i = 0; i != gridLines; i++, p1 += s1, p2 += s2)
                line(p1, p2, gridColor);
        }
        // right
        {
            glm::vec3 p1 = pp[1];
            glm::vec3 p2 = pp[2];
            const glm::vec3 s1 = (pp[5] - pp[1]) / float(gridLines);
            const glm::vec3 s2 = (pp[6] - pp[2]) / float(gridLines);
            for (int i = 0; i != gridLines; i++, p1 += s1, p2 += s2)
                line(p1, p2, gridColor);
        }
    }

    void LineCanvas3D::circle(const glm::vec3& center, float radius, const glm::vec3& color, int segments)
    {
        segments = std::max(segments, 3);
        float angleStep = 2.0f * glm::pi<float>() / toFloat(segments);

        // Default to XZ plane
        glm::vec3 prevPoint = center + glm::vec3(radius, 0, 0);

        for (int i = 1; i <= segments; ++i)
        {
            float angle = toFloat(i) * angleStep;
            glm::vec3 currPoint = center + glm::vec3(glm::cos(angle) * radius, 0, glm::sin(angle) * radius);
            line(prevPoint, currPoint, color);
            prevPoint = currPoint;
        }
    }

    void LineCanvas3D::sphere(const glm::vec3& center, float radius, const glm::vec3& color, int segments)
    {
        // 3 circles (XY, YZ, XZ)
        segments = std::max(segments, 3);
        float angleStep = 2.0f * glm::pi<float>() / toFloat(segments);

        auto drawCircle = [&](int axis)
        {
            glm::vec3 prev(0.f);
            if (axis == 0) prev = center + glm::vec3(radius, 0, 0); // XY
            if (axis == 1) prev = center + glm::vec3(0, radius, 0); // YZ
            if (axis == 2) prev = center + glm::vec3(radius, 0, 0); // XZ

            for (int i = 1; i <= segments; ++i)
            {
                float a = toFloat(i) * angleStep;
                float c = glm::cos(a) * radius;
                float s = glm::sin(a) * radius;
                glm::vec3 curr(0.f);

                if (axis == 0) curr = center + glm::vec3(c, s, 0);
                if (axis == 1) curr = center + glm::vec3(0, c, s);
                if (axis == 2) curr = center + glm::vec3(c, 0, s);

                line(prev, curr, color);
                prev = curr;
            }
        };

        drawCircle(0);
        drawCircle(1);
        drawCircle(2);
    }

    void LineCanvas3D::render(const pnkr::renderer::RHIFrameContext& ctx, const glm::mat4& viewProj)
    {
        if (!m_initialized || m_vertices.empty())
        {
            return;
        }

        // Update current frame offset for ring buffering
        m_currentFrameIndex = ctx.frameIndex % kMaxFrames;

        // Calculate size requirements
        uint64_t currentSize = m_vertices.size() * sizeof(LineVertex);

        // Ensure buffer exists and is large enough
        // Note: The reference implementation resizes. We recreate if too small.
        if (!m_vertexBuffer || (m_maxVertices * sizeof(LineVertex) < currentSize))
        {
            // Expand capacity if needed
            while (m_maxVertices * sizeof(LineVertex) < currentSize)
            {
                m_maxVertices *= 2;
            }
            createVertexBuffer();
        }

        // Upload vertex data to the current frame slot in the ring buffer
        uploadVertexData();

        // Bind pipeline and set state
        m_renderer->bindPipeline(ctx.commandBuffer, m_pipeline);

        // Bind vertex buffer with offset
        uint64_t frameOffset = m_currentFrameIndex * (m_maxVertices * sizeof(LineVertex));
        ctx.commandBuffer->bindVertexBuffer(0, m_vertexBuffer.get(), frameOffset);

        // Set push constants (MVP matrix)
        PushConstants pc{};
        pc.mvp = viewProj;
        m_renderer->pushConstants(ctx.commandBuffer, m_pipeline,
                                  rhi::ShaderStage::Vertex, pc);

        // Draw lines
        ctx.commandBuffer->draw(static_cast<uint32_t>(m_vertices.size()), 1, 0, 0);
    }

    void LineCanvas3D::createPipeline()
    {
        // Define vertex input
        std::vector<rhi::VertexInputBinding> bindings = {
            {.binding = 0, .stride = sizeof(LineVertex), .inputRate = rhi::VertexInputRate::Vertex}
        };

        std::vector<rhi::VertexInputAttribute> attributes = {
            {
                .location = 0, .binding = 0, .format = rhi::Format::R32G32B32_SFLOAT,
                .offset = offsetof(LineVertex, position), .semantic = rhi::VertexSemantic::Position
            },
            {
                .location = 1, .binding = 0, .format = rhi::Format::R32G32B32_SFLOAT,
                .offset = offsetof(LineVertex, color), .semantic = rhi::VertexSemantic::Color
            }
        };

        // Define push constants
        std::vector<rhi::PushConstantRange> pushConstants = {
            {.stages = rhi::ShaderStage::Vertex, .offset = 0, .size = sizeof(PushConstants)}
        };

        // Load SPIR-V shaders
        auto loadSpirvShader = [](const std::filesystem::path& shaderPath, rhi::ShaderStage stage)
        {
            std::ifstream file(shaderPath, std::ios::binary);
            if (!file.is_open())
            {
                // Fallback attempt for different working directories
                std::filesystem::path alt = std::filesystem::path("bin") / shaderPath;
                file.open(alt, std::ios::binary);
                if (!file.is_open())
                    throw std::runtime_error("Failed to open SPIR-V shader file: " + shaderPath.string());
            }

            std::vector<uint32_t> spirv;
            file.seekg(0, std::ios::end);
            size_t fileSize = file.tellg();
            file.seekg(0, std::ios::beg);

            spirv.resize(fileSize / sizeof(uint32_t));
            file.read(reinterpret_cast<char*>(spirv.data()), fileSize);

            return rhi::ShaderModuleDescriptor{.stage = stage, .spirvCode = spirv, .entryPoint = "main"};
        };

        // Shaders are expected to be in "shaders/" relative to CWD or executable
        std::filesystem::path shaderDir = "shaders";
        auto vertShader = loadSpirvShader(shaderDir / "line_canvas.vert.spv", rhi::ShaderStage::Vertex);
        auto fragShader = loadSpirvShader(shaderDir / "line_canvas.frag.spv", rhi::ShaderStage::Fragment);

        rhi::GraphicsPipelineDescriptor desc{};
        desc.shaders = {vertShader, fragShader};
        desc.vertexBindings = bindings;
        desc.vertexAttributes = attributes;
        desc.topology = rhi::PrimitiveTopology::LineList;

        desc.rasterization.polygonMode = rhi::PolygonMode::Fill;
        desc.rasterization.cullMode = rhi::CullMode::None;
        desc.rasterization.lineWidth = 1.0F;

        // Depth stencil state - Allow lines to draw over geometry if depth test is disabled in Logic,
        // but typically debug lines want to be depth tested but maybe not write depth.
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
        // If resizing, release old buffer
        if (m_vertexBuffer)
        {
            m_vertexBuffer.reset();
        }

        // Create buffer: Size = MaxVertices * MaxFrames (for ring buffering)
        m_vertexBuffer = m_renderer->device()->createBuffer({
            .size = u64(m_maxVertices * kMaxFrames) * sizeof(LineVertex),
            .usage = rhi::BufferUsage::VertexBuffer,
            .memoryUsage = rhi::MemoryUsage::CPUToGPU, // Host visible for frequent updates
            .debugName = "LineCanvas3D_VertexBuffer"
        });
    }

    void LineCanvas3D::uploadVertexData()
    {
        if (m_vertices.empty())
        {
            return;
        }

        // Calculate offset into ring buffer
        uint64_t frameOffset = m_currentFrameIndex * (m_maxVertices * sizeof(LineVertex));
        uint64_t uploadSize = m_vertices.size() * sizeof(LineVertex);

        // Direct map upload since buffer is CPUToGPU
        m_vertexBuffer->uploadData(m_vertices.data(), uploadSize, frameOffset);
    }
} // namespace pnkr::renderer::debug
