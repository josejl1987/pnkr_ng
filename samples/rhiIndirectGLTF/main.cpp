#include "pnkr/engine.hpp"
#include "pnkr/renderer/IndirectRenderer.hpp"
#include "pnkr/app/Application.hpp"
#include "pnkr/platform/FileDialog.hpp"
#include "pnkr/core/RecentFiles.hpp"
#include "pnkr/debug/RenderDoc.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/CameraController.hpp"
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>

using namespace pnkr;

namespace {

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
    if (ImGui::Checkbox(name, &on)) {
        if (on) mask |= bit;
        else mask &= ~bit;
        dirty = true;
    }
}

} // namespace

class IndirectSample : public pnkr::app::Application {
public:
    IndirectSample() : Application({.title = "Indirect Rendering - Bistro", .width = 1824, .height = 928, .createRenderer = true}) {}

    std::shared_ptr<renderer::scene::ModelDOD> m_model;
    std::unique_ptr<renderer::IndirectRenderer> m_indirectRenderer;
    renderer::scene::Camera m_camera;
    renderer::scene::CameraController m_cameraController{{-19.2609997, 8.46500015, -7.31699991}, 20.801124201214570, -16.146098030003937f};
    TextureHandle m_brdfLut;
    TextureHandle m_irradiance;
    TextureHandle m_prefilter;

    bool m_useGltfCamera = false;
    int32_t m_activeGltfCameraNode = -1; // SceneGraphDOD node id (includes synthetic root)
    std::vector<uint32_t> m_gltfCameraNodes;

    std::optional<std::filesystem::path> m_pendingLoad;
    pnkr::core::RecentFiles m_recent{"rhiIndirectGLTF", 12};
    bool m_showMaterialEditor = true;
    int m_selectedMaterial = 0;
    bool m_materialDirty = false;
    glm::vec3 m_cameraPosUI{0.0f};
    glm::vec3 m_cameraTargetUI{0.0f, 0.0f, -1.0f};

