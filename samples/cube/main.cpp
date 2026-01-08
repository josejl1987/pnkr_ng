#include "pnkr/renderer/geometry/GeometryUtils.hpp"
#include "../common/SampleApp.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/transform.hpp"
#include "pnkr/renderer/vulkan/PushConstants.h"
#include "pnkr/renderer/geometry/VertexInputDescription.h"

using namespace pnkr;
using namespace pnkr::samples;

class CubeSample : public Application {
    MeshHandle m_cubeMesh;
    PipelineHandle m_cubePipe;
    renderer::scene::Camera m_camera;
    vk::Extent2D m_lastExtent{0, 0};

public:
    CubeSample() : Application({"PNKR - Cube", 800, 600}) {}

    void onInit() override {
        auto cubeData = GeometryUtils::getCube();
        m_cubeMesh = m_renderer.createMesh(cubeData.vertices, cubeData.indices);

        renderer::VulkanPipeline::Config cfg{};
        cfg.m_vertSpvPath = getShaderPath("cube.vert.spv");
        cfg.m_fragSpvPath = getShaderPath("cube.frag.spv");
        cfg.m_pushConstantSize = sizeof(PushConstants);
        cfg.m_pushConstantStages = vk::ShaderStageFlagBits::eVertex;
        cfg.m_depth.testEnable = true;
        cfg.m_depth.writeEnable = true;
        m_cubePipe = m_renderer.createPipeline(cfg);

        m_camera.lookAt({1.5f, 1.2f, 1.5f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
    }

    void onRender(const renderer::RenderFrameContext& ctx) override {
        if (ctx.m_extent.width != m_lastExtent.width || ctx.m_extent.height != m_lastExtent.height) {
            m_lastExtent = ctx.m_extent;
            const float aspect = float(m_lastExtent.width) / float(m_lastExtent.height);
            m_camera.setPerspective(glm::radians(60.0f), aspect, 0.1f, 10.0f);
        }

        static float timeVal = 0.0f;
        timeVal += ctx.m_deltaTime;

        renderer::scene::Transform xform;
        xform.m_rotation = glm::angleAxis(timeVal, glm::vec3{0.0f, 1.0f, 0.0f});

        ctx.m_cmd->bindPipeline(m_renderer.getPipeline(m_cubePipe));
        PushConstants pc{xform.mat4(), m_camera.viewProj()};
        ctx.m_cmd->pushConstants(renderer::rhi::ShaderStage::Vertex, pc);
        auto meshView = m_renderer.getMeshView(m_cubeMesh);
        if (!meshView) return;
        if (!meshView->vertexPulling)
        {
            ctx.m_cmd->bindVertexBuffer(0, meshView->vertexBuffer, 0);
        }
        ctx.m_cmd->bindIndexBuffer(meshView->indexBuffer, 0, false);
        ctx.m_cmd->drawIndexed(meshView->indexCount, 1, 0, 0, 0);
    }
};

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    CubeSample app;
    return app.run();
}

