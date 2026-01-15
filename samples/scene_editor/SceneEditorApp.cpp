#include "SceneEditorApp.hpp"
#include "ui/SceneGraphEditor.hpp"
#include "ui/MaterialEditor.hpp"
#include <imgui.h>
#include "pnkr/core/logger.hpp"
#include "pnkr/renderer/io/GLTFLoader.hpp"
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <limits>

using namespace pnkr;
using namespace pnkr::renderer;
using namespace pnkr::renderer::scene;
namespace io = pnkr::renderer::io;

namespace
{
    struct Ray
    {
        glm::vec3 origin;
        glm::vec3 dir;
    };

    static Ray makeMouseRay_WorkRect(const pnkr::renderer::scene::Camera& cam,
                                     const ImVec2& mousePos,
                                     const ImVec2& workPos,
                                     const ImVec2& workSize)
    {

        const float x = (mousePos.x - workPos.x) / workSize.x;
        const float y = (mousePos.y - workPos.y) / workSize.y;

        if (x < 0.f || x > 1.f || y < 0.f || y > 1.f)
        {
            return Ray{{0, 0, 0}, {0, 0, 0}};
        }

        const float ndcX = x * 2.0f - 1.0f;
        const float ndcY = 1.0f - y * 2.0f;
        const float ndcZNear = 0.0f;
        const float ndcZFar = 1.0f;

        const glm::mat4 invVP = glm::inverse(cam.viewProj());

        glm::vec4 nearH = invVP * glm::vec4(ndcX, ndcY, ndcZNear, 1.0f);
        glm::vec4 farH = invVP * glm::vec4(ndcX, ndcY, ndcZFar, 1.0f);

        nearH /= nearH.w;
        farH /= farH.w;

        glm::vec3 origin = glm::vec3(nearH);
        glm::vec3 dir = glm::normalize(glm::vec3(farH - nearH));

        return Ray{origin, dir};
    }

    static bool intersectRayAABB(const glm::vec3& ro, const glm::vec3& rd,
                                 const glm::vec3& bmin, const glm::vec3& bmax,
                                 float& outT)
    {

        const glm::vec3 invD = 1.0f / rd;

        glm::vec3 t0 = (bmin - ro) * invD;
        glm::vec3 t1 = (bmax - ro) * invD;

        glm::vec3 tmin = glm::min(t0, t1);
        glm::vec3 tmax = glm::max(t0, t1);

        float tNear = glm::max(glm::max(tmin.x, tmin.y), tmin.z);
        float tFar = glm::min(glm::min(tmax.x, tmax.y), tmax.z);

        if (tFar < 0.0f || tNear > tFar) return false;

        outT = (tNear >= 0.0f) ? tNear : tFar;
        return true;
    }

    static void transformAABB(const glm::vec3& localMin, const glm::vec3& localMax,
                              const glm::mat4& M,
                              glm::vec3& outMin, glm::vec3& outMax)
    {
        glm::vec3 corners[8] = {
            {localMin.x, localMin.y, localMin.z},
            {localMax.x, localMin.y, localMin.z},
            {localMin.x, localMax.y, localMin.z},
            {localMax.x, localMax.y, localMin.z},
            {localMin.x, localMin.y, localMax.z},
            {localMax.x, localMin.y, localMax.z},
            {localMin.x, localMax.y, localMax.z},
            {localMax.x, localMax.y, localMax.z},
        };

        outMin = glm::vec3(std::numeric_limits<float>::infinity());
        outMax = glm::vec3(-std::numeric_limits<float>::infinity());

        for (glm::vec3 c : corners)
        {
            glm::vec3 w = glm::vec3(M * glm::vec4(c, 1.0f));
            outMin = glm::min(outMin, w);
            outMax = glm::max(outMax, w);
        }
    }
}

SceneEditorApp::SceneEditorApp() : pnkr::app::Application({
    .title = "PNKR Scene Editor (Indirect)",
    .width = 1600,
    .height = 900,
    .createRenderer = true
})
{
}

SceneEditorApp::~SceneEditorApp() = default;

