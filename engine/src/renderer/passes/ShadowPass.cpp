#include "pnkr/renderer/passes/ShadowPass.hpp"
#include "pnkr/renderer/passes/RenderPassUtils.hpp"
#include "pnkr/core/common.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstring>
#include <cmath>

#include "pnkr/rhi/BindlessManager.hpp"

namespace pnkr::renderer
{
    namespace
    {
    glm::vec3 computeDirectionalLightDir(float thetaDeg, float phiDeg) {
      float theta = glm::radians(thetaDeg);
      float phi = glm::radians(phiDeg);
      return glm::normalize(glm::vec3(std::cos(phi) * std::sin(theta),
                                      std::sin(phi),
                                      std::cos(phi) * std::cos(theta)));
    }
    }

    void ShadowPass::init(RHIRenderer *renderer, uint32_t ,
                          uint32_t ) {
      m_renderer = renderer;

      rhi::TextureDescriptor shadowDesc{};
      shadowDesc.extent = {
          .width = m_shadowDim, .height = m_shadowDim, .depth = 1};
      shadowDesc.format = rhi::Format::D32_SFLOAT;
      shadowDesc.usage = rhi::TextureUsage::DepthStencilAttachment |
                         rhi::TextureUsage::Sampled;
      shadowDesc.debugName = "ShadowMap";
      m_shadowMap = m_renderer->createTexture("ShadowMap", shadowDesc);
      m_shadowLayout = rhi::ResourceLayout::Undefined;

      if (m_renderer->isBindlessEnabled()) {
        auto *shadowTex = m_renderer->getTexture(m_shadowMap);
        m_shadowMapBindlessIndex =
            m_renderer->device()->getBindlessManager()->registerShadowTexture2D(
                shadowTex);
      }

      auto shadowVert = rhi::Shader::load(rhi::ShaderStage::Vertex,
                                          "shaders/shadow.vert.spv");

      if (!shadowVert) {
        core::Logger::Render.error("ShadowPass: Failed to load shader.");
        return;
      }
      core::Logger::Render.info("ShadowPass: Shader loaded successfully.");

      rhi::RHIPipelineBuilder shadowBuilder;
      shadowBuilder.setShaders(shadowVert.get(), nullptr)
          .setTopology(rhi::PrimitiveTopology::TriangleList)
          .setPolygonMode(rhi::PolygonMode::Fill)
          .setDepthBiasEnable(true)
          .enableDepthTest(true, rhi::CompareOp::LessOrEqual)
          .setDynamicStates({rhi::DynamicState::Viewport,
                             rhi::DynamicState::Scissor,
                             rhi::DynamicState::DepthBias})
          .setDepthFormat(rhi::Format::D32_SFLOAT)
          .setCullMode(rhi::CullMode::None)
          .setColorFormats({})
          .setName("ShadowPass");

      m_shadowPipeline =
          m_renderer->createGraphicsPipeline(shadowBuilder.buildGraphics());
      if (m_shadowPipeline.isValid()) {
          core::Logger::Render.info("ShadowPass: Main pipeline created successfully.");
      }

      shadowBuilder.setCullMode(rhi::CullMode::None)
          .setName("ShadowPassDoubleSided");
      m_shadowPipelineDoubleSided =
          m_renderer->createGraphicsPipeline(shadowBuilder.buildGraphics());
      if (m_shadowPipelineDoubleSided.isValid()) {
          core::Logger::Render.info("ShadowPass: Double-sided pipeline created successfully.");
      }

      uint32_t flightCount = m_renderer->getSwapchain()->framesInFlight();
      m_shadowDrawBuffer =
          std::make_unique<IndirectDrawBuffer>(m_renderer, 50000, flightCount);
    }

    void ShadowPass::resize(uint32_t , uint32_t ,
                            const MSAASettings & ) {}

