#include "pnkr/renderer/passes/ShadowPass.hpp"
#include "pnkr/renderer/passes/RenderPassUtils.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/renderer/ShaderHotReloader.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtc/epsilon.hpp>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <array>

#include "pnkr/rhi/BindlessManager.hpp"
#include "pnkr/renderer/geometry/Frustum.hpp"

namespace pnkr::renderer
{
    namespace
    {
        glm::vec3 computeDirectionalLightDir(float thetaDeg, float phiDeg)
        {
            float theta = glm::radians(thetaDeg);
            float phi = glm::radians(phiDeg);
            return glm::normalize(glm::vec3(std::cos(phi) * std::sin(theta),
                                            std::sin(phi),
                                            std::cos(phi) * std::cos(theta)));
        }
    }

    void ShadowPass::init(RHIRenderer* renderer, uint32_t,
                          uint32_t, ShaderHotReloader* hotReloader)
    {
        m_renderer = renderer;
        m_hotReloader = hotReloader;

        rhi::TextureDescriptor shadowDesc{};
        shadowDesc.extent = {
            .width = m_shadowDim, .height = m_shadowDim, .depth = 1
        };
        shadowDesc.format = rhi::Format::D32_SFLOAT;
        shadowDesc.usage = rhi::TextureUsage::DepthStencilAttachment |
            rhi::TextureUsage::Sampled;
        shadowDesc.debugName = "ShadowMap";
        m_shadowMap = m_renderer->createTexture("ShadowMap", shadowDesc);
        m_shadowLayout = rhi::ResourceLayout::Undefined;

        if (m_renderer->isBindlessEnabled())
        {
            auto* shadowTex = m_renderer->getTexture(m_shadowMap);
            m_shadowMapBindlessIndex =
                m_renderer->device()->getBindlessManager()->registerShadowTexture2D(
                    shadowTex);
        }

        auto shadowVert = rhi::Shader::load(rhi::ShaderStage::Vertex,
                                            "shaders/shadow.vert.spv");

        if (!shadowVert)
        {
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
                     .setDynamicStates({
                         rhi::DynamicState::Viewport,
                         rhi::DynamicState::Scissor,
                         rhi::DynamicState::DepthBias
                     })
                     .setDepthFormat(rhi::Format::D32_SFLOAT)
                     .setCullMode(rhi::CullMode::None)
                     .setColorFormats({})
                     .setName("ShadowPass");

        auto pipelineDesc = shadowBuilder.buildGraphics();
        if (m_hotReloader != nullptr)
        {
            std::array sources = {
                ShaderSourceInfo{
                    .path = "/shaders/renderer/indirect/shadow.slang",
                    .entryPoint = "vertexMain",
                    .stage = rhi::ShaderStage::Vertex,
                    .dependencies = {}
                }
            };
            m_shadowPipeline = m_hotReloader->createGraphicsPipeline(pipelineDesc, sources);
        }
        else
        {
            m_shadowPipeline = m_renderer->createGraphicsPipeline(pipelineDesc);
        }
        if (m_shadowPipeline.isValid())
        {
            core::Logger::Render.info("ShadowPass: Main pipeline created successfully.");
        }

        shadowBuilder.setCullMode(rhi::CullMode::None)
                     .setName("ShadowPassDoubleSided");
        auto shadowDoubleDesc = shadowBuilder.buildGraphics();
        if (m_hotReloader != nullptr)
        {
            std::array sources = {
                ShaderSourceInfo{
                    .path = "/shaders/renderer/indirect/shadow.slang",
                    .entryPoint = "vertexMain",
                    .stage = rhi::ShaderStage::Vertex,
                    .dependencies = {}
                }
            };
            m_shadowPipelineDoubleSided =
                m_hotReloader->createGraphicsPipeline(shadowDoubleDesc, sources);
        }
        else
        {
            m_shadowPipelineDoubleSided =
                m_renderer->createGraphicsPipeline(shadowDoubleDesc);
        }
        if (m_shadowPipelineDoubleSided.isValid())
        {
            core::Logger::Render.info("ShadowPass: Double-sided pipeline created successfully.");
        }

        uint32_t flightCount = m_renderer->getSwapchain()->framesInFlight();
        m_shadowDrawBuffer =
            std::make_unique<IndirectDrawBuffer>(m_renderer, 50000, flightCount);
    }

    void ShadowPass::resize(uint32_t, uint32_t,
                            const MSAASettings&)
    {
    }

    void ShadowPass::execute(const RenderPassContext& ctx)
    {
        PNKR_PROFILE_SCOPE("Record Shadow Pass");
        using namespace passes::utils;

        ScopedPassMarkers passScope(ctx.cmd, "Shadow Pass", 0.3F, 0.3F, 0.3F, 1.0F);

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

            const auto& scene = ctx.model->scene();
            auto lightView = scene.registry().view<scene::LightSource, scene::WorldTransform>();
            int currentLightIdx = 0;
            lightView.each([&](ecs::Entity, scene::LightSource& ls, scene::WorldTransform& world)
            {
                if (currentLightIdx == ctx.resources.shadowCasterIndex)
                {
                    const glm::mat4& worldM = world.matrix;
                    lightPos = glm::vec3(worldM[3]);
                    lightType = ls.type;
                    glm::vec3 baseDir = ls.direction;
                    if (glm::length(baseDir) < 0.0001F)
                    {
                        baseDir = glm::vec3(0.0F, -1.0F, 0.0F);
                    }
                    lightDir = glm::normalize(glm::vec3(worldM * glm::vec4(baseDir, 0.0F)));
                }
                currentLightIdx++;
            });

            glm::mat4 lightViewMat(1.0f);
            glm::mat4 lightProjMat(1.0f);

            if (lightType == scene::LightType::Directional)
            {
                if (!ctx.settings.shadow.useSceneLightDirection)
                {
                    lightDir = computeDirectionalLightDir(
                        ctx.settings.shadow.thetaDeg, ctx.settings.shadow.phiDeg);
                }

                // ============================================================
                // ROBUST DIRECTIONAL LIGHT SHADOW FRUSTUM CALCULATION
                // ============================================================

                const auto& shadowSettings = ctx.settings.shadow;

                if (shadowSettings.useManualFrustum)
                {
                    // Manual mode: use user-specified values
                    glm::vec3 center = shadowSettings.manualCenter;
                    float orthoSize = shadowSettings.manualOrthoSize;
                    float nearZ = shadowSettings.manualNear;
                    float farZ = shadowSettings.manualFar;

                    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
                    if (std::abs(glm::dot(lightDir, up)) > 0.99f)
                    {
                        up = glm::vec3(1.0f, 0.0f, 0.0f);
                    }
                    lightViewMat = glm::lookAt(center - lightDir * (farZ * 0.5f), center, up);

                    float xyPadding = shadowSettings.extraXYPadding;
                    float zPadding = shadowSettings.extraZPadding;

                    lightProjMat = glm::orthoRH_ZO(
                        -orthoSize - xyPadding, orthoSize + xyPadding,
                        -orthoSize - xyPadding, orthoSize + xyPadding,
                        nearZ - zPadding, farZ + zPadding
                    );

                    core::Logger::Render.debug(
                        "ShadowPass [MANUAL]: center({:.2f}, {:.2f}, {:.2f}) "
                        "orthoSize={:.2f} near={:.2f} far={:.2f}",
                        center.x, center.y, center.z, orthoSize, nearZ, farZ
                    );
                }
                else
                {
                    // Auto mode: Camera Frustum-Based Tight Shadow Fitting
                    // Based on: https://docs.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps

                    // ============================================================
                    // Auto mode: Whole-scene fitting (cookbook method)
                    // ============================================================
                    const auto& shadowSettings = ctx.settings.shadow;

                    // Step 1: Compute world-space AABB of ALL shadow casters (cookbook style)
                    // Transform each mesh's local bounds by its world transform, then combine
                    scene::BoundingBox sceneAABB;
                    const auto& meshBounds = ctx.model->meshBounds();
                    
                    auto meshView = scene.registry().view<scene::MeshRenderer, scene::WorldTransform>();
                    meshView.each([&](ecs::Entity, const scene::MeshRenderer& mr, const scene::WorldTransform& wt)
                    {
                        if (mr.meshID >= 0 && static_cast<size_t>(mr.meshID) < meshBounds.size())
                        {
                            const auto& localBounds = meshBounds[mr.meshID];
                            if (localBounds.isValid())
                            {
                                // Transform the 8 corners of local bounds to world space
                                scene::BoundingBox worldBounds = scene::transformAabbFast(localBounds, wt.matrix);
                                sceneAABB.combine(worldBounds);
                            }
                        }
                    });

                    if (!sceneAABB.isValid())
                    {
                        sceneAABB.m_min = glm::vec3(-10.0f);
                        sceneAABB.m_max = glm::vec3(10.0f);
                    }

                    // Step 2: Build light view matrix
                    // Position light BEHIND the scene so all geometry is in front (negative Z)
                    // The cookbook assumes scene at origin, but Bistro is offset
                    glm::vec3 sceneCenter = (sceneAABB.m_min + sceneAABB.m_max) * 0.5f;
                    glm::vec3 sceneExtent = sceneAABB.m_max - sceneAABB.m_min;
                    float sceneDiagonalRadius = glm::length(sceneExtent) * 0.5f;

                    glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);
                    if (std::abs(glm::dot(lightDir, up)) > 0.99f)
                    {
                        up = glm::vec3(0.0f, 1.0f, 0.0f);
                    }

                    // Position light behind scene center, looking toward center
                    glm::vec3 lightEye = sceneCenter - lightDir * sceneDiagonalRadius;
                    lightViewMat = glm::lookAt(lightEye, sceneCenter, up);

                    // Step 3: Transform scene AABB to light space
                    scene::BoundingBox sceneLS;
                    std::array corners = {
                        glm::vec3(sceneAABB.m_min.x, sceneAABB.m_min.y, sceneAABB.m_min.z),
                        glm::vec3(sceneAABB.m_max.x, sceneAABB.m_min.y, sceneAABB.m_min.z),
                        glm::vec3(sceneAABB.m_min.x, sceneAABB.m_max.y, sceneAABB.m_min.z),
                        glm::vec3(sceneAABB.m_max.x, sceneAABB.m_max.y, sceneAABB.m_min.z),
                        glm::vec3(sceneAABB.m_min.x, sceneAABB.m_min.y, sceneAABB.m_max.z),
                        glm::vec3(sceneAABB.m_max.x, sceneAABB.m_min.y, sceneAABB.m_max.z),
                        glm::vec3(sceneAABB.m_min.x, sceneAABB.m_max.y, sceneAABB.m_max.z),
                        glm::vec3(sceneAABB.m_max.x, sceneAABB.m_max.y, sceneAABB.m_max.z)
                    };

                    for (const auto& c : corners)
                    {
                        sceneLS.combine(glm::vec3(lightViewMat * glm::vec4(c, 1.0f)));
                    }

                    // Step 4: Stabilization - snap the light-space AABB center to texel grid
                    glm::vec2 centerLS(
                        (sceneLS.m_min.x + sceneLS.m_max.x) * 0.5f,
                        (sceneLS.m_min.y + sceneLS.m_max.y) * 0.5f
                    );

                    glm::vec2 extentLS(
                        sceneLS.m_max.x - sceneLS.m_min.x,
                        sceneLS.m_max.y - sceneLS.m_min.y
                    );

                    // Compute texel size based on ACTUAL scene extent
                    float maxExtent = std::max(extentLS.x, extentLS.y);
                    float worldUnitsPerTexel = maxExtent / static_cast<float>(m_shadowDim);

                    // Snap to texel grid (with conservative filter footprint)
                    const int32_t MaxDownsampleFactor = 4;
                    const float snapIncrement = worldUnitsPerTexel * MaxDownsampleFactor;

                    glm::vec2 snappedCenter;
                    snappedCenter.x = std::floor(centerLS.x / snapIncrement) * snapIncrement;
                    snappedCenter.y = std::floor(centerLS.y / snapIncrement) * snapIncrement;

                    // Rebuild AABB from snapped center
                    float halfExtent = maxExtent * 0.5f;
                    glm::vec2 minXY = snappedCenter - glm::vec2(halfExtent);
                    glm::vec2 maxXY = snappedCenter + glm::vec2(halfExtent);

                    // Step 5: Build orthographic projection (cookbook style)
                    // Use orthoLH_ZO with SWAPPED Z (max.z, min.z) - this is the Vulkan cookbook trick
                    // The swap flips the depth direction to match Vulkan's expectations
                    lightProjMat = glm::orthoLH_ZO(
                        minXY.x - shadowSettings.extraXYPadding,
                        maxXY.x + shadowSettings.extraXYPadding,
                        minXY.y - shadowSettings.extraXYPadding,
                        maxXY.y + shadowSettings.extraXYPadding,
                        sceneLS.m_max.z + shadowSettings.extraZPadding,  // near = max.z (closest)
                        sceneLS.m_min.z - shadowSettings.extraZPadding   // far = min.z (farthest)
                    );

                    core::Logger::Render.debug(
                        "ShadowPass [COOKBOOK]: Extent:{:.2f}x{:.2f} Texel:{:.4f} Snap:{:.4f} "
                        "Z_LS:[{:.2f},{:.2f}] SceneWS:[({:.1f},{:.1f},{:.1f})-({:.1f},{:.1f},{:.1f})] Draws:{}",
                        extentLS.x, extentLS.y, worldUnitsPerTexel, snapIncrement,
                        sceneLS.m_max.z, sceneLS.m_min.z,  // max.z = near, min.z = far (swapped)
                        sceneAABB.m_min.x, sceneAABB.m_min.y, sceneAABB.m_min.z,
                        sceneAABB.m_max.x, sceneAABB.m_max.y, sceneAABB.m_max.z,
                        ctx.shadowDodContext.opaqueCount + ctx.shadowDodContext.opaqueDoubleSidedCount
                    );

                    // Verification
                    glm::vec3 testPt = glm::vec3(snappedCenter.x, snappedCenter.y, sceneLS.m_max.z - 0.1f);
                    glm::vec4 clip = lightProjMat * glm::vec4(testPt, 1.0f);
                    core::Logger::Render.debug("Shadow test clipZ (at Z={:.3f}) = {:.3f} / {:.3f}",
                                               testPt.z, clip.z / clip.w, clip.w);
                }
            }
            else
            {
                // Spot/Point light (your existing code)
                const float fov = (ctx.settings.shadow.fov > 0.01F) ? ctx.settings.shadow.fov : 45.0F;
                lightViewMat = glm::lookAt(lightPos, lightPos + lightDir, glm::vec3(0, 1, 0));
                lightProjMat = glm::perspective(
                    glm::radians(fov), 1.0F,
                    ctx.settings.shadow.nearPlane,
                    ctx.settings.shadow.farPlane
                );
            }

            m_lastLightView = lightViewMat;
            m_lastLightProj = lightProjMat;

            const glm::mat4 lightViewProjRaw = lightProjMat * lightViewMat;
            const glm::mat4 scaleBias(
                0.5F, 0.0F, 0.0F, 0.0F,
                0.0F, 0.5F, 0.0F, 0.0F,
                0.0F, 0.0F, 1.0F, 0.0F,
                0.5F, 0.5F, 0.0F, 1.0F
            );

            gpuShadowData.lightViewProjRaw = lightViewProjRaw;
            gpuShadowData.lightViewProjBiased = scaleBias * lightViewProjRaw;
            gpuShadowData.shadowMapTexture = util::u32(m_shadowMapBindlessIndex);
            gpuShadowData.shadowMapTexelSize = glm::vec2(1.0F / static_cast<float>(m_shadowDim));
            gpuShadowData.shadowBias = ctx.settings.shadow.biasConst * 0.0001F;

            // Rest of your rendering code...
            auto* shadowTex = m_renderer->getTexture(m_shadowMap);

            RenderingInfoBuilder builder;
            builder.setRenderArea(m_shadowDim, m_shadowDim)
                   .setDepthAttachment(shadowTex, rhi::LoadOp::Clear, rhi::StoreOp::Store);

            ctx.cmd->beginRendering(builder.get());
            setFullViewport(ctx.cmd, m_shadowDim, m_shadowDim);

            // Scale bias by world units per texel (or 1.0 if not directional)
            float biasScale = 1.0f;
            if (lightType == scene::LightType::Directional && !ctx.settings.shadow.useManualFrustum) {
                // Estimate bias scale using projection width

                float orthoWidth = 1.0f / lightProjMat[0][0] * 2.0f; 
                biasScale = orthoWidth / float(m_shadowDim);
            }
            
            float constBias = ctx.settings.shadow.biasConst * biasScale;
            ctx.cmd->setDepthBias(constBias, 0.0F, ctx.settings.shadow.biasSlope);

            gpu::IndirectPushConstants shadowPC{};
            shadowPC.instances = (ctx.frameBuffers.shadowTransformBuffer != INVALID_BUFFER_HANDLE)
                                     ? m_renderer->getBuffer(ctx.frameBuffers.shadowTransformBuffer)->getDeviceAddress()
                                     : ctx.instanceXformAddr;
            shadowPC.vertices = m_renderer->getBuffer(ctx.model->vertexBuffer())->getDeviceAddress();

            auto alloc = ctx.frameManager.allocateUpload(sizeof(gpu::ShadowDataGPU), 16);
            if (alloc.mappedPtr != nullptr)
            {
                std::memcpy(alloc.mappedPtr, &gpuShadowData, sizeof(gpu::ShadowDataGPU));
            }
            shadowPC.shadowData = alloc.deviceAddress;

            ctx.cmd->bindIndexBuffer(m_renderer->getBuffer(ctx.model->indexBuffer()), 0, false);

            if (ctx.resources.drawLists != nullptr)
            {
                const auto* dodLists = &ctx.shadowDodContext;

                if (dodLists->opaqueCount > 0)
                {
                    ctx.cmd->bindPipeline(m_renderer->getPipeline(m_shadowPipeline));
                    ctx.cmd->pushConstants(rhi::ShaderStage::Vertex, shadowPC);

                    auto indirectBuf = (ctx.resources.shadowIndirectOpaqueBuffer.buffer.isValid())
                                           ? ctx.resources.shadowIndirectOpaqueBuffer
                                           : ctx.frameBuffers.indirectOpaqueBuffer;

                    ctx.cmd->drawIndexedIndirect(
                        m_renderer->getBuffer(indirectBuf.buffer),
                        indirectBuf.offset + 16,
                        dodLists->opaqueCount,
                        sizeof(gpu::DrawIndexedIndirectCommandGPU));
                }

                if (dodLists->opaqueDoubleSidedCount > 0)
                {
                    ctx.cmd->bindPipeline(m_renderer->getPipeline(m_shadowPipelineDoubleSided));
                    ctx.cmd->pushConstants(rhi::ShaderStage::Vertex, shadowPC);

                    auto indirectBuf = (ctx.resources.shadowIndirectOpaqueDoubleSidedBuffer.buffer.isValid())
                                           ? ctx.resources.shadowIndirectOpaqueDoubleSidedBuffer
                                           : ctx.frameBuffers.indirectOpaqueDoubleSidedBuffer;

                    ctx.cmd->drawIndexedIndirect(
                        m_renderer->getBuffer(indirectBuf.buffer),
                        indirectBuf.offset + 16,
                        dodLists->opaqueDoubleSidedCount,
                        sizeof(gpu::DrawIndexedIndirectCommandGPU));
                }
            }

            ctx.cmd->endRendering();

        }

        if (ctx.shadowDataAddr != 0 && (ctx.frameBuffers.mappedShadowData != nullptr) &&
            ctx.lightCount > 0 && ctx.resources.shadowCasterIndex >= 0 &&
            util::u32(ctx.resources.shadowCasterIndex) < ctx.lightCount)
        {
            auto* shadowDataArray = static_cast<gpu::ShadowDataGPU*>(ctx.frameBuffers.mappedShadowData);
            shadowDataArray[ctx.resources.shadowCasterIndex].lightViewProjRaw = gpuShadowData.lightViewProjRaw;
            shadowDataArray[ctx.resources.shadowCasterIndex].lightViewProjBiased = gpuShadowData.lightViewProjBiased;
            shadowDataArray[ctx.resources.shadowCasterIndex].shadowBias = gpuShadowData.shadowBias;
        }
    }
}
