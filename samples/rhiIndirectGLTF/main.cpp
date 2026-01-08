#include "pnkr/engine.hpp"
#include "pnkr/renderer/IndirectRenderer.hpp"
#include "pnkr/renderer/physics/ClothSystem.hpp"
#include "pnkr/renderer/RenderSettings.hpp"
#include "pnkr/renderer/BRDFLutGenerator.hpp"
#include "pnkr/renderer/io/GLTFLoader.hpp"
#include "pnkr/assets/AssetImporter.hpp"
#include "pnkr/renderer/io/ModelUploader.hpp"
#include "pnkr/core/TaskSystem.hpp"
#include "pnkr/app/Application.hpp"
#include "pnkr/platform/FileDialog.hpp"
#include "pnkr/core/RecentFiles.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/CameraController.hpp"
#include "pnkr/renderer/geometry/GeometryUtils.hpp"
#include "pnkr/renderer/debug/DebugLayer.hpp"
#include "pnkr/core/cvar.hpp"
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <cmath>
#include <mutex>
#include <fstream>
#include <filesystem>

using namespace pnkr;
using namespace pnkr::renderer::scene;
namespace io = pnkr::renderer::io;

namespace
{
    AUTO_CVAR_STRING(s_lastModelPath, "Last loaded model path", "");
    AUTO_CVAR_STRING(s_lastSkyboxPath, "Last loaded skybox path", "", core::CVarFlags::save);
    AUTO_CVAR_BOOL(b_skyboxFlipY, "Flip skybox vertically", true, core::CVarFlags::save);
    AUTO_CVAR_FLOAT(r_camPosX, "Camera X position", -19.2609997f, core::CVarFlags::save);
    AUTO_CVAR_FLOAT(r_camPosY, "Camera Y position", 8.46500015f, core::CVarFlags::save);
    AUTO_CVAR_FLOAT(r_camPosZ, "Camera Z position", -7.31699991f, core::CVarFlags::save);
    AUTO_CVAR_FLOAT(r_camYaw, "Camera Yaw", 20.8011242f, core::CVarFlags::save);
    AUTO_CVAR_FLOAT(r_camPitch, "Camera Pitch", -16.14609803f, core::CVarFlags::save);
    AUTO_CVAR_BOOL(b_useGltfCamera, "Whether to use GLTF camera", false, core::CVarFlags::save);
    AUTO_CVAR_INT(i_logLevel, "Log Level (0=Trace, 6=Off)", 2, core::CVarFlags::save);
    AUTO_CVAR_FLOAT(r_camFov, "Camera FOV", 60.0f, core::CVarFlags::save);
    AUTO_CVAR_FLOAT(r_skyboxRotation, "Skybox Rotation", 0.0f, core::CVarFlags::save);

    bool DrawLightControls(LightSource& light, [[maybe_unused]] int index, bool isShadowCaster, SceneGraphDOD* scene, ecs::Entity entity)
    {
        bool removeRequested = false;
        ImGui::PushID(static_cast<int>(entity));
        std::string id = "##Light" + std::to_string(entity);
        const char* typeName = (light.type == LightType::Directional)
                                   ? "Dir"
                                   : (light.type == LightType::Point)
                                   ? "Point"
                                   : "Spot";
        const char* name = "Light";
        if (scene && entity != ecs::NULL_ENTITY && scene->registry().has<Name>(entity))
        {
            name = scene->registry().get<Name>(entity).str.c_str();
        }

        if (ImGui::TreeNode((void*)(intptr_t)entity, "%s [%s]%s",
                            name, typeName, isShadowCaster ? " (Shadow Caster)" : ""))
        {
            ImGui::Checkbox("Debug Draw", &light.debugDraw);

            if (scene && entity != ecs::NULL_ENTITY)
            {
                glm::vec3 pos = glm::vec3(scene->registry().get<LocalTransform>(entity).matrix[3]);
                const std::string posLabel = "Position" + id;
                if (ImGui::DragFloat3(posLabel.c_str(), &pos.x, 0.1f))
                {
                    scene->registry().get<LocalTransform>(entity).matrix[3] = glm::vec4(pos, 1.0f);
                    scene->markAsChanged(entity);
                }
            }

            int type = static_cast<int>(light.type);
            const char* types[] = {"Directional", "Point", "Spot"};
            const std::string typeLabel = "Type" + id;
            if (ImGui::Combo(typeLabel.c_str(), &type, types, 3))
            {
                light.type = static_cast<LightType>(type);
            }

            const std::string colorLabel = "Color" + id;
            ImGui::ColorEdit3(colorLabel.c_str(), &light.color.x);
            const std::string intensityLabel = "Intensity" + id;
            ImGui::DragFloat(intensityLabel.c_str(), &light.intensity, 0.1f, 0.0f, 100.0f);

            if (light.type != LightType::Directional)
            {
                const std::string directionLabel = "Direction" + id;
                if (ImGui::DragFloat3(directionLabel.c_str(), &light.direction.x, 0.01f, -1.0f, 1.0f))
                {
                    if (glm::length(light.direction) > 0.0001f)
                    {
                        light.direction = glm::normalize(light.direction);
                    }
                }
            }

            if (light.type == LightType::Spot)
            {
                float innerDeg = glm::degrees(light.innerConeAngle);
                float outerDeg = glm::degrees(light.outerConeAngle);
                const std::string innerLabel = "Inner Angle" + id;
                if (ImGui::DragFloat(innerLabel.c_str(), &innerDeg, 1.0f, 0.0f, 90.0f))
                {
                    light.innerConeAngle = glm::radians(innerDeg);
                }
                const std::string outerLabel = "Outer Angle" + id;
                if (ImGui::DragFloat(outerLabel.c_str(), &outerDeg, 1.0f, 0.0f, 90.0f))
                {
                    light.outerConeAngle = glm::radians(outerDeg);
                }
            }

            if (light.type != LightType::Directional)
            {
                const std::string rangeLabel = "Range" + id;
                ImGui::DragFloat(rangeLabel.c_str(), &light.range, 0.5f, 0.0f, 1000.0f);
            }

            if (ImGui::Button("Remove Light"))
            {
                removeRequested = true;
            }

            ImGui::TreePop();
        }
        ImGui::PopID();
        return removeRequested;
    }
}

