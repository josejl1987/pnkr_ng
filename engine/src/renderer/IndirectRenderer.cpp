#include "pnkr/renderer/IndirectRenderer.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/profiler.hpp"
#include "pnkr/renderer/GlobalResourcePool.hpp"
#include "pnkr/renderer/GPUBufferSlice.hpp"
#include "pnkr/renderer/IndirectPipeline.hpp"
#include "pnkr/renderer/SceneUniformProvider.hpp"
#include "pnkr/renderer/framegraph/FrameGraph.hpp"
#include "pnkr/renderer/geometry/Frustum.hpp"
#include "pnkr/renderer/gpu_shared/SceneShared.h"
#include "pnkr/renderer/passes/CullingPass.hpp"
#include "pnkr/renderer/passes/GeometryPass.hpp"
#include "pnkr/renderer/passes/OITPass.hpp"
#include "pnkr/renderer/passes/PostProcessPass.hpp"
#include "pnkr/renderer/passes/RenderPassUtils.hpp"
#include "pnkr/renderer/passes/SSAOPass.hpp"
#include "pnkr/renderer/passes/ShadowPass.hpp"
#include "pnkr/renderer/passes/TransmissionPass.hpp"
#include "pnkr/renderer/passes/WBOITPass.hpp"
#include "pnkr/renderer/scene/AnimationSystem.hpp"
#include "pnkr/renderer/scene/Bounds.hpp"
#include "pnkr/renderer/scene/GLTFUnifiedDOD.hpp"
#include "pnkr/renderer/shader_payload_helpers.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <span>
#include <utility>

#include "pnkr/renderer/gpu_shared/SkinningShared.h"
#include "pnkr/renderer/environment/EnvironmentProcessor.hpp"
#include "pnkr/core/cvar.hpp"
#include "pnkr/renderer/physics/ClothSystem.hpp"

namespace pnkr::renderer
{
    using namespace gpu;
    using namespace pnkr::renderer::scene;
    using pnkr::core::Logger;

    AUTO_CVAR_FLOAT(rShadowBias, "Constant depth bias for shadow mapping",
                    0.005F);
    AUTO_CVAR_INT(r_msaa, "MSAA sample count (1, 2, 4)", 1,
                  core::CVarFlags::save);
    AUTO_CVAR_BOOL(r_msaaSampleShading, "Enable MSAA per-sample shading", false,
                   core::CVarFlags::save);

    IndirectRenderer::IndirectRenderer() = default;

    IndirectRenderer::~IndirectRenderer()
    {
      if (m_renderer != nullptr) {
        m_renderer->device()->waitIdle();
      }
        m_resourceMgr.purgeAll();
        m_frameManager.shutdown();
    }

