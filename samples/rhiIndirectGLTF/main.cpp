#include "pnkr/engine.hpp"
#include "pnkr/renderer/IndirectRenderer.hpp"
#include "pnkr/app/Application.hpp"
#include "pnkr/platform/FileDialog.hpp"
#include "pnkr/core/RecentFiles.hpp"
#include "pnkr/debug/RenderDoc.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/CameraController.hpp"
#include "pnkr/renderer/geometry/GeometryUtils.hpp"
#include "pnkr/renderer/debug/DebugLayer.hpp"
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <cmath>

using namespace pnkr;
using namespace pnkr::renderer::scene;

namespace
{
    constexpr uint32_t kInvalidId = 0xFFFFFFFFu;

    bool EditTextureSlot(const char* label, uint32_t& tex, uint32_t& samp, uint32_t& uv)
    {
        bool changed = false;
        if (ImGui::TreeNode(label))
        {
            changed |= ImGui::InputScalar("Texture", ImGuiDataType_U32, &tex);
            changed |= ImGui::InputScalar("Sampler", ImGuiDataType_U32, &samp);
            changed |= ImGui::InputScalar("UV Set", ImGuiDataType_U32, &uv);

            if (ImGui::Button("Set Invalid"))
            {
                tex = kInvalidId;
                samp = 0;
                uv = 0;
                changed = true;
            }
            ImGui::TreePop();
        }
        return changed;
    }

    void ToggleBit(const char* name, uint32_t bit, uint32_t& mask, bool& dirty)
    {
        bool on = (mask & bit) != 0;
        if (ImGui::Checkbox(name, &on))
        {
            if (on) mask |= bit;
            else mask &= ~bit;
            dirty = true;
        }
    }

    float g_LightYAngle;
    float g_LightXAngle;
    float g_LightDist;

    bool DrawLightControls(LightSource& light, int index, bool isShadowCaster, SceneGraphDOD* scene, ecs::Entity entity)
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
        if (scene && entity != ecs::NULL_ENTITY && scene->registry.has<Name>(entity))
        {
            name = scene->registry.get<Name>(entity).str.c_str();
        }