class IndirectSample : public app::Application
{
public:
    IndirectSample() : Application({
        .title = "Indirect Rendering - Bistro", .width = 1824, .height = 928, .rendererConfig = {}
    })
    {
    }

    std::shared_ptr<ModelDOD> m_model;
    std::unique_ptr<renderer::IndirectRenderer> m_indirectRenderer;
    Camera m_camera;
    Camera m_lightCamera;
    CameraController m_cameraController{
        {r_camPosX.get(), r_camPosY.get(), r_camPosZ.get()}, r_camYaw.get(), r_camPitch.get()
    };
    renderer::TexturePtr m_brdfLut;
    renderer::TexturePtr m_irradiance;
    renderer::TexturePtr m_prefilter;
    std::unique_ptr<renderer::debug::DebugLayer> m_debugLayer;
    bool m_debugLayerInitialized = false;

    struct LoadTask : enki::ITaskSet {
        std::filesystem::path path;
        std::unique_ptr<pnkr::assets::ImportedModel> result;
        std::mutex resultMutex;
        pnkr::assets::LoadProgress* progress = nullptr;
        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override {
            (void)range; (void)threadnum;
            auto loaded = pnkr::assets::AssetImporter::loadGLTF(path, progress);
            {
                std::lock_guard<std::mutex> lock(resultMutex);
                result = std::move(loaded);
            }
        }
    };
    LoadTask m_loadTask;
    pnkr::assets::LoadProgress m_loadProgress;
    bool m_isLoading = false;
    bool m_showLoadingModal = false;

    bool m_useGltfCamera = false;
    int32_t m_activeGltfCameraNode = -1;
    std::vector<uint32_t> m_gltfCameraNodes;

    std::optional<std::filesystem::path> m_pendingLoad;
    std::filesystem::path m_pendingPath;
    std::optional<std::filesystem::path> m_pendingSkyboxPath;

    core::RecentFiles m_recent{"rhiIndirectGLTF", 12};
    bool m_showMaterialEditor = true;
    int m_selectedMaterial = 0;
    bool m_materialDirty = false;
    glm::vec3 m_cameraPosUI{0.0f};
    glm::vec3 m_cameraTargetUI{0.0f, 0.0f, -1.0f};
    float m_fov = 60.0f;
    bool m_showShadowMap = true;

    void onInit() override
    {
        core::CVarSystem::loadFromIni(baseDir() / "user_settings.ini");
        core::Logger::setLevel(static_cast<core::LogLevel>(i_logLevel.get()));
        m_recent.load();

        if (b_useGltfCamera.get()) {
            m_useGltfCamera = true;
        }

        // Apply loaded camera state
        m_cameraController.setPosition({r_camPosX.get(), r_camPosY.get(), r_camPosZ.get()});
        m_cameraController.setRotation(r_camYaw.get(), r_camPitch.get());

        // Style tweaks for a cleaner look
        ImGui::GetStyle().FrameRounding = 4.0f;
        ImGui::GetStyle().GrabRounding = 4.0f;
        ImGui::GetStyle().WindowPadding = ImVec2(10, 10);
        ImGui::GetStyle().ItemSpacing = ImVec2(8, 6);

        m_debugLayer = std::make_unique<renderer::debug::DebugLayer>();
        m_debugLayer->initialize(m_renderer.get());
        m_debugLayer->setDepthTestEnabled(true);
        m_debugLayerInitialized = true;

        // Check if BRDF LUT exists, generate if missing
        const std::filesystem::path brdfPath = "assets/brdf_lut.ktx2";
        if (!std::filesystem::exists(brdfPath)) {
            core::Logger::info("BRDF LUT not found, generating...");
            
            std::error_code ec;
            std::filesystem::create_directories(brdfPath.parent_path(), ec);
            
            if (renderer::BRDFLutGenerator::generateAndSave(m_renderer.get(), brdfPath.string())) {
                core::Logger::info("BRDF LUT generated successfully");
            } else {
                core::Logger::warn("Failed to generate BRDF LUT, PBR will look flat");
            }
        }

        m_brdfLut = m_renderer->assets()->loadTextureKTX("assets/brdf_lut.ktx2");
        m_irradiance = m_renderer->assets()->loadTextureKTX("assets/immenstadter_horn_2k_irradiance.ktx");
        m_prefilter = m_renderer->assets()->loadTextureKTX("assets/immenstadter_horn_2k_prefilter.ktx");

        if (!m_brdfLut.isValid() ||
            !m_irradiance.isValid() ||
            !m_prefilter.isValid())
        {
            core::Logger::warn("One or more IBL textures failed to load. PBR will look flat.");
        }

        const std::string lastSkyboxPath = s_lastSkyboxPath.get();
        if (!lastSkyboxPath.empty() && std::filesystem::exists(lastSkyboxPath))
        {
            m_pendingSkyboxPath = lastSkyboxPath;
        }

        std::string modelToLoad = s_lastModelPath.get();
        if (modelToLoad.empty() || !std::filesystem::exists(modelToLoad)) {
            modelToLoad = (baseDir() / "assets/rubber_duck/scene.gltf").string();
        }
        loadModel(modelToLoad);

        m_cameraController.applyToCamera(m_camera);
        m_fov = r_camFov.get();
        m_camera.setPerspective(glm::radians(m_fov), (float)m_config.width / m_config.height, 0.1f, 1000.0f);
        m_cameraPosUI = m_cameraController.position();
        m_cameraTargetUI = m_cameraPosUI + m_cameraController.front();
    }

