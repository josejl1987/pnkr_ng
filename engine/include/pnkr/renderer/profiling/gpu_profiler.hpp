#pragma once
#include <string>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

#include "pnkr/rhi/rhi_command_buffer.hpp"
#include "pnkr/core/Handle.h"

namespace pnkr::renderer
{

    struct GPUTimeQuery
    {
        std::string name;
        double startMs = 0.0;
        double elapsedMs = 0.0;
        uint16_t startQueryIndex = 0;
        uint16_t endQueryIndex = 0;
        uint16_t parentIndex = 0;
        uint16_t depth = 0;
        uint32_t frameIndex = 0;
        uint32_t color = 0;
    };

    struct GPUPipelineStatistics
    {
        uint64_t inputAssemblyVertices = 0;
        uint64_t inputAssemblyPrimitives = 0;
        uint64_t vertexShaderInvocations = 0;
        uint64_t geometryShaderInvocations = 0;
        uint64_t geometryShaderPrimitives = 0;
        uint64_t clippingInvocations = 0;
        uint64_t clippingPrimitives = 0;
        uint64_t fragmentShaderInvocations = 0;
        uint64_t tessControlPatches = 0;
        uint64_t tessEvalInvocations = 0;
        uint64_t computeShaderInvocations = 0;

        void reset()
        {
            *this = GPUPipelineStatistics{};
        }

        void add(const GPUPipelineStatistics& other)
        {
            inputAssemblyVertices += other.inputAssemblyVertices;
            inputAssemblyPrimitives += other.inputAssemblyPrimitives;
            vertexShaderInvocations += other.vertexShaderInvocations;
            geometryShaderInvocations += other.geometryShaderInvocations;
            geometryShaderPrimitives += other.geometryShaderPrimitives;
            clippingInvocations += other.clippingInvocations;
            clippingPrimitives += other.clippingPrimitives;
            fragmentShaderInvocations += other.fragmentShaderInvocations;
            tessControlPatches += other.tessControlPatches;
            tessEvalInvocations += other.tessEvalInvocations;
            computeShaderInvocations += other.computeShaderInvocations;
        }
    };

    struct GPUMemoryStatistics
    {
        uint64_t allocatedBytes = 0;
        uint64_t usedBytes = 0;
        uint64_t budgetBytes = 0;
        uint32_t allocationCount = 0;
        uint32_t bufferCount = 0;
        uint32_t textureCount = 0;
        uint64_t textureBytes = 0;
        uint64_t bufferBytes = 0;
        std::vector<struct TextureMemoryInfo> textureList;

        double getUsagePercent() const
        {
            return budgetBytes > 0 ? (usedBytes * 100.0 / budgetBytes) : 0.0;
        }

        double getTexturePercent() const
        {
            return usedBytes > 0 ? (textureBytes * 100.0 / usedBytes) : 0.0;
        }
    };

    struct TextureMemoryInfo
    {
        TextureHandle handle{INVALID_TEXTURE_HANDLE};
        std::string name;
        uint64_t sizeBytes = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t mipLevels = 0;
        rhi::Format format = rhi::Format::Undefined;
        bool isStreaming = false;
        double allocationTimeMs = 0.0;
    };

    struct GPUDrawCallStatistics
    {
        uint32_t drawCalls = 0;
        uint32_t drawIndirectCalls = 0;
        uint32_t dispatchCalls = 0;
        uint32_t trianglesDrawn = 0;
        uint32_t verticesProcessed = 0;
        uint32_t instancesDrawn = 0;
        uint32_t pipelineswitches = 0;
        uint32_t descriptorBinds = 0;

        void reset()
        {
            *this = GPUDrawCallStatistics{};
        }
    };

    struct GPUBandwidthStatistics
    {
        uint64_t bytesRead = 0;
        uint64_t bytesWritten = 0;
        double readBandwidthGBps = 0.0;
        double writeBandwidthGBps = 0.0;
        double totalBandwidthGBps = 0.0;
        double peakBandwidthGBps = 0.0;

        double getUtilizationPercent() const
        {
            return peakBandwidthGBps > 0 ? (totalBandwidthGBps * 100.0 / peakBandwidthGBps) : 0.0;
        }
    };

    struct GPUUtilizationStatistics
    {
        double gpuUtilizationPercent = 0.0;
        double shaderUtilizationPercent = 0.0;
        double textureSamplerPercent = 0.0;
        double ropUtilizationPercent = 0.0;
        double l2CacheHitRate = 0.0;
        double smOccupancyPercent = 0.0;
        double waveOccupancyPercent = 0.0;
        double cuUtilizationPercent = 0.0;
    };

    enum class GPUBottleneck
    {
        None,
        MemoryBound,
        ComputeBound,
        LatencyBound,
        RasterBound,
        TextureBound,
        ROPBound,
        VertexProcessingBound,
        GeometryBound,
        SynchronizationBound
    };

    struct GPUPerformanceWarning
    {
        std::string message;
        float severity;
        uint32_t frameIndex;
    };

    struct GPUStreamingStatistics
    {
        uint32_t queuedAssets = 0;
        uint32_t inFlightAssets = 0;
        uint64_t stagingUsedBytes = 0;
        uint64_t stagingTotalBytes = 0;
        uint32_t activeTempBuffers = 0;