    void IndirectRenderer::init(RHIRenderer* renderer, std::shared_ptr<ModelDOD> model,
                                TextureHandle brdf, TextureHandle irradiance, TextureHandle prefilter,
                                TextureHandle skybox)
    {
        m_renderer = renderer;
        m_renderer->setUseDefaultRenderPass(false);
        m_resourceMgr.setRenderer(m_renderer);
        m_model = std::move(model);

        m_frameGraph = std::make_unique<FrameGraph>(renderer);
        m_frameGraph->setResourceManager(&m_resourceMgr);

        m_resourcePool = std::make_unique<GlobalResourcePool>();
        m_resourcePool->init(m_renderer, &m_settings, &m_resources);

        m_sceneUniforms = std::make_unique<SceneUniformProvider>();
        m_sceneUniforms->init(m_renderer, &m_frameManager, &m_settings, &m_resources);
        m_sceneUniforms->setModel(m_model.get());

        m_frameManager.init(m_renderer, m_renderer->getSwapchain()->framesInFlight());

        registerPass(m_shadowPassPtr);
        registerPass(m_cullingPassPtr);
        registerPass(m_geometryPassPtr);
        registerPass(m_ssaoPassPtr);
        registerPass(m_transmissionPassPtr);
        registerPass(m_oitPassPtr);
        registerPass(m_wboitPassPtr);
        registerPass(m_postProcessPassPtr);

        uint32_t width = m_renderer->getSwapchain()->extent().width;

        uint32_t height = m_renderer->getSwapchain()->extent().height;

        for (auto& pass : m_passes)
        {
            pass->init(m_renderer, width, height);
        }

        m_envProcessor = std::make_unique<EnvironmentProcessor>(renderer);

        m_clothSystem = std::make_unique<physics::ClothSystem>();
        m_clothSystem->init(m_renderer->device(), m_renderer->resourceManager());

        createGlobalResources(width, height);

        MSAASettings effectiveMsaa = m_settings.msaa;
        effectiveMsaa.sampleCount = m_resources.effectiveMsaaSamples;
        for (auto &pass : m_passes) {
          pass->resize(width, height, effectiveMsaa);
        }

        if (m_ssaoPassPtr != nullptr) {
          m_resources.ssaoOutput = m_ssaoPassPtr->getSSAOTexture();
        }

        if (brdf == INVALID_TEXTURE_HANDLE ||
            ((m_renderer->assets() != nullptr) &&
             brdf == m_renderer->assets()->getErrorTexture())) {
          core::Logger::Render.info("IndirectRenderer: BRDF LUT invalid or "
                                    "missing, generating procedural LUT...");
          EnvironmentProcessor processor(m_renderer);
          m_resources.brdfLut = processor.generateBRDFLUT();
        } else {
          m_resources.brdfLut = brdf;
        }

        m_skyboxFlipY = false;
        m_resources.irradianceMap = irradiance;
        m_resources.prefilterMap = prefilter;

        auto isCubemap = [&](TextureHandle handle) -> bool {
          if (handle == INVALID_TEXTURE_HANDLE) {
            return false;
          }
          auto *tex = m_renderer->getTexture(handle);
          return tex && tex->type() == rhi::TextureType::TextureCube;
        };

        TextureHandle skyboxCandidate = skybox;
        if (skyboxCandidate == INVALID_TEXTURE_HANDLE && isCubemap(prefilter))
        {
            skyboxCandidate = prefilter;
        }

        if ((m_resources.irradianceMap == INVALID_TEXTURE_HANDLE ||
             m_resources.prefilterMap == INVALID_TEXTURE_HANDLE) && isCubemap(skyboxCandidate))
        {
            core::Logger::Render.info("IndirectRenderer: IBL maps missing, generating from skybox...");
            EnvironmentProcessor processor(m_renderer);
            GeneratedIBL ibl = processor.processEnvironment(skyboxCandidate, m_skyboxFlipY);

            if (ibl.irradianceMap == INVALID_TEXTURE_HANDLE || ibl.prefilteredMap == INVALID_TEXTURE_HANDLE)
            {
                core::Logger::Render.error("IndirectRenderer: Failed to generate IBL maps");
            }
            else
            {
                if (m_resources.irradianceMap == INVALID_TEXTURE_HANDLE)
                {
                    m_resources.irradianceMap = ibl.irradianceMap;
                }
                else
                {
                    m_resourceMgr.destroyTextureDeferred(ibl.irradianceMap);
                }

                if (m_resources.prefilterMap == INVALID_TEXTURE_HANDLE)
                {
                    m_resources.prefilterMap = ibl.prefilteredMap;
                }
                else
                {
                    m_resourceMgr.destroyTextureDeferred(ibl.prefilteredMap);
                }
            }
        }

        auto comp = rhi::Shader::load(rhi::ShaderStage::Compute, "shaders/skinning.spv");
        rhi::RHIPipelineBuilder builder;
        m_skinningPipeline = m_renderer->createComputePipeline(
            builder.setComputeShader(comp.get()).setName("Skinning").buildCompute());

        m_pipeline = std::make_unique<IndirectPipeline>(IndirectPipeline::Dependencies{
            .renderer = m_renderer,
            .frameManager = &m_frameManager,
            .resources = &m_resources,
            .settings = &m_settings,
            .model = m_model.get(),
            .skinningPipeline = m_skinningPipeline,
            .cullingPass = m_cullingPassPtr,
            .geometryPass = m_geometryPassPtr,
            .shadowPass = m_shadowPassPtr,
            .ssaoPass = m_ssaoPassPtr,
            .transmissionPass = m_transmissionPassPtr,
            .oitPass = m_oitPassPtr,
            .wboitPass = m_wboitPassPtr,
            .postProcessPass = m_postProcessPassPtr,
            .clothSystem = m_clothSystem.get(),
        });

        m_materialHeap.initialize(m_renderer, 10000);
        m_jointBuffer.initialize(m_renderer, 65536);

        if (m_model)
        {
            const auto& materials = m_model->materials();
            if (!materials.empty())
            {
                m_model->scene().setMaterialBaseIndex(m_materialHeap.allocateBlock(materials));
            }
        }

        if (isCubemap(skyboxCandidate))
        {
            m_skybox.init(*m_renderer, skyboxCandidate);
            m_skybox.setFlipY(m_skyboxFlipY);
        }

        uploadEnvironmentData();
    }

    void IndirectRenderer::createGlobalResources(uint32_t width, uint32_t height)
    {
        m_resourcePool->create(width, height, static_cast<uint32_t>(r_msaa.get()),
                               r_msaaSampleShading.get());
    }

    void IndirectRenderer::resize(uint32_t width, uint32_t height)
    {
        auto result =
            m_resourcePool->resize(width, height,
                                   static_cast<uint32_t>(r_msaa.get()),
                                   r_msaaSampleShading.get());
        if (!result.sizeChanged && !result.msaaChanged) {
            return;
        }

        MSAASettings effectiveMsaa = m_settings.msaa;
        effectiveMsaa.sampleCount = m_resources.effectiveMsaaSamples;

        m_width = width;
        m_height = height;

        if (result.msaaChanged)
        {
            m_skybox.resize(m_resources.effectiveMsaaSamples);
        }

        if (m_wboitPassPtr != nullptr) {
          m_wboitPassPtr->resize(m_width, m_height, effectiveMsaa);
        }
        if (m_oitPassPtr != nullptr) {
          m_oitPassPtr->resize(m_width, m_height, effectiveMsaa);
        }
        for (auto &pass : m_passes) {
          pass->resize(width, height, effectiveMsaa);
        }

        if (m_ssaoPassPtr != nullptr) {
          m_resources.ssaoOutput = m_ssaoPassPtr->getSSAOTexture();
        }
    }