void SceneEditorApp::onInit()
{

    std::filesystem::path modelPath = "assets/Bistro.glb";
    if (!std::filesystem::exists(modelPath))
    {
        modelPath = "../samples/rhiPBR/assets/DamagedHelmet.gltf";
    }

    m_model = io::GLTFLoader::load(*m_renderer, modelPath, false);
    if (!m_model)
    {
        pnkr::core::Logger::error("Failed to load model: {}", modelPath.string());
        return;
    }

    m_indirectRenderer = std::make_unique<renderer::IndirectRenderer>();
    m_indirectRenderer->init(m_renderer.get(), m_model);
    m_indirectRenderer->setWireframe(m_drawWireframe);

    m_debugLines = std::make_unique<renderer::debug::DebugLayer>();
    m_debugLines->initialize(m_renderer.get());

    m_camera.setPerspective(glm::radians(45.0f), (float)m_config.width / (float)m_config.height, 0.1f, 200.0f);
    m_cameraController.applyToCamera(m_camera);
}

void SceneEditorApp::onUpdate(float dt)
{
    m_cameraController.update(m_input, dt);
    m_cameraController.applyToCamera(m_camera);
    float aspect = (float)m_window.width() / (float)m_window.height();
    m_camera.setPerspective(glm::radians(45.0f), aspect, 0.1f, 200.0f);

    if (!m_model)
    {
        return;
    }

    tryPick();

    if (m_sceneDirty)
    {
        m_model->scene().recalculateGlobalTransformsFull();
        m_indirectRenderer->updateGlobalTransforms();
        m_sceneDirty = false;
    }

    if (m_debugLines)
    {
        const auto& scene = m_model->scene();
        const auto& bounds = m_model->meshBounds();
        const glm::vec3 allColor(1.0f, 0.0f, 0.0f);
        const glm::vec3 selectedColor(0.0f, 1.0f, 0.0f);

        auto meshView = scene.registry().view<MeshRenderer, WorldTransform>();
        meshView.each([&](ecs::Entity nodeId, MeshRenderer& mr, WorldTransform& world)
        {
            int32_t meshIdx = mr.meshID;
            if (meshIdx < 0 || static_cast<size_t>(meshIdx) >= bounds.size()) return;

            const auto& box = bounds[meshIdx];
            const glm::vec3 size = box.m_max - box.m_min;
            const glm::vec3 center = (box.m_min + box.m_max) * 0.5f;
            glm::mat4 boxTransform = world.matrix * glm::translate(glm::mat4(1.0f), center);
            m_debugLines->box(boxTransform, size, allColor);
        });

        if (m_selectedNode >= 0)
        {
            ecs::Entity selectedEntity = static_cast<ecs::Entity>(m_selectedNode);
            if (scene.registry().has<MeshRenderer>(selectedEntity) && scene.registry().has<WorldTransform>(selectedEntity))
            {
                int32_t meshIdx = scene.registry().get<MeshRenderer>(selectedEntity).meshID;
                if (meshIdx >= 0 && static_cast<size_t>(meshIdx) < bounds.size())
                {
                    const auto& box = bounds[meshIdx];
                    const glm::vec3 size = box.m_max - box.m_min;
                    const glm::vec3 center = (box.m_min + box.m_max) * 0.5f;
                    glm::mat4 boxTransform = scene.registry().get<WorldTransform>(selectedEntity).matrix * glm::translate(
                        glm::mat4(1.0f), center);
                    m_debugLines->box(boxTransform, size, selectedColor);
                }
            }
        }

        // Draw shadow frustum in yellow (like the cookbook)
        if (m_drawShadowFrustum && m_indirectRenderer)
        {
            const glm::vec3 shadowFrustumColor(1.0f, 1.0f, 0.0f); // Yellow
            m_debugLines->frustum(
                m_indirectRenderer->getShadowView(),
                m_indirectRenderer->getShadowProj(),
                shadowFrustumColor
            );
        }
    }
}

