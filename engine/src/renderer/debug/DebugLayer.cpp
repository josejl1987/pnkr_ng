#include "pnkr/renderer/debug/DebugLayer.hpp"
#include "pnkr/rhi/rhi_device.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/rhi/rhi_pipeline.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/common.hpp"

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
    DebugLayer::DebugLayer() = default;
    DebugLayer::~DebugLayer() = default;

    void DebugLayer::initialize(pnkr::renderer::RHIRenderer* renderer)
    {
        if (m_initialized || (renderer == nullptr))
        {
            return;
        }

        m_renderer = renderer;

        // Reserve vertex storage
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
                core::Logger::warn("[DebugLayer] Max vertex capacity reached!");
                warned = true;
            }
            return false;
        }
        return true;
    }

    DebugLayer::LineVertex* DebugLayer::appendVertices(size_t count)
    {
        if (!hasCapacity(count)) return nullptr;
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
        LineVertex* v = appendVertices(2);
        if (!v) return;
        v[0] = {.position = start, .color = color};
        v[1] = {.position = end, .color = color};
    }

    void DebugLayer::box(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color)
    {
        LineVertex* v = appendVertices(24);
        if (!v) return;

        // Axis Aligned Box
        glm::vec3 pts[8] = {
            glm::vec3(max.x, max.y, max.z), glm::vec3(max.x, max.y, min.z),
            glm::vec3(max.x, min.y, max.z), glm::vec3(max.x, min.y, min.z),
            glm::vec3(min.x, max.y, max.z), glm::vec3(min.x, max.y, min.z),
            glm::vec3(min.x, min.y, max.z), glm::vec3(min.x, min.y, min.z),
        };

        static constexpr uint8_t edges[24] = {
            0, 1, 2, 3, 4, 5, 6, 7,
            0, 2, 1, 3, 4, 6, 5, 7,
            0, 4, 1, 5, 2, 6, 3, 7
        };

        for (size_t i = 0; i < 24; i += 2)
        {
            v[i + 0] = {.position = pts[edges[i + 0]], .color = color};
            v[i + 1] = {.position = pts[edges[i + 1]], .color = color};
        }
    }

    void DebugLayer::box(const glm::mat4& transform, const glm::vec3& size, const glm::vec3& color)
    {
        LineVertex* v = appendVertices(24);
        if (!v) return;

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

        static constexpr uint8_t edges[24] = {
            0, 1, 2, 3, 4, 5, 6, 7,
            0, 2, 1, 3, 4, 6, 5, 7,
            0, 4, 1, 5, 2, 6, 3, 7
        };

        for (size_t i = 0; i < 24; i += 2)
        {
            v[i + 0] = {.position = pts[edges[i + 0]], .color = color};
            v[i + 1] = {.position = pts[edges[i + 1]], .color = color};
        }
    }

    void DebugLayer::plane(const glm::vec3& origin, const glm::vec3& v1, const glm::vec3& v2,
                           int segments1, int segments2, const glm::vec3& color)
    {
        // Estimate vertices: (segments1 + 1) * 2 + (segments2 + 1) * 2
        // If segments are 0, it draws at least the outline
        int count1 = std::max(1, segments1);
        int count2 = std::max(1, segments2);

        if (!hasCapacity((count1 + 1 + count2 + 1) * 2)) return;

        // Draw Outline
        line(origin - 0.5f * v1 - 0.5f * v2, origin - 0.5f * v1 + 0.5f * v2, color);
        line(origin + 0.5f * v1 - 0.5f * v2, origin + 0.5f * v1 + 0.5f * v2, color);
        line(origin - 0.5f * v1 + 0.5f * v2, origin + 0.5f * v1 + 0.5f * v2, color);
        line(origin - 0.5f * v1 - 0.5f * v2, origin + 0.5f * v1 - 0.5f * v2, color);

        // Inner lines along V1 direction
        for (int i = 1; i < count1; i++)
        {
            float t = ((float)i - (float)count1 / 2.0f) / (float)count1;
            const glm::vec3 o1 = origin + t * v1;
            line(o1 - 0.5f * v2, o1 + 0.5f * v2, color);
        }

        // Inner lines along V2 direction
        for (int i = 1; i < count2; i++)
        {
            float t = ((float)i - (float)count2 / 2.0f) / (float)count2;
            const glm::vec3 o2 = origin + t * v2;
            line(o2 - 0.5f * v1, o2 + 0.5f * v1, color);
        }
    }

    void DebugLayer::frustum(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& color)
    {
        // Outline (12 lines) + Grid lines (20 * 4 = 80 lines) -> ~184 vertices
        if (!hasCapacity(200)) return;

        // FIX: Vulkan Clip Space Z is [0, 1], not [-1, 1]
        const glm::vec3 corners[] = {
            glm::vec3(-1, -1, 0), glm::vec3(+1, -1, 0), glm::vec3(+1, +1, 0), glm::vec3(-1, +1, 0),
            glm::vec3(-1, -1, 1), glm::vec3(+1, -1, 1), glm::vec3(+1, +1, 1), glm::vec3(-1, +1, 1)
        };

        glm::vec3 pp[8];
        glm::mat4 invVP = glm::inverse(proj * view);

        m_lastFrustumCorners.clear();
        for (int i = 0; i < 8; i++)
        {
            glm::vec4 q = invVP * glm::vec4(corners[i], 1.0f);
            pp[i] = glm::vec3(q.x / q.w, q.y / q.w, q.z / q.w);
            m_lastFrustumCorners.push_back(pp[i]);
        }

        // Outline
        line(pp[0], pp[4], color);
        line(pp[1], pp[5], color);
        line(pp[2], pp[6], color);
        line(pp[3], pp[7], color);
        line(pp[0], pp[1], color);
        line(pp[1], pp[2], color);
        line(pp[2], pp[3], color);
        line(pp[3], pp[0], color);
        line(pp[4], pp[5], color);
        line(pp[5], pp[6], color);
        line(pp[6], pp[7], color);
        line(pp[7], pp[4], color);

        // Grid Lines on frustum faces (matching LineCanvas3D logic)
        const glm::vec3 gridColor = color * 0.7f;
        const int gridLines = 20;

        auto drawGridFace = [&](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3)
        {
            // Vertical lines
            {
                glm::vec3 start = p0;
                glm::vec3 end = p3;
                const glm::vec3 stepStart = (p1 - p0) / (float)gridLines;
                const glm::vec3 stepEnd = (p2 - p3) / (float)gridLines;

                for (int i = 0; i < gridLines; ++i)
                {
                    start += stepStart;
                    end += stepEnd;
                    line(start, end, gridColor);
                }
            }
            // Horizontal lines
            {
                glm::vec3 start = p0;
                glm::vec3 end = p1;
                const glm::vec3 stepStart = (p3 - p0) / (float)gridLines;
                const glm::vec3 stepEnd = (p2 - p1) / (float)gridLines;

                for (int i = 0; i < gridLines; ++i)
                {
                    start += stepStart;
                    end += stepEnd;
                    line(start, end, gridColor);
                }
            }
        };

        drawGridFace(pp[0], pp[1], pp[5], pp[4]); // Bottom
        drawGridFace(pp[3], pp[2], pp[6], pp[7]); // Top
        drawGridFace(pp[0], pp[3], pp[7], pp[4]); // Left
        drawGridFace(pp[1], pp[2], pp[6], pp[5]); // Right
    }

    void DebugLayer::circle(const glm::vec3& center, float radius, const glm::vec3& normal, const glm::vec3& color,
                            int segments)
    {
        if (!hasCapacity(static_cast<size_t>(segments * 2))) return;
        segments = std::max(segments, 3);

        // Build orthonormal basis
        glm::vec3 up = std::abs(normal.z) < 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
        glm::vec3 right = glm::normalize(glm::cross(up, normal));
        glm::vec3 tangent = glm::cross(normal, right);

        float step = glm::two_pi<float>() / (float)segments;
        for (int i = 0; i < segments; ++i)
        {
            float a1 = i * step;
            float a2 = (i + 1) * step;

            glm::vec3 p1 = center + (right * glm::cos(a1) + tangent * glm::sin(a1)) * radius;
            glm::vec3 p2 = center + (right * glm::cos(a2) + tangent * glm::sin(a2)) * radius;
            line(p1, p2, color);
        }
    }

    void DebugLayer::sphere(const glm::vec3& center, float radius, const glm::vec3& color, int segments)
    {
        // 3 major circles (XY, YZ, XZ)
        segments = std::max(segments, 3);
        if (!hasCapacity(static_cast<size_t>(segments * 6))) return;

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

    void DebugLayer::render(const pnkr::renderer::RHIFrameContext& ctx, const glm::mat4& viewProj)
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

        // 1. Determine ring buffer offset
        uint32_t frameSlot = ctx.frameIndex % kMaxFrames;
        uint64_t offset = u64(frameSlot * m_maxVertices) * sizeof(LineVertex);

        // 2. Upload
        m_vertexBuffer->uploadData(m_verticesRender.data(), m_verticesRender.size() * sizeof(LineVertex), offset);

        // 3. Record Commands
        auto* cmd = ctx.commandBuffer;

        // Use dynamic depth state based on config
        cmd->setDepthTestEnable(m_depthTestEnabled);
        cmd->setDepthWriteEnable(false); // Debug lines usually shouldn't write depth
        cmd->setDepthCompareOp(rhi::CompareOp::LessOrEqual);

        m_renderer->bindPipeline(cmd, m_pipeline);

        cmd->bindVertexBuffer(0, m_vertexBuffer.get(), offset);

        PushConstants pc{viewProj};
        m_renderer->pushConstants(cmd, m_pipeline, rhi::ShaderStage::Vertex, pc);

        cmd->draw(static_cast<uint32_t>(m_verticesRender.size()));

        // Restore default state if necessary, though command buffer resets usually handle this
        cmd->setDepthTestEnable(true);
        cmd->setDepthWriteEnable(true);

        // 4. Clear for next frame (Immediate Mode)
        m_verticesRender.clear();
    }

    void DebugLayer::createPipeline()
    {
        // Try to load compiled SPIR-V shaders
        std::filesystem::path shaderDir = "shaders";

        auto loadSpirvShader = [](const std::filesystem::path& shaderPath, rhi::ShaderStage stage)
        {
            std::ifstream file(shaderPath, std::ios::binary);
            if (!file.is_open())
            {
                // Fallback attempt
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

        auto vert = loadSpirvShader(shaderDir / "line_canvas.vert.spv", rhi::ShaderStage::Vertex);
        auto frag = loadSpirvShader(shaderDir / "line_canvas.frag.spv", rhi::ShaderStage::Fragment);

        rhi::GraphicsPipelineDescriptor desc{};
        desc.shaders = {vert, frag};

        // Define vertex input (reusing LineVertex layout)
        desc.vertexBindings = {{.binding = 0, .stride = sizeof(LineVertex), .inputRate = rhi::VertexInputRate::Vertex}};
        desc.vertexAttributes = {
            {
                .location = 0, .binding = 0, .format = rhi::Format::R32G32B32_SFLOAT,
                .offset = offsetof(LineVertex, position), .semantic = rhi::VertexSemantic::Position
            },
            {
                .location = 1, .binding = 0, .format = rhi::Format::R32G32B32_SFLOAT,
                .offset = offsetof(LineVertex, color), .semantic = rhi::VertexSemantic::Color
            }
        };

        desc.topology = rhi::PrimitiveTopology::LineList;
        desc.rasterization.polygonMode = rhi::PolygonMode::Fill;
        desc.rasterization.cullMode = rhi::CullMode::None;
        desc.rasterization.lineWidth = 1.0f;

        // Depth/Stencil (dynamic)
        desc.depthStencil.depthTestEnable = true;
        desc.depthStencil.depthWriteEnable = false;
        desc.depthStencil.depthCompareOp = rhi::CompareOp::LessOrEqual;

        desc.blend.attachments = {{.blendEnable = false}};
        desc.colorFormats = {m_renderer->getDrawColorFormat()};
        desc.depthFormat = m_renderer->getDrawDepthFormat();

        // Push constants
        desc.pushConstants = {{.stages = rhi::ShaderStage::Vertex, .offset = 0, .size = sizeof(PushConstants)}};

        desc.dynamicStates = {
            rhi::DynamicState::Viewport, rhi::DynamicState::Scissor,
            rhi::DynamicState::DepthTestEnable, rhi::DynamicState::DepthWriteEnable, rhi::DynamicState::DepthCompareOp
        };
        desc.debugName = "DebugLayerPipeline";

        m_pipeline = m_renderer->createGraphicsPipeline(desc);
    }

    void DebugLayer::allocateBuffer(uint64_t vertexCount)
    {
        // Ring buffer size: Max vertices per frame * Max frames in flight
        m_vertexBuffer = m_renderer->device()->createBuffer({
            .size = vertexCount * kMaxFrames * sizeof(LineVertex),
            .usage = rhi::BufferUsage::VertexBuffer,
            .memoryUsage = rhi::MemoryUsage::CPUToGPU,
            .debugName = "DebugLayer_VertexBuffer"
        });
    }
} // namespace pnkr::renderer::debug