    void IndirectRenderer::update(float dt)
    {
        PNKR_PROFILE_FUNCTION();
        if ((m_renderer != nullptr) &&
            (m_renderer->resourceManager() != nullptr)) {
          m_renderer->resourceManager()->reportToTracy();
        }

        m_dt = dt;
        processCompletedTextures();
        if (m_model)
        {
            AnimationSystem::update(*m_model, dt);
            m_model->scene().updateTransforms();

            if (!m_model->skins().empty())
            {
                auto skinnedBounds = AnimationSystem::calculateSkinnedBounds(*m_model);
                auto skinView = m_model->scene().registry().view<SkinComponent, LocalBounds, WorldTransform>();

                skinView.each([&](ecs::Entity entity, SkinComponent&, LocalBounds& lb, WorldTransform& wt)
                {
                    glm::mat4 invWorld = glm::inverse(wt.matrix);
                    lb.aabb = transformAabbFast(skinnedBounds, invWorld);
                    if (!m_model->scene().registry().has<BoundsDirtyTag>(entity))
                    {
                        m_model->scene().registry().emplace<BoundsDirtyTag>(entity);
                    }
                });
            }

            updateWorldBounds(m_model->scene());
        }
    }

    TransientAllocation IndirectRenderer::updateGlobalTransforms()
    {
        const auto& scene = m_model->scene();
        if (scene.topoOrder().empty()) {
          return {};
        }

        uint32_t maxEntityId = 0;
        for (ecs::Entity e : scene.topoOrder()) {
          maxEntityId = std::max(maxEntityId, (uint32_t)e);
        }
        size_t nodeCount = maxEntityId + 1;
        size_t dataSize = nodeCount * sizeof(gpu::InstanceData);

        auto alloc = m_frameManager.allocateUpload(dataSize, 16);

        if (alloc.mappedPtr != nullptr) {
          auto *dst = reinterpret_cast<::gpu::InstanceData *>(alloc.mappedPtr);
          auto &registry = m_model->scene().registry();

          uint64_t vertexBufferAddr = 0;
          if (m_model->vertexBuffer() != INVALID_BUFFER_HANDLE) {
            vertexBufferAddr = m_renderer->getBuffer(m_model->vertexBuffer())
                                   ->getDeviceAddress();
          }

          uint64_t skinnedVertexBufferAddr = 0;
          auto &frame = m_frameManager.getCurrentFrameBuffers();
          if (frame.skinnedVertexBuffer.isValid()) {
            skinnedVertexBufferAddr =
                m_renderer->getBuffer(frame.skinnedVertexBuffer.handle())
                    ->getDeviceAddress();
          }

          if (skinnedVertexBufferAddr == 0) {
            skinnedVertexBufferAddr = vertexBufferAddr;
          }

          for (ecs::Entity e : m_model->scene().topoOrder()) {
            auto &inst = dst[e];

            inst.world = glm::mat4(1.0F);
            inst.worldIT = glm::mat4(1.0F);
            inst.vertexBufferPtr = 0;
            inst.materialIndex = 0;
            inst.meshIndex = 0;

            {
              if (registry.has<WorldTransform>(e)) {
                const auto &wt = registry.get<WorldTransform>(e);
                inst.world = wt.matrix;
                inst.worldIT = glm::transpose(glm::inverse(wt.matrix));
              }

              if (registry.has<MeshRenderer>(e)) {
                const auto &mr = registry.get<MeshRenderer>(e);
                inst.materialIndex =
                    (mr.materialOverride >= 0)
                        ? static_cast<uint32_t>(mr.materialOverride)
                        : 0;
                inst.meshIndex =
                    (mr.meshID >= 0) ? static_cast<uint32_t>(mr.meshID) : 0;
                inst.vertexBufferPtr = vertexBufferAddr;
              } else if (registry.has<SkinnedMeshRenderer>(e)) {
                const auto &smr = registry.get<SkinnedMeshRenderer>(e);
                inst.materialIndex =
                    (smr.materialOverride >= 0)
                        ? static_cast<uint32_t>(smr.materialOverride)
                        : 0;
                inst.meshIndex = 0;
                inst.vertexBufferPtr = skinnedVertexBufferAddr;
              }
            }
          }
        }

        return alloc;
    }

