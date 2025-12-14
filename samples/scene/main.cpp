/**
 * @file main.cpp
 * @brief Triangle sample - Basic window and event loop
 *
 * Stage 0: No rendering yet, just event handling
 * Stage 1: Vulkan triangle rendering
 */

#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <pnkr/engine.hpp>

#include "pnkr/renderer/scene/Scene.hpp"
#include "pnkr/renderer/vulkan/PushConstants.h"
#include "pnkr/renderer/vulkan/geometry/Vertex.h"

int main(int argc, char** argv)
{
    namespace fs = std::filesystem;
    constexpr int kLogFps = 60;
    constexpr float kSize = 0.5F;


    std::vector<pnkr::renderer::Vertex> cubeVertices = {
        // Front face (red) - facing +Z
        {.m_position = {-kSize, -kSize, +kSize}, .m_color = {1, 0, 0}, .m_texCoord = {0, 0}},
        {.m_position = {+kSize, -kSize, +kSize}, .m_color = {1, 0, 0}, .m_texCoord = {1, 0}},
        {.m_position = {+kSize, +kSize, +kSize}, .m_color = {1, 0, 0}, .m_texCoord = {1, 1}},
        {.m_position = {-kSize, +kSize, +kSize}, .m_color = {1, 0, 0}, .m_texCoord = {0, 1}},

        // Back face (cyan) - facing -Z
        {.m_position = {+kSize, -kSize, -kSize}, .m_color = {0, 1, 1}, .m_texCoord = {0, 0}},
        {.m_position = {-kSize, -kSize, -kSize}, .m_color = {0, 1, 1}, .m_texCoord = {1, 0}},
        {.m_position = {-kSize, +kSize, -kSize}, .m_color = {0, 1, 1}, .m_texCoord = {1, 1}},
        {.m_position = {+kSize, +kSize, -kSize}, .m_color = {0, 1, 1}, .m_texCoord = {0, 1}},

        // Right face (red) - facing +X
        {.m_position = {+kSize, -kSize, +kSize}, .m_color = {1, 0, 0}, .m_texCoord = {0, 0}},
        {.m_position = {+kSize, -kSize, -kSize}, .m_color = {1, 0, 0}, .m_texCoord = {1, 0}},
        {.m_position = {+kSize, +kSize, -kSize}, .m_color = {1, 0, 0}, .m_texCoord = {1, 1}},
        {.m_position = {+kSize, +kSize, +kSize}, .m_color = {1, 0, 0}, .m_texCoord = {0, 1}},

        // Left face (green) - facing -X
        {.m_position = {-kSize, -kSize, -kSize}, .m_color = {0, 1, 0}, .m_texCoord = {0, 0}},
        {.m_position = {-kSize, -kSize, +kSize}, .m_color = {0, 1, 0}, .m_texCoord = {1, 0}},
        {.m_position = {-kSize, +kSize, +kSize}, .m_color = {0, 1, 0}, .m_texCoord = {1, 1}},
        {.m_position = {-kSize, +kSize, -kSize}, .m_color = {0, 1, 0}, .m_texCoord = {0, 1}},

        // Top face (blue) - facing +Y
        {.m_position = {-kSize, +kSize, +kSize}, .m_color = {0, 0, 1}, .m_texCoord = {0, 0}},
        {.m_position = {+kSize, +kSize, +kSize}, .m_color = {0, 0, 1}, .m_texCoord = {1, 0}},
        {.m_position = {+kSize, +kSize, -kSize}, .m_color = {0, 0, 1}, .m_texCoord = {1, 1}},
        {.m_position = {-kSize, +kSize, -kSize}, .m_color = {0, 0, 1}, .m_texCoord = {0, 1}},

        // Bottom face (yellow) - facing -Y
        {.m_position = {-kSize, -kSize, -kSize}, .m_color = {1, 1, 0}, .m_texCoord = {0, 0}},
        {.m_position = {+kSize, -kSize, -kSize}, .m_color = {1, 1, 0}, .m_texCoord = {1, 0}},
        {.m_position = {+kSize, -kSize, +kSize}, .m_color = {1, 1, 0}, .m_texCoord = {1, 1}},
        {.m_position = {-kSize, -kSize, +kSize}, .m_color = {1, 1, 0}, .m_texCoord = {0, 1}},
    };

    std::vector<uint32_t> cubeIndices = {
        // Front face
        0, 1, 2, 2, 3, 0,
        // Back face
        4, 5, 6, 6, 7, 4,
        // Right face
        8, 9, 10, 10, 11, 8,
        // Left face
        12, 13, 14, 14, 15, 12,
        // Top face
        16, 17, 18, 18, 19, 16,
        // Bottom face
        20, 21, 22, 22, 23, 20,
    };

    constexpr float kPlaneY = -0.6f;
    constexpr float kPlaneHalf = 2.5f;

    std::vector<pnkr::renderer::Vertex> planeVertices = {
            {.m_position = {-kPlaneHalf, kPlaneY, -kPlaneHalf}, .m_color = {1, 1, 1}},
            {.m_position = {+kPlaneHalf, kPlaneY, -kPlaneHalf}, .m_color = {1, 1, 1}},
            {.m_position = {+kPlaneHalf, kPlaneY, +kPlaneHalf}, .m_color = {1, 1, 1}},
            {.m_position = {-kPlaneHalf, kPlaneY, +kPlaneHalf}, .m_color = {1, 1, 1}},
        };

    std::vector<std::uint32_t> planeIndices = {
            0, 2, 1,
            2, 0, 3
        };


    try
        {
        constexpr int kWindowHeight = 600;
        constexpr int kWindowWidth = 800;
            pnkr::Log::init("[%H:%M:%S] [%-8l] %v");
            pnkr::Log::info("PNKR Engine v{}.{}.{}", PNKR_VERSION_MAJOR,
                            PNKR_VERSION_MINOR, PNKR_VERSION_PATCH);

        const fs::path exePath =
            (argc > 0 && argv != nullptr) ? fs::path(argv[0]) : fs::current_path();
        const fs::path shaderDir = exePath.parent_path() / "shaders";
        const fs::path textureDir = exePath.parent_path() / "textures";
        if (!fs::exists(shaderDir))
            {
                pnkr::Log::error("Shader directory not found: {}", shaderDir.string());
            return 1;

            }

        const fs::path cubeVert = shaderDir / "cube.vert.spv";
        const fs::path cubeFrag = shaderDir / "cube.frag.spv";

        if (!fs::exists(cubeVert))
            {
                pnkr::Log::error("Vertex shader not found: {}", cubeVert.string());
            return 1;

            }
        if (!fs::exists(cubeFrag))
            {
                pnkr::Log::error("Fragment shader not found: {}", cubeFrag.string());
            return 1;

            }


        float deltaTime = 0.0f;
        pnkr::Window window("PNKR - Camera scene", kWindowWidth, kWindowHeight);
            pnkr::Log::info("Window created: {}x{}", window.width(), window.height());

        pnkr::renderer::Renderer renderer(window);
        MeshHandle cube = renderer.createMesh(cubeVertices, cubeIndices);
        pnkr::renderer::VulkanPipeline::Config cubeCfg{};
            cubeCfg.m_vertSpvPath = cubeVert;
            cubeCfg.m_fragSpvPath = cubeFrag;
            cubeCfg.m_vertexInput = VertexInputDescription::VertexInputCube();
            cubeCfg.m_descriptorSetLayouts = {renderer.getTextureDescriptorLayout()};
            cubeCfg.m_pushConstantSize = sizeof(PushConstants);
            cubeCfg.m_pushConstantStages = vk::ShaderStageFlagBits::eVertex;
            cubeCfg.m_depth.testEnable = true;
            cubeCfg.m_depth.writeEnable = true;


        PipelineHandle cubePipe = renderer.createPipeline(cubeCfg);

        MeshHandle plane = renderer.createMesh(planeVertices, planeIndices);

        pnkr::renderer::VulkanPipeline::Config planeCfg = cubeCfg;
            planeCfg.m_fragSpvPath = shaderDir / "plane_tint.frag.spv";
        PipelineHandle planePipe = renderer.createPipeline(planeCfg);


        pnkr::renderer::scene::Scene scene;
            scene.camera().lookAt({1.5f, 1.2f, 1.5f}, {0, 0, 0}, {0, 1, 0});

            scene.objects().push_back({.xform = {}, .mesh = cube, .pipe = cubePipe});
            scene.objects().push_back({.xform = {}, .mesh = plane, .pipe = planePipe});

            scene.objects()[1].xform.m_translation = {0.f, -0.75f, 0.f};
            scene.objects()[1].xform.m_scale = {4.f, 1.f, 4.f};

        const fs::path texturePath = textureDir / "blini.png";

        pnkr::renderer::TextureHandle texture = renderer.loadTexture(texturePath);



        pnkr::Timer timer;
        pnkr::Input input;
            scene.cameraController().setPosition({3.0f, 2.0f, 3.0f});
            // Set render callback
            renderer.setRecordFunc([&](const pnkr::renderer::RenderFrameContext& ctx)
            {
                scene.update(ctx.m_deltaTime, ctx.m_extent, input);

                // Bind texture descriptor set before rendering
                vk::DescriptorSet texDescriptor = renderer.getTextureDescriptor(texture);
                ctx.m_cmd.bindDescriptorSets(
                    vk::PipelineBindPoint::eGraphics,
                    renderer.pipelineLayout(cubePipe),
                    0, // first set
                    texDescriptor,
                    nullptr
                );

                scene.record(ctx, renderer);
            });

        int frameCount = 0;


        while (window.isRunning())
            {
            try
                {
                    window.processEvents(&input);
                    deltaTime = timer.deltaTime();

                    // avoid crazy jumps when resizing / breakpoints
                if (deltaTime > 0.05f) deltaTime = 0.05f;


                    renderer.beginFrame(deltaTime);
                    renderer.drawFrame();
                    renderer.endFrame();

                if (++frameCount % kLogFps == 0)
                    {
                        pnkr::Log::debug("Running... (frames: {})", frameCount);

                    }
                }
            catch (const std::exception&e)
                {
                    pnkr::Log::error("Frame error: {}", e.what());
                break;

                }
            }

            pnkr::Log::info("Engine shutdown (rendered {} frames)", frameCount);
        return 0;

        }
    catch (const std::exception&e)
        {
            std::cerr << "FATAL ERROR: " << e.what() << '\n';
        return 1;

        }
    }
