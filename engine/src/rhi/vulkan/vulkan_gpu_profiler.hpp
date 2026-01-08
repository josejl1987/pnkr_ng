#pragma once
#include "pnkr/renderer/profiling/gpu_profiler.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>

namespace pnkr::renderer::rhi::vulkan {

struct VulkanGpuFramePools {
    vk::QueryPool timestampQueryPool;
    vk::QueryPool pipelineStatsQueryPool;
    pnkr::renderer::GpuTimeQueryTree* timeQueries = nullptr;
};

class VulkanGPUTimeQueriesManager : public pnkr::renderer::GPUTimeQueriesManager {
public:
    void init(vk::Device device, uint32_t queriesPerFrame, double timestampPeriod, bool pipelineStatsSupported);
    void shutdown(vk::Device device);

    void reset() override;
    void reset(uint32_t frameIndex) override;
    pnkr::renderer::GPUTimeQuery* pushQuery(uint32_t frameIndex, const char* name, uint16_t parentIndex, uint16_t depth) override;
    pnkr::renderer::GPUTimeQuery* pushQuery(uint32_t frameIndex, const char* name) override;
    pnkr::renderer::GPUTimeQuery* popQuery(uint32_t frameIndex) override;
    uint16_t openDepth(uint32_t frameIndex) const override;

    pnkr::renderer::GPUTimeQuery* getQuery(uint32_t frameIndex, uint16_t queryIndex) override;
    void resolve(uint32_t frameIndex) override;

    std::vector<pnkr::renderer::GPUTimeQuery>& getFrameQueries(uint32_t frameIndex) override;
    const std::vector<pnkr::renderer::GPUTimeQuery>& getFrameQueries(uint32_t frameIndex) const override;

    const pnkr::renderer::GPUFrameData& getFrameData(uint32_t frameIndex) const override;
    const pnkr::renderer::GPUFrameData& getLastResolvedFrameData() const override { return mLastResolvedFrameData; }
    void updatePipelineStatistics(uint32_t frameIndex, const pnkr::renderer::GPUPipelineStatistics& stats) override;
    void updateMemoryStatistics(const pnkr::renderer::GPUMemoryStatistics& stats) override;
    void updateDrawCallStatistics(uint32_t frameIndex, const pnkr::renderer::GPUDrawCallStatistics& stats) override;
    void updateStreamingStatistics(uint32_t frameIndex, const GPUStreamingStatistics& stats) override;

    void* getQueryPoolHandle(uint32_t frameIndex) override;
    uint32_t getQueriesPerFrame() const override { return mQueriesPerFrame; }
    void resetQueryPool(pnkr::renderer::rhi::RHICommandList* cmd, uint32_t frameIndex) override;

    void beginPipelineStatisticsQuery(pnkr::renderer::rhi::RHICommandList* cmd, uint32_t frameIndex) override;
    void endPipelineStatisticsQuery(pnkr::renderer::rhi::RHICommandList* cmd, uint32_t frameIndex) override;
    bool pipelineStatisticsSupported() const override { return mPipelineStatsSupported; }

    bool hasResolvedFrame() const override { return mHasResolvedFrame; }
    uint32_t lastResolvedFrameIndex() const override { return mLastResolvedFrameIndex; }

    vk::QueryPool getQueryPool(uint32_t frameIndex) const;

private:
    vk::Device mDevice;
    double mTimestampPeriod = 1.0;
    uint32_t mQueriesPerFrame = 0;
    bool mPipelineStatsSupported = false;
    bool mHasResolvedFrame = false;
    uint32_t mLastResolvedFrameIndex = 0;
    pnkr::renderer::GPUFrameData mLastResolvedFrameData;
    std::vector<VulkanGpuFramePools> mFramePools;
    std::vector<pnkr::renderer::GpuTimeQueryTree> mQueryTrees;
    std::vector<std::vector<uint32_t>> mQueryStack;
    std::vector<pnkr::renderer::GPUFrameData> mFrameData;
};

}
