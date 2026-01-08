#include "../common/SampleApp.h"

using namespace pnkr;
using namespace pnkr::samples;

class TriangleSample : public Application {
    PipelineHandle m_pipe{};

public:
    TriangleSample() : Application({"PNKR - Triangle", 800, 600, SDL_WINDOW_RESIZABLE}) {}

    void onInit() override {
        renderer::VulkanPipeline::Config cfg{};
        cfg.m_vertSpvPath = getShaderPath("triangle.vert.spv");
        cfg.m_fragSpvPath = getShaderPath("triangle.frag.spv");
        cfg.m_cullMode = vk::CullModeFlagBits::eNone;
        m_pipe = m_renderer.createPipeline(cfg);
    }

    void onRender(const renderer::RenderFrameContext& ctx) override {
        ctx.m_cmd->bindPipeline(m_renderer.getPipeline(m_pipe));
        ctx.m_cmd.draw(3, 1, 0, 0);
    }
};

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    TriangleSample app;
    return app.run();
}

