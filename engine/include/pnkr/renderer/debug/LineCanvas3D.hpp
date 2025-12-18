#pragma once

#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/rhi/rhi_types.hpp"
#include "pnkr/core/Handle.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace pnkr::renderer::debug
{
    class LineCanvas3D
    {
    public:
        LineCanvas3D();
        ~LineCanvas3D();

        // Disable copy/move
        LineCanvas3D(const LineCanvas3D&) = delete;
        LineCanvas3D& operator=(const LineCanvas3D&) = delete;
        LineCanvas3D(LineCanvas3D&&) = delete;
        LineCanvas3D& operator=(LineCanvas3D&&) = delete;

        // Lifecycle
        void initialize(RHIRenderer* renderer);
        void beginFrame();
        void endFrame();

        // Drawing primitives
        void line(const glm::vec3& start, const glm::vec3& end, const glm::vec3& color);
        void box(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color);
        void box(const glm::mat4& transform, const glm::vec3& size, const glm::vec3& color);
        void plane(const glm::vec3& origin, const glm::vec3& v1, const glm::vec3& v2,
                   int segments1, int segments2, const glm::vec3& color);
        void frustum(const glm::mat4& viewProj, const glm::vec3& color);
        void circle(const glm::vec3& center, float radius, const glm::vec3& color, int segments = 32);
        void sphere(const glm::vec3& center, float radius, const glm::vec3& color, int segments = 16);

        // Rendering
        void render(const RHIFrameContext& ctx, const glm::mat4& viewProj);

    private:
        struct LineVertex
        {
            glm::vec3 position;
            glm::vec3 color;
        };

        struct PushConstants
        {
            glm::mat4 mvp;
        };

        void createPipeline();
        void createVertexBuffer();
        void uploadVertexData();

        std::vector<LineVertex> m_vertices;
        std::unique_ptr<rhi::RHIBuffer> m_vertexBuffer;
        PipelineHandle m_pipeline;
        RHIRenderer* m_renderer = nullptr;

        // Ring buffer management
        static constexpr uint32_t kMaxFrames = 3;
        uint32_t m_maxVertices = 100000;
        uint32_t m_currentFrameIndex = 0;
        bool m_frameActive = false;
        bool m_initialized = false;
    };
} // namespace pnkr::renderer::debug