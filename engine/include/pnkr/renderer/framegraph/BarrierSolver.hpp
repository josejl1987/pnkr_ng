#pragma once

#include "pnkr/renderer/framegraph/FGTypes.hpp"
#include <vector>

namespace pnkr::renderer {

class FrameGraph;

class BarrierSolver {
public:
    static void solveBarriers(
        const PassEntry& pass,
        std::vector<ResourceEntry>& resources,
        FrameGraph& frameGraph,
        std::vector<rhi::RHIMemoryBarrier>& outBarriers
    );

private:
    struct DesiredAccess {
        bool used = false;
        FGAccess bestAccess = FGAccess::SampledRead;
        int priority = -1;
        rhi::ShaderStageFlags stages;
        FGHandle view{};
    };

    static rhi::ResourceLayout accessToLayout(FGAccess a);
    static rhi::ShaderStageFlags accessToStage(FGAccess a);
    static int accessPriority(FGAccess a);
};

} // namespace pnkr::renderer
