#include "SceneEditorApp.hpp"
#include "ui/SceneGraphEditor.hpp"
#include "ui/MaterialEditor.hpp"
#include <imgui.h>
#include "pnkr/core/logger.hpp"
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/norm.hpp>
#include <limits>

using namespace pnkr;
using namespace pnkr::renderer;

namespace {

struct Ray {
    glm::vec3 origin;
    glm::vec3 dir; // normalized
};

static Ray makeMouseRay_WorkRect(const pnkr::renderer::scene::Camera& cam,
                                 const ImVec2& mousePos,
                                 const ImVec2& workPos,
                                 const ImVec2& workSize)
{
    // Mouse -> normalized [0..1] in the work rect
    const float x = (mousePos.x - workPos.x) / workSize.x;
    const float y = (mousePos.y - workPos.y) / workSize.y;

    // Outside
    if (x < 0.f || x > 1.f || y < 0.f || y > 1.f) {
        return Ray{{0,0,0},{0,0,0}};
    }

    // Normalized Device Coordinates
    // Vulkan NDC: x,y in [-1,1], z in [0,1]
    const float ndcX = x * 2.0f - 1.0f;
    const float ndcY = 1.0f - y * 2.0f; // screen Y down -> NDC Y up
    const float ndcZNear = 0.0f;
    const float ndcZFar  = 1.0f;

    const glm::mat4 invVP = glm::inverse(cam.viewProj());

    glm::vec4 nearH = invVP * glm::vec4(ndcX, ndcY, ndcZNear, 1.0f);
    glm::vec4 farH  = invVP * glm::vec4(ndcX, ndcY, ndcZFar,  1.0f);

    nearH /= nearH.w;
    farH  /= farH.w;

    glm::vec3 origin = glm::vec3(nearH);
    glm::vec3 dir    = glm::normalize(glm::vec3(farH - nearH));

    return Ray{origin, dir};
}

static bool intersectRayAABB(const glm::vec3& ro, const glm::vec3& rd,
                             const glm::vec3& bmin, const glm::vec3& bmax,
                             float& outT)
{
    // Robust slab test
    const glm::vec3 invD = 1.0f / rd;

    glm::vec3 t0 = (bmin - ro) * invD;
    glm::vec3 t1 = (bmax - ro) * invD;

    glm::vec3 tmin = glm::min(t0, t1);
    glm::vec3 tmax = glm::max(t0, t1);

    float tNear = glm::max(glm::max(tmin.x, tmin.y), tmin.z);
    float tFar  = glm::min(glm::min(tmax.x, tmax.y), tmax.z);

    // hit if intervals overlap and Far is in front
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

    for (glm::vec3 c : corners) {
        glm::vec3 w = glm::vec3(M * glm::vec4(c, 1.0f));
        outMin = glm::min(outMin, w);
        outMax = glm::max(outMax, w);
    }
}

} // anonymous namespace

SceneEditorApp::SceneEditorApp() : pnkr::samples::RhiSampleApp({
    .title = "PNKR Scene Editor (Indirect)",
    .width = 1600,
    .height = 900,
    .createRenderer = true
}) {
}

SceneEditorApp::~SceneEditorApp() = default;

void SceneEditorApp::onInit() {
    // Load Model
    std::filesystem::path modelPath = "assets/Bistro.glb";
    if (!std::filesystem::exists(modelPath)) {
        modelPath = "../samples/rhiPBR/assets/DamagedHelmet.gltf";
    }

    m_model = scene::ModelDOD::load(*m_renderer, modelPath, false);
    if (!m_model) {
        pnkr::core::Logger::error("Failed to load model: {}", modelPath.string());
        return;
    }

    // Init Indirect Renderer
    m_indirectRenderer = std::make_unique<indirect::IndirectRenderer>();
    m_indirectRenderer->init(m_renderer.get(), m_model);
    m_indirectRenderer->setWireframe(m_drawWireframe);

    m_debugLines = std::make_unique<renderer::debug::LineCanvas3D>();
    m_debugLines->initialize(m_renderer.get());

    // Camera
    m_camera.setPerspective(glm::radians(45.0f), (float)m_config.width / (float)m_config.height, 0.1f, 1000.0f);
    m_cameraController.applyToCamera(m_camera);
}

void SceneEditorApp::onUpdate(float dt) {
    m_cameraController.update(m_input, dt);
    m_cameraController.applyToCamera(m_camera);
    float aspect = (float)m_window.width() / (float)m_window.height();
    m_camera.setPerspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f);

    if (!m_model) {
        return;
    }

    tryPick();

    if (m_sceneDirty) {
        m_model->scene().recalculateGlobalTransformsFull();
        m_indirectRenderer->updateGlobalTransforms();
        m_sceneDirty = false;
    }

    if (m_debugLines) {
        m_debugLines->beginFrame();

        const auto& scene = m_model->scene();
        const auto& bounds = m_model->meshBounds();
        const glm::vec3 allColor(1.0f, 0.0f, 0.0f);
        const glm::vec3 selectedColor(0.0f, 1.0f, 0.0f);

        for (size_t nodeId = 0; nodeId < scene.meshIndex.size(); ++nodeId) {
            int32_t meshIdx = scene.meshIndex[nodeId];
            if (meshIdx < 0 || static_cast<size_t>(meshIdx) >= bounds.size()) continue;

            const auto& box = bounds[meshIdx];
            const glm::vec3 size = box.m_max - box.m_min;
            const glm::vec3 center = (box.m_min + box.m_max) * 0.5f;
            glm::mat4 boxTransform = scene.global[nodeId] * glm::translate(glm::mat4(1.0f), center);
            m_debugLines->box(boxTransform, size, allColor);
        }

        if (m_selectedNode >= 0 && static_cast<size_t>(m_selectedNode) < scene.meshIndex.size()) {
            int32_t meshIdx = scene.meshIndex[m_selectedNode];
            if (meshIdx >= 0 && static_cast<size_t>(meshIdx) < bounds.size()) {
                const auto& box = bounds[meshIdx];
                const glm::vec3 size = box.m_max - box.m_min;
                const glm::vec3 center = (box.m_min + box.m_max) * 0.5f;
                glm::mat4 boxTransform = scene.global[m_selectedNode] * glm::translate(glm::mat4(1.0f), center);
                m_debugLines->box(boxTransform, size, selectedColor);
            }
        }

        m_debugLines->endFrame();
    }
}

