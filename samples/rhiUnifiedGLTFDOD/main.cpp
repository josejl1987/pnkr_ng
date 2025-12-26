#include "pnkr/engine.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/ModelDOD.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/CameraController.hpp"
#include "pnkr/renderer/scene/InfiniteGrid.hpp"
#include "pnkr/renderer/scene/Skybox.hpp"
#include "pnkr/renderer/scene/SceneGraph.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/app/Application.hpp"
#include "pnkr/core/logger.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <cmath>
#include <algorithm>

// [ADDED] Headers for Matrix Decomposition and Quaternion Math
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

// INCLUDE GENERATED HEADERS from ShaderStructGen
#include "pnkr/generated/gltf.frag.h"
#include "pnkr/generated/gltf.vert.h"
#include "pnkr/renderer/scene/GLTFUnifiedDOD.hpp"

using namespace pnkr;
using namespace pnkr::renderer::scene;

class UnifiedGLTFSample : public app::Application {
public:
    UnifiedGLTFSample() : Application({
        .title = "PNKR - Unified glTF Renderer",
        .width = 1824,
        .height = 928,
        .createRenderer = false
    }) {}

    GLTFUnifiedDODContext m_ctx;
    std::unique_ptr<ModelDOD> m_model;
    Camera m_camera;
    CameraController m_cameraController{{-19.261f, 8.465f, -7.317f}, 20.801124201214570, -16.146098030003937f};
    std::unique_ptr<InfiniteGrid> m_grid;
    std::unique_ptr<Skybox> m_skybox;

    TextureHandle m_irradiance, m_prefilter, m_brdfLut;

    // m_sceneColor: Main Render Target (Opaque + Transparent drawing happens here)
    // m_transmissionCopy: Sampled by shaders (Copy of m_sceneColor after Opaque pass)
    TextureHandle m_sceneColor;
    TextureHandle m_transmissionCopy;

    // [ADDED] Inspector State
    bool m_showInspector = true;
    int m_selectedNodeIndex = -1;
    glm::vec3 m_currentEulerRotation = glm::vec3(0.0f);
    renderer::rhi::ResourceLayout m_sceneColorLayout = renderer::rhi::ResourceLayout::Undefined;
    renderer::rhi::ResourceLayout m_transCopyLayout = renderer::rhi::ResourceLayout::Undefined;
    renderer::rhi::ResourceLayout m_depthLayout = renderer::rhi::ResourceLayout::Undefined;

    struct TRSUI {
        glm::vec3 t{0.0f};
        glm::quat r{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 s{1.0f};
    };

    static std::string getNodeLabel(const SceneGraphDOD& scene, uint32_t nodeId)
    {
        if (nodeId < scene.nameId.size()) {
            const int32_t nameId = scene.nameId[nodeId];
            if (nameId >= 0 && static_cast<size_t>(nameId) < scene.names.size())
                return scene.names[static_cast<size_t>(nameId)];
        }
        return "Node " + std::to_string(nodeId);
    }

    static TRSUI decomposeTRS(const glm::mat4& m)
    {
        TRSUI out{};
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(m, out.s, out.r, out.t, skew, perspective);
        out.r = glm::conjugate(out.r);
        return out;
    }

    static glm::mat4 composeTRS(const TRSUI& trs)
    {
        return glm::translate(glm::mat4(1.0f), trs.t) * glm::toMat4(trs.r) * glm::scale(glm::mat4(1.0f), trs.s);
    }

    void updateEulerFromLocal(uint32_t nodeId)
    {
        const auto& scene = m_ctx.model->scene();
        TRSUI trs = decomposeTRS(scene.local[nodeId]);
        m_currentEulerRotation = glm::degrees(glm::eulerAngles(trs.r));
    }

    void onInit() override {
        renderer::RendererConfig config;
        config.m_enableBindless = true;
        m_renderer = std::make_unique<renderer::RHIRenderer>(m_window, config);

        // 1. Load IBL Maps
        m_brdfLut = m_renderer->loadTextureKTX("assets/brdf_lut.ktx2");
        m_irradiance = m_renderer->loadTextureKTX("assets/piazza_bologni_1k_irradiance.ktx");
        m_prefilter = m_renderer->loadTextureKTX("assets/piazza_bologni_1k_prefilter.ktx");

        // 2. Load GLTF Model via DOD Context
        m_model = ModelDOD::load(*m_renderer, "assets/Bistro.glb");
        if (!m_model) throw cpptrace::runtime_error("Failed to load Bistro.glb");
        m_ctx.model = m_model.get();
        m_ctx.renderer = m_renderer.get();

        uploadMaterials(m_ctx);

        uploadEnvironment(m_ctx, m_prefilter, m_irradiance, m_brdfLut);

        // 3. Create Pipelines
        auto vs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Vertex, getShaderPath("gltf.vert.spv"));
        auto fs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Fragment, getShaderPath("gltf.frag.spv"));

