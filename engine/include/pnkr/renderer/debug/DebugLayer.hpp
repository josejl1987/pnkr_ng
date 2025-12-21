#pragma once

#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/rhi/rhi_types.hpp"
#include "pnkr/core/Handle.h"
#include "pnkr/renderer/scene/RHIScene.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <memory>

namespace pnkr::renderer::debug
{
    class DebugLayer
    {
    public:
        DebugLayer();
        ~DebugLayer();

        // Disable copy/move
        DebugLayer(const DebugLayer&) = delete;
        DebugLayer& operator=(const DebugLayer&) = delete;
        DebugLayer(DebugLayer&&) = delete;
        DebugLayer& operator=(DebugLayer&&) = delete;

        // Lifecycle
        void initialize(RHIRenderer* renderer);

        // Configuration
        void setDepthTestEnabled(bool enabled) { m_depthTestEnabled = enabled; }

        // Scene integration (optional)
        void setScene(scene::RHIScene* scene) { m_scene = scene; }
        void clear(); // Clear debug objects for next frame

        // Drawing primitives (these create debug objects that are rendered with the scene)
        void line(const glm::vec3& start, const glm::vec3& end, const glm::vec3& color);
        void box(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color);
        void box(const glm::mat4& transform, const glm::vec3& size, const glm::vec3& color);
        void plane(const glm::vec3& origin, const glm::vec3& v1, const glm::vec3& v2,
                   int segments1, int segments2, const glm::vec3& color);
        void frustum(const glm::mat4& viewProj, const glm::vec3& color);

        // Enhanced primitives with orientation support
        void circle(const glm::vec3& center, float radius, const glm::vec3& normal, const glm::vec3& color, int segments = 32);
        void sphere(const glm::vec3& center, float radius, const glm::vec3& color, int segments = 16);

        // Legacy circle method for backward compatibility (draws on XY plane)
        void circle(const glm::vec3& center, float radius, const glm::vec3& color, int segments = 32)
        {
            circle(center, radius, glm::vec3(0, 0, 1), color, segments);
        }

        // Direct rendering for immediate mode (when not using scene integration)
        void render(const RHIFrameContext& ctx, const glm::mat4& viewProj);

    private:
        struct LineVertex
        {
            glm::vec3 position;
            glm::vec3 color;

            struct SemanticMap
            {
                rhi::VertexSemantic m_semantic;
                uint32_t m_offset;
                rhi::Format m_format;
            };

            // Matches Vertex.h semantic mapping logic
            static std::vector<SemanticMap> getLayout() {
                return {
                    {rhi::VertexSemantic::Position, offsetof(LineVertex, position), rhi::Format::R32G32B32_SFLOAT},
                    {rhi::VertexSemantic::Color, offsetof(LineVertex, color), rhi::Format::R32G32B32_SFLOAT}
                };
            }
        };

        struct PushConstants
        {
            glm::mat4 mvp;
        };

        bool hasCapacity(size_t additionalVertices);
        void createPipeline();
        void allocateBuffer(uint64_t vertexCount);

        std::vector<LineVertex> m_vertices;
        std::unique_ptr<rhi::RHIBuffer> m_vertexBuffer;
        PipelineHandle m_pipeline;
        RHIRenderer* m_renderer = nullptr;
        scene::RHIScene* m_scene = nullptr;

        // Ring buffer management
        static constexpr uint32_t kMaxFrames = 3;
        uint32_t m_maxVertices = 100000;
        bool m_initialized = false;

                // Configuration

                bool m_depthTestEnabled = true;

            };

        } // namespace pnkr::renderer::debug

        