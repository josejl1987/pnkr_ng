#include "../common/GeometryUtils.h"
#include "../common/SampleApp.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include "pnkr/renderer/scene/Scene.hpp"
#include "pnkr/renderer/vulkan/PushConstants.h"
#include "pnkr/renderer/vulkan/geometry/VertexInputDescription.h"

using namespace pnkr;
using namespace pnkr::samples;

class SceneSample : public SampleApp {
    renderer::scene::Scene m_scene;
    MeshHandle m_cube{};
    MeshHandle m_plane{};
    PipelineHandle m_cubePipe{};
    PipelineHandle m_planePipe{};
    TextureHandle m_texture{};

public:
    SceneSample() : SampleApp({"PNKR - Camera scene", 800, 600}) {}

    void onInit() override {
        const auto cubeData = GeometryUtils::getCube();
        const auto planeData = GeometryUtils::getPlane(2.5f, -0.6f);

        m_cube = m_renderer.createMesh(cubeData.vertices, cubeData.indices);
        m_plane = m_renderer.createMesh(planeData.vertices, planeData.indices);

        renderer::VulkanPipeline::Config cubeCfg{};
        cubeCfg.m_vertSpvPath = getShaderPath("cube.vert.spv");
        cubeCfg.m_fragSpvPath = getShaderPath("cube.frag.spv");
        cubeCfg.m_vertexInput = renderer::Vertex::getLayout();
        cubeCfg.m_descriptorSetLayouts = {m_renderer.getTextureDescriptorLayout()};
        cubeCfg.m_pushConstantSize = sizeof(PushConstants);
        cubeCfg.m_pushConstantStages = vk::ShaderStageFlagBits::eVertex;
        cubeCfg.m_depth.testEnable = true;
        cubeCfg.m_depth.writeEnable = true;

        m_cubePipe = m_renderer.createPipeline(cubeCfg);

        renderer::VulkanPipeline::Config planeCfg = cubeCfg;
        planeCfg.m_fragSpvPath = getShaderPath("plane_tint.frag.spv");
        m_planePipe = m_renderer.createPipeline(planeCfg);

        const std::filesystem::path texturePath = baseDir() / "textures" / "blini.png";
        m_texture = m_renderer.loadTexture(texturePath);

        m_scene.camera().lookAt({1.5f, 1.2f, 1.5f}, {0, 0, 0}, {0, 1, 0});
        m_scene.cameraController().setPosition({3.0f, 2.0f, 3.0f});

        m_scene.objects().push_back({.xform = {}, .mesh = m_cube, .pipe = m_cubePipe});

        renderer::scene::Transform planeXf;
        planeXf.m_translation = {0.f, -0.75f, 0.f};
        planeXf.m_scale = {4.f, 1.f, 4.f};
        m_scene.objects().push_back({.xform = planeXf, .mesh = m_plane, .pipe = m_planePipe});
    }

    void onRender(const renderer::RenderFrameContext& ctx) override {
        m_scene.update(ctx.m_deltaTime, ctx.m_extent, m_input);

        vk::DescriptorSet texDescriptor = m_renderer.getTextureDescriptor(m_texture);
        ctx.m_cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            m_renderer.pipelineLayout(m_cubePipe),
            0,
            texDescriptor,
            nullptr);

        m_scene.record(ctx, m_renderer);
    }
};

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    SceneSample app;
    return app.run();
}
