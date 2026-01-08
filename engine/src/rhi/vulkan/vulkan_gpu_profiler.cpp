#include "rhi/vulkan/vulkan_gpu_profiler.hpp"
#include "pnkr/core/logger.hpp"
#include "rhi/vulkan/vulkan_command_buffer.hpp"
#include <array>
#include <cstddef>
#include "vulkan_cast.hpp"

using namespace pnkr::renderer;

namespace pnkr::renderer::rhi::vulkan {

namespace {
constexpr uint32_t K_PIPELINE_STATS_QUERY_COUNT = 1;

vk::QueryPipelineStatisticFlags pipelineStatFlags() {
    return vk::QueryPipelineStatisticFlagBits::eInputAssemblyVertices |
        vk::QueryPipelineStatisticFlagBits::eInputAssemblyPrimitives |
        vk::QueryPipelineStatisticFlagBits::eVertexShaderInvocations |
        vk::QueryPipelineStatisticFlagBits::eGeometryShaderInvocations |
        vk::QueryPipelineStatisticFlagBits::eGeometryShaderPrimitives |
        vk::QueryPipelineStatisticFlagBits::eClippingInvocations |
        vk::QueryPipelineStatisticFlagBits::eClippingPrimitives |
        vk::QueryPipelineStatisticFlagBits::eFragmentShaderInvocations |
        vk::QueryPipelineStatisticFlagBits::eTessellationControlShaderPatches |
        vk::QueryPipelineStatisticFlagBits::eTessellationEvaluationShaderInvocations |
        vk::QueryPipelineStatisticFlagBits::eComputeShaderInvocations;
}

constexpr uint32_t K_PIPELINE_STAT_COUNT = 11;
}

void VulkanGPUTimeQueriesManager::init(vk::Device device, uint32_t queriesPerFrame,
                                        double timestampPeriod, bool pipelineStatsSupported) {
    mDevice = device;
    mQueriesPerFrame = queriesPerFrame;
    mTimestampPeriod = timestampPeriod;
    mPipelineStatsSupported = pipelineStatsSupported;

    core::Logger::RHI.trace("GPU profiler: timestampPeriod={} ns", mTimestampPeriod);

    mFramePools.resize(kMaxFrames);
    mQueryTrees.resize(kMaxFrames);
    mQueryStack.resize(kMaxFrames);
    mFrameData.resize(kMaxFrames);

    for (uint32_t i = 0; i < kMaxFrames; i++) {
        vk::QueryPoolCreateInfo poolInfo;
        poolInfo.queryType = vk::QueryType::eTimestamp;
        poolInfo.queryCount = queriesPerFrame * 2;

        mFramePools[i].timestampQueryPool = device.createQueryPool(poolInfo);
        if (mPipelineStatsSupported) {
            vk::QueryPoolCreateInfo statsInfo{};
            statsInfo.queryType = vk::QueryType::ePipelineStatistics;
            statsInfo.queryCount = K_PIPELINE_STATS_QUERY_COUNT;
            statsInfo.pipelineStatistics = pipelineStatFlags();
            mFramePools[i].pipelineStatsQueryPool = device.createQueryPool(statsInfo);
        }
        mFramePools[i].timeQueries = &mQueryTrees[i];

        mQueryTrees[i].init(queriesPerFrame);
        mQueryStack[i].clear();
    }
}

void VulkanGPUTimeQueriesManager::shutdown(vk::Device device) {
    for (auto& pool : mFramePools) {
        if (pool.timestampQueryPool) {
            device.destroyQueryPool(pool.timestampQueryPool);
        }
        if (pool.pipelineStatsQueryPool) {
            device.destroyQueryPool(pool.pipelineStatsQueryPool);
        }
    }
    mFramePools.clear();
    mQueryTrees.clear();
    mQueryStack.clear();
    mFrameData.clear();
}

void VulkanGPUTimeQueriesManager::reset() {
    for (size_t i = 0; i < mQueryTrees.size(); ++i) {
        mQueryTrees[i].reset();
        mQueryStack[i].clear();
    }
}

void VulkanGPUTimeQueriesManager::reset(uint32_t frameIndex) {
    uint32_t index = frameIndex % kMaxFrames;
    mQueryTrees[index].reset();
    mQueryStack[index].clear();
    mFrameData[index].queries.clear();
    mFrameData[index].totalFrameTimeMs = 0.0;
    mFrameData[index].pipelineStats.reset();
    mFrameData[index].drawCallStats.reset();
    mFrameData[index].warnings.clear();

}

GPUTimeQuery* VulkanGPUTimeQueriesManager::pushQuery(uint32_t frameIndex, const char* name, uint16_t parentIndex, uint16_t depth) {
    uint32_t index = frameIndex % kMaxFrames;
    auto* query = mQueryTrees[index].push(name, parentIndex, depth);
    if (query == nullptr) {
      core::Logger::RHI.warn("GPU profiler: pushQuery failed (frameIndex={}, "
                             "index={}, allocated={}, max={})",
                             frameIndex, index,
                             mQueryTrees[index].allocatedCount(),
                             mQueriesPerFrame);
      return nullptr;
    }
    static uint32_t pushLogCount = 0;
    if (pushLogCount < 10) {
        core::Logger::RHI.trace("GPU profiler: pushQuery name='{}' frameIndex={} index={} startIdx={}",
                           query->name, frameIndex, index, query->startQueryIndex);
        pushLogCount++;
    }
    return query;
}

GPUTimeQuery* VulkanGPUTimeQueriesManager::pushQuery(uint32_t frameIndex, const char* name) {
    const uint32_t index = frameIndex % kMaxFrames;
    const uint16_t parentIdx = mQueryStack[index].empty() ? 0xFFFF : static_cast<uint16_t>(mQueryStack[index].back());
    const uint16_t depth = static_cast<uint16_t>(mQueryStack[index].size());

    auto* query = pushQuery(frameIndex, name, parentIdx, depth);
    if (query) {
        const uint32_t queryIdx = static_cast<uint32_t>(mQueryTrees[index].allocatedCount() - 1);
        mQueryStack[index].push_back(queryIdx);
    }
    return query;
}

GPUTimeQuery* VulkanGPUTimeQueriesManager::popQuery(uint32_t frameIndex) {
    const uint32_t index = frameIndex % kMaxFrames;
    if (mQueryStack[index].empty()) {
        return nullptr;
    }

    const uint32_t queryIdx = mQueryStack[index].back();
    mQueryStack[index].pop_back();
    return mQueryTrees[index].getQuery(static_cast<uint16_t>(queryIdx));
}

uint16_t VulkanGPUTimeQueriesManager::openDepth(uint32_t frameIndex) const {
    const uint32_t index = frameIndex % kMaxFrames;
    return static_cast<uint16_t>(mQueryStack[index].size());
}

GPUTimeQuery* VulkanGPUTimeQueriesManager::getQuery(uint32_t frameIndex, uint16_t queryIndex) {
    uint32_t index = frameIndex % kMaxFrames;
    return mQueryTrees[index].getQuery(queryIndex);
}

void VulkanGPUTimeQueriesManager::resolve(uint32_t frameIndex) {
    const uint32_t index = frameIndex % kMaxFrames;

    auto& pool      = mFramePools[index];
    auto& tree      = mQueryTrees[index];
    auto& frameData = mFrameData[index];

    const uint32_t queryCount = tree.completedCount();
    const uint32_t numTimestamps = queryCount * 2;

    bool timestampsReady = false;
    if (queryCount == 0) {
        static uint32_t emptyLogCount = 0;
        if (emptyLogCount < 5) {
            core::Logger::RHI.warn("GPU profiler: no completed queries (frameIndex={}, allocated={})",
                                frameIndex, tree.allocatedCount());
            emptyLogCount++;
        }
    } else {
        static uint32_t resolveLogCount = 0;
        if (resolveLogCount < 5) {
            core::Logger::RHI.trace("GPU profiler: resolve frameIndex={} index={} completed={} allocated={}",
                               frameIndex, index, queryCount, tree.allocatedCount());
            resolveLogCount++;
        }
    }
    if (queryCount > 0) {

      std::vector<uint64_t> results(static_cast<size_t>(numTimestamps * 2));

      const vk::Result result = mDevice.getQueryPoolResults(
          pool.timestampQueryPool, 0, numTimestamps,
          results.size() * sizeof(uint64_t), results.data(),
          sizeof(uint64_t) * 2,
          vk::QueryResultFlagBits::e64 |
              vk::QueryResultFlagBits::eWithAvailability);

      if (result == vk::Result::eSuccess) {
        timestampsReady = true;
        for (uint32_t i = 0; i < numTimestamps; i++) {
          if (results[(i * 2) + 1] == 0) {
            timestampsReady = false;
            break;
          }
        }
        if (!timestampsReady) {
          core::Logger::RHI.trace(
              "GPU profiler: timestamps not ready (queryCount={})", queryCount);
        }
        } else if (result == vk::Result::eNotReady) {
            core::Logger::RHI.trace("GPU profiler: query results not ready (queryCount={})", queryCount);
        } else {
            core::Logger::RHI.trace("GPU profiler: getQueryPoolResults failed (result={}, queryCount={})",
                               vk::to_string(result), queryCount);
        }

        if (timestampsReady) {
            frameData.queries = tree.queries();
            frameData.queries.resize(queryCount);

            const auto frameStartTimestamp = static_cast<double>(results[0]);
            double frameEndTimestamp = frameStartTimestamp;

            for (uint32_t i = 0; i < queryCount; i++) {
              const auto start = static_cast<double>(
                  results[static_cast<size_t>((i * 2) * 2)]);
              const auto end = static_cast<double>(
                  results[static_cast<size_t>((i * 2 + 1) * 2)]);

              if (end < start) {
                core::Logger::RHI.warn("GPU profiler timestamp pair inverted: "
                                       "query={} start={} end={}",
                                       i, start, end);
              }

                frameEndTimestamp = std::max(frameEndTimestamp, end);

                const double startNs = (start - frameStartTimestamp) * mTimestampPeriod;
                const double elapsedNs = (end - start) * mTimestampPeriod;

                frameData.queries[i].startMs = startNs / 1000000.0;
                frameData.queries[i].elapsedMs = elapsedNs / 1000000.0;
            }

            frameData.totalFrameTimeMs = (frameEndTimestamp - frameStartTimestamp) * mTimestampPeriod / 1000000.0;

            if (frameData.totalFrameTimeMs <= 0.0) {
                static uint32_t zeroLogCount = 0;
                if (zeroLogCount < 5) {
                    const uint32_t dumpCount = std::min<uint32_t>(queryCount, 4);
                    core::Logger::RHI.warn("GPU profiler: zero frame time (queries={})", queryCount);
                    for (uint32_t i = 0; i < dumpCount; i++) {
                      const uint64_t start =
                          results[static_cast<size_t>((i * 2) * 2)];
                      const uint64_t end =
                          results[static_cast<size_t>((i * 2 + 1) * 2)];
                      core::Logger::RHI.warn("GPU profiler timestamp pair: "
                                             "query={} start={} end={}",
                                             i, start, end);
                    }
                    zeroLogCount++;
                }
            }
        }
    }

    bool pipelineStatsReady = false;
    if (mPipelineStatsSupported && pool.pipelineStatsQueryPool) {
      std::array<uint64_t, K_PIPELINE_STAT_COUNT> statsResults{};
      const vk::Result statsResult = mDevice.getQueryPoolResults(
          pool.pipelineStatsQueryPool, 0, K_PIPELINE_STATS_QUERY_COUNT,
          sizeof(statsResults), statsResults.data(), sizeof(uint64_t),
          vk::QueryResultFlagBits::e64);

      if (statsResult == vk::Result::eSuccess) {
        GPUPipelineStatistics stats{};
        stats.inputAssemblyVertices = statsResults[0];
        stats.inputAssemblyPrimitives = statsResults[1];
        stats.vertexShaderInvocations = statsResults[2];
        stats.geometryShaderInvocations = statsResults[3];
        stats.geometryShaderPrimitives = statsResults[4];
        stats.clippingInvocations = statsResults[5];
        stats.clippingPrimitives = statsResults[6];
        stats.fragmentShaderInvocations = statsResults[7];
        stats.tessControlPatches = statsResults[8];
        stats.tessEvalInvocations = statsResults[9];
        stats.computeShaderInvocations = statsResults[10];
        frameData.pipelineStats = stats;
        pipelineStatsReady = true;
      }
    }

    if (timestampsReady || pipelineStatsReady) {
        if (frameData.drawCallStats.drawCalls == 0 &&
            frameData.drawCallStats.drawIndirectCalls == 0 &&
            mLastResolvedFrameData.drawCallStats.drawCalls > 0) {
            frameData.drawCallStats = mLastResolvedFrameData.drawCallStats;
        }

        mHasResolvedFrame = true;
        mLastResolvedFrameIndex = frameIndex;
        mLastResolvedFrameData = frameData;
    }

    if (frameData.totalFrameTimeMs > 0.1) {
        const uint32_t totalDraws = frameData.drawCallStats.drawCalls + frameData.drawCallStats.drawIndirectCalls;
        if (totalDraws > 1000) {
            frameData.primaryBottleneck = GPUBottleneck::VertexProcessingBound;
        } else if (frameData.memoryStats.getUsagePercent() > 90.0) {
            frameData.primaryBottleneck = GPUBottleneck::MemoryBound;
        } else {
            frameData.primaryBottleneck = GPUBottleneck::None;
        }
    }
}

std::vector<GPUTimeQuery>& VulkanGPUTimeQueriesManager::getFrameQueries(uint32_t frameIndex) {
    uint32_t index = frameIndex % kMaxFrames;
    return mQueryTrees[index].queries();
}

const std::vector<GPUTimeQuery>& VulkanGPUTimeQueriesManager::getFrameQueries(uint32_t frameIndex) const {
    uint32_t index = frameIndex % kMaxFrames;
    return mQueryTrees[index].queries();
}

const GPUFrameData& VulkanGPUTimeQueriesManager::getFrameData(uint32_t frameIndex) const {
    uint32_t index = frameIndex % kMaxFrames;
    return mFrameData[index];
}

void VulkanGPUTimeQueriesManager::updatePipelineStatistics(uint32_t frameIndex, const GPUPipelineStatistics& stats) {
    uint32_t index = frameIndex % kMaxFrames;
    mFrameData[index].pipelineStats = stats;
}

void VulkanGPUTimeQueriesManager::updateMemoryStatistics(const GPUMemoryStatistics& stats) {
    for (auto& fd : mFrameData) {
        fd.memoryStats = stats;
    }
}

void VulkanGPUTimeQueriesManager::updateDrawCallStatistics(uint32_t frameIndex, const GPUDrawCallStatistics& stats) {
    uint32_t index = frameIndex % kMaxFrames;
    mFrameData[index].drawCallStats = stats;
}

void VulkanGPUTimeQueriesManager::updateStreamingStatistics(uint32_t frameIndex, const GPUStreamingStatistics& stats) {
    uint32_t index = frameIndex % kMaxFrames;
    mFrameData[index].streamingStats = stats;
}

void* VulkanGPUTimeQueriesManager::getQueryPoolHandle(uint32_t frameIndex) {
    uint32_t index = frameIndex % kMaxFrames;
    return (void*)((VkQueryPool)mFramePools[index].timestampQueryPool);
}

void VulkanGPUTimeQueriesManager::resetQueryPool(RHICommandList* cmd, uint32_t frameIndex) {
    uint32_t index = frameIndex % kMaxFrames;
    auto *vkCmd = rhi_cast<VulkanRHICommandBuffer>(cmd);
    vkCmd->commandBuffer().resetQueryPool(mFramePools[index].timestampQueryPool, 0, mQueriesPerFrame * 2);
    if (mPipelineStatsSupported && mFramePools[index].pipelineStatsQueryPool) {
      vkCmd->commandBuffer().resetQueryPool(
          mFramePools[index].pipelineStatsQueryPool, 0,
          K_PIPELINE_STATS_QUERY_COUNT);
    }
}

vk::QueryPool VulkanGPUTimeQueriesManager::getQueryPool(uint32_t frameIndex) const {
    uint32_t index = frameIndex % kMaxFrames;
    return mFramePools[index].timestampQueryPool;
}

void VulkanGPUTimeQueriesManager::beginPipelineStatisticsQuery(RHICommandList* cmd,
                                                               uint32_t frameIndex) {
    if (!mPipelineStatsSupported) {
        return;
    }
    uint32_t index = frameIndex % kMaxFrames;
    if (!mFramePools[index].pipelineStatsQueryPool) {
        return;
    }
    auto *vkCmd = rhi_cast<VulkanRHICommandBuffer>(cmd);
    vkCmd->commandBuffer().beginQuery(mFramePools[index].pipelineStatsQueryPool, 0, {});
}

void VulkanGPUTimeQueriesManager::endPipelineStatisticsQuery(RHICommandList* cmd,
                                                             uint32_t frameIndex) {
    if (!mPipelineStatsSupported) {
        return;
    }
    uint32_t index = frameIndex % kMaxFrames;
    if (!mFramePools[index].pipelineStatsQueryPool) {
        return;
    }
    auto *vkCmd = rhi_cast<VulkanRHICommandBuffer>(cmd);
    vkCmd->commandBuffer().endQuery(mFramePools[index].pipelineStatsQueryPool, 0);
}

}