    void onShutdown() override
    {
        // Persistence
        if (m_model) {
            s_lastModelPath.set(m_loadTask.path.string());
            s_lastModelPath.flags = s_lastModelPath.flags | core::CVarFlags::save;
        }
        
        r_camPosX.set(m_cameraController.position().x);
        r_camPosY.set(m_cameraController.position().y);
        r_camPosZ.set(m_cameraController.position().z);
        r_camYaw.set(m_cameraController.yaw());
        r_camPitch.set(m_cameraController.pitch());
        r_camFov.set(m_fov);
        if (m_indirectRenderer) {
            r_skyboxRotation.set(m_indirectRenderer->settings().skyboxRotation);
        }
        b_useGltfCamera.set(m_useGltfCamera);
        i_logLevel.set(static_cast<int>(core::Logger::getLevel()));

        core::CVarSystem::saveToIni(baseDir() / "user_settings.ini");

        if (m_renderer && m_renderer->device()) {
            m_renderer->device()->waitIdle();
        }

        m_indirectRenderer.reset();
        m_model.reset();
        m_debugLayer.reset();

        Application::onShutdown();
    }

    void loadModel(const std::filesystem::path& path)
    {
        if (m_isLoading && !m_loadTask.GetIsComplete())
        {
            pnkr::core::Logger::warn("Model load already in progress.");
            return;
        }

        core::Logger::info("Loading model ASYNC from: {}", path.string());
        m_loadTask.path = path;
        m_loadProgress.reset();
        m_loadTask.progress = &m_loadProgress;
        m_isLoading = true;
        m_showLoadingModal = true;
        core::TaskSystem::scheduler().AddTaskSetToPipe(&m_loadTask);

        m_recent.add(path);
    }

    void finalizeLoad()
    {
        m_indirectRenderer.reset();
        m_model.reset();

        std::unique_ptr<pnkr::assets::ImportedModel> loaded;
        {
            std::lock_guard<std::mutex> lock(m_loadTask.resultMutex);
            loaded = std::move(m_loadTask.result);
        }

        if (!loaded) {
            core::Logger::error("Failed to load model");
            m_isLoading = false;
            return;
        }

        m_model = io::ModelUploader::upload(*m_renderer, std::move(*loaded));

        {
            auto planeData = renderer::geometry::GeometryUtils::getPlane(50.0f, 50.0f, 1);
            m_model->addPrimitiveToScene(*m_renderer, planeData, 0, glm::mat4(1.0f), "GroundPlane");
        }
        m_model->dropCpuGeometry();

        if (!m_model->animations().empty())
        {
            auto& state = m_model->animationState();
            state.animIndex = 0;
            state.isPlaying = true;
            state.isLooping = true;
            core::Logger::info("Playing animation 0: {}", m_model->animations()[0].name);
        }

        m_indirectRenderer = std::make_unique<renderer::IndirectRenderer>();
        m_indirectRenderer->init(m_renderer.get(), m_model, m_brdfLut.handle(), m_irradiance.handle(), m_prefilter.handle());

        if (m_pendingSkyboxPath && std::filesystem::exists(*m_pendingSkyboxPath))
        {
            m_indirectRenderer->loadEnvironmentMap(*m_pendingSkyboxPath, b_skyboxFlipY.get());
        }
        m_indirectRenderer->settings().skyboxRotation = r_skyboxRotation.get();

        m_renderer->setRecordFunc([this](const renderer::RHIFrameContext& ctx)
        {
            onRecord(ctx);
        });

        m_renderer->setComputeRecordFunc([this](const renderer::RHIFrameContext& ctx)
        {
            if (m_indirectRenderer)
            {
                m_indirectRenderer->dispatchSkinning(ctx.commandBuffer);
            }
        });

        m_activeGltfCameraNode = -1;
        rebuildGltfCameraNodeList();
        m_isLoading = false;
    }

    void rebuildGltfCameraNodeList()
    {
        m_gltfCameraNodes.clear();
        if (!m_model) return;
        const auto& scene = m_model->scene();
        for (ecs::Entity n : scene.topoOrder())
        {
            if (scene.registry().has<CameraComponent>(n))
                m_gltfCameraNodes.push_back(static_cast<uint32_t>(n));
        }

        if (m_activeGltfCameraNode < 0 && !m_gltfCameraNodes.empty())
            m_activeGltfCameraNode = (int32_t)m_gltfCameraNodes[0];
    }

