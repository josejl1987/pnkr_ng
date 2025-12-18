#include "pnkr/renderer/debug/DebugLayer.hpp"
#include "pnkr/rhi/rhi_device.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/rhi/rhi_pipeline.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include <filesystem>
#include <fstream>
#include <cstring>

namespace pnkr::renderer::debug
{
    DebugLayer::DebugLayer() = default;
    DebugLayer::~DebugLayer() = default;

    void DebugLayer::initialize(pnkr::renderer::RHIRenderer* renderer)
    {
        if (m_initialized || !renderer)
        {
            return;
        }

        m_renderer = renderer;

        // Reserve vertex storage
        m_vertices.reserve(m_maxVertices);

        createPipeline();
        allocateBuffer(m_maxVertices);

        m_initialized = true;
    }

    bool DebugLayer::hasCapacity(size_t additionalVertices)
    {
        if (m_vertices.size() + additionalVertices > m_maxVertices) {
            static bool warned = false;
            if (!warned) {
                core::Logger::warn("[DebugLayer] Max vertex capacity reached!");
                warned = true;
            }
            return false;
        }
        return true;
    }

    // Scene integration methods removed for simplicity - DebugLayer can work standalone or with scenes

    void DebugLayer::clear()
    {
        m_vertices.clear();
    }

    void DebugLayer::line(const glm::vec3& start, const glm::vec3& end, const glm::vec3& color)
    {
        if (!hasCapacity(2)) return;

        m_vertices.push_back({start, color});
        m_vertices.push_back({end, color});
    }

    void DebugLayer::box(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color)
    {
        if (!hasCapacity(24)) return; // 12 edges * 2 vertices per edge

        glm::vec3 corners[8] = {
            glm::vec3(min.x, min.y, min.z),
            glm::vec3(max.x, min.y, min.z),
            glm::vec3(max.x, max.y, min.z),
            glm::vec3(min.x, max.y, min.z),
            glm::vec3(min.x, min.y, max.z),
            glm::vec3(max.x, min.y, max.z),
            glm::vec3(max.x, max.y, max.z),
            glm::vec3(min.x, max.y, max.z)
        };

        // Bottom edges
        line(corners[0], corners[1], color);
        line(corners[1], corners[2], color);
        line(corners[2], corners[3], color);
        line(corners[3], corners[0], color);

        // Top edges
        line(corners[4], corners[5], color);
        line(corners[5], corners[6], color);
        line(corners[6], corners[7], color);
        line(corners[7], corners[4], color);

        // Vertical edges
        line(corners[0], corners[4], color);
        line(corners[1], corners[5], color);
        line(corners[2], corners[6], color);
        line(corners[3], corners[7], color);
    }

    void DebugLayer::box(const glm::mat4& transform, const glm::vec3& size, const glm::vec3& color)
    {
        if (!hasCapacity(24)) return; // 12 edges * 2 vertices per edge

        glm::vec3 halfSize = size * 0.5f;
        glm::vec3 min = -halfSize;
        glm::vec3 max = halfSize;

        glm::vec3 corners[8] = {
            glm::vec3(transform * glm::vec4(min.x, min.y, min.z, 1.0f)),
            glm::vec3(transform * glm::vec4(max.x, min.y, min.z, 1.0f)),
            glm::vec3(transform * glm::vec4(max.x, max.y, min.z, 1.0f)),
            glm::vec3(transform * glm::vec4(min.x, max.y, min.z, 1.0f)),
            glm::vec3(transform * glm::vec4(min.x, min.y, max.z, 1.0f)),
            glm::vec3(transform * glm::vec4(max.x, min.y, max.z, 1.0f)),
            glm::vec3(transform * glm::vec4(max.x, max.y, max.z, 1.0f)),
            glm::vec3(transform * glm::vec4(min.x, max.y, max.z, 1.0f))
        };

        // Bottom edges
        line(corners[0], corners[1], color);
        line(corners[1], corners[2], color);
        line(corners[2], corners[3], color);
        line(corners[3], corners[0], color);

        // Top edges
        line(corners[4], corners[5], color);
        line(corners[5], corners[6], color);
        line(corners[6], corners[7], color);
        line(corners[7], corners[4], color);

        // Vertical edges
        line(corners[0], corners[4], color);
        line(corners[1], corners[5], color);
        line(corners[2], corners[6], color);
        line(corners[3], corners[7], color);
    }

