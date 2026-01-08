#pragma once

#include "pnkr/renderer/debug/DebugUtils.hpp"
#include "pnkr/renderer/gpu_shared/LineCanvasShared.h"
#include "pnkr/core/Handle.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <memory>

#include "pnkr/renderer/RHIResourceManager.hpp"

namespace pnkr::renderer
{
    class RHIRenderer;
    struct RHIFrameContext;
}

namespace pnkr::renderer::debug
{
    class DebugLayer : public IDebugSink
    {
    public:
        DebugLayer();
        ~DebugLayer() override;

        DebugLayer(const DebugLayer&) = delete;
        DebugLayer& operator=(const DebugLayer&) = delete;
        DebugLayer(DebugLayer&&) = delete;
        DebugLayer& operator=(DebugLayer&&) = delete;

        void initialize(RHIRenderer* renderer);

        void setDepthTestEnabled(bool enabled) { m_depthTestEnabled = enabled; }

        void clear();

        void line(const glm::vec3& start, const glm::vec3& end, const glm::vec3& color) override;
        void box(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color);
        void box(const glm::mat4& transform, const glm::vec3& size, const glm::vec3& color);
        void plane(const glm::vec3& origin, const glm::vec3& v1, const glm::vec3& v2,
                   int segments1, int segments2, const glm::vec3& color);
        void frustum(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& color);

        const std::vector<glm::vec3>& getLastFrustumCorners() const { return m_lastFrustumCorners; }

        void circle(const glm::vec3& center, float radius, const glm::vec3& normal, const glm::vec3& color, int segments = 32);
        void sphere(const glm::vec3& center, float radius, const glm::vec3& color, int segments = 16);

        void circle(const glm::vec3& center, float radius, const glm::vec3& color, int segments = 32)
        {
            circle(center, radius, glm::vec3(0, 0, 1), color, segments);
        }

        void render(const RHIFrameContext& ctx, const glm::mat4& viewProj);

    private:
        bool hasCapacity(size_t additionalVertices);
        gpu::LineVertex* appendVertices(size_t count);
        void createPipeline();
        void allocateBuffer(uint64_t vertexCount);

        std::vector<gpu::LineVertex> m_verticesPending;
        std::vector<gpu::LineVertex> m_verticesRender;
        std::vector<glm::vec3> m_lastFrustumCorners;
        std::unique_ptr<rhi::RHIBuffer> m_vertexBuffer;
        PipelinePtr m_pipeline;
        RHIRenderer* m_renderer = nullptr;

        static constexpr uint32_t kMaxFrames = 3;
        uint32_t m_maxVertices = 100000;
        bool m_initialized = false;

        bool m_depthTestEnabled = true;
    };
}