void SceneEditorApp::onImGui()
{
    if (!m_model) return;

    ImGuizmo::BeginFrame();

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Exit"))
            {
                SDL_Event quit_event;
                quit_event.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&quit_event);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Draw Wireframe", nullptr, &m_drawWireframe))
            {
                m_indirectRenderer->setWireframe(m_drawWireframe);
            }
            ImGui::MenuItem("Shadow Frustum", nullptr, &m_drawShadowFrustum);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    ImGui::Begin("Scene Graph");
    int newSelection = -1;
    for (auto root : m_model->scene().roots())
    {
        int sel = ui::renderSceneTree(m_model->scene(), static_cast<uint32_t>(root), m_selectedNode);
        if (sel != -1) newSelection = sel;
    }
    if (newSelection != -1) m_selectedNode = newSelection;
    ImGui::End();

    ImGui::Begin("Inspector");
    if (m_selectedNode != -1)
    {
        ecs::Entity selectedEntity = static_cast<ecs::Entity>(m_selectedNode);
        auto& scene = m_model->scene();

        glm::vec3 centerOffset(0.0f);
        int32_t meshIdx = -1;
        if (scene.registry().has<MeshRenderer>(selectedEntity))
        {
            meshIdx = scene.registry().get<MeshRenderer>(selectedEntity).meshID;
            if (meshIdx >= 0 && (size_t)meshIdx < m_model->meshBounds().size())
            {
                const auto& box = m_model->meshBounds()[meshIdx];
                centerOffset = (box.m_min + box.m_max) * 0.5f;
            }
        }

        if (ui::editTransformUI(m_camera, m_model->scene(), m_selectedNode, centerOffset))
        {
            m_sceneDirty = true;
        }

        if (scene.registry().has<MeshRenderer>(selectedEntity))
        {
            int32_t meshIdx = scene.registry().get<MeshRenderer>(selectedEntity).meshID;
            if (meshIdx >= 0 && (size_t)meshIdx < m_model->meshes().size())
            {
                const auto& mesh = m_model->meshes()[meshIdx];
                for (size_t i = 0; i < mesh.primitives.size(); ++i)
                {
                    uint32_t matIdx = mesh.primitives[i].materialIndex;
                    if (ImGui::CollapsingHeader(("Material: " + std::to_string(matIdx)).c_str()))
                    {
                        if (ui::renderMaterialEditor(m_model->materialsCPUMutable()[matIdx]))
                        {
                            // Sync MaterialCPU to MaterialData
                            auto& cpu = m_model->materialsCPU()[matIdx];
                            auto& data = m_model->materialsMutable()[matIdx];
                            data.baseColorFactor = cpu.baseColorFactor;
                            data.metallicFactor = cpu.metallicFactor;
                            data.roughnessFactor = cpu.roughnessFactor;
                            data.emissiveFactor = cpu.emissiveFactor;
                            data.emissiveStrength = cpu.emissiveStrength;
                            data.doubleSided = cpu.doubleSided;
                            data.anisotropyFactor = cpu.anisotropyFactor;
                            data.anisotropyRotation = cpu.anisotropyRotation;
                            data.iridescenceFactor = cpu.iridescenceFactor;
                            data.iridescenceIor = cpu.iridescenceIor;
                            data.iridescenceThicknessMinimum = cpu.iridescenceThicknessMinimum;
                            data.iridescenceThicknessMaximum = cpu.iridescenceThicknessMaximum;
                            data.transmissionFactor = cpu.transmissionFactor;
                            data.ior = cpu.ior;
                            data.volumeThicknessFactor = cpu.volumeThicknessFactor;
                            data.volumeAttenuationColor = cpu.volumeAttenuationColor;
                            data.volumeAttenuationDistance = cpu.volumeAttenuationDistance;

                            m_indirectRenderer->updateMaterial(m_model->scene().materialBaseIndex() + matIdx);
                        }
                    }
                }
            }
        }
    }

    ImGui::Begin("Animations");
    if (!m_model->animations().empty())
    {
        auto& state = m_model->animationState();
        if (ImGui::Checkbox("Playing", &state.isPlaying)) {}
        if (ImGui::Checkbox("Looping", &state.isLooping)) {}
        
        std::vector<const char*> animNames;
        for (const auto& anim : m_model->animations()) animNames.push_back(anim.name.c_str());
        
        int currentAnim = (state.animIndex == ~0u) ? -1 : (int)state.animIndex;
        if (ImGui::Combo("Active Animation", &currentAnim, animNames.data(), (int)animNames.size()))
        {
            state.animIndex = (uint32_t)currentAnim;
            state.currentTime = 0.0f;
        }
    } else {
        ImGui::Text("No animations found.");
    }
    ImGui::End();

    ImGui::Begin("Cameras");
    if (!m_model->cameras().empty())
    {
        std::vector<const char*> camNames;
        for (const auto& cam : m_model->cameras()) camNames.push_back(cam.name.c_str());
        
        static int selectedCam = -1;
        if (ImGui::Combo("Scene Camera", &selectedCam, camNames.data(), (int)camNames.size()))
        {
            if (selectedCam >= 0) {
                // Find node with this camera
                auto& registry = m_model->scene().registry();
                auto view = registry.view<renderer::scene::CameraComponent, renderer::scene::WorldTransform>();
                for (auto entity : view) {
                    const auto& camComp = registry.get<renderer::scene::CameraComponent>(entity);
                    if (camComp.cameraID == selectedCam) {
                         const auto& transform = registry.get<renderer::scene::WorldTransform>(entity);
                         
                         // Decompose transform
                         glm::vec3 scale;
                         glm::quat rotation;
                         glm::vec3 translation;
                         glm::vec3 skew;
                         glm::vec4 perspective;
                         glm::decompose(transform.matrix, scale, rotation, translation, skew, perspective);
                         
                         m_cameraController.setPosition(translation);
                         
                         // Convert quat to yaw/pitch (approximate)
                         glm::vec3 euler = glm::eulerAngles(rotation);
                         m_cameraController.setRotation(glm::degrees(euler.y), glm::degrees(euler.x)); 
                         break;
                    }
                }
            }
        }
        if (ImGui::Button("Reset Free Camera")) {
            m_cameraController = renderer::scene::CameraController{{-19.261f, 8.465f, -7.317f}};
        }
        ImGui::SliderFloat("Camera Speed", &m_cameraController.moveSpeed(), 0.5f, 20.0f);
    } else {
        ImGui::Text("No scene cameras found.");
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
                ImGui::SliderFloat("Bloom Threshold", &settings.bloomThreshold, 0.0f, 10.0f);
                ImGui::SliderFloat("Bloom Knee", &settings.bloomKnee, 0.0f, 1.0f);
                ImGui::SliderFloat("Firefly Threshold", &settings.bloomFireflyThreshold, 0.0f, 50.0f);
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
            ImGui::End();
        }
    }