        uint64_t bytesUploadedThisFrame = 0;
        uint64_t bytesUploadedTotal = 0;
        uint32_t texturesCompletedThisFrame = 0;
        uint32_t texturesCompletedTotal = 0;
        double uploadBandwidthMBps = 0.0;

        double avgLatencyMs = 0.0;
        double minLatencyMs = 0.0;
        double maxLatencyMs = 0.0;
        double p95LatencyMs = 0.0;
        uint32_t latencySampleCount = 0;

        uint64_t streamingPoolBudget = 0;
        uint64_t streamingPoolUsed = 0;
        double poolUtilizationPercent = 0.0;
        bool poolOverBudget = false;

        uint64_t totalFileReadBytes = 0;
        double avgFileReadTimeMs = 0.0;
        double avgDecodeTimeMs = 0.0;
        uint32_t pendingFileReads = 0;
        uint32_t failedLoads = 0;

        double transferThreadUtilization = 0.0;
        uint32_t batchesSubmittedTotal = 0;
        double avgBatchSizeMB = 0.0;
    };

    struct GPUFrameData
    {
        double totalFrameTimeMs = 0.0;
        std::vector<GPUTimeQuery> queries;
        GPUPipelineStatistics pipelineStats;
        GPUMemoryStatistics memoryStats;
        GPUDrawCallStatistics drawCallStats;
        GPUBandwidthStatistics bandwidthStats;
        GPUUtilizationStatistics utilizationStats;
        GPUBottleneck primaryBottleneck = GPUBottleneck::None;
        std::vector<GPUPerformanceWarning> warnings;
        struct GPUStreamingStatistics streamingStats;
    };

    class GpuTimeQueryTree
    {
    public:
        GpuTimeQueryTree() = default;
        ~GpuTimeQueryTree() = default;

        GpuTimeQueryTree(const GpuTimeQueryTree&) = delete;
        GpuTimeQueryTree& operator=(const GpuTimeQueryTree&) = delete;

        GpuTimeQueryTree(GpuTimeQueryTree&& other) noexcept;
        GpuTimeQueryTree& operator=(GpuTimeQueryTree&& other) noexcept;

        void init(uint32_t maxQueries);
        void reset();

        GPUTimeQuery* push(const char* name, uint16_t parentIndex, uint16_t depth);
        GPUTimeQuery* getQuery(uint16_t index);

        std::vector<GPUTimeQuery>& queries() { return mTimeQueries; }
        const std::vector<GPUTimeQuery>& queries() const { return mTimeQueries; }
        uint16_t allocatedCount() const { return mAllocatedTimeQuery; }
        uint16_t completedCount() const { return mCompletedTimeQuery; }

    private:
        std::vector<GPUTimeQuery> mTimeQueries;
        uint16_t mCurrentTimeQuery = 0;
        uint16_t mAllocatedTimeQuery = 0;
        uint16_t mCompletedTimeQuery = 0;
        mutable std::mutex m_mutex;
    };

    class GPUTimeQueriesManager
    {
    public:
        virtual ~GPUTimeQueriesManager() = default;

        virtual void reset() = 0;
        virtual void reset(uint32_t frameIndex) = 0;
        virtual GPUTimeQuery* pushQuery(uint32_t frameIndex, const char* name, uint16_t parentIndex, uint16_t depth) = 0;
        virtual GPUTimeQuery* pushQuery(uint32_t frameIndex, const char* name) = 0;
        virtual GPUTimeQuery* popQuery(uint32_t frameIndex) = 0;
        virtual uint16_t openDepth(uint32_t frameIndex) const = 0;

        virtual GPUTimeQuery* getQuery(uint32_t frameIndex, uint16_t queryIndex) = 0;
        virtual void resolve(uint32_t frameIndex) = 0;

        virtual std::vector<GPUTimeQuery>& getFrameQueries(uint32_t frameIndex) = 0;
        virtual const std::vector<GPUTimeQuery>& getFrameQueries(uint32_t frameIndex) const = 0;

        virtual const GPUFrameData& getFrameData(uint32_t frameIndex) const = 0;
        virtual const GPUFrameData& getLastResolvedFrameData() const = 0;
        virtual void updatePipelineStatistics(uint32_t frameIndex, const GPUPipelineStatistics& stats) = 0;
            virtual void updateMemoryStatistics(const GPUMemoryStatistics& stats) = 0;
            virtual void updateDrawCallStatistics(uint32_t frameIndex, const GPUDrawCallStatistics& stats) = 0;
            virtual void updateStreamingStatistics(uint32_t frameIndex, const GPUStreamingStatistics& stats) = 0;
                virtual void* getQueryPoolHandle(uint32_t frameIndex) = 0;
        virtual uint32_t getQueriesPerFrame() const = 0;
        virtual void resetQueryPool(rhi::RHICommandList* cmd, uint32_t frameIndex) = 0;

        virtual void beginPipelineStatisticsQuery(rhi::RHICommandList* cmd, uint32_t frameIndex) = 0;
        virtual void endPipelineStatisticsQuery(rhi::RHICommandList* cmd, uint32_t frameIndex) = 0;
        virtual bool pipelineStatisticsSupported() const = 0;

        virtual bool hasResolvedFrame() const = 0;
        virtual uint32_t lastResolvedFrameIndex() const = 0;

        static constexpr uint32_t kMaxFrames = 3;
    };
}