        if (ImGui::TreeNode((void*)(intptr_t)entity, "%s [%s]%s",
                            name, typeName, isShadowCaster ? " (Shadow Caster)" : ""))
        {
            ImGui::Checkbox("Debug Draw", &light.debugDraw);

            if (scene && entity != ecs::NULL_ENTITY)
            {
                glm::vec3 pos = glm::vec3(scene->registry.get<LocalTransform>(entity).matrix[3]);
                if (ImGui::DragFloat3(("Position" + id).c_str(), &pos.x, 0.1f))
                {
                    scene->registry.get<LocalTransform>(entity).matrix[3] = glm::vec4(pos, 1.0f);
                    scene->markAsChanged(entity);
                }
            }

            int type = static_cast<int>(light.type);
            const char* types[] = {"Directional", "Point", "Spot"};
            if (ImGui::Combo(("Type" + id).c_str(), &type, types, 3))
            {
                light.type = static_cast<LightType>(type);
            }

            ImGui::ColorEdit3(("Color" + id).c_str(), &light.color.x);
            ImGui::DragFloat(("Intensity" + id).c_str(), &light.intensity, 0.1f, 0.0f, 100.0f);

            if (light.type != LightType::Directional)
            {
                if (ImGui::DragFloat3(("Direction" + id).c_str(), &light.direction.x, 0.01f, -1.0f, 1.0f))
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
                if (ImGui::DragFloat(("Inner Angle" + id).c_str(), &innerDeg, 1.0f, 0.0f, 90.0f))
                {
                    light.innerConeAngle = glm::radians(innerDeg);
                }
                if (ImGui::DragFloat(("Outer Angle" + id).c_str(), &outerDeg, 1.0f, 0.0f, 90.0f))
                {
                    light.outerConeAngle = glm::radians(outerDeg);
                }
            }

            if (light.type != LightType::Directional)
            {
                ImGui::DragFloat(("Range" + id).c_str(), &light.range, 0.5f, 0.0f, 1000.0f);
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
} // namespace

class IndirectSample : public app::Application
{
public:
    IndirectSample() : Application({
        .title = "Indirect Rendering - Bistro", .width = 1824, .height = 928, .createRenderer = true
    })
    {
    }

    std::shared_ptr<ModelDOD> m_model;
    std::unique_ptr<renderer::IndirectRenderer> m_indirectRenderer;
    Camera m_camera;
    Camera m_lightCamera;
    CameraController m_cameraController{
        {-19.2609997, 8.46500015, -7.31699991}, 20.801124201214570, -16.146098030003937f
    };
    TextureHandle m_brdfLut;
    TextureHandle m_irradiance;
    TextureHandle m_prefilter;
    renderer::debug::DebugLayer m_debugLayer;
    bool m_debugLayerInitialized = false;

    bool m_useGltfCamera = false;
    int32_t m_activeGltfCameraNode = -1; // SceneGraphDOD node id (includes synthetic root)
    std::vector<uint32_t> m_gltfCameraNodes;

    std::optional<std::filesystem::path> m_pendingLoad;
    core::RecentFiles m_recent{"rhiIndirectGLTF", 12};
    bool m_showMaterialEditor = true;
    int m_selectedMaterial = 0;
    bool m_materialDirty = false;
    glm::vec3 m_cameraPosUI{0.0f};
    glm::vec3 m_cameraTargetUI{0.0f, 0.0f, -1.0f};
    bool m_showShadowMap = true;

    // Shadow / Light controls
    float m_lightFOV = 45.0f;
    float m_lightInnerAngle = 30.0f;
    float m_lightNear = 0.1f;
    float m_lightFar = 200.0f;
    float m_lightDist = 15.0f;
    float m_lightDepthBiasConst = 2.0f;
    float m_lightDepthBiasSlope = 2.5f;

    renderer::SSAOSettings m_ssaoSettings;

    void onInit() override
    {
        m_recent.load();

        m_debugLayer.initialize(m_renderer.get());
        m_debugLayer.setDepthTestEnabled(true);
        m_debugLayerInitialized = true;

        // Load IBL Textures
        m_brdfLut = m_renderer->loadTextureKTX("assets/brdf_lut.ktx2");
        m_irradiance = m_renderer->loadTextureKTX("assets/immenstadter_horn_2k_irradiance.ktx");
        m_prefilter = m_renderer->loadTextureKTX("assets/immenstadter_horn_2k_prefilter.ktx");

        if (m_brdfLut == INVALID_TEXTURE_HANDLE ||
            m_irradiance == INVALID_TEXTURE_HANDLE ||
            m_prefilter == INVALID_TEXTURE_HANDLE)
        {
            core::Logger::warn("One or more IBL textures failed to load. PBR will look flat.");
        }

        loadModel(baseDir() / "assets/rubber_duck/scene.gltf");

        // Setup Camera
        m_cameraController.applyToCamera(m_camera);
        m_camera.setPerspective(45.0f, (float)m_config.width / m_config.height, 0.1f, 1000.0f);
        m_cameraPosUI = m_cameraController.position();
        m_cameraTargetUI = m_cameraPosUI + m_cameraController.front();
    }

    void loadModel(const std::filesystem::path& path)
    {
        m_renderer->device()->waitIdle();

        m_indirectRenderer.reset();
        m_model.reset();

        core::Logger::info("Loading model from: {}", path.string());

        m_model = ModelDOD::load(*m_renderer, path);
        if (!m_model)
        {
            core::Logger::error("Failed to load model");
            return;
        }

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

        // Init Indirect Renderer
        m_indirectRenderer = std::make_unique<renderer::IndirectRenderer>();
        m_indirectRenderer->init(m_renderer.get(), m_model, m_brdfLut, m_irradiance, m_prefilter);

        m_renderer->setComputeRecordFunc([this](const renderer::RHIFrameContext& ctx)
        {
            if (m_indirectRenderer)
            {
                m_indirectRenderer->dispatchSkinning(ctx.commandBuffer);
            }
        });

        m_activeGltfCameraNode = -1;
        rebuildGltfCameraNodeList();

        m_recent.add(path);
    }

    void rebuildGltfCameraNodeList()
    {
        m_gltfCameraNodes.clear();
        if (!m_model) return;
        const auto& scene = m_model->scene();
        for (ecs::Entity n : scene.topoOrder)
        {
            if (scene.registry.has<CameraComponent>(n))
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
        if (!scene.registry.has<CameraComponent>(entity)) return;
        if (!scene.registry.has<WorldTransform>(entity)) return;

        const int32_t camIndex = scene.registry.get<CameraComponent>(entity).cameraID;
        if (camIndex < 0 || (size_t)camIndex >= m_model->cameras().size()) return;

        // View = inverse(world)
        const glm::mat4 world = scene.registry.get<WorldTransform>(entity).matrix;
        const glm::mat4 view = glm::affineInverse(world);
        m_camera.setViewMatrix(view);

        // Projection from glTF camera
        const auto& gc = m_model->cameras()[(size_t)camIndex];
        if (gc.type == GltfCamera::Type::Perspective)
        {
            const float aspect = (gc.aspectRatio > 0.0f) ? gc.aspectRatio : viewportAspect;
            const float zfar = (gc.zfar > 0.0f) ? gc.zfar : 10000.0f; // pragmatic clamp
            m_camera.setPerspective(gc.yfovRad, aspect, gc.znear, zfar);
        }
        else
        {
            // glTF orthographic uses xmag/ymag as half-extents
            const float l = -gc.xmag;
            const float r = gc.xmag;
            const float b = -gc.ymag;
            const float t = gc.ymag;
            m_camera.setOrthographic(l, r, b, t, gc.znear, gc.zfar);
        }
    }


    void onUpdate(float dt) override
    {
        if (m_pendingLoad)
        {
            loadModel(*m_pendingLoad);
            m_pendingLoad.reset();
        }

        const float aspect = (float)m_config.width / (float)m_config.height;

        if (!m_useGltfCamera)
        {
            m_cameraController.update(m_input, dt);
            m_cameraController.applyToCamera(m_camera);
        }
        else
        {
            // Ensure transforms are up to date
            m_model->scene().updateTransforms();
            applySelectedGltfCamera(aspect);
        }

        if (m_indirectRenderer)
        {
            renderer::ShadowSettings shadowSettings;
            shadowSettings.fov = m_lightFOV;
            shadowSettings.orthoSize = m_lightInnerAngle;
            shadowSettings.nearPlane = m_lightNear;
            shadowSettings.farPlane = m_lightFar;
            shadowSettings.distFromCam = m_lightDist;
            shadowSettings.biasConst = m_lightDepthBiasConst;
            shadowSettings.biasSlope = m_lightDepthBiasSlope;
            m_indirectRenderer->setShadowSettings(shadowSettings);
            m_indirectRenderer->setSSAOSettings(m_ssaoSettings);

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

            if (event.key.scancode == SDL_SCANCODE_F10)
            {
                auto& rd = m_renderer->renderdoc();

                // Try to lazy load if not available
                if (!rd.isAvailable())
                {
                    if (rd.init())
                    {
                        core::Logger::info("RenderDoc hot-loaded via F10.");
                    }
                    else
                    {
                        core::Logger::error("RenderDoc DLL not found. Cannot capture.");
                        return;
                    }
                }

                rd.toggleCapture();
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

            if (ImGui::BeginMenu("Capture"))
            {
                auto& rd = m_renderer->renderdoc();
                bool avail = rd.isAvailable();

                // Status Indicator
                std::string status = rd.getOverlayText();
                if (rd.isCapturing())
                {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "(( %s ))", status.c_str());
                }
                else
                {
                    ImGui::TextDisabled("%s", status.c_str());
                }
                ImGui::Separator();

                if (!avail)
                {
                    if (ImGui::MenuItem("Load RenderDoc DLL"))
                    {
                        rd.init();
                    }
                }
                else
                {
                    if (rd.isCapturing())
                    {
                        if (ImGui::MenuItem("Stop Capture", "F10"))
                        {
                            rd.toggleCapture();
                        }
                    }
                    else
                    {
                        if (ImGui::MenuItem("Start Capture", "F10"))
                        {
                            rd.toggleCapture();
                        }
                    }

                    if (ImGui::MenuItem("Launch Replay UI"))
                    {
                        rd.launchReplayUI();
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        if (!m_model) return;

        ImGui::Begin("glTF Cameras");
        ImGui::Checkbox("Use glTF camera", &m_useGltfCamera);

        ImGui::Separator();
        ImGui::Text("Cameras found: %d", (int)m_gltfCameraNodes.size());

        if (!m_gltfCameraNodes.empty())
        {
            // Build preview label for current selection
            const auto& scene = m_model->scene();
            int currentIdx = 0;
            for (int i = 0; i < (int)m_gltfCameraNodes.size(); ++i)
                if ((int32_t)m_gltfCameraNodes[i] == m_activeGltfCameraNode)
                {
                    currentIdx = i;
                    break;
                }

            if (ImGui::BeginCombo("Active", std::to_string(currentIdx).c_str()))
            {
                for (int i = 0; i < (int)m_gltfCameraNodes.size(); ++i)
                {
                    const uint32_t nodeId = m_gltfCameraNodes[i];
                    ecs::Entity entity = static_cast<ecs::Entity>(nodeId);
                    int32_t camIndex = -1;
                    if (scene.registry.has<CameraComponent>(entity))
                    {
                        camIndex = scene.registry.get<CameraComponent>(entity).cameraID;
                    }
                    const char* camName = (camIndex >= 0 && (size_t)camIndex < m_model->cameras().size())
                                              ? m_model->cameras()[(size_t)camIndex].name.c_str()
                                              : "(unnamed)";

                    std::string label = std::to_string(i) + " | node " + std::to_string(nodeId) + " | " + camName;
                    const bool selected = (int32_t)nodeId == m_activeGltfCameraNode;
                    if (ImGui::Selectable(label.c_str(), selected))
                        m_activeGltfCameraNode = (int32_t)nodeId;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        ImGui::End();

        if (ImGui::Begin("SSAO"))
        {
            ImGui::Checkbox("Enable SSAO", &m_ssaoSettings.enabled);
            ImGui::SliderFloat("Radius", &m_ssaoSettings.radius, 0.01f, 2.0f);
            ImGui::SliderFloat("Bias", &m_ssaoSettings.bias, 0.0f, 0.5f);
            ImGui::SliderFloat("Intensity", &m_ssaoSettings.intensity, 0.1f, 5.0f);
            ImGui::SliderFloat("Blur Sharpness", &m_ssaoSettings.blurSharpness, 0.0f, 100.0f);
            ImGui::SliderFloat("Strength", &m_ssaoSettings.strength, 0.0f, 1.0f);

            // Debug view
            if (m_indirectRenderer)
            {
                auto h = m_indirectRenderer->getSSAOTexture();
                auto id = m_imgui.getTextureID(h);
                float w = ImGui::GetContentRegionAvail().x;
                if (id != -1) ImGui::Image(id, ImVec2(w, w * 9.0f / 16.0f));
            }
        }
        ImGui::End();

        if (m_indirectRenderer && ImGui::Begin("HDR Settings"))
        {
            auto& settings = m_indirectRenderer->hdrSettings();

            ImGui::SliderFloat("Exposure", &settings.exposure, 0.1f, 5.0f);
            ImGui::SliderFloat("Adaptation Speed", &settings.adaptationSpeed, 0.0f, 10.0f);

            ImGui::Checkbox("Enable Bloom", &settings.enableBloom);
            if (settings.enableBloom)
            {
                ImGui::SliderFloat("Bloom Strength", &settings.bloomStrength, 0.0f, 1.0f);
                ImGui::SliderFloat("Bloom Threshold", &settings.bloomThreshold, 0.0f, 5.0f);
                ImGui::SliderInt("Bloom Passes", &settings.bloomPasses, 1, 6);
            }

            const char* items[] = {"None", "Reinhard", "Uchimura", "Khronos PBR"};
            int item = static_cast<int>(settings.mode);
            if (ImGui::Combo("Tone Mapper", &item, items, 4))
            {
                settings.mode = static_cast<renderer::HDRSettings::ToneMapMode>(item);
            }

            if (settings.mode == renderer::HDRSettings::ToneMapMode::Reinhard)
            {
                ImGui::SliderFloat("Max White", &settings.reinhardMaxWhite, 0.5f, 10.0f);
            }
            else if (settings.mode == renderer::HDRSettings::ToneMapMode::Uchimura)
            {
                ImGui::SliderFloat("Max Brightness (P)", &settings.u_P, 1.0f, 100.0f);
                ImGui::SliderFloat("Contrast (a)", &settings.u_a, 0.0f, 5.0f);
                ImGui::SliderFloat("Linear Start (m)", &settings.u_m, 0.0f, 1.0f);
                ImGui::SliderFloat("Linear Length (l)", &settings.u_l, 0.0f, 1.0f);
                ImGui::SliderFloat("Black Tightness (c)", &settings.u_c, 1.0f, 3.0f);
                ImGui::SliderFloat("Pedestal (b)", &settings.u_b, 0.0f, 1.0f);
            }
            else if (settings.mode == renderer::HDRSettings::ToneMapMode::KhronosPBR)
            {
                ImGui::SliderFloat("Compression Start", &settings.k_Start, 0.0f, 1.0f);
                ImGui::SliderFloat("Desaturation", &settings.k_Desat, 0.0f, 1.0f);
            }
        }
        if (m_indirectRenderer) ImGui::End();

        if (m_indirectRenderer) {
            ImGui::Begin("Culling Settings");

            static bool enableCulling = true;
            if (ImGui::Checkbox("Enable CPU Frustum Culling", &enableCulling)) {
                m_indirectRenderer->setCullingEnabled(enableCulling);
            }

            static bool freeze = false;
            if (ImGui::Checkbox("Freeze Culling View (P)", &freeze)) {
                m_indirectRenderer->setFreezeCullingView(freeze);
            }

            static bool debugDraw = false;
            if (ImGui::Checkbox("Draw Debug Bounds", &debugDraw)) {
                m_indirectRenderer->setDrawDebugBounds(debugDraw);
            }

            ImGui::Text("Visible Meshes: %u", m_indirectRenderer->getVisibleMeshCount());
            ImGui::End();
        }

        ImGui::Begin("Camera");
        ImGui::TextUnformatted("Position/target controls use the free camera.");
        if (m_useGltfCamera)
        {
            ImGui::TextUnformatted("Disable glTF camera to edit.");
        }
        ImGui::BeginDisabled(m_useGltfCamera);
        ImGui::InputFloat3("Position", &m_cameraPosUI.x);
        ImGui::InputFloat3("Target", &m_cameraTargetUI.x);
        if (ImGui::Button("Use Current"))
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
        ImGui::End();

        ImGui::Begin("Shadow Controls");
        ImGui::Checkbox("Show Shadow Map", &m_showShadowMap);
        ImGui::Separator();

        ImGui::Begin("Frustum Debugger");
        const auto& corners = m_debugLayer.getLastFrustumCorners();
        if (corners.size() >= 8)
        {
            ImGui::Text("Near Plane (Z=0)");
            ImGui::InputFloat3("N-BL", (float*)&corners[0]);
            ImGui::InputFloat3("N-BR", (float*)&corners[1]);
            ImGui::InputFloat3("N-TR", (float*)&corners[2]);
            ImGui::InputFloat3("N-TL", (float*)&corners[3]);

            ImGui::Separator();
            ImGui::Text("Far Plane (Z=1)");
            ImGui::InputFloat3("F-BL", (float*)&corners[4]);
            ImGui::InputFloat3("F-BR", (float*)&corners[5]);
            ImGui::InputFloat3("F-TR", (float*)&corners[6]);
            ImGui::InputFloat3("F-TL", (float*)&corners[7]);
        }
        else
        {
            ImGui::TextDisabled("No frustum captured yet.");
        }
        ImGui::End();

        ImGui::Text("Depth Bias");
        ImGui::SliderFloat("Constant", &m_lightDepthBiasConst, 0.0f, 5.0f);
        ImGui::SliderFloat("Slope", &m_lightDepthBiasSlope, 0.0f, 5.0f);

        ImGui::Separator();
        ImGui::Text("Light Projection");
        ImGui::SliderFloat("FOV", &m_lightFOV, 15.0f, 120.0f);
        ImGui::SliderFloat("Ortho Size", &m_lightInnerAngle, 5.0f, 100.0f);
        ImGui::SliderFloat("Near", &m_lightNear, 0.1f, 10.0f);
        ImGui::SliderFloat("Far", &m_lightFar, 10.0f, 500.0f);

        ImGui::Separator();
        ImGui::Text("Light Position");
        ImGui::SliderFloat("Distance", &m_lightDist, 1.0f, 100.0f);

        ImGui::Separator();
        ImGui::Text("Scene Lights");
        if (m_indirectRenderer)
        {
            if (m_model)
            {
                if (ImGui::Button("Add Directional"))
                {
                    Light l{};
                    l.m_type = LightType::Directional;
                    l.m_intensity = 1.0f;
                    l.m_direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.2f));
                    m_model->addLight(l, glm::mat4(1.0f), "New Directional");
                }
                ImGui::SameLine();
                if (ImGui::Button("Add Point"))
                {
                    Light l{};
                    l.m_type = LightType::Point;
                    l.m_intensity = 5.0f;
                    l.m_range = 20.0f;
                    glm::mat4 t = glm::translate(glm::mat4(1.0f),
                                                 m_cameraController.position() + m_cameraController.front() * 5.0f);
                    m_model->addLight(l, t, "New Point");
                }
                ImGui::SameLine();
                if (ImGui::Button("Add Spot"))
                {
                    Light l{};
                    l.m_type = LightType::Spot;
                    l.m_intensity = 10.0f;
                    l.m_range = 30.0f;
                    l.m_innerConeAngle = glm::radians(20.0f);
                    l.m_outerConeAngle = glm::radians(30.0f);
                    l.m_direction = m_cameraController.front();
                    glm::mat4 t = glm::translate(glm::mat4(1.0f), m_cameraController.position());
                    m_model->addLight(l, t, "New Spot");
                }
            }

            int shadowIdx = m_indirectRenderer->getShadowCasterIndex();
            const auto& scene = m_model->scene();
            auto lightPool = scene.registry.view<LightSource>();

            int currentUIIdx = 0;
            ecs::Entity toRemove = ecs::NULL_ENTITY;
            int removeIdx = -1;

            lightPool.each([&](ecs::Entity entity, LightSource& ls)
            {
                if (DrawLightControls(ls, currentUIIdx, currentUIIdx == shadowIdx, const_cast<SceneGraphDOD*>(&scene),
                                      entity))
                {
                    toRemove = entity;
                    removeIdx = currentUIIdx;
                }
                currentUIIdx++;
            });

            if (toRemove != ecs::NULL_ENTITY)
            {
                m_model->removeLight(removeIdx);
            }

            if (currentUIIdx == 0)
            {
                ImGui::TextDisabled("No lights in scene.");
            }
        }

        ImGui::End();

        if (m_showShadowMap && m_indirectRenderer)
        {
            ImGui::Begin("Shadow Map");
            TextureHandle shadowHandle = m_indirectRenderer->getShadowMapTexture();
            auto texID = m_imgui.getTextureID(shadowHandle);
            if (texID != -1)
            {
                float availWidth = ImGui::GetContentRegionAvail().x;
                ImGui::Image(texID, ImVec2(availWidth, availWidth));
            }
            else
            {
                ImGui::TextDisabled("Shadow map not available.");
            }
            ImGui::End();
        }

        if (m_showMaterialEditor && m_indirectRenderer)
        {
            ImGui::Begin("Material Editor", &m_showMaterialEditor);

            auto mats = m_indirectRenderer->materialsCPU();
            if (mats.empty())
            {
                ImGui::TextUnformatted("No materials packed.");
                ImGui::End();
                return;
            }

            if (m_selectedMaterial < 0) m_selectedMaterial = 0;
            if (m_selectedMaterial >= (int)mats.size()) m_selectedMaterial = (int)mats.size() - 1;

            ImGui::BeginChild("mat_list", ImVec2(260, 0), true);
            for (int i = 0; i < (int)mats.size(); ++i)
            {
                char label[64];
                std::snprintf(label, sizeof(label), "Material %d", i);
                if (ImGui::Selectable(label, m_selectedMaterial == i))
                    m_selectedMaterial = i;
            }
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("mat_edit", ImVec2(0, 0), true);

            auto& m = mats[(size_t)m_selectedMaterial];

            if (ImGui::BeginTabBar("mat_tabs"))
            {
                if (ImGui::BeginTabItem("Core"))
                {
                    m_materialDirty |= ImGui::ColorEdit4("BaseColorFactor", &m.baseColorFactor.x);
                    m_materialDirty |= ImGui::SliderFloat("MetallicFactor", &m.metallicRoughnessNormalOcclusion.x, 0.0f,
                                                          1.0f);
                    m_materialDirty |= ImGui::SliderFloat("RoughnessFactor", &m.metallicRoughnessNormalOcclusion.y,
                                                          0.04f, 1.0f);
                    m_materialDirty |= ImGui::SliderFloat("NormalScale", &m.metallicRoughnessNormalOcclusion.z, 0.0f,
                                                          2.0f);
                    m_materialDirty |= ImGui::SliderFloat("OcclusionStrength", &m.metallicRoughnessNormalOcclusion.w,
                                                          0.0f, 1.0f);

                    m_materialDirty |= ImGui::ColorEdit3("EmissiveFactor", &m.emissiveFactorAlphaCutoff.x);
                    m_materialDirty |= ImGui::SliderFloat("AlphaCutoff", &m.emissiveFactorAlphaCutoff.w, 0.0f, 1.0f);

                    m_materialDirty |= ImGui::SliderFloat("IOR", &m.ior, 1.0f, 2.5f);

                    int alphaMode = (int)m.alphaMode;
                    const char* modes[] = {"OPAQUE (0)", "MASK (1)", "BLEND (2)"};
                    if (ImGui::Combo("AlphaMode", &alphaMode, modes, 3))
                    {
                        m.alphaMode = (uint32_t)alphaMode;
                        m_materialDirty = true;
                    }

                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Textures"))
                {
                    m_materialDirty |= EditTextureSlot("BaseColorTexture", m.baseColorTexture,
                                                       m.baseColorTextureSampler, m.baseColorTextureUV);
                    m_materialDirty |= EditTextureSlot("MetallicRoughnessTexture", m.metallicRoughnessTexture,
                                                       m.metallicRoughnessTextureSampler, m.metallicRoughnessTextureUV);
                    m_materialDirty |= EditTextureSlot("NormalTexture", m.normalTexture, m.normalTextureSampler,
                                                       m.normalTextureUV);
                    m_materialDirty |= EditTextureSlot("OcclusionTexture", m.occlusionTexture,
                                                       m.occlusionTextureSampler, m.occlusionTextureUV);
                    m_materialDirty |= EditTextureSlot("EmissiveTexture", m.emissiveTexture, m.emissiveTextureSampler,
                                                       m.emissiveTextureUV);

                    m_materialDirty |= EditTextureSlot("SheenColorTexture", m.sheenColorTexture,
                                                       m.sheenColorTextureSampler, m.sheenColorTextureUV);
                    m_materialDirty |= EditTextureSlot("SheenRoughnessTexture", m.sheenRoughnessTexture,
                                                       m.sheenRoughnessTextureSampler, m.sheenRoughnessTextureUV);

                    m_materialDirty |= EditTextureSlot("ClearCoatTexture", m.clearCoatTexture,
                                                       m.clearCoatTextureSampler, m.clearCoatTextureUV);
                    m_materialDirty |= EditTextureSlot("ClearCoatRoughnessTexture", m.clearCoatRoughnessTexture,
                                                       m.clearCoatRoughnessTextureSampler,
                                                       m.clearCoatRoughnessTextureUV);
                    m_materialDirty |= EditTextureSlot("ClearCoatNormalTexture", m.clearCoatNormalTexture,
                                                       m.clearCoatNormalTextureSampler, m.clearCoatNormalTextureUV);

                    m_materialDirty |= EditTextureSlot("SpecularTexture", m.specularTexture, m.specularTextureSampler,
                                                       m.specularTextureUV);
                    m_materialDirty |= EditTextureSlot("SpecularColorTexture", m.specularColorTexture,
                                                       m.specularColorTextureSampler, m.specularColorTextureUV);

                    m_materialDirty |= EditTextureSlot("TransmissionTexture", m.transmissionTexture,
                                                       m.transmissionTextureSampler, m.transmissionTextureUV);
                    m_materialDirty |= EditTextureSlot("ThicknessTexture", m.thicknessTexture,
                                                       m.thicknessTextureSampler, m.thicknessTextureUV);

                    m_materialDirty |= EditTextureSlot("IridescenceTexture", m.iridescenceTexture,
                                                       m.iridescenceTextureSampler, m.iridescenceTextureUV);
                    m_materialDirty |= EditTextureSlot("IridescenceThicknessTexture", m.iridescenceThicknessTexture,
                                                       m.iridescenceThicknessTextureSampler,
                                                       m.iridescenceThicknessTextureUV);

                    m_materialDirty |= EditTextureSlot("AnisotropyTexture", m.anisotropyTexture,
                                                       m.anisotropyTextureSampler, m.anisotropyTextureUV);

                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Extensions"))
                {
                    ToggleBit("Sheen", 1u << 2, m.materialType, m_materialDirty);
                    ToggleBit("ClearCoat", 1u << 3, m.materialType, m_materialDirty);
                    ToggleBit("Specular", 1u << 4, m.materialType, m_materialDirty);
                    ToggleBit("Transmission", 1u << 5, m.materialType, m_materialDirty);
                    ToggleBit("Volume", 1u << 6, m.materialType, m_materialDirty);
                    ToggleBit("Unlit", 1u << 7, m.materialType, m_materialDirty);

                    ImGui::Separator();

                    if (m.materialType & (1u << 2))
                    {
                        m_materialDirty |= ImGui::ColorEdit3("SheenColorFactor", &m.sheenFactors.x);
                        m_materialDirty |= ImGui::SliderFloat("SheenRoughnessFactor", &m.sheenFactors.w, 0.0f, 1.0f);
                    }

                    if (m.materialType & (1u << 3))
                    {
                        m_materialDirty |= ImGui::SliderFloat("ClearcoatFactor", &m.clearcoatTransmissionThickness.x,
                                                              0.0f, 1.0f);
                        m_materialDirty |= ImGui::SliderFloat("ClearcoatRoughness", &m.clearcoatTransmissionThickness.y,
                                                              0.0f, 1.0f);
                    }

                    if (m.materialType & (1u << 4))
                    {
                        m_materialDirty |= ImGui::ColorEdit3("SpecularColorFactor", &m.specularFactors.x);
                        m_materialDirty |= ImGui::SliderFloat("SpecularFactor", &m.specularFactors.w, 0.0f, 1.0f);
                    }

                    if (m.materialType & (1u << 5))
                    {
                        m_materialDirty |= ImGui::SliderFloat("TransmissionFactor", &m.clearcoatTransmissionThickness.z,
                                                              0.0f, 1.0f);
                    }

                    if (m.materialType & (1u << 6))
                    {
                        m_materialDirty |= ImGui::SliderFloat("ThicknessFactor", &m.clearcoatTransmissionThickness.w,
                                                              0.0f, 10.0f);
                        m_materialDirty |= ImGui::ColorEdit3("AttenuationColor", &m.attenuation.x);
                        m_materialDirty |= ImGui::SliderFloat("AttenuationDistance", &m.attenuation.w, 0.0f, 100.0f);
                    }

                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Advanced"))
                {
                    ImGui::Text("materialType mask: 0x%08X", m.materialType);
                    ImGui::Text("alphaMode: %u", m.alphaMode);
                    ImGui::Text("BaseColorTexture: %s (%u)", (m.baseColorTexture == kInvalidId) ? "INVALID" : "OK",
                                m.baseColorTexture);
                    ImGui::Text("MRTexture: %s (%u)", (m.metallicRoughnessTexture == kInvalidId) ? "INVALID" : "OK",
                                m.metallicRoughnessTexture);
                    ImGui::Text("NormalTexture: %s (%u)", (m.normalTexture == kInvalidId) ? "INVALID" : "OK",
                                m.normalTexture);
                    ImGui::Text("OcclusionTexture: %s (%u)", (m.occlusionTexture == kInvalidId) ? "INVALID" : "OK",
                                m.occlusionTexture);
                    ImGui::Text("EmissiveTexture: %s (%u)", (m.emissiveTexture == kInvalidId) ? "INVALID" : "OK",
                                m.emissiveTexture);

                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            ImGui::Separator();

            if (ImGui::Button("Apply to GPU") || (m_materialDirty && ImGui::IsKeyPressed(ImGuiKey_Enter)))
            {
                m_indirectRenderer->uploadMaterialsToGPU();
                m_materialDirty = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Repack from Model"))
            {
                m_indirectRenderer->repackMaterialsFromModel();
                m_indirectRenderer->uploadMaterialsToGPU();
                m_materialDirty = false;
            }

            ImGui::EndChild();
            ImGui::End();
        }
    }

    void onRecord(const renderer::RHIFrameContext& ctx) override
    {
        if (m_indirectRenderer)
        {
            m_indirectRenderer->draw(ctx.commandBuffer, m_camera, ctx.backBuffer->extent().width,
                                     ctx.backBuffer->extent().height, &m_debugLayer);
        }


        if (m_debugLayerInitialized && m_indirectRenderer && m_model)
        {
            const auto& scene = m_model->scene();

            int shadowCasterIdx = m_indirectRenderer->getShadowCasterIndex();


            auto lightView = scene.registry.view<LightSource, WorldTransform>();

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
                    // Draw direction vector

                    m_debugLayer.line(pos, pos + dir * 5.0f, color);


                    // If shadow caster, draw the shadow frustum

                    if (static_cast<int>(currentLightIndex) == shadowCasterIdx)
                    {
                        float s = (m_lightInnerAngle > 0.01f) ? m_lightInnerAngle : 40.0f;

                        // Replicate IndirectRenderer::draw logic for position

                        glm::vec3 eye = pos - dir * m_lightDist;

                        glm::mat4 view = glm::lookAt(eye, pos, glm::vec3(0, 1, 0));

                        glm::mat4 proj = glm::orthoRH_ZO(-s, s, -s, s, m_lightNear, m_lightFar);

                        m_debugLayer.frustum(view, proj, color);
                    }
                }

                else if (light.type == LightType::Point)
                {
                    m_debugLayer.sphere(pos, 0.5f, color);
                }

                else if (light.type == LightType::Spot)
                {
                    float range = (light.range <= 0.0f) ? 10.0f : light.range;
                    float fovDeg = glm::degrees(light.outerConeAngle) * 2.0f;


                    glm::vec3 up = (std::abs(dir.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                    glm::mat4 view = glm::lookAt(pos, pos + dir, up);
                    glm::mat4 proj = glm::perspective(glm::radians(fovDeg), 1.0f, 0.1f, range);


                    m_debugLayer.frustum(view, proj, color);
                }

                currentLightIndex++;
            });


            m_debugLayer.render(ctx, m_camera.viewProj());
        }
    }
};

int main(int /*argc*/, char** /*argv*/)
{
    IndirectSample app;
    return app.run();
}
