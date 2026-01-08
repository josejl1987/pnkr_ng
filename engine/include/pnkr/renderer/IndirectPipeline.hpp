#pragma once

#include "framegraph/FGTypes.hpp"
#include "pnkr/renderer/RenderPipeline.hpp"
#include "pnkr/renderer/RHIResourceManager.hpp"

namespace pnkr::renderer {

class FrameManager;
class RHIRenderer;
struct RenderGraphResources;
struct RenderSettings;
namespace scene { class ModelDOD; class SpriteSystem; }
namespace physics { class ClothSystem; }

class CullingPass;
class GeometryPass;
class ShadowPass;
class SSAOPass;
class TransmissionPass;
class OITPass;
class WBOITPass;
class PostProcessPass;

class IndirectPipeline final : public RenderPipeline {
public:
    struct Dependencies {
        RHIRenderer* renderer = nullptr;
        FrameManager* frameManager = nullptr;
        RenderGraphResources* resources = nullptr;
        RenderSettings* settings = nullptr;
        scene::ModelDOD* model = nullptr;
        PipelinePtr skinningPipeline{};

        CullingPass* cullingPass = nullptr;
        GeometryPass* geometryPass = nullptr;
        ShadowPass* shadowPass = nullptr;
        SSAOPass* ssaoPass = nullptr;
        TransmissionPass* transmissionPass = nullptr;
        OITPass* oitPass = nullptr;
        WBOITPass* wboitPass = nullptr;
        PostProcessPass* postProcessPass = nullptr;
        physics::ClothSystem* clothSystem = nullptr;
        scene::SpriteSystem* spriteSystem = nullptr;
    };

    explicit IndirectPipeline(const Dependencies& deps);

    void setup(FrameGraph& frameGraph,
               const IndirectDrawContext& drawCtx,
               RenderPassContext& passCtx) override;

private:
    void addShadowPass(FrameGraph& fg, const RenderPassContext& ctx);
    void addSkinningPass(FrameGraph& fg, const RenderPassContext& ctx,
                         const IndirectDrawContext& drawCtx);
    void addCullingPass(FrameGraph& fg, const RenderPassContext& ctx);
    void addGeometryPasses(FrameGraph& fg, const RenderPassContext& ctx,
                           const IndirectDrawContext& drawCtx);
    void addPostProcessPasses(FrameGraph& fg, const RenderPassContext& ctx);
    void addClothPass(FrameGraph& fg, const RenderPassContext& ctx);
    void addSpritePass(FrameGraph& fg, const RenderPassContext& ctx,
                       FGHandle color, FGHandle depth);

    struct GeometryPassData {
        FGHandle color;
        FGHandle depth;
        FGHandle resolveColor;
        FGHandle resolveDepth;
    };

    GeometryPassData addMainGeometryPass(FrameGraph& fg,
                                         const RenderPassContext& ctx);
    FGHandle addSkyboxPass(FrameGraph& fg, const RenderPassContext& ctx,
                           FGHandle color, FGHandle depth);
    void addSSAOPasses(FrameGraph& fg, const RenderPassContext& ctx, FGHandle depthResolved);
    FGHandle addTransmissionPasses(FrameGraph& fg, const RenderPassContext& ctx,
                                   FGHandle sceneColor);
    void addOITPasses(FrameGraph& fg, const RenderPassContext& ctx,
                      const IndirectDrawContext& drawCtx,
                      FGHandle& ioColor, FGHandle& ioDepth);
    void addWBOITPasses(FrameGraph& fg, const RenderPassContext& ctx,
                        const IndirectDrawContext& drawCtx,
                        FGHandle& ioColor, FGHandle& ioDepth, FGHandle resolveTarget = {});
    void addTransmissionGeometryPass(FrameGraph& fg, const RenderPassContext& ctx,
                                     FGHandle color, FGHandle depth,
                                     FGHandle transFull);
    void addTransparentPass(FrameGraph& fg, const RenderPassContext& ctx,
                            FGHandle color, FGHandle depth, FGHandle transFull);
    void addResolvePass(FrameGraph& fg, const RenderPassContext& ctx,
                        FGHandle src, FGHandle dst);

    Dependencies m_deps{};
};

}