    void DebugLayer::plane(const glm::vec3& origin, const glm::vec3& v1, const glm::vec3& v2,
                           int segments1, int segments2, const glm::vec3& color)
    {
        if (segments1 <= 0 || segments2 <= 0) return;

        // Draw grid lines parallel to v1, distributed along v2
        for (int i = 0; i <= segments2; ++i)
        {
            // Map i from [0, segments2] to [-0.5, 0.5]
            float t = static_cast<float>(i) / static_cast<float>(segments2) - 0.5f;
            glm::vec3 offset = v2 * t;
            glm::vec3 start = origin + offset - (v1 * 0.5f);
            glm::vec3 end   = origin + offset + (v1 * 0.5f);
            line(start, end, color);
        }

        // Draw grid lines parallel to v2, distributed along v1
        for (int i = 0; i <= segments1; ++i)
        {
            // Map i from [0, segments1] to [-0.5, 0.5]
            float t = static_cast<float>(i) / static_cast<float>(segments1) - 0.5f;
            glm::vec3 offset = v1 * t;
            glm::vec3 start = origin + offset - (v2 * 0.5f);
            glm::vec3 end   = origin + offset + (v2 * 0.5f);
            line(start, end, color);
        }
    }

    void DebugLayer::frustum(const glm::mat4& viewProj, const glm::vec3& color)
    {
        // Define frustum corners in NDC space
        glm::vec3 corners[8] = {
            glm::vec3(-1, -1, -1),
            glm::vec3( 1, -1, -1),
            glm::vec3( 1,  1, -1),
            glm::vec3(-1,  1, -1),
            glm::vec3(-1, -1,  1),
            glm::vec3( 1, -1,  1),
            glm::vec3( 1,  1,  1),
            glm::vec3(-1,  1,  1)
        };

        glm::mat4 invViewProj = glm::inverse(viewProj);

        // Transform corners to world space
        for (int i = 0; i < 8; ++i)
        {
            glm::vec4 corner = invViewProj * glm::vec4(corners[i], 1.0f);
            corners[i] = glm::vec3(corner.x / corner.w, corner.y / corner.w, corner.z / corner.w);
        }

        // Near plane
        line(corners[0], corners[1], color);
        line(corners[1], corners[2], color);
        line(corners[2], corners[3], color);
        line(corners[3], corners[0], color);

        // Far plane
        line(corners[4], corners[5], color);
        line(corners[5], corners[6], color);
        line(corners[6], corners[7], color);
        line(corners[7], corners[4], color);

        // Connecting edges
        line(corners[0], corners[4], color);
        line(corners[1], corners[5], color);
        line(corners[2], corners[6], color);
        line(corners[3], corners[7], color);
    }

    void DebugLayer::circle(const glm::vec3& center, float radius, const glm::vec3& normal, const glm::vec3& color, int segments)
    {
        if (!hasCapacity(segments * 2)) return;
        if (segments < 3) segments = 3;

        // Build orthonormal basis for the circle orientation
        glm::vec3 up = std::abs(normal.z) < 0.999f ? glm::vec3(0,0,1) : glm::vec3(1,0,0);
        glm::vec3 right = glm::normalize(glm::cross(up, normal));
        glm::vec3 tangent = glm::cross(normal, right);

        float step = glm::two_pi<float>() / (float)segments;
        for (int i = 0; i < segments; ++i) {
            float a1 = i * step;
            float a2 = (i + 1) * step;

            glm::vec3 p1 = center + (right * glm::cos(a1) + tangent * glm::sin(a1)) * radius;
            glm::vec3 p2 = center + (right * glm::cos(a2) + tangent * glm::sin(a2)) * radius;
            line(p1, p2, color);
        }
    }