    void onInit() override {
        m_recent.load();
        
        // Load IBL Textures
        m_brdfLut    = m_renderer->loadTextureKTX("assets/brdf_lut.ktx2");
        m_irradiance = m_renderer->loadTextureKTX("assets/immenstadter_horn_2k_irradiance.ktx");
        m_prefilter  = m_renderer->loadTextureKTX("assets/immenstadter_horn_2k_prefilter.ktx");
        
        if (m_brdfLut == INVALID_TEXTURE_HANDLE || 
            m_irradiance == INVALID_TEXTURE_HANDLE || 
            m_prefilter == INVALID_TEXTURE_HANDLE) 
        {
            pnkr::core::Logger::warn("One or more IBL textures failed to load. PBR will look flat.");
        }

        loadModel(baseDir() / "assets/AnimatedMorphCube.glb");

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

        pnkr::core::Logger::info("Loading model from: {}", path.string());

        m_model = renderer::scene::ModelDOD::load(*m_renderer, path);
        if (!m_model) {
            pnkr::core::Logger::error("Failed to load model");
            return;
        }

        if (!m_model->animations().empty()) {
            auto& state = m_model->animationState();
            state.animIndex = 0;
            state.isPlaying = true;
            state.isLooping = true;
            pnkr::core::Logger::info("Playing animation 0: {}", m_model->animations()[0].name);
        }

        // Init Indirect Renderer
        m_indirectRenderer = std::make_unique<renderer::IndirectRenderer>();
        m_indirectRenderer->init(m_renderer.get(), m_model, m_brdfLut, m_irradiance, m_prefilter);

        m_renderer->setComputeRecordFunc([this](const renderer::RHIFrameContext& ctx) {
            if (m_indirectRenderer) {
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
        for (uint32_t n : scene.topoOrder)
        {
            if (n == scene.root) continue;
            if (n < scene.cameraIndex.size() && scene.cameraIndex[n] >= 0)
                m_gltfCameraNodes.push_back(n);
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
        if (nodeId >= scene.global.size()) return;
        if (nodeId >= scene.cameraIndex.size()) return;

        const int32_t camIndex = scene.cameraIndex[nodeId];
        if (camIndex < 0 || (size_t)camIndex >= m_model->cameras().size()) return;

        // View = inverse(world)
        const glm::mat4 world = scene.global[nodeId];
        const glm::mat4 view  = glm::affineInverse(world);
        m_camera.setViewMatrix(view);

        // Projection from glTF camera
        const auto& gc = m_model->cameras()[(size_t)camIndex];
        if (gc.type == renderer::scene::GltfCamera::Type::Perspective)
        {
            const float aspect = (gc.aspectRatio > 0.0f) ? gc.aspectRatio : viewportAspect;
            const float zfar   = (gc.zfar > 0.0f) ? gc.zfar : 10000.0f; // pragmatic clamp
            m_camera.setPerspective(gc.yfovRad, aspect, gc.znear, zfar);
        }
        else
        {
            // glTF orthographic uses xmag/ymag as half-extents
            const float l = -gc.xmag;
            const float r =  gc.xmag;
            const float b = -gc.ymag;
            const float t =  gc.ymag;
            m_camera.setOrthographic(l, r, b, t, gc.znear, gc.zfar);
        }
    }

    void onUpdate(float dt) override {
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
            m_model->scene().recalculateGlobalTransformsDirty();
            applySelectedGltfCamera(aspect);
        }

        if (m_indirectRenderer) {
            m_indirectRenderer->update(dt);
        }
    }

    void onEvent(const SDL_Event& event) override
    {
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            if (event.key.scancode == SDL_SCANCODE_F10)
            {
                auto& rd = m_renderer->renderdoc();
                
                // Try to lazy load if not available
                if (!rd.isAvailable()) {
                    if (rd.init()) {
                        pnkr::core::Logger::info("RenderDoc hot-loaded via F10.");
                    } else {
                        pnkr::core::Logger::error("RenderDoc DLL not found. Cannot capture.");
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
                    if (auto p = pnkr::platform::FileDialog::OpenGLTFDialog())
                        m_pendingLoad = *p;
                }
        
                if (auto pick = m_recent.drawImGuiMenu("Recent Files")) {
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
                if (rd.isCapturing()) {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "(( %s ))", status.c_str());
                } else {
                    ImGui::TextDisabled("%s", status.c_str());
                }
                ImGui::Separator();

                if (!avail) {
                    if (ImGui::MenuItem("Load RenderDoc DLL")) {
                        rd.init();
                    }
                } else {
                    if (rd.isCapturing()) {
                        if (ImGui::MenuItem("Stop Capture", "F10")) {
                            rd.toggleCapture();
                        }
                    } else {
                        if (ImGui::MenuItem("Start Capture", "F10")) {
                            rd.toggleCapture();
                        }
                    }

                    if (ImGui::MenuItem("Launch Replay UI")) {
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
                if ((int32_t)m_gltfCameraNodes[i] == m_activeGltfCameraNode) { currentIdx = i; break; }

            if (ImGui::BeginCombo("Active", std::to_string(currentIdx).c_str()))
            {
                for (int i = 0; i < (int)m_gltfCameraNodes.size(); ++i)
                {
                    const uint32_t nodeId = m_gltfCameraNodes[i];
                    const int32_t camIndex = scene.cameraIndex[nodeId];
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

        ImGui::Begin("Camera");
        ImGui::TextUnformatted("Position/target controls use the free camera.");
        if (m_useGltfCamera) {
            ImGui::TextUnformatted("Disable glTF camera to edit.");
        }
        ImGui::BeginDisabled(m_useGltfCamera);
        ImGui::InputFloat3("Position", &m_cameraPosUI.x);
        ImGui::InputFloat3("Target", &m_cameraTargetUI.x);
        if (ImGui::Button("Use Current")) {
            m_cameraPosUI = m_cameraController.position();
            m_cameraTargetUI = m_cameraPosUI + m_cameraController.front();
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply")) {
            m_useGltfCamera = false;
            m_cameraController.setLookAt(m_cameraPosUI, m_cameraTargetUI);
            m_cameraController.applyToCamera(m_camera);
        }
        ImGui::EndDisabled();
        ImGui::End();

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
                    m_materialDirty |= ImGui::SliderFloat("MetallicFactor", &m.metallicRoughnessNormalOcclusion.x, 0.0f, 1.0f);
                    m_materialDirty |= ImGui::SliderFloat("RoughnessFactor", &m.metallicRoughnessNormalOcclusion.y, 0.04f, 1.0f);
                    m_materialDirty |= ImGui::SliderFloat("NormalScale", &m.metallicRoughnessNormalOcclusion.z, 0.0f, 2.0f);
                    m_materialDirty |= ImGui::SliderFloat("OcclusionStrength", &m.metallicRoughnessNormalOcclusion.w, 0.0f, 1.0f);

                    m_materialDirty |= ImGui::ColorEdit3("EmissiveFactor", &m.emissiveFactorAlphaCutoff.x);
                    m_materialDirty |= ImGui::SliderFloat("AlphaCutoff", &m.emissiveFactorAlphaCutoff.w, 0.0f, 1.0f);

                    m_materialDirty |= ImGui::SliderFloat("IOR", &m.ior, 1.0f, 2.5f);

                    int alphaMode = (int)m.alphaMode;
                    const char* modes[] = { "OPAQUE (0)", "MASK (1)", "BLEND (2)" };
                    if (ImGui::Combo("AlphaMode", &alphaMode, modes, 3))
                    {
                        m.alphaMode = (uint32_t)alphaMode;
                        m_materialDirty = true;
                    }

                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Textures"))
                {
                    m_materialDirty |= EditTextureSlot("BaseColorTexture", m.baseColorTexture, m.baseColorTextureSampler, m.baseColorTextureUV);
                    m_materialDirty |= EditTextureSlot("MetallicRoughnessTexture", m.metallicRoughnessTexture, m.metallicRoughnessTextureSampler, m.metallicRoughnessTextureUV);
                    m_materialDirty |= EditTextureSlot("NormalTexture", m.normalTexture, m.normalTextureSampler, m.normalTextureUV);
                    m_materialDirty |= EditTextureSlot("OcclusionTexture", m.occlusionTexture, m.occlusionTextureSampler, m.occlusionTextureUV);
                    m_materialDirty |= EditTextureSlot("EmissiveTexture", m.emissiveTexture, m.emissiveTextureSampler, m.emissiveTextureUV);

                    m_materialDirty |= EditTextureSlot("SheenColorTexture", m.sheenColorTexture, m.sheenColorTextureSampler, m.sheenColorTextureUV);
                    m_materialDirty |= EditTextureSlot("SheenRoughnessTexture", m.sheenRoughnessTexture, m.sheenRoughnessTextureSampler, m.sheenRoughnessTextureUV);

                    m_materialDirty |= EditTextureSlot("ClearCoatTexture", m.clearCoatTexture, m.clearCoatTextureSampler, m.clearCoatTextureUV);
                    m_materialDirty |= EditTextureSlot("ClearCoatRoughnessTexture", m.clearCoatRoughnessTexture, m.clearCoatRoughnessTextureSampler, m.clearCoatRoughnessTextureUV);
                    m_materialDirty |= EditTextureSlot("ClearCoatNormalTexture", m.clearCoatNormalTexture, m.clearCoatNormalTextureSampler, m.clearCoatNormalTextureUV);

                    m_materialDirty |= EditTextureSlot("SpecularTexture", m.specularTexture, m.specularTextureSampler, m.specularTextureUV);
                    m_materialDirty |= EditTextureSlot("SpecularColorTexture", m.specularColorTexture, m.specularColorTextureSampler, m.specularColorTextureUV);

                    m_materialDirty |= EditTextureSlot("TransmissionTexture", m.transmissionTexture, m.transmissionTextureSampler, m.transmissionTextureUV);
                    m_materialDirty |= EditTextureSlot("ThicknessTexture", m.thicknessTexture, m.thicknessTextureSampler, m.thicknessTextureUV);

                    m_materialDirty |= EditTextureSlot("IridescenceTexture", m.iridescenceTexture, m.iridescenceTextureSampler, m.iridescenceTextureUV);
                    m_materialDirty |= EditTextureSlot("IridescenceThicknessTexture", m.iridescenceThicknessTexture, m.iridescenceThicknessTextureSampler, m.iridescenceThicknessTextureUV);

                    m_materialDirty |= EditTextureSlot("AnisotropyTexture", m.anisotropyTexture, m.anisotropyTextureSampler, m.anisotropyTextureUV);

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
                        m_materialDirty |= ImGui::SliderFloat("ClearcoatFactor", &m.clearcoatTransmissionThickness.x, 0.0f, 1.0f);
                        m_materialDirty |= ImGui::SliderFloat("ClearcoatRoughness", &m.clearcoatTransmissionThickness.y, 0.0f, 1.0f);
                    }

                    if (m.materialType & (1u << 4))
                    {
                        m_materialDirty |= ImGui::ColorEdit3("SpecularColorFactor", &m.specularFactors.x);
                        m_materialDirty |= ImGui::SliderFloat("SpecularFactor", &m.specularFactors.w, 0.0f, 1.0f);
                    }

                    if (m.materialType & (1u << 5))
                    {
                        m_materialDirty |= ImGui::SliderFloat("TransmissionFactor", &m.clearcoatTransmissionThickness.z, 0.0f, 1.0f);
                    }

                    if (m.materialType & (1u << 6))
                    {
                        m_materialDirty |= ImGui::SliderFloat("ThicknessFactor", &m.clearcoatTransmissionThickness.w, 0.0f, 10.0f);
                        m_materialDirty |= ImGui::ColorEdit3("AttenuationColor", &m.attenuation.x);
                        m_materialDirty |= ImGui::SliderFloat("AttenuationDistance", &m.attenuation.w, 0.0f, 100.0f);
                    }

                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Advanced"))
                {
                    ImGui::Text("materialType mask: 0x%08X", m.materialType);
                    ImGui::Text("alphaMode: %u", m.alphaMode);
                    ImGui::Text("BaseColorTexture: %s (%u)", (m.baseColorTexture == kInvalidId) ? "INVALID" : "OK", m.baseColorTexture);
                    ImGui::Text("MRTexture: %s (%u)", (m.metallicRoughnessTexture == kInvalidId) ? "INVALID" : "OK", m.metallicRoughnessTexture);
                    ImGui::Text("NormalTexture: %s (%u)", (m.normalTexture == kInvalidId) ? "INVALID" : "OK", m.normalTexture);
                    ImGui::Text("OcclusionTexture: %s (%u)", (m.occlusionTexture == kInvalidId) ? "INVALID" : "OK", m.occlusionTexture);
                    ImGui::Text("EmissiveTexture: %s (%u)", (m.emissiveTexture == kInvalidId) ? "INVALID" : "OK", m.emissiveTexture);

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

    void onRecord(const renderer::RHIFrameContext& ctx) override {
        if (m_indirectRenderer) {
            m_indirectRenderer->draw(ctx.commandBuffer, m_camera, ctx.backBuffer->extent().width, ctx.backBuffer->extent().height);
        }
    }
};

int main(int /*argc*/, char** /*argv*/) {
    IndirectSample app;
    return app.run();
}