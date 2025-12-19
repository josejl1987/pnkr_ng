#include "pnkr/renderer/debug/LineCanvas3D.hpp"
#include "pnkr/rhi/rhi_device.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/rhi/rhi_pipeline.hpp"
#include "pnkr/core/common.hpp"
#include <algorithm>
#include <cstddef>
#include <math.h>

#include <filesystem>
#include <fstream>
#include <cstring>
#include <glm/ext/scalar_constants.hpp>

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
        if (!m_frameActive || m_vertices.size() + 2 >= m_maxVertices)
        {
            return;
        }

        m_vertices.push_back({.position=start, .color=color});
        m_vertices.push_back({.position=end, .color=color});
    }

    void LineCanvas3D::box(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color)
    {
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

    void LineCanvas3D::box(const glm::mat4& transform, const glm::vec3& size, const glm::vec3& color)
    {
        glm::vec3 halfSize = size * 0.5F;
        glm::vec3 min = -halfSize;
        glm::vec3 max = halfSize;

        glm::vec3 corners[8] = {
            glm::vec3(transform * glm::vec4(min.x, min.y, min.z, 1.0F)),
            glm::vec3(transform * glm::vec4(max.x, min.y, min.z, 1.0F)),
            glm::vec3(transform * glm::vec4(max.x, max.y, min.z, 1.0F)),
            glm::vec3(transform * glm::vec4(min.x, max.y, min.z, 1.0F)),
            glm::vec3(transform * glm::vec4(min.x, min.y, max.z, 1.0F)),
            glm::vec3(transform * glm::vec4(max.x, min.y, max.z, 1.0F)),
            glm::vec3(transform * glm::vec4(max.x, max.y, max.z, 1.0F)),
            glm::vec3(transform * glm::vec4(min.x, max.y, max.z, 1.0F))
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

    void LineCanvas3D::plane(const glm::vec3& origin, const glm::vec3& v1, const glm::vec3& v2,
                            int segments1, int segments2, const glm::vec3& color)
    {
        if (segments1 <= 0 || segments2 <= 0)
        {
            return;
        }

        glm::vec3 step1 = v1 / toFloat(segments1);
        glm::vec3 step2 = v2 / toFloat(segments2);

        // Draw grid lines parallel to v1
        for (int i = 0; i <= segments1; ++i)
        {
            glm::vec3 start = origin + toFloat(i) * step1 - v2 * 0.5F;
            glm::vec3 end = origin + toFloat(i) * step1 + v2 * 0.5F;
            line(start, end, color);
        }

        // Draw grid lines parallel to v2
        for (int j = 0; j <= segments2; ++j)
        {
            glm::vec3 start = origin - v1 * 0.5F + toFloat(j) * step2;
            glm::vec3 end = origin + v1 * 0.5F + toFloat(j) * step2;
            line(start, end, color);
        }
    }

    void LineCanvas3D::frustum(const glm::mat4& viewProj, const glm::vec3& color)
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
        for (auto & i : corners)
        {
            glm::vec4 corner = invViewProj * glm::vec4(i, 1.0F);
            i = glm::vec3(corner.x / corner.w, corner.y / corner.w, corner.z / corner.w);
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

    void LineCanvas3D::circle(const glm::vec3& center, float radius, const glm::vec3& color, int segments)
    {
        segments = std::max(segments, 3);

        float angleStep = 2.0f * glm::pi<float>() / toFloat(segments);

        glm::vec3 prevPoint = center + glm::vec3(radius, 0, 0);

        for (int i = 1; i <= segments; ++i)
        {
            float angle = toFloat(i) * angleStep;
            glm::vec3 currPoint = center + glm::vec3(glm::cos(angle) * radius, glm::sin(angle) * radius, 0);
            line(prevPoint, currPoint, color);
            prevPoint = currPoint;
        }
    }

    void LineCanvas3D::sphere(const glm::vec3& center, float radius, const glm::vec3& color, int segments)
    {
        segments = std::max(segments, 3);

        // Draw latitude circles
        for (int lat = 0; lat <= segments; ++lat)
        {
            float theta = glm::pi<float>() * toFloat(lat) / toFloat(segments);
            float sinTheta = glm::sin(theta);
            float cosTheta = glm::cos(theta);

            glm::vec3 prevPoint = center + glm::vec3(radius * sinTheta, radius * cosTheta, 0);

            for (int lon = 1; lon <= segments; ++lon)
            {
                float phi = 2.0F * glm::pi<float>() / segments;
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
            float phi = 2.0f * glm::pi<float>() * toFloat(lon) / toFloat(segments);
            float sinPhi = glm::sin(phi);
            float cosPhi = glm::cos(phi);

            glm::vec3 prevPoint = center + glm::vec3(0, radius, 0);

            for (int lat = 1; lat <= segments; ++lat)
            {
                float theta = glm::pi<float>() * toFloat(lat) / toFloat(segments);
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

    void LineCanvas3D::render(const pnkr::renderer::RHIFrameContext& ctx, const glm::mat4& viewProj)
    {
        if (!m_initialized || m_vertices.empty())
        {
            return;
        }

        // Update current frame offset
        m_currentFrameIndex = ctx.frameIndex % kMaxFrames;

        // Calculate offset for this frame
        uint32_t frameOffset = m_currentFrameIndex * m_maxVertices;

        // Check if we need to resize the buffer
        uint32_t requiredSize = (frameOffset + m_vertices.size()) * sizeof(LineVertex);
        if (m_vertexBuffer && requiredSize > m_vertexBuffer->size())
        {
            createVertexBuffer();
        }

        // Upload vertex data
        uploadVertexData();

        // Bind pipeline and set state
        m_renderer->bindPipeline(ctx.commandBuffer, m_pipeline);

        // Bind vertex buffer
        ctx.commandBuffer->bindVertexBuffer(0, m_vertexBuffer.get(), frameOffset * sizeof(LineVertex));

        // Set push constants (MVP matrix)
        PushConstants pc{};
        pc.mvp = viewProj;
        m_renderer->pushConstants(ctx.commandBuffer, m_pipeline,
            rhi::ShaderStage::Vertex, pc);

        // Draw lines
        ctx.commandBuffer->draw(m_vertices.size(), 1, 0, 0);
    }

    void LineCanvas3D::createPipeline()
    {
        // Define vertex input
        std::vector<rhi::VertexInputBinding> bindings = {
            {.binding=0, .stride=sizeof(LineVertex), .inputRate=rhi::VertexInputRate::Vertex}
        };

        std::vector<rhi::VertexInputAttribute> attributes = {
            {.location=0, .binding=0, .format=rhi::Format::R32G32B32_SFLOAT, .offset=offsetof(LineVertex, position), .semantic=rhi::VertexSemantic::Position},
            {.location=1, .binding=0, .format=rhi::Format::R32G32B32_SFLOAT, .offset=offsetof(LineVertex, color), .semantic=rhi::VertexSemantic::Color}
        };

        // Define push constants
        std::vector<rhi::PushConstantRange> pushConstants = {
            {.stages=rhi::ShaderStage::Vertex, .offset=0, .size=sizeof(PushConstants)}
        };

        // Load SPIR-V shaders
        auto loadSpirvShader = [](const std::filesystem::path& shaderPath, rhi::ShaderStage stage) {
            std::ifstream file(shaderPath, std::ios::binary);
            if (!file.is_open())
            {
                throw std::runtime_error("Failed to open SPIR-V shader file: " + shaderPath.string());
            }

            // Read file into vector of bytes
            std::vector<uint32_t> spirv;
            file.seekg(0, std::ios::end);
            size_t fileSize = file.tellg();
            file.seekg(0, std::ios::beg);

            spirv.resize(fileSize / sizeof(uint32_t));
            file.read(reinterpret_cast<char*>(spirv.data()), fileSize);

            return rhi::ShaderModuleDescriptor{.stage=stage, .spirvCode=spirv, .entryPoint="main"};
        };

        // Try to load compiled SPIR-V shaders from current directory first
        std::filesystem::path shaderDir = std::filesystem::current_path() / "shaders";
        if (!std::filesystem::exists(shaderDir)) {
            // Fallback to engine source directory
            shaderDir = std::filesystem::path(__FILE__).parent_path() / "shaders";
        }

        auto vertShader = loadSpirvShader(shaderDir / "line_canvas.vert.spv", rhi::ShaderStage::Vertex);
        auto fragShader = loadSpirvShader(shaderDir / "line_canvas.frag.spv", rhi::ShaderStage::Fragment);

        // Create graphics pipeline descriptor
        rhi::GraphicsPipelineDescriptor desc{};
        desc.shaders = {vertShader, fragShader};
        desc.vertexBindings = bindings;
        desc.vertexAttributes = attributes;
        desc.topology = rhi::PrimitiveTopology::LineList;

        // Rasterization state
        desc.rasterization.polygonMode = rhi::PolygonMode::Fill;
        desc.rasterization.cullMode = rhi::CullMode::None;
        desc.rasterization.lineWidth = 1.0F;

        // Depth stencil state
        desc.depthStencil.depthTestEnable = true;
        desc.depthStencil.depthWriteEnable = true;
        desc.depthStencil.depthCompareOp = rhi::CompareOp::LessOrEqual;

        // Color blend state
        desc.blend.attachments = {{.blendEnable = false}};

        // Render target formats
        desc.colorFormats = {m_renderer->getDrawColorFormat()};
        desc.depthFormat = m_renderer->getDrawDepthFormat();

        desc.pushConstants = pushConstants;
        desc.debugName = "LineCanvas3D Pipeline";

        // Create pipeline through RHIRenderer
        m_pipeline = m_renderer->createGraphicsPipeline(desc);
    }

    void LineCanvas3D::createVertexBuffer()
    {
        // Create buffer through device
        m_vertexBuffer = m_renderer->device()->createBuffer(
            u64(m_maxVertices * kMaxFrames) * sizeof(LineVertex),
            rhi::BufferUsage::VertexBuffer,
            rhi::MemoryUsage::CPUToGPU);
    }

    void LineCanvas3D::uploadVertexData()
    {
        if (m_vertices.empty())
        {
            return;
        }

        uint32_t frameOffset = m_currentFrameIndex * m_maxVertices;
        uint32_t uploadSize = m_vertices.size() * sizeof(LineVertex);

        // Use the convenient uploadData method
        m_vertexBuffer->uploadData(m_vertices.data(), uploadSize, frameOffset * sizeof(LineVertex));
    }
} // namespace pnkr::renderer::debug