    void ShadowPass::execute(const RenderPassContext& ctx)
    {
        PNKR_PROFILE_SCOPE("Record Shadow Pass");
        using namespace passes::utils;

        ScopedPassMarkers passScope(ctx.cmd, "Shadow Pass", 0.3F, 0.3F, 0.3F,
                                    1.0F);

        core::Logger::Render.debug("ShadowPass: execute - enabled={}, pipeline={}, casterIdx={}, drawLists={}",
            ctx.settings.shadow.enabled,
            m_shadowPipeline.isValid(),
            ctx.resources.shadowCasterIndex,
            ctx.resources.drawLists != nullptr);

        ctx.resources.shadowMap = m_shadowMap;
        ctx.resources.shadowMapBindlessIndex = util::u32(m_shadowMapBindlessIndex);

        gpu::ShadowDataGPU gpuShadowData{};
        std::memset(&gpuShadowData, 0, sizeof(gpuShadowData));

        if (ctx.settings.shadow.enabled && m_shadowPipeline != INVALID_PIPELINE_HANDLE &&
            ctx.resources.shadowCasterIndex != -1)
        {
          glm::vec3 lightDir = glm::vec3(0.0F, -1.0F, 0.0F);
          auto lightPos = glm::vec3(0.0F);
          scene::LightType lightType = scene::LightType::Directional;

          const auto &scene = ctx.model->scene();
          auto lightView =
              scene.registry().view<scene::LightSource, scene::WorldTransform>();
          int currentLightIdx = 0;
          lightView.each([&](ecs::Entity, scene::LightSource &ls,
                             scene::WorldTransform &world) {
            if (currentLightIdx == ctx.resources.shadowCasterIndex) {
              const glm::mat4 &worldM = world.matrix;
              lightPos = glm::vec3(worldM[3]);
              lightType = ls.type;
              glm::vec3 baseDir = ls.direction;
              if (glm::length(baseDir) < 0.0001F) {
                baseDir = glm::vec3(0.0F, -1.0F, 0.0F);
              }
              lightDir =
                  glm::normalize(glm::vec3(worldM * glm::vec4(baseDir, 0.0F)));
            }
            currentLightIdx++;
          });

          glm::mat4 lightViewMat(1.0f);
          glm::mat4 lightProjMat(1.0f);
          if (lightType == scene::LightType::Directional) {
            if (!ctx.settings.shadow.useSceneLightDirection) {
              lightDir = computeDirectionalLightDir(
                  ctx.settings.shadow.thetaDeg, ctx.settings.shadow.phiDeg);
            }

            scene::BoundingBox sceneAABB{};

            auto view = scene.registry().view<scene::WorldBounds, scene::Visibility, scene::MeshRenderer>();

            view.each([&](ecs::Entity, const scene::WorldBounds& wb, const scene::Visibility& vis, const scene::MeshRenderer& mr) {
                if (vis.visible && mr.meshID >= 0) {
                    sceneAABB.combine(wb.aabb);
                }
            });

            if (!sceneAABB.isValid()) {

                sceneAABB.m_min = glm::vec3(-10.0f);
                sceneAABB.m_max = glm::vec3(10.0f);
            }

            glm::vec3 sceneCenter = (sceneAABB.m_min + sceneAABB.m_max) * 0.5f;
            float sceneRadius = glm::length(sceneAABB.m_max - sceneAABB.m_min) * 0.5f;

            float cameraDistance = sceneRadius + 10.0f;
            glm::vec3 cameraPos = sceneCenter - lightDir * cameraDistance;

            core::Logger::Render.debug("ShadowPass: Scene AABB min({:.2f}, {:.2f}, {:.2f}) max({:.2f}, {:.2f}, {:.2f}) Radius: {:.2f}",
                sceneAABB.m_min.x, sceneAABB.m_min.y, sceneAABB.m_min.z,
                sceneAABB.m_max.x, sceneAABB.m_max.y, sceneAABB.m_max.z, sceneRadius);

            glm::vec3 up = glm::vec3(0, 1, 0);
            if (std::abs(glm::dot(lightDir, up)) > 0.99f) {
                up = glm::vec3(0, 0, 1);
            }
            lightViewMat = glm::lookAt(cameraPos, sceneCenter, up);

            scene::BoundingBox sceneAABB_LS{};
            glm::vec3 corners[8] = {
                {sceneAABB.m_min.x, sceneAABB.m_min.y, sceneAABB.m_min.z},
                {sceneAABB.m_max.x, sceneAABB.m_min.y, sceneAABB.m_min.z},
                {sceneAABB.m_min.x, sceneAABB.m_max.y, sceneAABB.m_min.z},
                {sceneAABB.m_max.x, sceneAABB.m_max.y, sceneAABB.m_min.z},
                {sceneAABB.m_min.x, sceneAABB.m_min.y, sceneAABB.m_max.z},
                {sceneAABB.m_max.x, sceneAABB.m_min.y, sceneAABB.m_max.z},
                {sceneAABB.m_min.x, sceneAABB.m_max.y, sceneAABB.m_max.z},
                {sceneAABB.m_max.x, sceneAABB.m_max.y, sceneAABB.m_max.z}
            };

            for (int i = 0; i < 8; ++i) {
                sceneAABB_LS.combine(glm::vec3(lightViewMat * glm::vec4(corners[i], 1.0f)));
            }

            float minX = sceneAABB_LS.m_min.x;
            float maxX = sceneAABB_LS.m_max.x;
            float minY = sceneAABB_LS.m_min.y;
            float maxY = sceneAABB_LS.m_max.y;

            float worldUnitsPerTexel = std::max(maxX - minX, maxY - minY) / static_cast<float>(m_shadowDim);

            minX = std::floor(minX / worldUnitsPerTexel) * worldUnitsPerTexel;
            maxX = std::floor(maxX / worldUnitsPerTexel) * worldUnitsPerTexel;
            minY = std::floor(minY / worldUnitsPerTexel) * worldUnitsPerTexel;
            maxY = std::floor(maxY / worldUnitsPerTexel) * worldUnitsPerTexel;

            float zPadding = ctx.settings.shadow.extraZPadding;

            core::Logger::Render.debug("ShadowPass: LS Bounds X[{:.2f}, {:.2f}] Y[{:.2f}, {:.2f}] Z[{:.2f}, {:.2f}] TexelSize: {:.4f}",
                minX, maxX, minY, maxY, -sceneAABB_LS.m_max.z - zPadding, -sceneAABB_LS.m_min.z + zPadding, worldUnitsPerTexel);

            lightProjMat = glm::orthoRH_ZO(minX, maxX, minY, maxY, -sceneAABB_LS.m_max.z - zPadding, -sceneAABB_LS.m_min.z + zPadding);

            lightProjMat[1][1] *= -1.0f;

          } else {
            const float fov = (ctx.settings.shadow.fov > 0.01F)
                                  ? ctx.settings.shadow.fov
                                  : 45.0F;
            lightViewMat =
                glm::lookAt(lightPos, lightPos + lightDir, glm::vec3(0, 1, 0));
            lightProjMat = glm::perspective(glm::radians(fov), 1.0F,
                                            ctx.settings.shadow.nearPlane,
                                            ctx.settings.shadow.farPlane);
          }

            m_lastLightView = lightViewMat;
            m_lastLightProj = lightProjMat;

            const glm::mat4 lightViewProjRaw = lightProjMat * lightViewMat;
            const glm::mat4 scaleBias(0.5F, 0.0F, 0.0F, 0.0F, 0.0F, 0.5F, 0.0F,
                                      0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.5F, 0.5F,
                                      0.0F, 1.0F);

            gpuShadowData.lightViewProjRaw = lightViewProjRaw;
            gpuShadowData.lightViewProjBiased = scaleBias * lightViewProjRaw;
            gpuShadowData.shadowMapTexture = util::u32(m_shadowMapBindlessIndex);

            gpuShadowData.shadowMapTexelSize =
                glm::vec2(1.0F / static_cast<float>(m_shadowDim));
            gpuShadowData.shadowBias = ctx.settings.shadow.biasConst * 0.0001F;

            auto* shadowTex = m_renderer->getTexture(m_shadowMap);

            RenderingInfoBuilder builder;
            builder.setRenderArea(m_shadowDim, m_shadowDim)
                .setDepthAttachment(
                    shadowTex,
                    rhi::LoadOp::Clear, rhi::StoreOp::Store);

            ctx.cmd->beginRendering(builder.get());
            setFullViewport(ctx.cmd, m_shadowDim, m_shadowDim);
            ctx.cmd->setDepthBias(ctx.settings.shadow.biasConst, 0.0F,
                                  ctx.settings.shadow.biasSlope);

            gpu::IndirectPushConstants shadowPC {};
            shadowPC.instances = (ctx.frameBuffers.shadowTransformBuffer != INVALID_BUFFER_HANDLE)
                                         ? m_renderer->getBuffer(ctx.frameBuffers.shadowTransformBuffer)->
                                                       getDeviceAddress()
                                         : ctx.instanceXformAddr;
            shadowPC.vertices = m_renderer->getBuffer(ctx.model->vertexBuffer())->getDeviceAddress();

            auto alloc = ctx.frameManager.allocateUpload(sizeof(gpu::ShadowDataGPU), 16);
            if (alloc.mappedPtr != nullptr) {
              std::memcpy(alloc.mappedPtr, &gpuShadowData,
                          sizeof(gpu::ShadowDataGPU));
            }
            shadowPC.shadowData = alloc.deviceAddress;

            ctx.cmd->bindIndexBuffer(m_renderer->getBuffer(ctx.model->indexBuffer()), 0, false);

            if (ctx.resources.drawLists != nullptr) {
              const auto *dodLists =
                  static_cast<const scene::GLTFUnifiedDODContext *>(
                      ctx.resources.drawLists);

              core::Logger::Render.debug("ShadowPass: DrawLists - opaque={}, doubleSided={}",
                  dodLists->opaqueCount, dodLists->opaqueDoubleSidedCount);

              if (dodLists->opaqueCount > 0) {
                ctx.cmd->bindPipeline(
                    m_renderer->getPipeline(m_shadowPipeline));
                ctx.cmd->pushConstants(rhi::ShaderStage::Vertex, shadowPC);
                ctx.cmd->drawIndexedIndirect(
                    m_renderer->getBuffer(
                        ctx.frameBuffers.indirectOpaqueBuffer.buffer),
                    ctx.frameBuffers.indirectOpaqueBuffer.offset + 16,
                    dodLists->opaqueCount,
                    sizeof(gpu::DrawIndexedIndirectCommandGPU));
              }

              if (dodLists->opaqueDoubleSidedCount > 0) {
                ctx.cmd->bindPipeline(
                    m_renderer->getPipeline(m_shadowPipelineDoubleSided));
                ctx.cmd->pushConstants(rhi::ShaderStage::Vertex, shadowPC);
                ctx.cmd->drawIndexedIndirect(
                    m_renderer->getBuffer(
                        ctx.frameBuffers.indirectOpaqueDoubleSidedBuffer
                            .buffer),
                    ctx.frameBuffers.indirectOpaqueDoubleSidedBuffer.offset +
                        16,
                    dodLists->opaqueDoubleSidedCount,
                    sizeof(gpu::DrawIndexedIndirectCommandGPU));
              }
            }

            ctx.cmd->endRendering();
        }

        if (ctx.shadowDataAddr != 0 &&
            (ctx.frameBuffers.mappedShadowData != nullptr) &&
            ctx.lightCount > 0 &&
            ctx.resources.shadowCasterIndex >= 0 &&
            util::u32(ctx.resources.shadowCasterIndex) < ctx.lightCount) {

          auto *shadowDataArray = static_cast<gpu::ShadowDataGPU *>(
              ctx.frameBuffers.mappedShadowData);

          shadowDataArray[ctx.resources.shadowCasterIndex].lightViewProjRaw = gpuShadowData.lightViewProjRaw;
          shadowDataArray[ctx.resources.shadowCasterIndex].lightViewProjBiased = gpuShadowData.lightViewProjBiased;
          shadowDataArray[ctx.resources.shadowCasterIndex].shadowBias = gpuShadowData.shadowBias;

        }
    }
}