    void IndirectRenderer::processCompletedTextures()
    {
      if ((m_renderer == nullptr) || !m_model) {
        return;
      }
        auto* assets = m_renderer->assets();
        if (assets == nullptr) {
          return;
        }

        auto completed = assets->consumeCompletedTextures();
        if (completed.empty()) {
          return;
        }

        bool envNeedsUpdate = false;

        for (const auto handle : completed)
        {
            if (handle == m_resources.irradianceMap ||
                handle == m_resources.prefilterMap ||
                handle == m_resources.brdfLut ||
                handle == m_sourceSkyboxHandle)
            {
                envNeedsUpdate = true;
                break;
            }
        }

        auto& pending = m_model->pendingTexturesMutable();
        auto& textures = m_model->texturesMutable();

        bool anyModelTextureUpdated = false;
        if (!pending.empty())
        {
            for (const auto handle : completed)
            {
                for (size_t i = 0; i < pending.size(); ++i)
                {
                    if (pending[i] == handle)
                    {
                        textures[i] = handle;
                        pending[i] = INVALID_TEXTURE_HANDLE;
                        anyModelTextureUpdated = true;
                        break;
                    }
                }
            }
        }

        if (anyModelTextureUpdated)
        {
            const auto& modelMaterials = m_model->materials();
            const uint32_t baseIndex = m_model->scene().materialBaseIndex();

            for (uint32_t i = 0; i < modelMaterials.size(); ++i)
            {
                m_materialHeap.setMaterial(baseIndex + i, modelMaterials[i]);
            }
        }

        if (envNeedsUpdate)
        {
            if (m_sourceSkyboxHandle != INVALID_TEXTURE_HANDLE)
            {
                Logger::Render.info("Skybox source texture finished loading, re-triggering setSkybox");
                setSkybox(m_sourceSkyboxHandle, m_skyboxFlipY);
            }
            else
            {
                Logger::Render.info("IBL/BRDF textures finished loading, re-packing environment data");
                uploadEnvironmentData();
            }
        }
    }

    void IndirectRenderer::updateLightsAndShadows(IndirectDrawContext& ctx)
    {
        ctx.lightCount = m_sceneUniforms->updateLights(ctx.lightSlice, ctx.lightAddr);

        uint32_t shadowDataCount = std::max(1u, ctx.lightCount);
        size_t shadowDataSize = shadowDataCount * sizeof(::gpu::ShadowDataGPU);

        auto alloc = m_frameManager.allocateUpload(shadowDataSize, 16);
        ctx.shadowDataAddr = alloc.deviceAddress;

        m_frameManager.getCurrentFrame().mappedShadowData = alloc.mappedPtr;

        if (alloc.mappedPtr != nullptr) {
             auto* shadowDataArray = reinterpret_cast<::gpu::ShadowDataGPU*>(alloc.mappedPtr);

             for(uint32_t i = 0; i < shadowDataCount; ++i) {
                 shadowDataArray[i] = {};
                 shadowDataArray[i].lightViewProjRaw = glm::mat4(1.0f);
                 shadowDataArray[i].lightViewProjBiased = glm::mat4(1.0f);
                 shadowDataArray[i].shadowBias = rShadowBias.get();
                 shadowDataArray[i].shadowMapTexture = util::u32(rhi::TextureBindlessHandle::Invalid);
                 shadowDataArray[i].shadowMapSampler = 0;
             }

             if (m_shadowPassPtr != nullptr &&
                 m_resources.shadowCasterIndex >= 0 &&
                 (uint32_t)m_resources.shadowCasterIndex < shadowDataCount)
             {
                 auto *tex = m_renderer->getTexture(m_shadowPassPtr->getShadowMap());
                 if (tex != nullptr) {
                     auto& entry = shadowDataArray[m_resources.shadowCasterIndex];

                     entry.shadowMapTexture = m_shadowPassPtr->getShadowMapBindlessHandle().index();
                     entry.shadowMapSampler = (uint32_t)m_renderer->getBindlessSamplerIndex(rhi::SamplerAddressMode::ClampToBorder);
                     entry.shadowMapTexelSize = {1.0F / (float)tex->extent().width, 1.0F / (float)tex->extent().height};
                 }
             }
        }
    }

    void IndirectRenderer::uploadEnvironmentData()
    {
        m_sceneUniforms->updateEnvironmentData(
            m_resources.prefilterMap,
            m_resources.irradianceMap,
            m_resources.brdfLut,
            m_settings.iblStrength,
            m_settings.skyboxRotation);
    }

    void
    IndirectRenderer::updateMorphTargets(rhi::RHICommandList * ) {

      if (m_model->morphStates().empty() ||
          m_model->morphVertexBuffer() == INVALID_BUFFER_HANDLE) {
        return;
      }

      auto &frame = m_frameManager.getCurrentFrameBuffers();
      const auto &morphStates = m_model->morphStates();

      const size_t bytes = morphStates.size() * sizeof(::gpu::MorphState);
      auto alloc = m_frameManager.allocateUpload(bytes, 16);
      if (alloc.mappedPtr != nullptr) {
        std::memcpy(alloc.mappedPtr, morphStates.data(), bytes);
      }
      frame.morphStateBuffer = alloc.buffer;
      frame.morphStateOffset = alloc.offset;
      frame.morphStateDeviceAddr = alloc.deviceAddress;
    }

