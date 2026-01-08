#pragma once

#include "pnkr/engine.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/ModelDOD.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/Skybox.hpp"
#include "pnkr/renderer/debug/DebugLayer.hpp"
#include "pnkr/renderer/RenderResourceManager.h"
#include "pnkr/renderer/RenderSettings.hpp"
#include "pnkr/renderer/FrameManager.hpp"
#include "pnkr/renderer/passes/IRenderPass.hpp"
#include "pnkr/renderer/framegraph/FrameGraph.hpp"
#include "pnkr/renderer/gpu_shared/SceneShared.h"
#include "pnkr/renderer/material/GlobalMaterialHeap.hpp"
#include "pnkr/renderer/skinning/GlobalJointBuffer.hpp"
#include "pnkr/renderer/IndirectDrawContext.hpp"
#include "pnkr/renderer/ShaderHotReloader.hpp"
#include "pnkr/renderer/scene/SpriteSystem.hpp"

#include <memory>
#include <vector>
#include <span>
#include <functional>

namespace pnkr::renderer {

    namespace physics { class ClothSystem; }
    class EnvironmentProcessor;

    class GlobalResourcePool;
    class SceneUniformProvider;
    class RenderPipeline;

    class IndirectRenderer {
    public:
        IndirectRenderer();
        ~IndirectRenderer();

        void init(RHIRenderer* renderer, std::shared_ptr<scene::ModelDOD> model,
                  TextureHandle brdf = INVALID_TEXTURE_HANDLE,
                  TextureHandle irradiance = INVALID_TEXTURE_HANDLE,
                  TextureHandle prefilter = INVALID_TEXTURE_HANDLE,
                  TextureHandle skybox = INVALID_TEXTURE_HANDLE);
        void createGlobalResources(uint32_t width, uint32_t height);

        void resize(uint32_t width, uint32_t height);
        void update(float dt);
        void draw(rhi::RHICommandList* cmd, const scene::Camera& camera, uint32_t width, uint32_t height,
                  debug::DebugLayer* debugLayer = nullptr,
                  std::function<void(rhi::RHICommandList*)> uiRender = {});

        RenderSettings& settings() { return m_settings; }
        const RenderSettings& settings() const { return m_settings; }

        void setShadowSettings(const ShadowSettings& s) { m_settings.shadow = s; }
        void setSSAOSettings(const SSAOSettings& s) { m_settings.ssao = s; }
        HDRSettings& hdrSettings() { return m_settings.hdr; }
        void setIBLStrength(float strength);
        void updateMaterial(uint32_t materialIndex);
        void setWireframe(bool enabled) { m_settings.drawWireframe = enabled; }
        void setCullingMode(CullingMode mode) { m_settings.cullingMode = mode; }
        void setFreezeCullingView(bool freeze) { m_settings.freezeCulling = freeze; }
        void setDrawDebugBounds(bool draw) { m_settings.drawDebugBounds = draw; }
        int getShadowCasterIndex() const { return m_resources.shadowCasterIndex; }
        uint32_t getFrameIndex() const { return m_frameManager.getCurrentFrameIndex(); }
        FrameManager& getFrameManager() { return m_frameManager; }

        TextureHandle getShadowMapTexture() const { return m_resources.shadowMap; }
        glm::mat4 getShadowView() const;
        glm::mat4 getShadowProj() const;
        TextureHandle getSSAOTexture() const { return m_resources.ssaoOutput; }
        uint32_t getVisibleMeshCount() const { return m_visibleMeshCount; }

        GlobalMaterialHeap& getMaterialHeap() { return m_materialHeap; }
        const GlobalMaterialHeap& getMaterialHeap() const { return m_materialHeap; }

        GlobalJointBuffer& getJointBuffer() { return m_jointBuffer; }
        const GlobalJointBuffer& getJointBuffer() const { return m_jointBuffer; }

        physics::ClothSystem* getClothSystem() { return m_clothSystem.get(); }
        ShaderHotReloader* getHotReloader() { return m_hotReloader.get(); }
        scene::SpriteSystem* getSpriteSystem() { return m_spriteSystem.get(); }

        void loadEnvironmentMap(const std::filesystem::path& path, bool flipY = false);
        void setSkybox(TextureHandle skybox, bool flipY = false);

        static void dispatchSkinning(rhi::RHICommandList *cmd);
        TransientAllocation updateGlobalTransforms();
        void uploadEnvironmentData();
 
    private:
        IndirectDrawContext prepareFrame(rhi::RHICommandList* cmd, const scene::Camera& camera, uint32_t width, uint32_t height, debug::DebugLayer* debugLayer = nullptr);
        void processCompletedTextures();

        void updateMorphTargets(rhi::RHICommandList* cmd);
        void updateLightsAndShadows(IndirectDrawContext& ctx);
        void buildDrawLists(IndirectDrawContext& ctx, const scene::Camera& camera);

        static void calculateFrustumPlanes(const glm::mat4& viewProj, glm::vec4(&outPlanes)[6]);

        template<typename T>
        void registerPass(T*& outPtr)
        {
            auto pass = std::make_unique<T>();
            outPtr = pass.get();
            m_passes.push_back(std::move(pass));
        }

        RHIRenderer* m_renderer = nullptr;
        RenderResourceManager m_resourceMgr;
        std::shared_ptr<scene::ModelDOD> m_model;

        FrameManager m_frameManager;

        RenderGraphResources m_resources;
        RenderSettings m_settings;
        std::unique_ptr<FrameGraph> m_frameGraph;
        std::unique_ptr<GlobalResourcePool> m_resourcePool;
        std::unique_ptr<SceneUniformProvider> m_sceneUniforms;
        std::unique_ptr<RenderPipeline> m_pipeline;

        std::vector<std::unique_ptr<IRenderPass>> m_passes;

        class CullingPass* m_cullingPassPtr = nullptr;
        class GeometryPass* m_geometryPassPtr = nullptr;
        class ShadowPass* m_shadowPassPtr = nullptr;
        class SSAOPass* m_ssaoPassPtr = nullptr;
        class TransmissionPass* m_transmissionPassPtr = nullptr;
        class OITPass* m_oitPassPtr = nullptr;
        class WBOITPass* m_wboitPassPtr = nullptr;
        class PostProcessPass* m_postProcessPassPtr = nullptr;

        PipelinePtr m_skinningPipeline;

        GlobalMaterialHeap m_materialHeap;
        GlobalJointBuffer m_jointBuffer;

        scene::Skybox m_skybox;
        TextureHandle m_sourceSkyboxHandle = INVALID_TEXTURE_HANDLE;
        TextureHandle m_convertedSkyboxHandle = INVALID_TEXTURE_HANDLE;
        bool m_skyboxFlipY = false;

        uint32_t m_visibleMeshCount = 0;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        float m_dt = 0.016f;

        std::unique_ptr<EnvironmentProcessor> m_envProcessor;
        std::unique_ptr<ShaderHotReloader> m_hotReloader;
        std::unique_ptr<physics::ClothSystem> m_clothSystem;
        std::unique_ptr<scene::SpriteSystem> m_spriteSystem;
        bool m_hasAsyncComputeWork = false;

        glm::mat4 m_cullingViewProj{1.0f};

    };
}