    void applySelectedGltfCamera(float viewportAspect)
    {
        if (!m_model) return;
        auto& scene = m_model->scene();
        if (m_activeGltfCameraNode < 0) return;
        const uint32_t nodeId = (uint32_t)m_activeGltfCameraNode;
        ecs::Entity entity = static_cast<ecs::Entity>(nodeId);
        if (!scene.registry().has<CameraComponent>(entity)) return;
        if (!scene.registry().has<WorldTransform>(entity)) return;

        const int32_t camIndex = scene.registry().get<CameraComponent>(entity).cameraID;
        if (camIndex < 0 || (size_t)camIndex >= m_model->cameras().size()) return;

        const glm::mat4 world = scene.registry().get<WorldTransform>(entity).matrix;
        const glm::mat4 view = glm::affineInverse(world);
        m_camera.setViewMatrix(view);

        const auto& gc = m_model->cameras()[(size_t)camIndex];
        if (gc.type == GltfCamera::Type::Perspective)
        {
            const float aspect = (gc.aspectRatio > 0.0f) ? gc.aspectRatio : viewportAspect;
            const float zfar = (gc.zfar > 0.0f) ? gc.zfar : 10000.0f;
            m_camera.setPerspective(gc.yfovRad, aspect, gc.znear, zfar);
        }
        else
        {

            const float l = -gc.xmag;
            const float r = gc.xmag;
            const float b = -gc.ymag;
            const float t = gc.ymag;
            m_camera.setOrthographic(l, r, b, t, gc.znear, gc.zfar);
        }
    }

    void onUpdate(float dt) override
    {
        if (m_isLoading && m_loadTask.GetIsComplete())
        {
            finalizeLoad();
        }

        if (m_pendingLoad)
        {
            loadModel(*m_pendingLoad);
            m_pendingLoad.reset();
        }

        const float aspect = (float)m_config.width / (float)m_config.height;

        if (!m_useGltfCamera)
        {
            m_camera.setPerspective(glm::radians(m_fov), aspect, 0.1f, 1000.0f);
            m_cameraController.update(m_input, dt);
            m_cameraController.applyToCamera(m_camera);
        }
        else
        {

            m_model->scene().updateTransforms();
            applySelectedGltfCamera(aspect);
        }

        if (m_indirectRenderer)
        {
            m_indirectRenderer->update(dt);
        }
    }