void SceneEditorApp::onImGui() {
    if (!m_model) return;

    ImGuizmo::BeginFrame();

    // Main Menu
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Exit")) {
                SDL_Event quit_event;
                quit_event.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&quit_event);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Draw Wireframe", nullptr, &m_drawWireframe)) {
                m_indirectRenderer->setWireframe(m_drawWireframe);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // Scene Graph
    ImGui::Begin("Scene Graph");
    int newSelection = ui::renderSceneTree(m_model->scene(), m_model->scene().root, m_selectedNode);
    if (newSelection != -1) m_selectedNode = newSelection;
    ImGui::End();

    // Inspector
    ImGui::Begin("Inspector");
    if (m_selectedNode != -1) {
        // Calculate center offset for the gizmo (AABB center)
        glm::vec3 centerOffset(0.0f);
        int32_t meshIdx = m_model->scene().meshIndex[m_selectedNode];
        if (meshIdx >= 0 && (size_t)meshIdx < m_model->meshBounds().size()) {
            const auto& box = m_model->meshBounds()[meshIdx];
            centerOffset = (box.m_min + box.m_max) * 0.5f;
        }

        // Transform Editor
        if (ui::editTransformUI(m_camera, m_model->scene(), m_selectedNode, centerOffset)) {
            m_sceneDirty = true;
        }

        // Material Editor
        if (meshIdx >= 0) {
            const auto& mesh = m_model->meshes()[meshIdx];
            for (size_t i = 0; i < mesh.primitives.size(); ++i) {
                uint32_t matIdx = mesh.primitives[i].materialIndex;
                if (ImGui::CollapsingHeader(("Material " + std::to_string(matIdx)).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    
                    if (matIdx >= m_model->materialsCPU().size()) {
                        ImGui::Text("Material CPU data unavailable.");
                        continue;
                    }

                    // const_cast to allow editing CPU mirror data
                    auto& cpuMat = const_cast<scene::MaterialCPU&>(m_model->materialsCPU()[matIdx]);
                    
                    // FIX: avoid ImGui ID collisions when rendering multiple materials in the same window.
                    ImGui::PushID((int)matIdx);
                    const bool changed = ui::renderMaterialEditor(cpuMat);
                    ImGui::PopID();

                    if (changed) {
                        // Sync CPU material to GPU material
                        auto& gpuMat = m_model->materialsMutable()[matIdx];
                        
                        gpuMat.m_baseColorFactor = glm::make_vec4(cpuMat.baseColorFactor);
                        gpuMat.m_metallicFactor = cpuMat.metallic;
                        gpuMat.m_roughnessFactor = cpuMat.roughness;
                        gpuMat.m_emissiveFactor = glm::make_vec3(cpuMat.emissiveFactor);
                        gpuMat.m_emissiveStrength = cpuMat.emissiveFactor[3];
                        
                        m_indirectRenderer->updateMaterial(matIdx);
                    }
                }
            }
        }
    } else {
        ImGui::Text("Select a node in the Scene Graph to inspect.");
    }
    ImGui::End();
}

void SceneEditorApp::onRecord(const renderer::RHIFrameContext& ctx) {
    if (!m_model) return;

    m_indirectRenderer->draw(ctx.commandBuffer, m_camera);
    if (m_debugLines) {
        m_debugLines->render(ctx, m_camera.viewProj());
    }
}

void SceneEditorApp::onEvent(const SDL_Event& event) {
    pnkr::samples::RhiSampleApp::onEvent(event);
}

void SceneEditorApp::tryPick() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;
    if (ImGuizmo::IsUsing()) return;

    // Choose a click condition (pressed or released). Released is nicer with ImGuizmo.
    if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left)) return;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 pos = vp->Pos;
    const ImVec2 size = vp->Size;

    Ray ray = makeMouseRay_WorkRect(m_camera, io.MousePos, pos, size);
    if (glm::length2(ray.dir) < 0.5f) return; // invalid/outside

    const auto& scene = m_model->scene();
    const auto& bounds = m_model->meshBounds();

    int bestNode = -1;
    float bestT = std::numeric_limits<float>::infinity();

    // Iterate nodes that have meshes
    for (size_t nodeId = 0; nodeId < scene.meshIndex.size(); ++nodeId) {
        int32_t meshIdx = scene.meshIndex[nodeId];
        if (meshIdx < 0 || (size_t)meshIdx >= bounds.size()) continue;

        const auto& box = bounds[meshIdx];

        glm::vec3 wmin, wmax;
        transformAABB(box.m_min, box.m_max, scene.global[nodeId], wmin, wmax);

        float t = 0.0f;
        if (intersectRayAABB(ray.origin, ray.dir, wmin, wmax, t)) {
            if (t < bestT) {
                bestT = t;
                bestNode = (int)nodeId;
            }
        }
    }

    if (bestNode != -1) {
        m_selectedNode = bestNode;
    }
}