    void IndirectRenderer::buildDrawLists(IndirectDrawContext& ctx, const scene::Camera& camera)
    {
        ctx.dodContext.renderer = m_renderer;
        ctx.dodContext.model = m_model.get();
        ctx.dodContext.mergeByMaterial = false;
        ctx.dodContext.uploadTransformBuffer = false;
        ctx.dodContext.uploadIndirectBuffers = false;
        ctx.dodContext.systemMeshCount = static_cast<uint32_t>(SystemMeshType::Count);
        ctx.dodContext.ignoreVisibility = (m_settings.cullingMode == CullingMode::GPU);

        auto& frame = m_frameManager.getCurrentFrameBuffers();
        if (frame.skinnedVertexBuffer.isValid())
        {
            ctx.dodContext.vertexBufferOverride = m_renderer->getBuffer(frame.skinnedVertexBuffer.handle())->
                                                              getDeviceAddress();
        }

        static core::LinearAllocator cpuTempAllocator(
            static_cast<size_t>(8 * 1024 * 1024));
        cpuTempAllocator.reset();
        scene::GLTFUnifiedDOD::buildDrawLists(ctx.dodContext, camera.position(), cpuTempAllocator);

        if (ctx.dodContext.transformCount > 0)
        {
            const size_t bytes = ctx.dodContext.transformCount * sizeof(gpu::InstanceData);
            auto alloc = m_frameManager.allocateUpload(bytes, 16);
            if (alloc.mappedPtr != nullptr)
            {
                std::memcpy(alloc.mappedPtr, ctx.dodContext.transforms, bytes);
            }
            ctx.instanceXformSlice = {.offset = alloc.offset, .size = bytes};
            ctx.instanceXformAddr = alloc.deviceAddress;
        }
    }

    void IndirectRenderer::setIBLStrength(float strength)
    {
        m_settings.iblStrength = strength;
        uploadEnvironmentData();
    }

    void IndirectRenderer::updateMaterial(uint32_t materialIndex)
    {
        m_materialHeap.updateMaterial(materialIndex);
    }

    void IndirectRenderer::setSkybox(TextureHandle skybox, bool flipY)
    {
      if (m_renderer == nullptr) {
        return;
      }
        if (skybox == INVALID_TEXTURE_HANDLE)
        {
            Logger::Render.error("IndirectRenderer: Invalid skybox handle");
            return;
        }

        auto* tex = m_renderer->getTexture(skybox);
        if (tex == nullptr) {
          Logger::Render.error("IndirectRenderer: Skybox texture invalid");
          return;
        }

        m_renderer->device()->waitIdle();
        m_skyboxFlipY = flipY;
        m_sourceSkyboxHandle = skybox;

        EnvironmentProcessor processor(m_renderer);
        TextureHandle targetSkybox = skybox;

        if (m_convertedSkyboxHandle != INVALID_TEXTURE_HANDLE && m_convertedSkyboxHandle != skybox)
        {
            m_resourceMgr.destroyTextureDeferred(m_convertedSkyboxHandle);
            m_convertedSkyboxHandle = INVALID_TEXTURE_HANDLE;
        }

        if (tex->type() == rhi::TextureType::Texture2D)
        {
            Logger::Render.info("IndirectRenderer: Converting Equirectangular Skybox to Cubemap (1024x1024). Handle: {}", util::u32(skybox.index));
            m_convertedSkyboxHandle = processor.convertEquirectangularToCubemap(skybox, 1024);
            targetSkybox = m_convertedSkyboxHandle;

            if (targetSkybox == INVALID_TEXTURE_HANDLE) {
                Logger::Render.error("Failed to convert skybox");
                return;
            }
        }
        else if (tex->type() != rhi::TextureType::TextureCube)
        {
            Logger::Render.error("IndirectRenderer: Skybox texture is not a compatible type (2D or Cube). Type={}", (int)tex->type());
            return;
        }

        GeneratedIBL ibl = processor.processEnvironment(targetSkybox, m_skyboxFlipY);

        if (ibl.irradianceMap == INVALID_TEXTURE_HANDLE || ibl.prefilteredMap == INVALID_TEXTURE_HANDLE)
        {
            Logger::Render.error("Failed to generate IBL maps for skybox");
            return;
        }

        if (m_resources.irradianceMap != INVALID_TEXTURE_HANDLE) {
          m_resourceMgr.destroyTextureDeferred(m_resources.irradianceMap);
        }

        if (m_resources.prefilterMap != INVALID_TEXTURE_HANDLE) {
          m_resourceMgr.destroyTextureDeferred(m_resources.prefilterMap);
        }

        m_resources.irradianceMap = ibl.irradianceMap;
        m_resources.prefilterMap = ibl.prefilteredMap;

        m_skybox.init(*m_renderer, targetSkybox);
        m_skybox.setFlipY(m_skyboxFlipY);
        m_skybox.setRotation(m_settings.skyboxRotation);
        m_skybox.setRotation(m_settings.skyboxRotation);
        m_resources.skyboxCubemap = targetSkybox;
        uploadEnvironmentData();
    }

