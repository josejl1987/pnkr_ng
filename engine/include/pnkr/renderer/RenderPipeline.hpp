#pragma once

namespace pnkr::renderer {

struct IndirectDrawContext;
struct RenderPassContext;
class FrameGraph;

class RenderPipeline {
public:
    virtual ~RenderPipeline() = default;
    virtual void setup(FrameGraph& frameGraph,
                       const IndirectDrawContext& drawCtx,
                       RenderPassContext& passCtx) = 0;
};

}