    void DebugLayer::sphere(const glm::vec3& center, float radius, const glm::vec3& color, int segments)
    {
        if (segments < 3) segments = 3;

        // Estimate vertex count: (segments + 1) * segments * 2 lines for latitude +
        // segments * segments lines for longitude = approximately segments * (segments + 3) * 2 vertices
        size_t estimatedVertices = segments * (segments + 3) * 2;
        if (!hasCapacity(estimatedVertices)) return;

        // Draw latitude circles
        for (int lat = 0; lat <= segments; ++lat)
        {
            float theta = glm::pi<float>() * static_cast<float>(lat) / static_cast<float>(segments);
            float sinTheta = glm::sin(theta);
            float cosTheta = glm::cos(theta);

            glm::vec3 prevPoint = center + glm::vec3(radius * sinTheta, radius * cosTheta, 0);

            for (int lon = 1; lon <= segments; ++lon)
            {
                float phi = 2.0f * glm::pi<float>() * static_cast<float>(lon) / static_cast<float>(segments);
                float sinPhi = glm::sin(phi);
                float cosPhi = glm::cos(phi);

                glm::vec3 currPoint = center + glm::vec3(
                    radius * sinTheta * cosPhi,
                    radius * cosTheta,
                    radius * sinTheta * sinPhi
                );

                line(prevPoint, currPoint, color);
                prevPoint = currPoint;
            }
        }

        // Draw longitude circles
        for (int lon = 0; lon < segments; ++lon)
        {
            float phi = 2.0f * glm::pi<float>() * static_cast<float>(lon) / static_cast<float>(segments);
            float sinPhi = glm::sin(phi);
            float cosPhi = glm::cos(phi);

            glm::vec3 prevPoint = center + glm::vec3(0, radius, 0);

            for (int lat = 1; lat <= segments; ++lat)
            {
                float theta = glm::pi<float>() * static_cast<float>(lat) / static_cast<float>(segments);
                float sinTheta = glm::sin(theta);
                float cosTheta = glm::cos(theta);

                glm::vec3 currPoint = center + glm::vec3(
                    radius * sinTheta * cosPhi,
                    radius * cosTheta,
                    radius * sinTheta * sinPhi
                );

                line(prevPoint, currPoint, color);
                prevPoint = currPoint;
            }
        }
    }

    void DebugLayer::render(const pnkr::renderer::RHIFrameContext& ctx, const glm::mat4& viewProj)
    {
        if (!m_initialized || m_vertices.empty())
        {
            return;
        }

        // Recreate pipeline if depth testing configuration changed
        if (m_pipelineDirty)
        {
            createPipeline();
            m_pipelineDirty = false;
        }

        // 1. Determine ring buffer offset
        uint32_t frameSlot = ctx.frameIndex % kMaxFrames;
        uint64_t offset = frameSlot * m_maxVertices * sizeof(LineVertex);

        // 2. Upload
        m_vertexBuffer->uploadData(m_vertices.data(), m_vertices.size() * sizeof(LineVertex), offset);

        // 3. Record Commands
        auto* cmd = ctx.commandBuffer;
        m_renderer->bindPipeline(cmd, m_pipeline);

        cmd->bindVertexBuffer(0, m_vertexBuffer.get(), offset);

        PushConstants pc { viewProj };
        m_renderer->pushConstants(cmd, m_pipeline, rhi::ShaderStage::Vertex, pc);

        cmd->draw(static_cast<uint32_t>(m_vertices.size()));

        // 4. Immediate Clear (ready for next frame's logic)
        m_vertices.clear();
    }

    void DebugLayer::createPipeline()
    {
        // Try to load compiled SPIR-V shaders from build directory first
        std::filesystem::path shaderDir = std::filesystem::current_path() / "shaders";
        if (!std::filesystem::exists(shaderDir / "line_canvas.vert.spv")) {
            // Fallback to binary shaders directory
            shaderDir = std::filesystem::current_path() / "bin" / "shaders";
        }

        auto vert = rhi::Shader::load(rhi::ShaderStage::Vertex, shaderDir / "line_canvas.vert.spv");
        auto frag = rhi::Shader::load(rhi::ShaderStage::Fragment, shaderDir / "line_canvas.frag.spv");

        rhi::RHIPipelineBuilder builder;
        builder.setShaders(vert.get(), frag.get())
               .useVertexType<LineVertex>() // Uses the static layout we defined
               .setTopology(rhi::PrimitiveTopology::LineList)
               .setLineWidth(1.0f)
               .setNoBlend()
               .setColorFormat(m_renderer->getDrawColorFormat())
               .setDepthFormat(m_renderer->getDrawDepthFormat())
               .setName("DebugLinePipeline");

        // Configure depth testing based on settings
        if (m_depthTestEnabled)
        {
            builder.enableDepthTest(true, rhi::CompareOp::LessOrEqual);
        }
        else
        {
            builder.disableDepthTest();
        }

        m_pipeline = m_renderer->createGraphicsPipeline(builder.buildGraphics());
    }

    void DebugLayer::allocateBuffer(uint64_t vertexCount)
    {
        // Ring buffer size: Max vertices per frame * Max frames in flight
        m_vertexBuffer = m_renderer->device()->createBuffer(
            vertexCount * kMaxFrames * sizeof(LineVertex),
            rhi::BufferUsage::VertexBuffer,
            rhi::MemoryUsage::CPUToGPU
        );
    }
} // namespace pnkr::renderer::debug