    void IndirectRenderer::loadEnvironmentMap(const std::filesystem::path& path, bool flipY)
    {
      if (m_renderer == nullptr) {
        return;
      }

        std::string ext = path.extension().string();
        std::ranges::transform(ext, ext.begin(),
                               [](unsigned char c) { return std::tolower(c); });

        TextureHandle newSkybox = INVALID_TEXTURE_HANDLE;
        if (ext == ".hdr" || ext == ".png" || ext == ".jpg" || ext == ".tga")
        {
            newSkybox = m_renderer->assets()->loadTexture(path, false).handle();
        }
        else
        {
            newSkybox = m_renderer->assets()->loadTextureKTX(path, true).handle();
        }

        if (newSkybox == INVALID_TEXTURE_HANDLE)
        {
            Logger::Render.error("Failed to load skybox texture: {}", path.string());
            return;
        }

        setSkybox(newSkybox, flipY);

        Logger::Render.info("Environment map loaded successfully");
    }

    void IndirectRenderer::dispatchSkinning(rhi::RHICommandList* cmd)
    {
        (void)cmd;
    }

    void IndirectRenderer::calculateFrustumPlanes(const glm::mat4& viewProj, glm::vec4 (&outPlanes)[6])
    {
        outPlanes[0] = glm::row(viewProj, 3) + glm::row(viewProj, 0);
        outPlanes[1] = glm::row(viewProj, 3) - glm::row(viewProj, 0);
        outPlanes[2] = glm::row(viewProj, 3) + glm::row(viewProj, 1);
        outPlanes[3] = glm::row(viewProj, 3) - glm::row(viewProj, 1);
        outPlanes[4] = glm::row(viewProj, 3) + glm::row(viewProj, 2);
        outPlanes[5] = glm::row(viewProj, 3) - glm::row(viewProj, 2);

        for (auto &outPlane : outPlanes) {
          outPlane /= glm::length(glm::vec3(outPlane));
        }
    }

