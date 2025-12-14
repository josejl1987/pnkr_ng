#include <pnkr/engine.hpp>
#include <pnkr/renderer/scene/Scene.hpp>
#include <pnkr/renderer/scene/Model.hpp>
#include <filesystem>

#include "pnkr/renderer/vulkan/PushConstants.h"

using namespace pnkr;
using namespace pnkr::renderer;
using namespace pnkr::renderer::scene;

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    Log::init();
    Log::info("Starting GLTF Sample");

    // Fix path handling
    fs::path exePath = argc > 0 ? fs::path(argv[0]).parent_path() : fs::current_path();
    fs::path assetDir = exePath / "assets";
    fs::path shaderDir = exePath / "shaders";

    Window window("PNKR - GLTF Viewer", 1280, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    Renderer renderer(window);
    Input input;
    Timer timer;
    Scene scene;

    // Load Model
    // Ensure you have a 'Duck.glb' or similar in samples/gltf/assets/
    fs::path modelPath = assetDir / "Duck.glb";

    // Fallback if not found (or download it manually for testing)
    if (!fs::exists(modelPath)) {
        Log::error("Model not found at: {}", modelPath.string());
        Log::info("Please place a .glb file there.");
        return 1;
    }

    auto model = Model::load(renderer, modelPath);
    if (!model) {
        return 1;
    }

    // Create Pipeline (Textured)
    PipelineConfig cfg{};
    cfg.m_vertSpvPath = shaderDir / "gltf.vert.spv";
    cfg.m_fragSpvPath = shaderDir / "gltf.frag.spv";
    cfg.m_vertexInput = VertexInputDescription::VertexInputCube(); // Standard vertex layout

    // IMPORTANT: Tell pipeline about texture descriptor set layout
    cfg.m_descriptorSetLayouts = { renderer.getTextureDescriptorLayout() };

    cfg.m_pushConstantSize = sizeof(PushConstants);
    cfg.m_depth.testEnable = true;
    cfg.m_depth.writeEnable = true;

    auto pipeline = renderer.createPipeline(cfg);

    // Setup Camera
    scene.cameraController().setPosition({0.0f, 1.0f, 3.0f});
    scene.cameraController().setMoveSpeed(2.0f);

    // Capture mouse
    window.setRelativeMouseMode(true);

    renderer.setRecordFunc([&](const RenderFrameContext& ctx) {
        // Update Camera
        scene.update(ctx.m_deltaTime, ctx.m_extent, input);

        // Update Model Animations/Transforms
        model->updateTransforms();

        // Render Model Nodes
        for (const auto& node : model->nodes()) {
            if (!node.meshPrimitives) continue;

            PushConstants pc;
            pc.m_model = node.worldTransform.mat4();
            pc.m_viewProj = scene.camera().viewProj();

            for (const auto& prim : *node.meshPrimitives) {
                // Get Material
                const auto& mat = model->materials()[prim.materialIndex];

                // Bind Pipeline
                renderer.bindPipeline(ctx.m_cmd, pipeline);

                // Bind Texture (Material)
                vk::DescriptorSet set = renderer.getTextureDescriptor(mat.baseColorTexture);
                ctx.m_cmd.bindDescriptorSets(
                    vk::PipelineBindPoint::eGraphics,
                    renderer.pipelineLayout(pipeline),
                    0,
                    set,
                    nullptr
                );

                renderer.pushConstants(ctx.m_cmd, pipeline, vk::ShaderStageFlagBits::eVertex, pc);
                renderer.bindMesh(ctx.m_cmd, prim.mesh);
                renderer.drawMesh(ctx.m_cmd, prim.mesh);
            }
        }
    });

    while (window.isRunning()) {
        window.processEvents(&input);

        // Toggle mouse mode
        if (input.isKeyDown(SDL_SCANCODE_ESCAPE)) {
            window.setRelativeMouseMode(false);
        }
        if (input.isMouseButtonDown(SDL_BUTTON_LEFT)) {
            window.setRelativeMouseMode(true);
        }

        renderer.beginFrame(timer.deltaTime());
        renderer.drawFrame();
        renderer.endFrame();
    }

    return 0;
}
