#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <filesystem>
#include "pnkr/rhi/rhi_pipeline.hpp"

namespace pnkr::renderer::debug {

    class IDebugSink {
    public:
        virtual ~IDebugSink() = default;
        virtual void line(const glm::vec3& start, const glm::vec3& end, const glm::vec3& color) = 0;
    };

    namespace utils {
        void box(IDebugSink& sink, const glm::vec3& min, const glm::vec3& max, const glm::vec3& color);
        void box(IDebugSink& sink, const glm::mat4& transform, const glm::vec3& size, const glm::vec3& color);
        void plane(IDebugSink& sink, const glm::vec3& origin, const glm::vec3& v1, const glm::vec3& v2,
                   int segments1, int segments2, const glm::vec3& color);
        void frustum(IDebugSink& sink, const glm::mat4& viewProj, const glm::vec3& color, std::vector<glm::vec3>* outCorners = nullptr);
        void frustum(IDebugSink& sink, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& color, std::vector<glm::vec3>* outCorners = nullptr);
        void circle(IDebugSink& sink, const glm::vec3& center, float radius, const glm::vec3& normal, const glm::vec3& color, int segments = 32);
        void sphere(IDebugSink& sink, const glm::vec3& center, float radius, const glm::vec3& color, int segments = 16);

        pnkr::renderer::rhi::ShaderModuleDescriptor loadSpirvShader(const std::filesystem::path& shaderPath, pnkr::renderer::rhi::ShaderStage stage);
    }
}