    IndirectDrawContext IndirectRenderer::prepareFrame(
        rhi::RHICommandList* cmd, const scene::Camera& camera, uint32_t width, uint32_t height, debug::DebugLayer* debugLayer)
    {
        (void)width; (void)height; (void)debugLayer;
        IndirectDrawContext ctx;
        auto& frame = m_frameManager.getCurrentFrameBuffers();

        if (!m_settings.freezeCulling)
        {
            m_cullingViewProj = camera.viewProj();
        }

        m_jointBuffer.reset();

        if (!m_model->skins().empty())
        {
            auto joints = scene::AnimationSystem::updateSkinning(*m_model);
            if (!joints.empty())
            {
                const auto& state = m_model->animationState();
                const auto& m0 = joints[0];

                if (!m_model->skins().empty())
                {
                    const auto& skin = m_model->skins()[0];
                    if (!skin.joints.empty())
                    {
                        ecs::Entity j0 = skin.joints[0];
                        if (m_model->scene().registry().has<WorldTransform>(j0))
                        {
                            const auto& worldM = m_model->scene().registry().get<WorldTransform>(j0).matrix;
                            const auto& ibm = skin.inverseBindMatrices[0];
                            Logger::Render.debug(
                                "Anim: {} | Time: {:.3f} | World0[0,0]: {:.3f} | IBM0[0,0]: {:.3f} | Final0[0,0]: {:.3f}",
                                state.animIndex, state.currentTime, worldM[0][0], ibm[0][0], m0[0][0]);
                        }
                    }
                }

                auto alloc = m_jointBuffer.allocate(static_cast<uint32_t>(joints.size()));
                UploadJointsRequest uploadRequest{
                    .renderer = *m_renderer,
                    .cmd = *cmd,
                    .frameManager = m_frameManager,
                    .alloc = alloc,
                    .matrices = std::span<const glm::mat4>(joints.data(), joints.size())
                };
                m_jointBuffer.uploadJoints(uploadRequest);
                frame.jointMatricesBuffer = BufferPtr(nullptr, m_jointBuffer.getBufferHandle());
            }
        }

        if (!m_model->skins().empty() || !m_model->morphStates().empty())
        {
            {
                auto& registry = m_model->scene().registry();
                auto view = registry.view<MeshRenderer, WorldTransform>();

                std::vector<gpu::MeshXform> meshXforms(m_model->meshes().size());
                for (auto& mx : meshXforms)
                {
                  mx.invModel = glm::mat4(1.0F);
                  mx.normalWorldToLocal = glm::mat4(1.0F);
                }

                view.each([&](ecs::Entity, const MeshRenderer& mr, const WorldTransform& wt)
                {
                    if (mr.meshID >= 0 && (size_t)mr.meshID < meshXforms.size())
                    {
                        meshXforms[mr.meshID].invModel = glm::inverse(wt.matrix);
                        meshXforms[mr.meshID].normalWorldToLocal = glm::transpose(wt.matrix);
                    }
                });

                const auto xformAlloc = m_frameManager.allocateUpload(meshXforms.size() * sizeof(gpu::MeshXform), 16);
                if (xformAlloc.mappedPtr != nullptr) {
                  std::memcpy(xformAlloc.mappedPtr, meshXforms.data(),
                              meshXforms.size() * sizeof(gpu::MeshXform));
                }
                ctx.skinningMeshXformAddr = xformAlloc.deviceAddress;
            }
            if (!frame.skinnedVertexBuffer.isValid() ||
                m_renderer->getBuffer(frame.skinnedVertexBuffer.handle())->size() < m_renderer->getBuffer(
                    m_model->vertexBuffer())->size())
            {
                auto* srcBuf = m_renderer->getBuffer(m_model->vertexBuffer());
                frame.skinnedVertexBuffer = m_renderer->createBuffer("SkinnedVertexBuffer", {
                                                                         .size = srcBuf->size(),
                                                                         .usage = rhi::BufferUsage::StorageBuffer |
                                                                         rhi::BufferUsage::VertexBuffer |
                                                                         rhi::BufferUsage::ShaderDeviceAddress,
                                                                         .memoryUsage = rhi::MemoryUsage::GPUOnly,
                                                                         .debugName = "SkinnedVertexBuffer"
                                                                     });
            }
        }

        updateMorphTargets(cmd);

        updateLightsAndShadows(ctx);

        m_sceneUniforms->updateCamera(camera, width, height, m_dt,
                                      m_settings.debugLightView,
                                      m_shadowPassPtr, ctx.cameraDataSlice,
                                      ctx.cameraDataAddr);
        m_sceneUniforms->updateEnvironmentBuffer(ctx.environmentSlice,
                                                 ctx.environmentAddr);

        ctx.sceneDataAddr = ctx.cameraDataAddr;

        {
            auto alloc = updateGlobalTransforms();
            ctx.transformSlice = {.offset = alloc.offset, .size = alloc.size};
            ctx.transformAddr = alloc.deviceAddress;
            ctx.instanceXformAddr = alloc.deviceAddress;
        }

        m_materialHeap.flushUpdates(m_renderer, cmd, m_frameManager);
        ctx.materialBufferAddr = m_materialHeap.getMaterialBufferAddress();


        if (m_settings.cullingMode == CullingMode::CPU)
        {
            PNKR_PROFILE_SCOPE("CPU_Culling");

            auto frustum = geometry::createFrustum(m_cullingViewProj);
            auto cullView = m_model->scene().registry().view<MeshRenderer, WorldBounds, Visibility>();

            uint32_t visibleCount = 0;
            cullView.each([&](ecs::Entity, const MeshRenderer&, const WorldBounds& wb, Visibility& vis)
            {
                bool visible = geometry::isBoxInFrustum(frustum, wb.aabb);
                vis.visible = visible ? 1 : 0;
                if (visible) {
                  visibleCount++;
                }

                if (m_settings.drawDebugBounds && debugLayer)
                {
                    glm::vec3 color = visible ? glm::vec3(0.0F, 1.0F, 0.0F) : glm::vec3(1.0F, 0.0F, 0.0F);
                    debugLayer->box(wb.aabb.m_min, wb.aabb.m_max, color);
                }
            });

            m_visibleMeshCount = visibleCount;
        }
        else if (m_settings.cullingMode == CullingMode::None)
        {
            auto visView = m_model->scene().registry().view<Visibility>();
            visView.each([](ecs::Entity, Visibility& vis)
            {
                vis.visible = 1;
            });
        }

        buildDrawLists(ctx, camera);

        auto uploadIndirect = [&](const DrawIndexedIndirectCommandGPU *commands,
                                  uint32_t count) -> TransientAllocation {
          const size_t size = 16 + (static_cast<size_t>(count) *
                                    sizeof(DrawIndexedIndirectCommandGPU));
          auto alloc = m_frameManager.allocateUpload(size, 256);

          if (alloc.mappedPtr) {
            std::memset(alloc.mappedPtr, 0, 16);

            std::memcpy(alloc.mappedPtr, &count, sizeof(uint32_t));

            if (count > 0 && commands) {
              const size_t cmdBytes = static_cast<size_t>(count) *
                                      sizeof(DrawIndexedIndirectCommandGPU);
              std::memcpy(reinterpret_cast<uint8_t *>(alloc.mappedPtr) + 16,
                          commands, cmdBytes);
            }
          }
          return alloc;
        };

        frame.indirectOpaqueAlloc = uploadIndirect(ctx.dodContext.indirectOpaque, ctx.dodContext.opaqueCount);
        frame.indirectOpaqueDoubleSidedAlloc = uploadIndirect(ctx.dodContext.indirectOpaqueDoubleSided,
                                                              ctx.dodContext.opaqueDoubleSidedCount);
        frame.indirectTransmissionAlloc = uploadIndirect(ctx.dodContext.indirectTransmission,
                                                         ctx.dodContext.transmissionCount);
        frame.indirectTransmissionDoubleSidedAlloc = uploadIndirect(ctx.dodContext.indirectTransmissionDoubleSided,
                                                                    ctx.dodContext.transmissionDoubleSidedCount);
        frame.indirectTransparentAlloc = uploadIndirect(ctx.dodContext.indirectTransparent,
                                                        ctx.dodContext.transparentCount);

        frame.indirectOpaqueBuffer = makeSlice(*m_renderer, frame.indirectOpaqueAlloc.buffer,
                                               frame.indirectOpaqueAlloc.offset, frame.indirectOpaqueAlloc.size, 16);
        frame.indirectOpaqueDoubleSidedBuffer = makeSlice(*m_renderer, frame.indirectOpaqueDoubleSidedAlloc.buffer,
                                                          frame.indirectOpaqueDoubleSidedAlloc.offset,
                                                          frame.indirectOpaqueDoubleSidedAlloc.size, 16);
        frame.indirectTransmissionBuffer = makeSlice(*m_renderer, frame.indirectTransmissionAlloc.buffer,
                                                     frame.indirectTransmissionAlloc.offset,
                                                     frame.indirectTransmissionAlloc.size, 16);
        frame.indirectTransmissionDoubleSidedBuffer = makeSlice(*m_renderer,
                                                                frame.indirectTransmissionDoubleSidedAlloc.buffer,
                                                                frame.indirectTransmissionDoubleSidedAlloc.offset,
                                                                frame.indirectTransmissionDoubleSidedAlloc.size, 16);
        frame.indirectTransparentBuffer = makeSlice(*m_renderer, frame.indirectTransparentAlloc.buffer,
                                                    frame.indirectTransparentAlloc.offset,
                                                    frame.indirectTransparentAlloc.size, 16);

        return ctx;
    }