    void onEvent(const SDL_Event& event) override
    {
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            if (event.key.scancode == SDL_SCANCODE_P)
            {
                static bool freeze = false;
                freeze = !freeze;
                if (m_indirectRenderer) m_indirectRenderer->setFreezeCullingView(freeze);
            }
        }
    }

    void onImGui() override
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open glTF/glb..."))
                {
                    if (auto p = platform::FileDialog::OpenGLTFDialog())
                        m_pendingLoad = *p;
                }

                if (auto pick = m_recent.drawImGuiMenu("Recent Files"))
                {
                    m_pendingLoad = *pick;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        if (m_showLoadingModal && m_isLoading)
        {
            ImGui::OpenPopup("Loading Model");
        }

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Loading Model", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoMove))
        {
            const float progress = m_loadProgress.getProgress();
            const std::string stage = m_loadProgress.getCurrentStageString();

            ImGui::Text("%s", stage.c_str());
            ImGui::Spacing();

            ImGui::ProgressBar(progress, ImVec2(400.0f, 0.0f));

            ImGui::Spacing();

            if (m_loadProgress.currentStage == pnkr::assets::LoadStage::LoadingTextures)
            {
                ImGui::Text("Textures: %u / %u",
                           m_loadProgress.texturesLoaded.load(),
                           m_loadProgress.texturesTotal.load());
            }
            else if (m_loadProgress.currentStage == pnkr::assets::LoadStage::ProcessingMeshes)
            {
                ImGui::Text("Primitives: %u / %u",
                           m_loadProgress.meshesProcessed.load(),
                           m_loadProgress.meshesTotal.load());
            }

            if (!m_isLoading || m_loadTask.GetIsComplete())
            {
                m_showLoadingModal = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        if (!m_model || !m_indirectRenderer) return;

        if (ImGui::Begin("Renderer Settings"))
        {
            auto& settings = m_indirectRenderer->settings();

            // --- SECTION 1: PERFORMANCE & SYSTEM ---
            if (ImGui::CollapsingHeader("Performance & System", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Columns(2, "StatsColumns", false);
                ImGui::SetColumnWidth(0, 150);

                ImGui::Text("FPS:"); ImGui::NextColumn();
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "%.1f (%.3f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
                ImGui::NextColumn();

                ImGui::Text("Visible Meshes:"); ImGui::NextColumn();
                ImGui::Text("%u", m_indirectRenderer->getVisibleMeshCount());
                ImGui::NextColumn();

                auto streamStats = m_renderer->assets()->getStreamingStatistics();
                ImGui::Text("Streaming:"); ImGui::NextColumn();
                if (streamStats.queuedAssets > 0) {
                    ImGui::ProgressBar(0.5f, ImVec2(-1, 0), "Active");
                } else {
                    ImGui::Text("Idle");
                }
                ImGui::NextColumn();

                ImGui::Columns(1);
                ImGui::Separator();

                ImGui::Checkbox("VSync", &m_vsync);
                if (ImGui::IsItemDeactivatedAfterEdit()) m_renderer->setVsync(m_vsync);

                ImGui::Checkbox("GPU Profiler", &m_showGpuProfiler);

                ImGui::Separator();

                // MSAA Controls
                if (auto* msaaCVar = static_cast<pnkr::core::CVar<int>*>(pnkr::core::CVarSystem::find("r_msaa")))
                {
                    int currentMsaa = msaaCVar->get();
                    int msaaIndex = 0;
                    if (currentMsaa >= 4) msaaIndex = 2;
                    else if (currentMsaa >= 2) msaaIndex = 1;

                    const char* msaaOptions[] = { "Off (1x)", "2x", "4x" };
                    if (ImGui::Combo("MSAA", &msaaIndex, msaaOptions, 3))
                    {
                        msaaCVar->set(1 << msaaIndex);
                        m_indirectRenderer->resize(m_config.width, m_config.height);
                    }

                    if (msaaCVar->get() > 1)
                    {
                        if (auto* msaaShadingCVar = static_cast<pnkr::core::CVar<bool>*>(pnkr::core::CVarSystem::find("r_msaaSampleShading")))
                        {
                            bool shading = msaaShadingCVar->get();
                            if (ImGui::Checkbox("Sample Shading", &shading))
                            {
                                msaaShadingCVar->set(shading);
                                m_indirectRenderer->resize(m_config.width, m_config.height);
                            }
                        }
                    }
                }

                ImGui::Separator();

                int logItem = static_cast<int>(pnkr::core::Logger::getLevel());
                const char* logLevels[] = { "Trace", "Debug", "Info", "Warn", "Error", "Critical", "Off" };
                if (ImGui::Combo("Log Level", &logItem, logLevels, 7)) {
                    pnkr::core::Logger::setLevel(static_cast<pnkr::core::LogLevel>(logItem));
                }

                ImGui::Separator();
                if (ImGui::Button("Wipe Asset Cache")) {
                    std::error_code ec;
                    std::filesystem::remove_all(resolveBasePath() / ".cache", ec);
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Debug Lines")) {
                    if (m_debugLayer) m_debugLayer->clear();
                }
            }

            // --- SECTION 2: SCENE & CAMERA ---
            if (ImGui::CollapsingHeader("Scene & Camera"))
            {
                if (ImGui::TreeNode("glTF Cameras"))
                {
                    ImGui::Checkbox("Use glTF camera", &m_useGltfCamera);
                    if (!m_gltfCameraNodes.empty())
                    {
                        const auto& scene = m_model->scene();
                        int currentIdx = 0;
                        for (int i = 0; i < (int)m_gltfCameraNodes.size(); ++i)
                            if ((int32_t)m_gltfCameraNodes[i] == m_activeGltfCameraNode)
                            {
                                currentIdx = i;
                                break;
                            }

                        const std::string currentIdxStr = std::to_string(currentIdx);
                        if (ImGui::BeginCombo("Active Camera", currentIdxStr.c_str()))
                        {
                            for (int i = 0; i < (int)m_gltfCameraNodes.size(); ++i)
                            {
                                const uint32_t nodeId = m_gltfCameraNodes[i];
                                ecs::Entity entity = static_cast<ecs::Entity>(nodeId);
                                int32_t camIndex = -1;
                                if (scene.registry().has<CameraComponent>(entity))
                                    camIndex = scene.registry().get<CameraComponent>(entity).cameraID;

                                const char* camName = (camIndex >= 0 && (size_t)camIndex < m_model->cameras().size())
                                                          ? m_model->cameras()[(size_t)camIndex].name.c_str()
                                                          : "(unnamed)";

                                std::string label = std::to_string(i) + " | " + camName;
                                if (ImGui::Selectable(label.c_str(), (int32_t)nodeId == m_activeGltfCameraNode))
                                    m_activeGltfCameraNode = (int32_t)nodeId;
                            }
                            ImGui::EndCombo();
                        }
                    }
                    ImGui::TreePop();
                }
 
                ImGui::Separator();
                ImGui::SliderFloat("FOV", &m_fov, 10.0f, 120.0f, "%.1f deg");
 
                if (ImGui::TreeNode("Free Camera Controls"))
                {
                    ImGui::BeginDisabled(m_useGltfCamera);
                    ImGui::InputFloat3("Position", &m_cameraPosUI.x);
                    ImGui::InputFloat3("Target", &m_cameraTargetUI.x);
                    if (ImGui::Button("Use Current View"))
                    {
                        m_cameraPosUI = m_cameraController.position();
                        m_cameraTargetUI = m_cameraPosUI + m_cameraController.front();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Apply"))
                    {
                        m_useGltfCamera = false;
                        m_cameraController.setLookAt(m_cameraPosUI, m_cameraTargetUI);
                        m_cameraController.applyToCamera(m_camera);
                    }
                    ImGui::EndDisabled();
                    ImGui::TreePop();
                }

                ImGui::Separator();
                ImGui::Checkbox("Wireframe Mode", &settings.drawWireframe);
                ImGui::Checkbox("Draw Debug Bounds", &settings.drawDebugBounds);

                const char* cullingModes[] = { "None", "CPU", "GPU" };
                int currentMode = static_cast<int>(settings.cullingMode);
                if (ImGui::Combo("Culling Mode", &currentMode, cullingModes, 3)) {
                    m_indirectRenderer->setCullingMode(static_cast<renderer::CullingMode>(currentMode));
                }
                ImGui::Checkbox("Freeze Culling View (P)", &settings.freezeCulling);
            }

            // --- SECTION: ANIMATIONS ---
            if (ImGui::CollapsingHeader("Animations", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const auto& animations = m_model->animations();
                if (animations.empty())
                {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No animations in this model");
                }
                else
                {
                    auto& state = m_model->animationState();
                    
                    // Animation selector
                    int currentAnim = (state.animIndex == ~0u) ? -1 : static_cast<int>(state.animIndex);
                    std::vector<const char*> animNames;
                    animNames.reserve(animations.size() + 1);
                    animNames.push_back("(None)");
                    for (const auto& anim : animations)
                        animNames.push_back(anim.name.c_str());
                    
                    int comboIdx = currentAnim + 1;
                    if (ImGui::Combo("Animation", &comboIdx, animNames.data(), static_cast<int>(animNames.size())))
                    {
                        if (comboIdx == 0)
                        {
                            state.animIndex = ~0u;
                            state.isPlaying = false;
                        }
                        else
                        {
                            state.animIndex = static_cast<uint32_t>(comboIdx - 1);
                            state.currentTime = 0.0f;
                            state.isPlaying = true;
                        }
                    }
                    
                    if (state.animIndex != ~0u && state.animIndex < static_cast<uint32_t>(animations.size()))
                    {
                        const auto& anim = animations[state.animIndex];
                        
                        // Playback controls
                        ImGui::Checkbox("Playing", &state.isPlaying);
                        ImGui::SameLine();
                        ImGui::Checkbox("Loop", &state.isLooping);
                        
                        // Timeline slider
                        float duration = anim.duration;
                        if (duration > 0.0f)
                        {
                            ImGui::SliderFloat("Time", &state.currentTime, 0.0f, duration, "%.2f s");
                        }
                        
                        // Animation info
                        ImGui::Text("Duration: %.2f s", duration);
                        ImGui::Text("Channels: %zu", anim.channels.size());
                        
                        // Reset button
                        if (ImGui::Button("Reset"))
                        {
                            state.currentTime = 0.0f;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Play from Start"))
                        {
                            state.currentTime = 0.0f;
                            state.isPlaying = true;
                        }
                    }
                }
            }

            // --- SECTION 3: LIGHTING & SHADOWS ---
            if (ImGui::CollapsingHeader("Lighting & Shadows"))
            {
                ImGui::SliderFloat("IBL Strength", &settings.iblStrength, 0.0f, 5.0f);
                if (ImGui::IsItemDeactivatedAfterEdit()) m_indirectRenderer->setIBLStrength(settings.iblStrength);
 
                if (ImGui::SliderFloat("Skybox Rotation", &settings.skyboxRotation, 0.0f, 360.0f, "%.1f deg"))
                {
                    m_indirectRenderer->uploadEnvironmentData();
                }
 
                bool flipSkyboxY = b_skyboxFlipY.get();
                if (ImGui::Checkbox("Flip Skybox Y", &flipSkyboxY))
                {
                    b_skyboxFlipY.set(flipSkyboxY);
                    const std::string lastSkyboxPath = s_lastSkyboxPath.get();
                    if (!lastSkyboxPath.empty() && std::filesystem::exists(lastSkyboxPath))
                    {
                        m_indirectRenderer->loadEnvironmentMap(lastSkyboxPath, flipSkyboxY);
                    }
                }

                if (ImGui::Button("Load Skybox (KTX2)"))
                {
                    auto path = platform::FileDialog::OpenImageDialog();
                    if (path)
                    {
                        s_lastSkyboxPath.set(path->string());
                        m_indirectRenderer->loadEnvironmentMap(*path, b_skyboxFlipY.get());
                    }
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Select a .ktx2 cubemap file to use as environment map");
                }

                if (ImGui::TreeNode("Shadow Settings"))
                {
                    ImGui::Checkbox("Show Shadow Map", &m_showShadowMap);
                    ImGui::Checkbox("Debug Light View", &settings.debugLightView);
                    ImGui::Text("Shadow View matches Directional Light.");
                    ImGui::Text("Shadow Bias is now controlled by 'r_shadowBias' CVAR.");
                    ImGui::SliderFloat("Shadow Slope Bias", &settings.shadow.biasSlope, 0.0f, 10.0f);
                    ImGui::SliderFloat("Shadow Ortho Size", &settings.shadow.orthoSize, 1.0f, 100.0f);
                    ImGui::SliderFloat("Shadow Distance", &settings.shadow.distFromCam, 1.0f, 100.0f);
                    ImGui::TreePop();
                }

                if (ImGui::TreeNode("Scene Lights"))
                {
                    if (ImGui::Button("Add Directional")) {
                        Light l{}; l.m_type = LightType::Directional; l.m_intensity = 1.0f;
                        l.m_direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.2f));
                        m_model->addLight(l, glm::mat4(1.0f), "New Directional");
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Add Point")) {
                        Light l{}; l.m_type = LightType::Point; l.m_intensity = 5.0f; l.m_range = 20.0f;
                        glm::mat4 t = glm::translate(glm::mat4(1.0f), m_cameraController.position() + m_cameraController.front() * 5.0f);
                        m_model->addLight(l, t, "New Point");
                    }

                    int shadowIdx = m_indirectRenderer->getShadowCasterIndex();
                    const auto& scene = m_model->scene();
                    auto lightPool = scene.registry().view<LightSource>();
                    int currentUIIdx = 0;
                    ecs::Entity toRemove = ecs::NULL_ENTITY;
                    int removeIdx = -1;

                    lightPool.each([&](ecs::Entity entity, LightSource& ls) {
                        if (DrawLightControls(ls, currentUIIdx, currentUIIdx == shadowIdx, const_cast<SceneGraphDOD*>(&scene), entity)) {
                            toRemove = entity; removeIdx = currentUIIdx;
                        }
                        currentUIIdx++;
                    });
                    if (toRemove != ecs::NULL_ENTITY) m_model->removeLight(removeIdx);
                    ImGui::TreePop();
                }
            }

            // --- SECTION 4: EFFECTS ---
            if (ImGui::CollapsingHeader("Post-Processing & Effects"))
            {
                if (ImGui::BeginTabBar("EffectTabs"))
                {
                    if (ImGui::BeginTabItem("Tonemapping & Bloom"))
                    {
                        const char* items[] = {"None", "Reinhard", "Uchimura", "Khronos PBR"};
                        int item = static_cast<int>(settings.hdr.mode);
                        if (ImGui::Combo("Tone Mapper", &item, items, 4))
                            settings.hdr.mode = static_cast<renderer::HDRSettings::ToneMapMode>(item);

                        ImGui::SliderFloat("Exposure", &settings.hdr.exposure, 0.1f, 5.0f);

                        ImGui::Separator();
                        ImGui::Checkbox("Enable Bloom", &settings.hdr.enableBloom);
                        if (settings.hdr.enableBloom) {
                            ImGui::SliderFloat("Bloom Strength", &settings.hdr.bloomStrength, 0.0f, 1.0f);
                            ImGui::SliderFloat("Bloom Threshold", &settings.hdr.bloomThreshold, 0.0f, 5.0f);
                            ImGui::SliderInt("Bloom Passes", &settings.hdr.bloomPasses, 1, 6);
                        }
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Anti-Aliasing (MSAA)"))
                    {
                        const char* msaaItems[] = { "Off (1x)", "2x MSAA", "4x MSAA" };
                        int currentMsaa = 0;
                        if (settings.msaa.sampleCount == 2) currentMsaa = 1;
                        else if (settings.msaa.sampleCount == 4) currentMsaa = 2;

                        if (ImGui::Combo("MSAA Samples", &currentMsaa, msaaItems, 3))
                        {
                            int samples = 1;
                            if (currentMsaa == 1) samples = 2;
                            else if (currentMsaa == 2) samples = 4;
                            
                            // Update CVar which will be picked up by next createGlobalResources
                            // We need to trigger a resize to apply it immediately
                            auto* cvar = core::CVarSystem::find("r_msaa");
                            if (cvar) cvar->setFromString(std::to_string(samples));
                            
                            m_indirectRenderer->resize(m_config.width, m_config.height);
                        }

                        if (ImGui::Checkbox("Per-Sample Shading", &settings.msaa.sampleShading))
                        {
                            auto* cvar = core::CVarSystem::find("r_msaaSampleShading");
                            if (cvar) cvar->setFromString(settings.msaa.sampleShading ? "true" : "false");
                            
                            m_indirectRenderer->resize(m_config.width, m_config.height);
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Higher quality edges and textures but much more expensive");

                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
            }

            // --- SECTION 5: SSAO ---
            if (ImGui::CollapsingHeader("SSAO (Ambient Occlusion)"))
            {
                ImGui::Checkbox("Enable SSAO", &settings.ssao.enabled);
                ImGui::SliderFloat("Radius", &settings.ssao.radius, 0.001f, 2.0f);
                ImGui::SliderFloat("Intensity", &settings.ssao.intensity, 0.0f, 10.0f);
                ImGui::SliderFloat("Attenuation Scale", &settings.ssao.attScale, 0.0f, 2.0f);
                ImGui::SliderFloat("Distance Scale", &settings.ssao.distScale, 0.0f, 5.0f);
                ImGui::SliderFloat("Blur Sharpness", &settings.ssao.blurSharpness, 0.0f, 100.0f);
                ImGui::SliderFloat("Ambient Bias", &settings.ssao.bias, 0.0f, 1.0f);

                auto h = m_indirectRenderer->getSSAOTexture();
                auto id = m_imgui.getTextureID(h);
                if (id != 0) {
                    float w = ImGui::GetContentRegionAvail().x;
                    ImGui::Image(id, ImVec2(w, w * 9.0f / 16.0f));
                }
            } // SSAO

            // --- SECTION 6: TRANSPARENCY ---
            if (ImGui::CollapsingHeader("Transparency (OIT)"))
            {
                const char* oitMethods[] = { "Linked Buffer (Accuracy)", "WBOIT (Performance)", "None (Depth-Sorted)" };
                int currentMethod = static_cast<int>(settings.oitMethod);
                if (ImGui::Combo("OIT Method", &currentMethod, oitMethods, 3)) {
                    settings.oitMethod = static_cast<renderer::OITMethod>(currentMethod);
                }

                if (settings.oitMethod == renderer::OITMethod::WBOIT) {
                    ImGui::Separator();
                    ImGui::Text("WBOIT Settings");
                    ImGui::SliderFloat("Opacity Boost", &settings.wboit.opacityBoost, 0.0f, 1.0f);
                    ImGui::Checkbox("Show Overlap Heatmap", &settings.wboit.showHeatmap);
                    
                    ImGui::BulletText("Best for: Complex many-layered transparency");
                    ImGui::BulletText("Note: Memory usage is fixed (Accum + Reveal)");
                } else if (settings.oitMethod == renderer::OITMethod::LinkedBuffer) {
                    ImGui::BulletText("Best for: Exact order accuracy");
                    ImGui::BulletText("Note: Memory usage depends on max nodes");
                } else {
                    ImGui::BulletText("Simple depth-sorted transparency");
                    ImGui::BulletText("Best for: Single-layer transparent objects");
                    ImGui::BulletText("Note: May have visual artifacts with overlapping transparency");
                }
            }

            // --- SECTION 7: CLOTH PHYSICS ---
            if (ImGui::CollapsingHeader("Cloth Physics"))
            {
                if (auto* cloth = m_indirectRenderer->getClothSystem())
                {
                    glm::vec3 wind = cloth->getWindDirection();
                    if (ImGui::DragFloat3("Wind Direction", &wind.x, 0.01f, -1.0f, 1.0f))
                    {
                        if (glm::length(wind) > 0.0001f) wind = glm::normalize(wind);
                        cloth->setWindDirection(wind);
                    }

                    float airDensity = cloth->getAirDensity();
                    if (ImGui::SliderFloat("Air Density", &airDensity, 0.0f, 5.0f)) {
                        cloth->setAirDensity(airDensity);
                    }
                    
                    float stiffness = cloth->getSpringStiffness();
                    if (ImGui::DragFloat("Spring Stiffness", &stiffness, 10.0f, 0.0f, 10000.0f)) {
                        cloth->setSpringStiffness(stiffness);
                    }

                    float damping = cloth->getSpringDamping();
                    if (ImGui::DragFloat("Spring Damping", &damping, 0.01f, 0.0f, 10.0f)) {
                        cloth->setSpringDamping(damping);
                    }

                    if (ImGui::Button("Reset Simulation")) {
                        cloth->resetSimulation();
                    }
                }
                else
                {
                    ImGui::TextDisabled("Cloth System not available");
                }
            }
        }
        ImGui::End();

        if (m_showShadowMap)
        {
            ImGui::Begin("Shadow Map Viewer", &m_showShadowMap);
            TextureHandle shadowHandle = m_indirectRenderer->getShadowMapTexture();
            auto texID = m_imgui.getTextureID(shadowHandle);
            if (texID != (ImTextureID)-1) {
                float availWidth = ImGui::GetContentRegionAvail().x;
                ImGui::Image(texID, ImVec2(availWidth, availWidth));
            }
            ImGui::End();
        }
    }

    void onRecord(const renderer::RHIFrameContext& ctx) override
    {
        if (m_indirectRenderer && m_model)
        {

            if (m_debugLayerInitialized)
            {
                const auto& scene = m_model->scene();
                const auto& shadow = m_indirectRenderer->settings().shadow;

                int shadowCasterIdx = m_indirectRenderer->getShadowCasterIndex();

                auto lightView = scene.registry().view<LightSource, WorldTransform>();

                uint32_t currentLightIndex = 0;

                lightView.each([&](ecs::Entity entity, LightSource& light, WorldTransform& world)
                {
                    (void)entity;

                    if (!light.debugDraw)
                    {
                        currentLightIndex++;
                        return;
                    }

                    const glm::mat4& worldM = world.matrix;
                    glm::vec3 pos = glm::vec3(worldM[3]);

                    glm::vec3 dir = light.direction;
                    if (glm::length(dir) < 0.0001f) dir = glm::vec3(0, -1, 0);
                    dir = glm::normalize(dir);
                    glm::vec3 color = light.color;

                    if (light.type == LightType::Directional)
                    {

                        m_debugLayer->line(pos, pos + dir * 5.0f, color);

                        if (static_cast<int>(currentLightIndex) == shadowCasterIdx)
                        {
                            float s = (shadow.orthoSize > 0.01f) ? shadow.orthoSize : 40.0f;

                            glm::vec3 eye = pos - dir * shadow.distFromCam;
                            glm::mat4 view = glm::lookAt(eye, pos, glm::vec3(0, 1, 0));
                            glm::mat4 proj = glm::orthoRH_ZO(-s, s, -s, s, shadow.nearPlane, shadow.farPlane);

                            m_debugLayer->frustum(view, proj, color);
                        }
                    }
                    else if (light.type == LightType::Point)
                    {
                        m_debugLayer->sphere(pos, 0.5f, color);
                    }
                    else if (light.type == LightType::Spot)
                    {
                        float range = (light.range <= 0.0f) ? 10.0f : light.range;
                        float fovDeg = glm::degrees(light.outerConeAngle) * 2.0f;

                        glm::vec3 up = (std::abs(dir.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                        glm::mat4 view = glm::lookAt(pos, pos + dir, up);
                        glm::mat4 proj = glm::perspective(glm::radians(fovDeg), 1.0f, 0.1f, range);

                        m_debugLayer->frustum(view, proj, color);
                    }

                    currentLightIndex++;
                });
            }

            m_indirectRenderer->draw(ctx.commandBuffer, m_camera, ctx.backBuffer->extent().width,
                                     ctx.backBuffer->extent().height, m_debugLayer.get(),
                                     [&](renderer::rhi::RHICommandList* cmd)
                                     {

                                         if (m_debugLayerInitialized && m_debugLayer) {
                                             m_debugLayer->render(ctx, m_camera.viewProj());
                                         }

                                         m_imgui.render(cmd);
                                     });
        }
    }
};

int main(int , char** )
{
    IndirectSample app;
    return app.run();
}