void SceneEditorApp::onRecord(const renderer::RHIFrameContext& ctx)
{
    if (!m_model) return;

    m_indirectRenderer->draw(ctx.commandBuffer, m_camera, ctx.backBuffer->extent().width,
                             ctx.backBuffer->extent().height, m_debugLines.get());
    if (m_debugLines)
    {
        m_debugLines->render(ctx, m_camera.viewProj());
    }
}

void SceneEditorApp::onEvent(const SDL_Event& event)
{
    pnkr::app::Application::onEvent(event);
}

void SceneEditorApp::tryPick()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;
    if (ImGuizmo::IsUsing()) return;

    if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left)) return;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 pos = vp->Pos;
    const ImVec2 size = vp->Size;

    Ray ray = makeMouseRay_WorkRect(m_camera, io.MousePos, pos, size);
    if (glm::length2(ray.dir) < 0.5f) return;

    const auto& scene = m_model->scene();
    const auto& bounds = m_model->meshBounds();

    int bestNode = -1;
    float bestT = std::numeric_limits<float>::infinity();

    auto meshView = scene.registry().view<MeshRenderer, WorldTransform>();

    meshView.each([&](ecs::Entity nodeId, MeshRenderer& mr, WorldTransform& world)
    {
        int32_t meshIdx = mr.meshID;
        if (meshIdx < 0 || static_cast<size_t>(meshIdx) >= bounds.size()) return;

        const auto& box = bounds[meshIdx];

        glm::vec3 wmin, wmax;
        transformAABB(box.m_min, box.m_max, world.matrix, wmin, wmax);

        float t = 0.0f;
        if (intersectRayAABB(ray.origin, ray.dir, wmin, wmax, t))
        {
            if (t < bestT)
            {
                bestT = t;
                bestNode = (int)nodeId;
            }
        }
    });

    if (bestNode != -1)
    {
        m_selectedNode = bestNode;
    }
}

