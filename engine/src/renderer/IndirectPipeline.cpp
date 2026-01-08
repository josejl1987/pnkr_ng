
#include "pnkr/renderer/IndirectPipeline.hpp"

#include "pnkr/core/logger.hpp"
#include "pnkr/renderer/FrameManager.hpp"
#include "pnkr/renderer/IndirectDrawContext.hpp"
#include "pnkr/renderer/framegraph/FrameGraph.hpp"
#include "pnkr/renderer/passes/CullingPass.hpp"
#include "pnkr/renderer/passes/GeometryPass.hpp"
#include "pnkr/renderer/passes/OITPass.hpp"
#include "pnkr/renderer/passes/PostProcessPass.hpp"
#include "pnkr/renderer/passes/RenderPassUtils.hpp"
#include "pnkr/renderer/passes/SSAOPass.hpp"
#include "pnkr/renderer/passes/ShadowPass.hpp"
#include "pnkr/renderer/passes/TransmissionPass.hpp"
#include "pnkr/renderer/passes/WBOITPass.hpp"
#include "pnkr/renderer/physics/ClothSystem.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/SpriteSystem.hpp"

namespace pnkr::renderer {

IndirectPipeline::IndirectPipeline(const Dependencies& deps) : m_deps(deps) {}

void IndirectPipeline::setup(FrameGraph& frameGraph,
                             const IndirectDrawContext& drawCtx,
                             RenderPassContext& passCtx)
{
    frameGraph.beginFrame(passCtx.viewportWidth, passCtx.viewportHeight);

    frameGraph.import("Backbuffer", m_deps.renderer->getBackbuffer(),
                      rhi::ResourceLayout::ColorAttachment, true, false);

    passCtx.fgSceneColor = frameGraph.import(
        "SceneColor", m_deps.renderer->getTexture(m_deps.resources->sceneColor),
        m_deps.resources->sceneColorLayout, false, false);
    passCtx.fgSceneDepth = frameGraph.import(
        "SceneDepth", m_deps.renderer->getTexture(m_deps.resources->sceneDepth),
        m_deps.resources->sceneDepthLayout, false, false);

    if (passCtx.msaaSamples > 1) {
        passCtx.fgMsaaColor = frameGraph.import(
            "MsaaColor", m_deps.renderer->getTexture(m_deps.resources->msaaColor),
            m_deps.resources->msaaColorLayout, false, false);
        passCtx.fgMsaaDepth = frameGraph.import(
            "MsaaDepth", m_deps.renderer->getTexture(m_deps.resources->msaaDepth),
            m_deps.resources->msaaDepthLayout, false, false);
    } else {
        passCtx.fgMsaaColor = passCtx.fgSceneColor;
        passCtx.fgMsaaDepth = passCtx.fgSceneDepth;
    }

    if (m_deps.shadowPass &&
        m_deps.shadowPass->getShadowMap() != INVALID_TEXTURE_HANDLE) {
        passCtx.fgShadowMap = frameGraph.import(
            "ShadowMapTex",
            m_deps.renderer->getTexture(m_deps.shadowPass->getShadowMap()),
            m_deps.resources->shadowLayout, false, true);
    } else {
        passCtx.fgShadowMap = {};
    }

    if (passCtx.settings.cullingMode == CullingMode::GPU) {
        addCullingPass(frameGraph, passCtx);
    }

    addClothPass(frameGraph, passCtx);

    addShadowPass(frameGraph, passCtx);
    addSkinningPass(frameGraph, passCtx, drawCtx);
    addGeometryPasses(frameGraph, passCtx, drawCtx);
    addPostProcessPasses(frameGraph, passCtx);

    frameGraph.compile();
    frameGraph.execute(passCtx.cmd);

    m_deps.resources->shadowLayout =
        frameGraph.getFinalLayout(frameGraph.getTexture(frameGraph.getResourceHandle("ShadowMapTex")));
    m_deps.resources->sceneColorLayout =
        frameGraph.getFinalLayout(frameGraph.getTexture(frameGraph.getResourceHandle("SceneColor")));
    m_deps.resources->sceneDepthLayout =
        frameGraph.getFinalLayout(frameGraph.getTexture(frameGraph.getResourceHandle("SceneDepth")));
    m_deps.resources->ssaoLayout =
        frameGraph.getFinalLayout(frameGraph.getTexture(frameGraph.getResourceHandle("SSAO")));
    m_deps.resources->transmissionLayout =
        frameGraph.getFinalLayout(frameGraph.getTexture(frameGraph.getResourceHandle("TransTex")));
}

void IndirectPipeline::addClothPass(FrameGraph& fg, const RenderPassContext& ctx)
{
    if (!m_deps.clothSystem) {
        return;
    }

    struct ClothData {
    };
    fg.addPass<ClothData>("ClothSimulation", [&](FrameGraphBuilder&, ClothData&) {},
                          [&](const ClothData&, const FrameGraphResources&,
                              rhi::RHICommandList* cmd) {
                              m_deps.clothSystem->update(cmd, ctx.dt);
                          });
}

void IndirectPipeline::addSpritePass(FrameGraph& fg, const RenderPassContext& ctx,
                                     FGHandle color, FGHandle depth)
{
    if (!m_deps.spriteSystem) {
        return;
    }

    struct SpriteData {
        FGHandle m_color;
        FGHandle m_depth;
    };

    fg.addPass<SpriteData>(
        "SpritePass",
        [&](FrameGraphBuilder& builder, SpriteData& data) {
            data.m_color =
                builder.write(color, FGAccess::ColorAttachmentWrite);
            data.m_depth = builder.read(depth, FGAccess::DepthAttachmentRead);
        },
        [&](const SpriteData& data, const FrameGraphResources& res,
            rhi::RHICommandList* c) {
            using namespace passes::utils;
            ScopedGpuMarker scope(c, "SpritePass");

            RenderingInfoBuilder builder;
            builder.setRenderArea(ctx.viewportWidth, ctx.viewportHeight)
                .addColorAttachment(res.getTexture(data.m_color),
                                    rhi::LoadOp::Load, rhi::StoreOp::Store)
                .setDepthAttachment(res.getTexture(data.m_depth),
                                    rhi::LoadOp::Load, rhi::StoreOp::Store);

            c->beginRendering(builder.get());
            setFullViewport(c, ctx.viewportWidth, ctx.viewportHeight);

            m_deps.spriteSystem->render(c, *ctx.camera, ctx.viewportWidth,
                                        ctx.viewportHeight, ctx.frameIndex);

            c->endRendering();
        });
}

void IndirectPipeline::addShadowPass(FrameGraph& fg, const RenderPassContext& ctx)
{
    struct ShadowData {
        FGHandle m_shadowMap;
    };
    fg.addPass<ShadowData>(
        "ShadowPass",
        [&](FrameGraphBuilder& builder, ShadowData& data) {
            data.m_shadowMap =
                builder.write(ctx.fgShadowMap, FGAccess::DepthAttachmentWrite);
        },
        [&](const ShadowData&, const FrameGraphResources&,
            rhi::RHICommandList* c) {
            using namespace passes::utils;
            auto passCtxCopy = ctx;
            passCtxCopy.cmd = c;
            ScopedGpuMarker scope(c, "ShadowPass");
            m_deps.shadowPass->execute(passCtxCopy);
        });
}

void IndirectPipeline::addSkinningPass(FrameGraph& fg,
                                       const RenderPassContext& ctx,
                                       const IndirectDrawContext& /*drawCtx*/)
{
    (void)(ctx);
    struct SkinData {
        FGHandle m_vertexBuffer;
        FGHandle m_skinnedVertexBuffer;
        FGHandle m_jointMatricesBuffer;
        FGHandle m_morphVertexBuffer;
        FGHandle m_morphStateBuffer;
        FGHandle m_meshXformBuffer;
    };

    fg.addPass<SkinData>(
        "SkinningPass",
        [&](FrameGraphBuilder& builder, SkinData& data) {
            auto& frame = m_deps.frameManager->getCurrentFrameBuffers();
            bool hasSkinning = (frame.jointMatricesBuffer.isValid());
            bool hasMorphing =
                (m_deps.model->morphVertexBuffer() != INVALID_BUFFER_HANDLE &&
                 frame.morphStateBuffer.isValid());

            if (!hasSkinning && !hasMorphing) {
                return;
            }

            // builder.useAsyncCompute(true); // Async compute not supported yet
            data.m_vertexBuffer = builder.read(
                fg.importBuffer("VertexBuffer",
                          m_deps.renderer->getBuffer(m_deps.model->vertexBuffer()),
                          rhi::ResourceLayout::VertexBufferRead),
                FGAccess::StorageRead);
            data.m_skinnedVertexBuffer = builder.write(
                fg.importBuffer("SkinnedVertexBuffer",
                          m_deps.renderer->getBuffer(
                              frame.skinnedVertexBuffer.handle()),
                          rhi::ResourceLayout::General),
                FGAccess::StorageWrite);

            if (hasSkinning) {
                data.m_jointMatricesBuffer = builder.read(
                    fg.importBuffer(
                        "JointMatricesBuffer",
                        m_deps.renderer->getBuffer(
                            frame.jointMatricesBuffer.handle()),
                        rhi::ResourceLayout::General),
                    FGAccess::StorageRead);
            }

            if (hasMorphing) {
                data.m_morphVertexBuffer = builder.read(
                    fg.importBuffer(
                        "MorphVertexBuffer",
                        m_deps.renderer->getBuffer(m_deps.model->morphVertexBuffer()),
                        rhi::ResourceLayout::VertexBufferRead),
                    FGAccess::StorageRead);
                data.m_morphStateBuffer = builder.read(
                    fg.importBuffer(
                        "MorphStateBuffer",
                        m_deps.renderer->getBuffer(
                            frame.morphStateBuffer.handle()),
                        rhi::ResourceLayout::General),
                    FGAccess::StorageRead);
                data.m_meshXformBuffer = builder.read(
                    fg.importBuffer("MeshXformBuffer",
                              m_deps.renderer->getBuffer(frame.meshXformBuffer.handle()),
                              rhi::ResourceLayout::General),
                    FGAccess::StorageRead);
            }
        },
        [&](const SkinData& /*data*/, const FrameGraphResources&,
            rhi::RHICommandList* c) {
            using namespace passes::utils;

            auto& frame = m_deps.frameManager->getCurrentFrameBuffers();
            bool hasSkinning = (frame.jointMatricesBuffer.isValid());
            bool hasMorphing =
                (m_deps.model->morphVertexBuffer().isValid() &&
                 frame.morphStateBuffer.isValid());
            if (!hasSkinning && !hasMorphing) {
                return;
            }

            auto passCtxCopy = ctx;
            passCtxCopy.cmd = c;
            ScopedGpuMarker scope(c, "SkinningPass");

            [[maybe_unused]] gpu::SkinningPushConstants pc{};
            // pc.vertices = m_deps.renderer->getBuffer(m_deps.model->vertexBuffer())
            //                   ->getDeviceAddress();
            // pc.skinnedVertices =
            //     m_deps.renderer->getBuffer(frame.skinnedVertexBuffer.handle())
            //         ->getDeviceAddress();
            // pc.jointMatrices = m_deps.frameManager->getCurrentFrameBuffers()
            //                        .jointMatricesDeviceAddr;
            // pc.morphVertices = (m_deps.model->morphVertexBuffer() != INVALID_BUFFER_HANDLE)
            //                        ? m_deps.renderer
            //                              ->getBuffer(m_deps.model->morphVertexBuffer())
            //                              ->getDeviceAddress()
            //                        : 0;
            // pc.morphStates = frame.morphStateDeviceAddr;
            // pc.meshXforms = drawCtx.skinningMeshXformAddr;

            // const uint32_t meshCount =
            //     static_cast<uint32_t>(m_deps.model->meshes().size());
            // pc.meshCount = meshCount;

            // dispatchCompute(m_deps.renderer, c, m_deps.skinningPipeline, pc,
            //                 meshCount, 1, 1);
        });
}
void IndirectPipeline::addCullingPass(FrameGraph& fg, const RenderPassContext& ctx)
{
    struct CullingData {
        FGHandle m_sceneBounds;
        FGHandle m_visibleList;
    };

    fg.addPass<CullingData>(
        "CullingPass",
        [&](FrameGraphBuilder& builder, CullingData& data) {
            data.m_sceneBounds = builder.importBuffer(
                "SceneBounds", m_deps.renderer->getBuffer(m_deps.model->boundsBuffer()),
                rhi::ResourceLayout::General);
            data.m_visibleList = builder.importBuffer(
                "VisibleList",
                m_deps.renderer->getBuffer(m_deps.model->visibleListBuffer()),
                rhi::ResourceLayout::General);

            builder.read(data.m_sceneBounds, FGAccess::StorageRead);
            builder.write(data.m_visibleList, FGAccess::StorageWrite);
        },
        [&](const CullingData&, const FrameGraphResources&,
            rhi::RHICommandList* c) {
            using namespace passes::utils;
            auto passCtxCopy = ctx;
            passCtxCopy.cmd = c;
            ScopedGpuMarker scope(c, "CullingPass");
            m_deps.cullingPass->execute(passCtxCopy);
        });
}

void IndirectPipeline::addGeometryPasses(FrameGraph& fg,
                                        const RenderPassContext& ctx,
                                        const IndirectDrawContext& drawCtx)
{
    auto geomData = addMainGeometryPass(fg, ctx);
    FGHandle color = geomData.color;
    FGHandle depth = geomData.depth;

    // if (ctx.settings.enableSkybox) { // Disabled pending settings update
        color = addSkyboxPass(fg, ctx, color, depth);
    // }

    addSSAOPasses(fg, ctx, geomData.resolveDepth);

    auto transFull = addTransmissionPasses(fg, ctx, color);
    addTransmissionGeometryPass(fg, ctx, color, depth, transFull);
    addTransparentPass(fg, ctx, color, depth, transFull);

    bool oitActive = false;
    if (ctx.settings.oitMethod != OITMethod::None &&
        ctx.dodContext.transparentCount > 0) {
        if (ctx.settings.oitMethod == OITMethod::WBOIT && m_deps.wboitPass) {
             addWBOITPasses(fg, ctx, drawCtx, color, depth, geomData.resolveColor);
             oitActive = true;
        } else if (m_deps.oitPass) {
            addOITPasses(fg, ctx, drawCtx, color, depth);
            oitActive = true;
        }
    }

    if (ctx.msaaSamples > 1 && !oitActive) {
        addResolvePass(fg, ctx, color, geomData.resolveColor);
    }

    FGHandle spriteColor = (ctx.msaaSamples > 1 && !oitActive)
                                ? geomData.resolveColor
                                : color;
    FGHandle spriteDepth = (ctx.msaaSamples > 1 && !oitActive)
                                ? geomData.resolveDepth
                                : depth;
    addSpritePass(fg, ctx, spriteColor, spriteDepth);
}

IndirectPipeline::GeometryPassData
IndirectPipeline::addMainGeometryPass(FrameGraph& fg,
                                      const RenderPassContext& ctx)
{
    struct GeometryData {
        FGHandle m_sceneColor;
        FGHandle m_sceneDepth;
        FGHandle m_msaaColor;
        FGHandle m_msaaDepth;
    };

    GeometryPassData passData{};

    fg.addPass<GeometryData>(
        "GeometryPass",
        [&](FrameGraphBuilder& builder, GeometryData& data) {
            data.m_sceneColor = builder.write(ctx.fgSceneColor,
                                              FGAccess::ColorAttachmentWrite);
            data.m_sceneDepth = builder.write(ctx.fgSceneDepth,
                                              FGAccess::DepthAttachmentWrite);
            data.m_msaaColor = builder.write(ctx.fgMsaaColor,
                                             FGAccess::ColorAttachmentWrite);
            data.m_msaaDepth = builder.write(ctx.fgMsaaDepth,
                                             FGAccess::DepthAttachmentWrite);

            passData.color = data.m_sceneColor;
            passData.depth = data.m_sceneDepth;
            passData.resolveColor = data.m_sceneColor;
            passData.resolveDepth = data.m_sceneDepth;

            if (ctx.msaaSamples > 1) {
                passData.color = data.m_msaaColor;
                passData.depth = data.m_msaaDepth;
            }
        },
        [&](const GeometryData& data, const FrameGraphResources& res,
            rhi::RHICommandList* c) {
            using namespace passes::utils;
            auto passCtxCopy = ctx;
            passCtxCopy.cmd = c;
            ScopedGpuMarker scope(c, "GeometryPass");
            
            rhi::RHITexture* color = res.getTexture(data.m_sceneColor);
            rhi::RHITexture* depth = res.getTexture(data.m_sceneDepth);
            rhi::RHITexture* resolveColor = nullptr;
            rhi::RHITexture* resolveDepth = nullptr;

            if (ctx.msaaSamples > 1) {
                color = res.getTexture(data.m_msaaColor);
                depth = res.getTexture(data.m_msaaDepth);
                resolveColor = res.getTexture(data.m_sceneColor);
                resolveDepth = res.getTexture(data.m_sceneDepth);
            }

            m_deps.geometryPass->executeMain(passCtxCopy, color, depth, resolveColor, resolveDepth);
        });

    return passData;
}

FGHandle IndirectPipeline::addSkyboxPass(FrameGraph& fg,
                                        const RenderPassContext& ctx,
                                        FGHandle color,
                                        FGHandle depth)
{
    struct SkyboxData {
        FGHandle m_color;
        FGHandle m_depth;
    };

    FGHandle outColor;
    fg.addPass<SkyboxData>(
        "SkyboxPass",
        [&](FrameGraphBuilder& builder, SkyboxData& data) {
            data.m_color =
                builder.write(color, FGAccess::ColorAttachmentWrite);
            data.m_depth =
                builder.write(depth, FGAccess::DepthAttachmentWrite);
            outColor = data.m_color;
        },
        [&](const SkyboxData& data, const FrameGraphResources& res,
            rhi::RHICommandList* c) {
            using namespace passes::utils;
            auto passCtxCopy = ctx;
            passCtxCopy.cmd = c;
            ScopedGpuMarker scope(c, "SkyboxPass");
            m_deps.geometryPass->drawSkybox(passCtxCopy,
                                            res.getTexture(data.m_color),
                                            res.getTexture(data.m_depth));
        });

    return outColor;
}

void IndirectPipeline::addSSAOPasses(FrameGraph& fg, const RenderPassContext& ctx, FGHandle depthResolved)
{
    if (!m_deps.ssaoPass) {
        return;
    }

    struct SSAOData {
        FGHandle m_depth;
        FGHandle m_ssao;
    };

    FGHandle ssaoRawHandle;
    fg.addPass<SSAOData>(
        "SSAO",
        [&](FrameGraphBuilder& builder, SSAOData& data) {
            data.m_depth = builder.read(depthResolved, FGAccess::SampledRead);
            data.m_ssao = builder.import(
                "SSAO", m_deps.renderer->getTexture(m_deps.ssaoPass->getSSAORawTexture()),
                rhi::ResourceLayout::Undefined);
            builder.write(data.m_ssao, FGAccess::StorageWrite);
            ssaoRawHandle = data.m_ssao;
        },
        [&](const SSAOData& data, const FrameGraphResources& res,
            rhi::RHICommandList* c) {
            using namespace passes::utils;
            auto passCtxCopy = ctx;
            passCtxCopy.cmd = c;
            passCtxCopy.fg = &res;
            passCtxCopy.fgSSAORaw = data.m_ssao;
            passCtxCopy.fgDepthResolved = data.m_depth;
            ScopedGpuMarker scope(c, "SSAO");
            m_deps.ssaoPass->executeGen(passCtxCopy, c);
        });

    struct SSAOBlurData {
        FGHandle m_input;
        FGHandle m_output;
        FGHandle m_depth;
    };

    fg.addPass<SSAOBlurData>(
        "SSAO_Blur",
        [&](FrameGraphBuilder& builder, SSAOBlurData& data) {
            data.m_input = builder.read(ssaoRawHandle, FGAccess::SampledRead);
            data.m_depth = builder.read(depthResolved, FGAccess::SampledRead);
            data.m_output = builder.import(
                "SSAO_Blur",
                m_deps.renderer->getTexture(m_deps.ssaoPass->getSSAOTexture()),
                rhi::ResourceLayout::Undefined);
            builder.write(data.m_output, FGAccess::StorageWrite);
        },
        [&](const SSAOBlurData& data, const FrameGraphResources& res,
            rhi::RHICommandList* c) {
            using namespace passes::utils;
            auto passCtxCopy = ctx;
            passCtxCopy.cmd = c;
            passCtxCopy.fg = &res;
            passCtxCopy.fgSSAORaw = data.m_input;
            passCtxCopy.fgSSAOBlur = data.m_output;
            passCtxCopy.fgDepthResolved = data.m_depth;
            ScopedGpuMarker scope(c, "SSAO_Blur");
            m_deps.ssaoPass->executeBlur(passCtxCopy, c);
        });
}

FGHandle IndirectPipeline::addTransmissionPasses(FrameGraph& fg,
                                                 const RenderPassContext& ctx,
                                                 FGHandle sceneColor)
{
    if (!m_deps.transmissionPass) {
        return sceneColor;
    }

    struct TransmissionData {
        FGHandle m_sceneColor;
    };

    FGHandle outSceneColor = sceneColor;
    fg.addPass<TransmissionData>(
        "Transmission",
        [&](FrameGraphBuilder& builder, TransmissionData& data) {
            data.m_sceneColor = builder.read(sceneColor, FGAccess::TransferSrc);
            outSceneColor = data.m_sceneColor;
            
            auto trans = m_deps.transmissionPass->getTextureHandle();
            if (trans == INVALID_TEXTURE_HANDLE) {
                return;
            }
            m_deps.resources->transmissionTexture = trans;
            auto transHandle = builder.import(
                "TransTex", m_deps.renderer->getTexture(trans),
                rhi::ResourceLayout::Undefined);
            builder.write(transHandle, FGAccess::TransferDst);
        },
        [&](const TransmissionData& /*data*/, const FrameGraphResources& res,
            rhi::RHICommandList* c) {
            using namespace passes::utils;
            auto passCtxCopy = ctx;
            passCtxCopy.cmd = c;
            passCtxCopy.fg = &res;
            ScopedGpuMarker scope(c, "Transmission");
            m_deps.transmissionPass->execute(passCtxCopy);
        });

    return outSceneColor;
}
void IndirectPipeline::addOITPasses(FrameGraph& fg,
                                   const RenderPassContext& ctx,
                                   const IndirectDrawContext& drawCtx,
                                   FGHandle& ioColor,
                                   FGHandle& ioDepth)
{
    (void)drawCtx;
    FGHandle oitSceneColor;
    FGHandle oitSceneDepth;
    FGHandle oitHeads;
    FGHandle oitNodes;

    struct OITData {
        FGHandle m_sceneColor;
        FGHandle m_sceneDepth;
        FGHandle m_heads;
        FGHandle m_nodes;
    };

    fg.addPass<OITData>(
        "OIT_Geometry",
        [&](FrameGraphBuilder& builder, OITData& data) {
            data.m_sceneColor =
                builder.write(ioColor, FGAccess::ColorAttachmentWrite);
            data.m_sceneDepth =
                builder.write(ioDepth, FGAccess::DepthAttachmentWrite);
            
            oitSceneColor = data.m_sceneColor;
            oitSceneDepth = data.m_sceneDepth;

            data.m_heads = builder.import(
                "OIT_Heads",
                m_deps.renderer->getTexture(m_deps.oitPass->getHeadsTexture()),
                rhi::ResourceLayout::Undefined, false, true);
            data.m_nodes = builder.importBuffer(
                "OIT_Nodes",
                m_deps.renderer->getBuffer(m_deps.oitPass->getNodesBuffer()));
            builder.write(data.m_heads, FGAccess::StorageWrite);
            builder.write(data.m_nodes, FGAccess::StorageWrite);
            
            oitHeads = data.m_heads;
            oitNodes = data.m_nodes;

            if (ctx.fgShadowMap.isValid()) {
                builder.read(ctx.fgShadowMap, FGAccess::SampledRead);
            }
            builder.read(fg.getResourceHandle("SSAO"), FGAccess::SampledRead);
        },
        [&](const OITData& data, const FrameGraphResources& res,
            rhi::RHICommandList* c) {
            using namespace passes::utils;
            auto passCtxCopy = ctx;
            passCtxCopy.cmd = c;
            passCtxCopy.fg = &res;
            passCtxCopy.fgOITHeads = data.m_heads;
            ScopedGpuMarker scope(c, "OIT_Geometry");
            m_deps.oitPass->executeGeometry(passCtxCopy, res.getTexture(data.m_sceneDepth));
        });

    struct CopyData {
        FGHandle m_sceneColor;
        FGHandle m_sceneColorCopy;
    };

    FGHandle copySceneColor;
    FGHandle copySceneColorCopy;

    fg.addPass<CopyData>(
        "OIT_CopyBackground",
        [&](FrameGraphBuilder& builder, CopyData& data) {
            data.m_sceneColor =
                builder.read(oitSceneColor, FGAccess::TransferSrc);
            data.m_sceneColorCopy = builder.import(
                "OIT_SceneColorCopy",
                m_deps.renderer->getTexture(m_deps.oitPass->getSceneColorCopy()),
                rhi::ResourceLayout::Undefined);
            builder.write(data.m_sceneColorCopy, FGAccess::TransferDst);
            
            copySceneColor = data.m_sceneColor;
            copySceneColorCopy = data.m_sceneColorCopy;
        },
        [&](const CopyData& data, const FrameGraphResources& res,
            rhi::RHICommandList* c) {
            using namespace passes::utils;
            ScopedGpuMarker scope(c, "OIT_CopyBackground");
            rhi::TextureCopyRegion region{};
            region.srcSubresource = {.mipLevel = 0, .arrayLayer = 0};
            region.dstSubresource = {.mipLevel = 0, .arrayLayer = 0};
            region.extent = {.width = ctx.viewportWidth,
                             .height = ctx.viewportHeight,
                             .depth = 1};

            if (ctx.msaaSamples > 1) {
                region.srcOffsets[0] = {0,0,0};
                region.dstOffsets[0] = {0,0,0};
                c->resolveTexture(res.getTexture(data.m_sceneColor), rhi::ResourceLayout::General,
                                  res.getTexture(data.m_sceneColorCopy), rhi::ResourceLayout::General, region);
            } else {
                c->copyTexture(res.getTexture(data.m_sceneColor),
                               res.getTexture(data.m_sceneColorCopy), region);
            }
        });

    struct CompositeData {
        FGHandle m_sceneColor;
        FGHandle m_sceneColorCopy;
        FGHandle m_heads;
        FGHandle m_nodes;
    };

    FGHandle compositeSceneColor;

    fg.addPass<CompositeData>(
        "OIT_Composite",
        [&](FrameGraphBuilder& builder, CompositeData& data) {
            data.m_sceneColor =
                builder.write(copySceneColor, FGAccess::ColorAttachmentWrite);
            data.m_sceneColorCopy =
                builder.read(copySceneColorCopy, FGAccess::SampledRead);
            data.m_heads = builder.read(oitHeads, FGAccess::SampledRead);
            data.m_nodes = builder.read(oitNodes, FGAccess::SampledRead);
            
            compositeSceneColor = data.m_sceneColor;
        },
        [&](const CompositeData& data, const FrameGraphResources& fgRes,
            rhi::RHICommandList* c) {
            using namespace passes::utils;
            auto passCtxCopy = ctx;
            passCtxCopy.cmd = c;
            passCtxCopy.fg = &fgRes;
            passCtxCopy.fgSceneColorCopy = data.m_sceneColorCopy;
            ScopedGpuMarker scope(c, "OIT_Composite");
            m_deps.oitPass->executeComposite(passCtxCopy, fgRes.getTexture(data.m_sceneColor));
        });

    ioColor = compositeSceneColor;
    ioDepth = oitSceneDepth;
}

void IndirectPipeline::addWBOITPasses(FrameGraph& fg,
                                     const RenderPassContext& ctx,
                                     const IndirectDrawContext& drawCtx,
                                     FGHandle& ioColor,
                                     FGHandle& ioDepth,
                                     FGHandle resolveTarget)
{
    (void)drawCtx;
    FGHandle wboitSceneColor;
    FGHandle wboitSceneDepth;
    FGHandle wboitAccum;
    FGHandle wboitReveal;

    struct WBOITData {
        FGHandle m_sceneColor;
        FGHandle m_sceneDepth;
        FGHandle m_accum;
        FGHandle m_reveal;
    };

    fg.addPass<WBOITData>(
        "WBOIT_Geometry",
        [&](FrameGraphBuilder& builder, WBOITData& data) {
            data.m_sceneColor =
                builder.write(ioColor, FGAccess::ColorAttachmentWrite);
            data.m_sceneDepth =
                builder.write(ioDepth, FGAccess::DepthAttachmentWrite);
            
            wboitSceneColor = data.m_sceneColor;
            wboitSceneDepth = data.m_sceneDepth;

            data.m_accum = builder.import(
                "WBOIT_Accum",
                m_deps.renderer->getTexture(m_deps.wboitPass->getAccumTexture()),
                rhi::ResourceLayout::Undefined);
            builder.write(data.m_accum, FGAccess::ColorAttachmentWrite);
            data.m_reveal = builder.import(
                "WBOIT_Reveal",
                m_deps.renderer->getTexture(m_deps.wboitPass->getRevealTexture()),
                rhi::ResourceLayout::Undefined);
            builder.write(data.m_reveal, FGAccess::ColorAttachmentWrite);
            
            wboitAccum = data.m_accum;
            wboitReveal = data.m_reveal;
        },
        [&](const WBOITData& data, const FrameGraphResources& res,
            rhi::RHICommandList* c) {
            using namespace passes::utils;
            auto passCtxCopy = ctx;
            passCtxCopy.cmd = c;
            ScopedGpuMarker scope(c, "WBOIT_Geometry");
            m_deps.wboitPass->executeGeometry(passCtxCopy, res.getTexture(data.m_sceneDepth));
        });

    struct CopyData {
        FGHandle m_sceneColor;
        FGHandle m_sceneColorCopy;
    };
    
    FGHandle wboitCopySceneColor;
    FGHandle wboitCopySceneColorCopy;

    fg.addPass<CopyData>(
        "WBOIT_CopyBackground",
        [&](FrameGraphBuilder& builder, CopyData& data) {
            data.m_sceneColor =
                builder.read(wboitSceneColor, FGAccess::TransferSrc);
            data.m_sceneColorCopy = builder.import(
                "WBOIT_SceneColorCopy",
                m_deps.renderer->getTexture(m_deps.wboitPass->getSceneColorCopy()),
                rhi::ResourceLayout::Undefined);
            builder.write(data.m_sceneColorCopy, FGAccess::TransferDst);
            
            wboitCopySceneColor = data.m_sceneColor;
            wboitCopySceneColorCopy = data.m_sceneColorCopy;
        },
        [&](const CopyData& data, const FrameGraphResources& res,
            rhi::RHICommandList* c) {
            using namespace passes::utils;
            ScopedGpuMarker scope(c, "WBOIT_CopyBackground");
            rhi::TextureCopyRegion region{};
            region.srcSubresource = {.mipLevel = 0, .arrayLayer = 0};
            region.dstSubresource = {.mipLevel = 0, .arrayLayer = 0};
            region.extent = {.width = ctx.viewportWidth,
                             .height = ctx.viewportHeight,
                             .depth = 1};

            if (ctx.msaaSamples > 1) {
                // Initialize offsets
                region.srcOffsets[0] = {0,0,0};
                region.dstOffsets[0] = {0,0,0};
                c->resolveTexture(res.getTexture(data.m_sceneColor), rhi::ResourceLayout::General,
                                  res.getTexture(data.m_sceneColorCopy), rhi::ResourceLayout::General, region);
            } else {
                c->copyTexture(res.getTexture(data.m_sceneColor),
                               res.getTexture(data.m_sceneColorCopy), region);
            }
        });

    struct CompositeData {
        FGHandle m_sceneColor;
        FGHandle m_sceneColorCopy;
        FGHandle m_accum;
        FGHandle m_reveal;
        FGHandle m_target;
    };
    
    FGHandle wboitCompositeSceneColor;

    fg.addPass<CompositeData>(
        "WBOIT_Composite",
        [&](FrameGraphBuilder& builder, CompositeData& data) {
            // Write to resolveTarget if provided, otherwise overwrite wboitCopySceneColor
            FGHandle targetHandle = resolveTarget.isValid() ? resolveTarget : wboitCopySceneColor;
            
            data.m_target = builder.write(targetHandle, FGAccess::ColorAttachmentWrite);
            data.m_sceneColorCopy =
                builder.read(wboitCopySceneColorCopy, FGAccess::SampledRead);
            data.m_accum =
                builder.read(wboitAccum, FGAccess::SampledRead);
            data.m_reveal =
                builder.read(wboitReveal, FGAccess::SampledRead);
            
            wboitCompositeSceneColor = data.m_target;
        },
        [&](const CompositeData& data, const FrameGraphResources& fgRes,
            rhi::RHICommandList* c) {
            using namespace passes::utils;
            auto passCtxCopy = ctx;
            passCtxCopy.cmd = c;
            passCtxCopy.fg = &fgRes;
            passCtxCopy.fgSceneColorCopy = data.m_sceneColorCopy;
            ScopedGpuMarker scope(c, "WBOIT_Composite");
            m_deps.wboitPass->executeComposite(passCtxCopy, fgRes.getTexture(data.m_target));
        });

    ioColor = wboitCompositeSceneColor;
    ioDepth = wboitSceneDepth;
}
void IndirectPipeline::addTransmissionGeometryPass(FrameGraph& fg,
                                                   const RenderPassContext& ctx,
                                                   FGHandle color,
                                                   FGHandle depth,
                                                   FGHandle transFull)
{
    struct TransGeoData {
        FGHandle m_color;
        FGHandle m_depth;
    };
    fg.addPass<TransGeoData>(
        "TransmissionGeometryPass",
        [&](FrameGraphBuilder& builder, TransGeoData& data) {
            data.m_color =
                builder.write(color, FGAccess::ColorAttachmentWrite);
            data.m_depth = builder.read(depth, FGAccess::DepthAttachmentRead);
            builder.read(transFull, FGAccess::SampledRead);
            if (ctx.fgShadowMap.isValid()) {
                builder.read(ctx.fgShadowMap, FGAccess::SampledRead);
            }
            builder.read(fg.getResourceHandle("SSAO"), FGAccess::SampledRead);
        },
        [&](const TransGeoData& data, const FrameGraphResources& res,
            rhi::RHICommandList* c) {
            using namespace passes::utils;
            auto pCtxCopy = ctx;
            pCtxCopy.cmd = c;
            ScopedGpuMarker scope(c, "TransmissionGeometryPass");

            RenderingInfoBuilder builder;
            builder.setRenderArea(ctx.viewportWidth, ctx.viewportHeight)
                .addColorAttachment(res.getTexture(data.m_color),
                                    rhi::LoadOp::Load, rhi::StoreOp::Store)
                .setDepthAttachment(res.getTexture(data.m_depth),
                                    rhi::LoadOp::Load, rhi::StoreOp::Store);

            c->beginRendering(builder.get());
            setFullViewport(c, ctx.viewportWidth, ctx.viewportHeight);

            gpu::OITPushConstants pc{};
            populateBaseIndirectPushConstants(ctx, pc.indirect, m_deps.renderer);
            pc.indirect.ssaoTextureIndex =
                util::u32(m_deps.renderer->getTextureBindlessIndex(
                    m_deps.renderer->getWhiteTexture()));
            pc.indirect.ssaoSamplerIndex = util::u32(
                m_deps.renderer->getBindlessSamplerIndex(
                    rhi::SamplerAddressMode::ClampToEdge));

            m_deps.geometryPass->drawTransparent(pCtxCopy, pc, true, false);

            c->endRendering();
        });
}

void IndirectPipeline::addTransparentPass(FrameGraph& fg,
                                          const RenderPassContext& ctx,
                                          FGHandle color,
                                          FGHandle depth,
                                          FGHandle transFull)
{
    if (ctx.settings.oitMethod != OITMethod::None) {
        return;
    }

    const auto* dod =
        static_cast<const scene::GLTFUnifiedDODContext*>(ctx.resources.drawLists);
    if ((dod == nullptr) || dod->transparentCount == 0) {
        return;
    }

    struct TranspData {
        FGHandle m_color;
        FGHandle m_depth;
    };
    fg.addPass<TranspData>(
        "TransparentPass",
        [&](FrameGraphBuilder& builder, TranspData& data) {
            data.m_color =
                builder.write(color, FGAccess::ColorAttachmentWrite);
            data.m_depth =
                builder.write(depth, FGAccess::DepthAttachmentWrite);
            builder.read(transFull, FGAccess::SampledRead);
            if (ctx.fgShadowMap.isValid()) {
                builder.read(ctx.fgShadowMap, FGAccess::SampledRead);
            }
            builder.read(fg.getResourceHandle("SSAO"), FGAccess::SampledRead);
        },
        [&](const TranspData& data, const FrameGraphResources& res,
            rhi::RHICommandList* c) {
            using namespace passes::utils;
            auto passCtxCopy = ctx;
            passCtxCopy.cmd = c;
            ScopedGpuMarker scope(c, "TransparentPass");

            RenderingInfoBuilder builder;
            builder.setRenderArea(ctx.viewportWidth, ctx.viewportHeight)
                .addColorAttachment(res.getTexture(data.m_color),
                                    rhi::LoadOp::Load, rhi::StoreOp::Store)
                .setDepthAttachment(res.getTexture(data.m_depth),
                                    rhi::LoadOp::Load, rhi::StoreOp::Store);

            c->beginRendering(builder.get());
            setFullViewport(c, ctx.viewportWidth, ctx.viewportHeight);

            gpu::OITPushConstants pc{};
            populateBaseIndirectPushConstants(ctx, pc.indirect, m_deps.renderer);
            pc.indirect.ssaoTextureIndex =
                util::u32(m_deps.renderer->getTextureBindlessIndex(
                    m_deps.renderer->getWhiteTexture()));
            pc.indirect.ssaoSamplerIndex = util::u32(
                m_deps.renderer->getBindlessSamplerIndex(
                    rhi::SamplerAddressMode::ClampToEdge));

            const bool useOIT =
                (m_deps.oitPass != nullptr) && (ctx.dodContext.transparentCount > 0);
            m_deps.geometryPass->drawTransparent(passCtxCopy, pc, false, !useOIT);
            c->endRendering();
        });
}

void IndirectPipeline::addResolvePass(FrameGraph& fg,
                                      const RenderPassContext& ctx,
                                      FGHandle src,
                                      FGHandle dst)
{
    struct ResolveData {
        FGHandle m_src;
        FGHandle m_dst;
    };

    fg.addPass<ResolveData>(
        "Resolve",
        [&](FrameGraphBuilder& builder, ResolveData& data) {
            data.m_src = builder.read(src, FGAccess::TransferSrc);
            data.m_dst = builder.write(dst, FGAccess::TransferDst);
        },
        [&](const ResolveData& data, const FrameGraphResources& res,
            rhi::RHICommandList* c) {
            using namespace passes::utils;
            ScopedGpuMarker scope(c, "Resolve");
            rhi::TextureCopyRegion region{};
            region.srcSubresource = {.mipLevel = 0, .arrayLayer = 0};
            region.dstSubresource = {.mipLevel = 0, .arrayLayer = 0};
            region.extent = {.width = ctx.viewportWidth,
                             .height = ctx.viewportHeight,
                             .depth = 1};

            auto* srcTex = res.getTexture(data.m_src);
            auto* dstTex = res.getTexture(data.m_dst);

            // Initialize offsets if they were zero-initialized in the struct
            region.srcOffsets[0] = {0,0,0};
            region.dstOffsets[0] = {0,0,0};
            c->resolveTexture(srcTex, res.getTextureLayout(data.m_src),
                              dstTex, res.getTextureLayout(data.m_dst), region);
        });
}

void IndirectPipeline::addPostProcessPasses(FrameGraph& fg,
                                            const RenderPassContext& ctx)
{
    struct PostData {
        FGHandle m_input;
        FGHandle m_output;
        FGHandle m_bright;
        FGHandle m_luminance;
        FGHandle m_bloom0;
        FGHandle m_bloom1;
        FGHandle m_meteredLum;
        FGHandle m_adaptedLum;
        FGHandle m_prevAdaptedLum;
        FGHandle m_ssao;
    };

    fg.addPass<PostData>(
        "PostProcess",
        [&](FrameGraphBuilder& builder, PostData& data) {
            data.m_input =
                builder.read(ctx.fgSceneColor, FGAccess::SampledRead);

            data.m_output =
                builder.write(fg.getResourceHandle("Backbuffer"),
                              FGAccess::ColorAttachmentWrite);

            FGHandle ssaoHandle = fg.getResourceHandle("SSAO_Blur");
            if (ssaoHandle.isValid()) {
                data.m_ssao = builder.read(ssaoHandle, FGAccess::SampledRead);
            } else {
                core::Logger::warn(
                    "PostProcess: Resource 'SSAO_Blur' handle is invalid. SSAO "
                    "will be disabled.");
            }

            const uint32_t fi = ctx.frameIndex;
            data.m_bright = builder.import(
                "PPBright",
                m_deps.renderer->getTexture(
                    m_deps.postProcessPass->getBrightPassTex()),
                rhi::ResourceLayout::Undefined, false, true);
            data.m_luminance = builder.import(
                "PPLuminance",
                m_deps.renderer->getTexture(
                    m_deps.postProcessPass->getLuminanceTex()),
                rhi::ResourceLayout::Undefined, false, true);
            data.m_bloom0 = builder.import(
                "PPBloom0",
                m_deps.renderer->getTexture(
                    m_deps.postProcessPass->getBloomTex0()),
                rhi::ResourceLayout::Undefined, false, true);
            data.m_bloom1 = builder.import(
                "PPBloom1",
                m_deps.renderer->getTexture(
                    m_deps.postProcessPass->getBloomTex1()),
                rhi::ResourceLayout::Undefined, false, true);
            data.m_meteredLum = builder.import(
                "PPMeteredLum",
                m_deps.renderer->getTexture(
                    m_deps.postProcessPass->getMeteredLumTex(fi)),
                rhi::ResourceLayout::Undefined, false, true);
            data.m_adaptedLum = builder.import(
                "PPAdaptedLum",
                m_deps.renderer->getTexture(
                    m_deps.postProcessPass->getAdaptedLumTex(fi)),
                rhi::ResourceLayout::Undefined, false, true);
            data.m_prevAdaptedLum = builder.import(
                "PPPrevAdaptedLum",
                m_deps.renderer->getTexture(
                    m_deps.postProcessPass->getPrevAdaptedLumTex(fi)),
                rhi::ResourceLayout::Undefined);

            builder.write(data.m_bright, FGAccess::StorageWrite);
            builder.write(data.m_luminance, FGAccess::StorageWrite);
            builder.write(data.m_bloom0, FGAccess::StorageWrite);
            builder.write(data.m_bloom1, FGAccess::StorageWrite);
            builder.write(data.m_meteredLum, FGAccess::StorageWrite);
            builder.write(data.m_adaptedLum, FGAccess::StorageWrite);
            builder.read(data.m_prevAdaptedLum, FGAccess::SampledRead);
        },
        [&](const PostData& data, const FrameGraphResources& res,
            rhi::RHICommandList* c) {
            using namespace passes::utils;
            auto passCtxCopy = ctx;
            passCtxCopy.cmd = c;
            passCtxCopy.fg = &res;
            passCtxCopy.fgPPBright = data.m_bright;
            passCtxCopy.fgPPLuminance = data.m_luminance;
            passCtxCopy.fgPPBloom0 = data.m_bloom0;
            passCtxCopy.fgPPBloom1 = data.m_bloom1;
            passCtxCopy.fgPPMeteredLum = data.m_meteredLum;
            passCtxCopy.fgPPAdaptedLum = data.m_adaptedLum;
            passCtxCopy.fgPPPrevAdaptedLum = data.m_prevAdaptedLum;
            ScopedGpuMarker scope(c, "PostProcess");
            m_deps.postProcessPass->execute(passCtxCopy);
        });

    if (ctx.uiRender) {
        struct UIPassData {
            FGHandle m_target;
        };
        fg.addPass<UIPassData>(
            "UIPass",
            [&](FrameGraphBuilder& b, UIPassData& data) {
                data.m_target =
                    b.write(fg.getResourceHandle("Backbuffer"),
                            FGAccess::ColorAttachmentWrite);
            },
            [&](const UIPassData& data, const FrameGraphResources& res,
                rhi::RHICommandList* c) {
                using namespace passes::utils;
                rhi::RenderingInfo info{};
                info.renderArea = {.x = 0,
                                   .y = 0,
                                   .width = ctx.viewportWidth,
                                   .height = ctx.viewportHeight};
                rhi::RenderingAttachment color{};
                color.texture = res.getTexture(data.m_target);
                color.loadOp = rhi::LoadOp::Load;
                color.storeOp = rhi::StoreOp::Store;
                info.colorAttachments.push_back(color);

                c->beginRendering(info);
                {
                    ScopedGpuMarker scope(c, "UIPass");
                    ctx.uiRender(c);
                }
                c->endRendering();
            });
    }
}

}
