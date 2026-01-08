#pragma once
#include "pnkr/renderer/profiling/gpu_profiler.hpp"
#include <imgui.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <deque>
#include <functional>

struct ImDrawList;

namespace pnkr::renderer::rhi {
    class BindlessManager;
}

namespace pnkr::renderer {

struct GPUTimingStatistics {
    double minMs = 1e10;
    double maxMs = 0.0;
    double avgMs = 0.0;
    uint32_t sampleCount = 0;
    uint32_t color = 0;

    void update(double newValue) {
        minMs = std::min(minMs, newValue);
        maxMs = std::max(maxMs, newValue);
        avgMs = (avgMs * sampleCount + newValue) / (sampleCount + 1);
        sampleCount++;
    }

    void reset() {
        minMs = 1e10;
        maxMs = 0.0;
        avgMs = 0.0;
        sampleCount = 0;
    }
};

class GPUProfilerImGui {
public:
    GPUProfilerImGui();

    void draw(const char* windowName, GPUTimeQueriesManager* profiler, uint32_t currentFrame, uint32_t framebufferWidth, uint32_t framebufferHeight);

    void setTextureResolver(std::function<ImTextureID(TextureHandle)> resolver) {
        m_textureResolver = std::move(resolver);
    }

    void setBindlessManager(rhi::BindlessManager* manager) {
        m_bindlessManager = manager;
    }

    void setPaused(bool paused) { mPaused = paused; }
    bool isPaused() const { return mPaused; }

    void resetStatistics();

private:

    void drawStackedTimeline();
    static void drawPipelineStatistics(const GPUPipelineStatistics &stats,
                                       bool supported,
                                       uint32_t framebufferWidth,
                                       uint32_t framebufferHeight);
    void drawMemoryStatistics(const GPUMemoryStatistics& stats);
    void drawTimingTree(const std::vector<GPUTimeQuery>& queries);
    void drawBindlessStatistics();
    void drawStatisticsTable();
    void drawLegend();

    uint32_t hashStringToColor(const std::string& str);
    void updateFrameHistory(const GPUFrameData& frameData);
    static const char *bottleneckToString(GPUBottleneck bottleneck);

  private:
    bool mPaused = false;

    bool mShowTimeline = true;

    static constexpr size_t kMaxHistoryFrames = 120;
    std::deque<GPUFrameData> mFrameHistory;

    std::unordered_map<std::string, GPUTimingStatistics> mStatistics;

    float mGraphHeight = 200.0f;
    float mLegendWidth = 200.0f;

    std::unordered_map<std::string, uint32_t> mColorCache;

    std::function<ImTextureID(TextureHandle)> m_textureResolver;
    rhi::BindlessManager* m_bindlessManager = nullptr;
};

}
