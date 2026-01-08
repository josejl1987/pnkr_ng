#pragma once

#include "pnkr/renderer/debug/DebugUtils.hpp"
#include "pnkr/renderer/gpu_shared/LineCanvasShared.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>

#include "pnkr/renderer/rhi_renderer.hpp"

namespace pnkr::renderer::debug
{
    class LineCanvas3D : public IDebugSink
    {
    public:
        LineCanvas3D();
        ~LineCanvas3D() override;

        LineCanvas3D(const LineCanvas3D&) = delete;
        LineCanvas3D& operator=(const LineCanvas3D&) = delete;
        LineCanvas3D(LineCanvas3D&&) = delete;
        LineCanvas3D& operator=(LineCanvas3D&&) = delete;

        void initialize(RHIRenderer* renderer);
        void beginFrame();
        void endFrame();

        void line(const glm::vec3& start, const glm::vec3& end, const glm::vec3& color) override;
        void box(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color);
        void box(const glm::mat4& transform, const glm::vec3& size, const glm::vec3& color);
        void plane(const glm::vec3 &origin, const glm::vec3 &v1,
                   const glm::vec3 &v2, int n1, int n2, const glm::vec3 &color);
        void frustum(const glm::mat4& viewProj, const glm::vec3& color);
        void circle(const glm::vec3& center, float radius, const glm::vec3& color, int segments = 32);
        void sphere(const glm::vec3& center, float radius, const glm::vec3& color, int segments = 16);

        void render(const RHIFrameContext& ctx, const glm::mat4& viewProj);

    private:
        void createPipeline();
        void createVertexBuffer();
        void uploadVertexData();

        std::vector<gpu::LineVertex> m_vertices;
        std::unique_ptr<rhi::RHIBuffer> m_vertexBuffer;
        PipelinePtr m_pipeline;
        RHIRenderer* m_renderer = nullptr;

        static constexpr uint32_t kMaxFrames = 3;
        uint32_t m_maxVertices = 100000;
        uint32_t m_currentFrameIndex = 0;
        bool m_frameActive = false;
        bool m_initialized = false;
    };
}