    void IndirectRenderer::draw(rhi::RHICommandList* cmd,
                                const Camera& camera, uint32_t width, uint32_t height,
                                debug::DebugLayer* debugLayer,
                                std::function<void(rhi::RHICommandList*)> uiRender)
    {
        using namespace passes::utils;
        PNKR_PROFILE_FUNCTION();
        ScopedGpuMarker frameScope(cmd, "Frame");

        m_settings.shadow.biasConst = rShadowBias.get();

        (void)debugLayer;

        struct FrameCompleteGuard
        {
          RenderResourceManager &m_manager;
          ~FrameCompleteGuard() { m_manager.onFrameComplete(); }
        } frameGuard{m_resourceMgr};

        resize(width, height);

        uint32_t rhiFrameIndex = m_renderer->getFrameIndex();
        m_frameManager.beginFrame(rhiFrameIndex);

        {
            rhi::RHIMemoryBarrier barrier{};
            barrier.texture = m_renderer->getBackbuffer();
            barrier.oldLayout = rhi::ResourceLayout::Undefined;
            barrier.newLayout = rhi::ResourceLayout::ColorAttachment;
            barrier.srcAccessStage = rhi::ShaderStage::None;
            barrier.dstAccessStage = rhi::ShaderStage::RenderTarget;

            cmd->pipelineBarrier(rhi::ShaderStage::None, rhi::ShaderStage::RenderTarget, {barrier});
        }

        if (auto* profiler = m_renderer->device()->gpuProfiler())
        {
            if (auto* assets = m_renderer->assets())
            {
                profiler->updateStreamingStatistics(rhiFrameIndex, assets->getStreamingStatistics());
            }
        }

        IndirectDrawContext drawCtx = prepareFrame(cmd, camera, width, height, debugLayer);

        m_visibleMeshCount = drawCtx.dodContext.opaqueCount +
            drawCtx.dodContext.opaqueDoubleSidedCount +
            drawCtx.dodContext.transmissionCount +
            drawCtx.dodContext.transmissionDoubleSidedCount +
            drawCtx.dodContext.transparentCount;

        if (m_transmissionPassPtr != nullptr) {
          m_resources.transmissionTexture =
              m_transmissionPassPtr->getTextureHandle();
        }

        RenderPassContext passCtx = {
            .cmd = cmd,
            .model = m_model.get(),
            .camera = &camera,
            .frameBuffers = m_frameManager.getCurrentFrameBuffers(),
            .frameManager = m_frameManager,
            .resources = m_resources,
            .settings = m_settings,
            .viewportWidth = width,
            .viewportHeight = height,
            .frameIndex = m_frameManager.getCurrentFrameIndex(),
            .msaaSamples = m_resources.effectiveMsaaSamples,
            .dt = m_dt,
            .uiRender = std::move(uiRender),

            .cameraDataAddr = drawCtx.cameraDataAddr,
            .sceneDataAddr = drawCtx.sceneDataAddr,
            .transformAddr = drawCtx.transformAddr,
            .lightAddr = drawCtx.lightAddr,
            .lightCount = drawCtx.lightCount,
            .materialAddr = m_materialHeap.getMaterialBufferAddress(),
            .environmentAddr = drawCtx.environmentAddr,
            .shadowDataAddr = drawCtx.shadowDataAddr,
            .instanceXformAddr = drawCtx.instanceXformAddr,
            .dodContext = drawCtx.dodContext};

        passCtx.cullingViewProj = m_cullingViewProj;

        passCtx.resources.drawLists = &drawCtx.dodContext;

        if (m_settings.cullingMode == CullingMode::GPU)
        {
            m_cullingPassPtr->prepare(passCtx);
        }

        m_pipeline->setup(*m_frameGraph, drawCtx, passCtx);
    }
}