        m_ctx.pipelineSolid = m_renderer->createGraphicsPipeline(
            renderer::rhi::RHIPipelineBuilder()
            .setShaders(vs.get(), fs.get(), nullptr)
            .useVertexType<renderer::Vertex>()
            .setCullMode(renderer::rhi::CullMode::None, true)
            .enableDepthTest()
            .setColorFormat(m_renderer->getDrawColorFormat())
            .setDepthFormat(m_renderer->getDrawDepthFormat())
            .setName("PBRPipeline")
            .buildGraphics()
        );

        m_ctx.pipelineTransparent = m_renderer->createGraphicsPipeline(
            renderer::rhi::RHIPipelineBuilder()
            .setShaders(vs.get(), fs.get(), nullptr)
            .useVertexType<renderer::Vertex>()
            .setCullMode(renderer::rhi::CullMode::None, true)
            .enableDepthTest(false)
            .setAlphaBlend()
            .setColorFormat(m_renderer->getDrawColorFormat())
            .setDepthFormat(m_renderer->getDrawDepthFormat())
            .setName("PBRTransparentPipeline")
            .buildGraphics()
        );

        m_cameraController.applyToCamera(m_camera);
        float aspect = (float)m_window.width() / m_window.height();
        m_camera.setPerspective(glm::radians(45.0f), aspect, 0.01f, 100.0f);

        m_grid = std::make_unique<InfiniteGrid>();
        m_grid->init(*m_renderer);

        // 1. Scene Color (Render Target)
        // Stores the actual drawn geometry.
        renderer::rhi::TextureDescriptor descRT{};
        descRT.extent = { (uint32_t)m_window.width(), (uint32_t)m_window.height(), 1 };
        descRT.format = m_renderer->getSwapchainColorFormat(); // Use same format as Backbuffer (SDR)
        descRT.usage = renderer::rhi::TextureUsage::ColorAttachment | renderer::rhi::TextureUsage::TransferSrc | renderer::rhi::TextureUsage::Sampled;
        descRT.mipLevels = 1;
        descRT.debugName = "SceneColor";
        m_sceneColor = m_renderer->createTexture(descRT);

        // 2. Transmission Copy (Texture)
        // Used for reading the background. Needs Mipmaps for roughness.
        renderer::rhi::TextureDescriptor descCopy{};
        descCopy.extent = descRT.extent;
        descCopy.format = descRT.format;
        descCopy.usage = renderer::rhi::TextureUsage::Sampled | renderer::rhi::TextureUsage::TransferDst | renderer::rhi::TextureUsage::TransferSrc;
        descCopy.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(descRT.extent.width, descRT.extent.height)))) + 1;
        descCopy.debugName = "TransmissionCopy";
        m_transmissionCopy = m_renderer->createTexture(descCopy);

        auto skyboxKtx = resolveSkyboxKtx();
        if (!skyboxKtx.empty())
        {
            auto skyboxHandle = m_renderer->loadTextureKTX(skyboxKtx);
            if (skyboxHandle != INVALID_TEXTURE_HANDLE)
            {
                m_skybox = std::make_unique<Skybox>();
                m_skybox->init(*m_renderer, skyboxHandle);
            }
        }

        initUI();
    }

    // [ADDED] ImGui Hook
    void onImGui() override {
        drawGLTFInspector();
    }

    void drawGLTFInspector() {
        if (!m_showInspector || !m_ctx.model) return;

        if (ImGui::Begin("glTF Inspector", &m_showInspector)) {

            // 1. Scene / Node Hierarchy
            if (ImGui::CollapsingHeader("Scene Hierarchy", ImGuiTreeNodeFlags_DefaultOpen)) {
                const auto& scene = m_ctx.model->scene();
                for (uint32_t nodeId : scene.roots) drawNodeTree(nodeId);
            }

            // 2. Selected Node Transform Editor
            if (m_selectedNodeIndex >= 0) {
                const auto& scene = m_ctx.model->scene();
                if (static_cast<size_t>(m_selectedNodeIndex) >= scene.hierarchy.size()) {
                    ImGui::Text("Invalid node index.");
                    ImGui::End();
                    return;
                }

                ImGui::Separator();
                if (ImGui::CollapsingHeader("Node Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                    drawNodeTransformEditor(m_selectedNodeIndex);
                }

                ImGui::Separator();
                if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
                    drawNodeMaterialEditor(m_selectedNodeIndex);
                }
            }
        }
        ImGui::End();
    }

    void drawNodeTree(int nodeIndex) {
        const uint32_t nodeId = static_cast<uint32_t>(nodeIndex);
        const auto& scene = m_ctx.model->scene();

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        const bool isLeaf = scene.hierarchy[nodeId].firstChild < 0;
        if (isLeaf) flags |= ImGuiTreeNodeFlags_Leaf;
        if (m_selectedNodeIndex == nodeIndex) flags |= ImGuiTreeNodeFlags_Selected;

        const std::string name = getNodeLabel(scene, nodeId);

        bool open = ImGui::TreeNodeEx((void*)(intptr_t)nodeIndex, flags, "%s", name.c_str());

        if (ImGui::IsItemClicked()) {
            m_selectedNodeIndex = nodeIndex;
            updateEulerFromLocal(nodeId);
        }

        if (open) {
            for (int32_t ch = scene.hierarchy[nodeId].firstChild; ch != -1; ch = scene.hierarchy[(uint32_t)ch].nextSibling)
                drawNodeTree((uint32_t)ch);
            ImGui::TreePop();
        }
    }

    void drawNodeTransformEditor(int nodeIndex) {
        const uint32_t nodeId = static_cast<uint32_t>(nodeIndex);
        auto& scene = m_ctx.model->scene();
        TRSUI trs = decomposeTRS(scene.local[nodeId]);
        bool dirty = false;

        // Translation
        if (ImGui::DragFloat3("Translation", &trs.t.x, 0.01f)) dirty = true;

        // Rotation
        glm::vec3 euler = m_currentEulerRotation;
        if (ImGui::DragFloat3("Rotation", &euler.x, 0.1f)) {
            m_currentEulerRotation = euler;
            trs.r = glm::quat(glm::radians(euler));
            dirty = true;
        }

        // Scale
        if (ImGui::DragFloat3("Scale", &trs.s.x, 0.01f)) dirty = true;

        if (dirty) {
            scene.local[nodeId] = composeTRS(trs);
            scene.markAsChanged(nodeId); // mark node + descendants for next recalc
            scene.recalculateGlobalTransformsDirty();
            GLTFUnifiedDOD::buildDrawLists(m_ctx, m_camera.position());
        }
    }

    void drawNodeMaterialEditor(int nodeIndex) {
        const uint32_t nodeId = static_cast<uint32_t>(nodeIndex);
        auto& scene = m_ctx.model->scene();
        if (nodeId >= scene.meshIndex.size() || scene.meshIndex[nodeId] < 0) {
            ImGui::Text("No mesh primitives on this node.");
            return;
        }

        const uint32_t meshId = static_cast<uint32_t>(scene.meshIndex[nodeId]);
        const auto& meshes = m_ctx.model->meshes();
        if (meshId >= meshes.size()) {
            ImGui::Text("Invalid mesh index.");
            return;
        }

        const auto& mesh = meshes[meshId];
        for (size_t i = 0; i < mesh.primitives.size(); ++i) {
            const uint32_t matIdx = mesh.primitives[i].materialIndex;

            std::string label = "Primitive " + std::to_string(i) + " (Material " + std::to_string(matIdx) + ")";
            if (ImGui::TreeNode(label.c_str())) {
                drawMaterialEditor(matIdx);
                ImGui::TreePop();
            }
        }
    }

    void drawMaterialEditor(uint32_t materialIndex) {
        if (materialIndex >= m_ctx.model->materials().size()) return;

        auto& mat = m_ctx.model->materialsMutable()[materialIndex];
        bool dirty = false;

        if (ImGui::Checkbox("Unlit", &mat.m_isUnlit)) dirty = true;

        ImGui::Separator();
        ImGui::Text("Base Material");

        if (ImGui::ColorEdit4("Base Color Factor", &mat.m_baseColorFactor.x)) dirty = true;

        if (!mat.m_isUnlit) {
            if (mat.m_isSpecularGlossiness) {
                if (ImGui::ColorEdit3("Specular Factor", &mat.m_specularFactor.x)) dirty = true;
                if (ImGui::SliderFloat("Glossiness Factor", &mat.m_glossinessFactor, 0.0f, 1.0f)) dirty = true;
            } else {
                if (ImGui::SliderFloat("Metallic Factor", &mat.m_metallicFactor, 0.0f, 1.0f)) dirty = true;
                if (ImGui::SliderFloat("Roughness Factor", &mat.m_roughnessFactor, 0.0f, 1.0f)) dirty = true;
            }

            ImGui::Separator();
            ImGui::Text("Clearcoat");
            if (ImGui::SliderFloat("Clearcoat Factor", &mat.m_clearcoatFactor, 0.0f, 1.0f)) dirty = true;
            if (ImGui::SliderFloat("Clearcoat Roughness", &mat.m_clearcoatRoughnessFactor, 0.0f, 1.0f)) dirty = true;
            if (ImGui::SliderFloat("Clearcoat Normal Scale", &mat.m_clearcoatNormalScale, 0.0f, 2.0f)) dirty = true;

            ImGui::Separator();
            ImGui::Text("Specular (Extension)");
            if (ImGui::Checkbox("Has Specular", &mat.m_hasSpecular)) dirty = true;
            if (ImGui::SliderFloat("Specular Factor Scalar", &mat.m_specularFactorScalar, 0.0f, 1.0f)) dirty = true;
            if (ImGui::ColorEdit3("Specular Color Factor", &mat.m_specularColorFactor.x)) dirty = true;
        }

        ImGui::Separator();
        if (ImGui::ColorEdit3("Emissive Factor", &mat.m_emissiveFactor.x)) dirty = true;
        if (ImGui::SliderFloat("Emissive Strength", &mat.m_emissiveStrength, 0.0f, 10.0f)) dirty = true;

        ImGui::Separator();
        if (ImGui::SliderFloat("Alpha Cutoff", &mat.m_alphaCutoff, 0.0f, 1.0f)) dirty = true;
        if (ImGui::SliderFloat("Normal Scale", &mat.m_normalScale, 0.0f, 2.0f)) dirty = true;
        if (ImGui::SliderFloat("Occlusion Strength", &mat.m_occlusionStrength, 0.0f, 1.0f)) dirty = true;

        if (dirty) {
            uploadMaterials(m_ctx);
        }
    }

    void onUpdate(float dt) override {
        m_cameraController.update(m_input, dt);
        m_cameraController.applyToCamera(m_camera);
        float aspect = (float)m_window.width() / m_window.height();
        m_camera.setPerspective((45.0f), aspect, 0.01f, 100.0f);

    }

     void onRecord(const renderer::RHIFrameContext& ctx) override {
         auto* cmd = ctx.commandBuffer;

         // MUST happen before any vkCmdPipelineBarrier2 that includes buffer/image barriers.
         cmd->endRendering();

         // 1. Setup Scene
         auto& scene = m_ctx.model->scene();
         scene.recalculateGlobalTransformsDirty();

         uploadLights(m_ctx);
         GLTFUnifiedDOD::buildDrawLists(m_ctx, m_camera.position());
         

         // Ensure CPU-updated buffers are visible before shader reads.
         {
            std::vector<renderer::rhi::RHIMemoryBarrier> bufBarriers;

            if (m_ctx.transformBuffer != INVALID_BUFFER_HANDLE) {
                renderer::rhi::RHIMemoryBarrier b{};
                b.buffer = m_renderer->getBuffer(m_ctx.transformBuffer);
                bufBarriers.push_back(b);
            }
            if (m_ctx.materialBuffer != INVALID_BUFFER_HANDLE) {
                renderer::rhi::RHIMemoryBarrier b{};
                b.buffer = m_renderer->getBuffer(m_ctx.materialBuffer);
                bufBarriers.push_back(b);
            }
            if (m_ctx.environmentBuffer != INVALID_BUFFER_HANDLE) {
                renderer::rhi::RHIMemoryBarrier b{};
                b.buffer = m_renderer->getBuffer(m_ctx.environmentBuffer);
                bufBarriers.push_back(b);
            }
            if (m_ctx.lightBuffer != INVALID_BUFFER_HANDLE) {
                renderer::rhi::RHIMemoryBarrier b{};
                b.buffer = m_renderer->getBuffer(m_ctx.lightBuffer);
                bufBarriers.push_back(b);
            }

            if (m_ctx.indirectOpaqueBuffer != INVALID_BUFFER_HANDLE) {
                renderer::rhi::RHIMemoryBarrier b{};
                b.buffer = m_renderer->getBuffer(m_ctx.indirectOpaqueBuffer);
                b.srcAccessStage = renderer::rhi::ShaderStage::Host;
                b.dstAccessStage = renderer::rhi::ShaderStage::DrawIndirect;
                bufBarriers.push_back(b);
            }
            if (m_ctx.indirectTransmissionBuffer != INVALID_BUFFER_HANDLE) {
                renderer::rhi::RHIMemoryBarrier b{};
                b.buffer = m_renderer->getBuffer(m_ctx.indirectTransmissionBuffer);
                b.srcAccessStage = renderer::rhi::ShaderStage::Host;
                b.dstAccessStage = renderer::rhi::ShaderStage::DrawIndirect;
                bufBarriers.push_back(b);
            }
            if (m_ctx.indirectTransparentBuffer != INVALID_BUFFER_HANDLE) {
                renderer::rhi::RHIMemoryBarrier b{};
                b.buffer = m_renderer->getBuffer(m_ctx.indirectTransparentBuffer);
                b.srcAccessStage = renderer::rhi::ShaderStage::Host;
                b.dstAccessStage = renderer::rhi::ShaderStage::DrawIndirect;
                bufBarriers.push_back(b);
            }

            if (!bufBarriers.empty()) {
                cmd->pipelineBarrier(
                    renderer::rhi::ShaderStage::Host,
                    renderer::rhi::ShaderStage::Vertex | renderer::rhi::ShaderStage::Fragment | renderer::rhi::ShaderStage::DrawIndirect,
                    bufBarriers);
            }
        }

        // Bind Global Bindless
        renderer::rhi::RHIDescriptorSet* bindlessSet = m_renderer->device()->getBindlessDescriptorSet();
        cmd->bindDescriptorSet(m_renderer->pipeline(m_ctx.pipelineSolid), 1, bindlessSet);

        // Update PerFrame Data
        ShaderGen::gltf_frag::PerFrameData pc {};
        pc.drawable.model  = glm::mat4(1);
        pc.drawable.view = m_camera.view();
        pc.drawable.proj = m_camera.proj();
        pc.drawable.cameraPos = glm::vec4(m_camera.position(), 1.0f);
        pc.drawable.transformBufferPtr = m_renderer->getBuffer(m_ctx.transformBuffer)->getDeviceAddress();
        pc.drawable.materialBufferPtr = m_renderer->getBuffer(m_ctx.materialBuffer)->getDeviceAddress();
        pc.drawable.environmentBufferPtr = m_renderer->getBuffer(m_ctx.environmentBuffer)->getDeviceAddress();
        pc.drawable.lightBufferPtr = (m_ctx.lightBuffer != INVALID_BUFFER_HANDLE) ? m_renderer->getBuffer(m_ctx.lightBuffer)->getDeviceAddress() : 0;
        pc.drawable.lightCount = (m_ctx.lightBuffer != INVALID_BUFFER_HANDLE) ? m_ctx.activeLightCount : 0;
        pc.drawable.envId = 0u;

        // Indirect draw helper (multi-draw)
        auto drawIndirect = [&](BufferHandle indirectBuf,
                                const std::vector<renderer::rhi::DrawIndexedIndirectCommand>& cmds,
                                const char* label,
                                float r, float g, float b)
        {
            if (indirectBuf == INVALID_BUFFER_HANDLE || cmds.empty()) return;
            cmd->beginDebugLabel(label, r, g, b, 1.0f);
            cmd->drawIndexedIndirect(m_renderer->getBuffer(indirectBuf),
                                     0,
                                     static_cast<uint32_t>(cmds.size()),
                                     static_cast<uint32_t>(sizeof(renderer::rhi::DrawIndexedIndirectCommand)));
            cmd->endDebugLabel();
        };

        // --- PHASE 1: Opaque Pass (Offscreen) ---
        auto* mainRT = m_renderer->getTexture(m_sceneColor);
        auto* backbuffer = m_renderer->getBackbuffer();
        auto* depth = m_renderer->getDepthTexture();

        // Barrier 1: Transition SceneColor (+ Depth) into attachment layouts.
        {
            std::vector<renderer::rhi::RHIMemoryBarrier> beginBarriers;

            renderer::rhi::RHIMemoryBarrier rtBarrier{};
            rtBarrier.texture = mainRT;
            rtBarrier.oldLayout = m_sceneColorLayout;
            rtBarrier.newLayout = renderer::rhi::ResourceLayout::ColorAttachment;
            rtBarrier.srcAccessStage = renderer::rhi::ShaderStage::All;
            rtBarrier.dstAccessStage = renderer::rhi::ShaderStage::RenderTarget;
            beginBarriers.push_back(rtBarrier);

            if (depth != nullptr) {
                renderer::rhi::RHIMemoryBarrier depthBarrier{};
                depthBarrier.texture = depth;
                depthBarrier.oldLayout = m_depthLayout;
                depthBarrier.newLayout = renderer::rhi::ResourceLayout::DepthStencilAttachment;
                depthBarrier.srcAccessStage = renderer::rhi::ShaderStage::All;
                depthBarrier.dstAccessStage = renderer::rhi::ShaderStage::DepthStencilAttachment;
                beginBarriers.push_back(depthBarrier);
            }

            cmd->pipelineBarrier(
                renderer::rhi::ShaderStage::All,
                renderer::rhi::ShaderStage::RenderTarget,
                beginBarriers);

            m_sceneColorLayout = renderer::rhi::ResourceLayout::ColorAttachment;
            if (depth != nullptr) {
                m_depthLayout = renderer::rhi::ResourceLayout::DepthStencilAttachment;
            }
        }

        renderer::rhi::RenderingInfo offscreenInfo{};
        offscreenInfo.renderArea = {0, 0, mainRT->extent().width, mainRT->extent().height};

        renderer::rhi::RenderingAttachment colorAtt{};
        colorAtt.texture = mainRT;
        colorAtt.loadOp = renderer::rhi::LoadOp::Clear;
        colorAtt.storeOp = renderer::rhi::StoreOp::Store;
        colorAtt.clearValue.isDepthStencil = false;
        colorAtt.clearValue.color.float32[0] = colorAtt.clearValue.color.float32[1] = colorAtt.clearValue.color.float32[2] =  colorAtt.clearValue.color.float32[3] = 1.0f;
        offscreenInfo.colorAttachments.push_back(colorAtt);

        renderer::rhi::RenderingAttachment depthAtt{};
        depthAtt.texture = depth;
        depthAtt.loadOp = renderer::rhi::LoadOp::Clear;
        depthAtt.storeOp = renderer::rhi::StoreOp::Store;
        depthAtt.clearValue.isDepthStencil = true;
        depthAtt.clearValue.depthStencil.depth = 1.0f;
        offscreenInfo.depthAttachment = &depthAtt;

        cmd->beginRendering(offscreenInfo);

        renderer::rhi::Viewport vp{0, 0, (float)mainRT->extent().width, (float)mainRT->extent().height, 0, 1};
        cmd->setViewport(vp);
        renderer::rhi::Rect2D sc{0, 0, mainRT->extent().width, mainRT->extent().height};
        cmd->setScissor(sc);

        if (m_ctx.model->vertexBuffer != INVALID_BUFFER_HANDLE) {
            cmd->bindVertexBuffer(0, m_renderer->getBuffer(m_ctx.model->vertexBuffer));
        }
        if (m_ctx.model->indexBuffer != INVALID_BUFFER_HANDLE) {
            cmd->bindIndexBuffer(m_renderer->getBuffer(m_ctx.model->indexBuffer), 0, false);
        }

        if (m_skybox) {
            m_skybox->draw(cmd, m_camera);
        }

        cmd->beginDebugLabel("Opaque Pass", 1.0f, 0.5f, 0.5f, 1.0f);
        m_renderer->bindPipeline(cmd, m_ctx.pipelineSolid);
        m_renderer->pushConstants(cmd, m_ctx.pipelineSolid,
            renderer::rhi::ShaderStage::Vertex | renderer::rhi::ShaderStage::Fragment | renderer::rhi::ShaderStage::DrawIndirect, pc);
        drawIndirect(m_ctx.indirectOpaqueBuffer, m_ctx.indirectOpaque, "Opaque", 0.7f, 0.7f, 0.7f);


        cmd->endDebugLabel();
        cmd->endRendering(); // End Opaque Pass

        // --- PHASE 2: Copy & Mipmap (The Sandwich) ---
        // Matches EID 43-84 in your log
        if (!m_ctx.indirectTransmission.empty()) {
            cmd->beginDebugLabel("Copy Transmission", 1.0f, 1.0f, 0.0f, 1.0f);

            auto* transCopy = m_renderer->getTexture(m_transmissionCopy);

            // Barrier 2: Prepare for Copy
            // SceneColor: Current -> TransferSrc
            // TransCopy:  Current -> TransferDst
            renderer::rhi::RHIMemoryBarrier toSrc{};
            toSrc.texture = mainRT;
            toSrc.oldLayout = m_sceneColorLayout;
            toSrc.newLayout = renderer::rhi::ResourceLayout::TransferSrc;
            toSrc.srcAccessStage = renderer::rhi::ShaderStage::RenderTarget;
            toSrc.dstAccessStage = renderer::rhi::ShaderStage::Transfer;

            renderer::rhi::RHIMemoryBarrier toDst{};
            toDst.texture = transCopy;
            toDst.oldLayout = m_transCopyLayout;
            toDst.newLayout = renderer::rhi::ResourceLayout::TransferDst;
            toDst.srcAccessStage = renderer::rhi::ShaderStage::All;
            toDst.dstAccessStage = renderer::rhi::ShaderStage::Transfer;

            cmd->pipelineBarrier(renderer::rhi::ShaderStage::All,
                                 renderer::rhi::ShaderStage::Transfer, {toSrc, toDst});
            m_sceneColorLayout = renderer::rhi::ResourceLayout::TransferSrc;
            m_transCopyLayout = renderer::rhi::ResourceLayout::TransferDst;

            // Execute Copy
            renderer::rhi::TextureCopyRegion copyR{};
            copyR.srcSubresource = {0, 0};
            copyR.dstSubresource = {0, 0};
            copyR.extent = mainRT->extent();
            cmd->copyTexture(mainRT, transCopy, copyR);

            // Generate Mips (transitions to ShaderReadOnly internally)
            transCopy->generateMipmaps(cmd);
            m_transCopyLayout = renderer::rhi::ResourceLayout::ShaderReadOnly;

            // Barrier 3: Restore for Rendering
            // SceneColor: TransferSrc -> ColorAttachment (We are about to write to it again!)
            renderer::rhi::RHIMemoryBarrier restoreRT{};
            restoreRT.texture = mainRT;
            restoreRT.oldLayout = renderer::rhi::ResourceLayout::TransferSrc;
            restoreRT.newLayout = renderer::rhi::ResourceLayout::ColorAttachment;
            restoreRT.srcAccessStage = renderer::rhi::ShaderStage::Transfer;
            restoreRT.dstAccessStage = renderer::rhi::ShaderStage::RenderTarget; // Wait for copy to finish before drawing
            cmd->pipelineBarrier(renderer::rhi::ShaderStage::Transfer,
                                 renderer::rhi::ShaderStage::RenderTarget,
                                 {restoreRT});
            m_sceneColorLayout = renderer::rhi::ResourceLayout::ColorAttachment;

            cmd->endDebugLabel();

            // Set Push Constants
            uint32_t fbIndex = m_renderer->getTextureBindlessIndex(m_transmissionCopy);
            if (fbIndex == 0xFFFFFFFF) fbIndex = m_renderer->getTextureBindlessIndex(m_renderer->getWhiteTexture());
            pc.drawable.transmissionFramebuffer = fbIndex;
            pc.drawable.transmissionFramebufferSampler = m_renderer->getBindlessSamplerIndex(renderer::rhi::SamplerAddressMode::ClampToEdge);
        } else {
            pc.drawable.transmissionFramebuffer = m_renderer->getTextureBindlessIndex(m_renderer->getWhiteTexture());
            pc.drawable.transmissionFramebufferSampler = m_renderer->getBindlessSamplerIndex(renderer::rhi::SamplerAddressMode::ClampToEdge);
        }

        // --- PHASE 3: Transmission Pass (Resume) ---
        // Matches EID 89 in your log: vkCmdBeginRendering(C=Load, D=Load)
        {
            offscreenInfo.colorAttachments[0].loadOp = renderer::rhi::LoadOp::Load; // KEEP OPAQUE PIXELS
            offscreenInfo.depthAttachment->loadOp = renderer::rhi::LoadOp::Load;    // KEEP DEPTH

            cmd->beginRendering(offscreenInfo);
            cmd->setViewport(vp);
            cmd->setScissor(sc);

            if (!m_ctx.indirectTransmission.empty()) {
                m_renderer->bindPipeline(cmd, m_ctx.pipelineSolid);
                m_renderer->pushConstants(cmd, m_ctx.pipelineSolid,
                    renderer::rhi::ShaderStage::Vertex | renderer::rhi::ShaderStage::Fragment | renderer::rhi::ShaderStage::DrawIndirect, pc);
                drawIndirect(m_ctx.indirectTransmissionBuffer, m_ctx.indirectTransmission, "Transmission Pass", 0.0f, 0.5f, 1.0f);
            }

            if (!m_ctx.indirectTransparent.empty()) {
                m_renderer->bindPipeline(cmd, m_ctx.pipelineTransparent);
                cmd->bindDescriptorSet(m_renderer->pipeline(m_ctx.pipelineTransparent), 1, bindlessSet);
                m_renderer->pushConstants(cmd, m_ctx.pipelineTransparent,
                    renderer::rhi::ShaderStage::Vertex | renderer::rhi::ShaderStage::Fragment | renderer::rhi::ShaderStage::DrawIndirect, pc);
                drawIndirect(m_ctx.indirectTransparentBuffer, m_ctx.indirectTransparent, "Transparent Pass", 0.5f, 1.0f, 0.5f);
            }

            cmd->endRendering();
        }

        // --- PHASE 4: Final Blit ---
        // SceneColor -> Swapchain
        {
            cmd->insertDebugLabel("Final Blit", 1.0f, 1.0f, 1.0f, 1.0f);

            // Barrier 4: Prepare Blit
            // SceneColor: ColorAtt -> TransferSrc
            // Backbuffer: ColorAtt -> TransferDst
            renderer::rhi::RHIMemoryBarrier rtToSrc{};
            rtToSrc.texture = mainRT;
            rtToSrc.oldLayout = m_sceneColorLayout;
            rtToSrc.newLayout = renderer::rhi::ResourceLayout::TransferSrc;
            rtToSrc.srcAccessStage = renderer::rhi::ShaderStage::RenderTarget;
            rtToSrc.dstAccessStage = renderer::rhi::ShaderStage::Transfer;

            renderer::rhi::RHIMemoryBarrier bbToDst{};
            bbToDst.texture = backbuffer;
            bbToDst.oldLayout = renderer::rhi::ResourceLayout::ColorAttachment;
            bbToDst.newLayout = renderer::rhi::ResourceLayout::TransferDst;
            bbToDst.srcAccessStage = renderer::rhi::ShaderStage::RenderTarget;
            bbToDst.dstAccessStage = renderer::rhi::ShaderStage::Transfer;

            cmd->pipelineBarrier(renderer::rhi::ShaderStage::RenderTarget, renderer::rhi::ShaderStage::Transfer, {rtToSrc, bbToDst});
            m_sceneColorLayout = renderer::rhi::ResourceLayout::TransferSrc;

            renderer::rhi::TextureCopyRegion blit{};
            blit.srcSubresource = {0, 0};
            blit.dstSubresource = {0, 0};
            blit.extent = mainRT->extent();
            cmd->copyTexture(mainRT, backbuffer, blit);

            // Barrier 5: Restore Backbuffer for UI/Present
            renderer::rhi::RHIMemoryBarrier bbToColor{};
            bbToColor.texture = backbuffer;
            bbToColor.oldLayout = renderer::rhi::ResourceLayout::TransferDst;
            bbToColor.newLayout = renderer::rhi::ResourceLayout::ColorAttachment;
            bbToColor.srcAccessStage = renderer::rhi::ShaderStage::Transfer;
            bbToColor.dstAccessStage = renderer::rhi::ShaderStage::RenderTarget;

            cmd->pipelineBarrier(renderer::rhi::ShaderStage::Transfer, renderer::rhi::ShaderStage::RenderTarget, {bbToColor});

            renderer::rhi::RHIMemoryBarrier rtBackToColor{};
            rtBackToColor.texture = mainRT;
            rtBackToColor.oldLayout = renderer::rhi::ResourceLayout::TransferSrc;
            rtBackToColor.newLayout = renderer::rhi::ResourceLayout::ColorAttachment;
            rtBackToColor.srcAccessStage = renderer::rhi::ShaderStage::Transfer;
            rtBackToColor.dstAccessStage = renderer::rhi::ShaderStage::RenderTarget;

            cmd->pipelineBarrier(renderer::rhi::ShaderStage::Transfer, renderer::rhi::ShaderStage::RenderTarget, {rtBackToColor});
            m_sceneColorLayout = renderer::rhi::ResourceLayout::ColorAttachment;
        }
        if (m_grid) {
            cmd->insertDebugLabel("Grid");
        //    m_grid->draw(cmd, m_camera);
        }
        // --- PHASE 5: Restart Swapchain Pass ---
        // For ImGui and RHIRenderer::endFrame
        {
            renderer::rhi::RenderingInfo swapchainInfo{};
            swapchainInfo.renderArea = {0, 0, backbuffer->extent().width, backbuffer->extent().height};

            renderer::rhi::RenderingAttachment bbAtt{};
            bbAtt.texture = backbuffer;
            bbAtt.loadOp = renderer::rhi::LoadOp::Load; // Keep Blit result
            bbAtt.storeOp = renderer::rhi::StoreOp::Store;
            swapchainInfo.colorAttachments.push_back(bbAtt);

            cmd->beginRendering(swapchainInfo);
            renderer::rhi::Viewport bbVp{0, 0, (float)backbuffer->extent().width, (float)backbuffer->extent().height, 0, 1};
            cmd->setViewport(bbVp);
            renderer::rhi::Rect2D bbSc{0, 0, backbuffer->extent().width, backbuffer->extent().height};
            cmd->setScissor(bbSc);
        }
    }

private:
    std::filesystem::path resolveSkyboxKtx() const {
        const std::vector<std::filesystem::path> candidates = {
            "assets/skybox.ktx", "assets/skybox.ktx2"
        };
        for (const auto& path : candidates) if (std::filesystem::exists(path)) return path;
        return {};
    }
};

int main() {
    UnifiedGLTFSample app;
    return app.run();
}







