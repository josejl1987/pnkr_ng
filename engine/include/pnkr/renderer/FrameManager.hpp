#pragma once

#include "pnkr/core/common.hpp"
#include "pnkr/core/Handle.h"
#include "pnkr/rhi/rhi_types.hpp"
#include "pnkr/renderer/GPUBufferSlice.hpp"
#include <vector>
#include <memory>
#include <mutex>
#include <string>

namespace pnkr::renderer {

    class RHIRenderer;

    struct DynamicUploadStats
    {
        uint64_t uploadBytesUsed = 0;
        uint64_t uploadBytesCapacity = 0;
        uint64_t scratchBytesUsed = 0;
        uint64_t scratchBytesCapacity = 0;
    };

    struct TransientAllocation {
        BufferPtr buffer;
        uint64_t offset = 0;
        uint64_t size = 0;
        std::byte* mappedPtr = nullptr;
        uint64_t deviceAddress = 0;

        bool isValid() const { return buffer.isValid() && size > 0; }
    };

    class LinearBufferAllocator {
    public:
        LinearBufferAllocator(RHIRenderer* renderer, rhi::BufferUsageFlags usage, rhi::MemoryUsage memUsage, uint64_t blockSize, std::string debugName);
        ~LinearBufferAllocator();

        void reset();
        TransientAllocation allocate(uint64_t size, uint64_t alignment);
        uint64_t capacity() const { return m_totalCapacity; }
        uint64_t used() const { return m_totalUsed; }

    private:
        struct Page {
            BufferPtr handle;
            rhi::RHIBuffer* rhiBuffer;
            uint64_t size;
            uint64_t cursor;
            std::byte* mappedPtr;
            uint64_t baseAddress;
        };

        void allocateNewPage(uint64_t minSize);

        RHIRenderer* m_renderer;
        rhi::BufferUsageFlags m_usage;
        rhi::MemoryUsage m_memUsage;
        uint64_t m_blockSize;
        std::string m_debugName;

        std::vector<Page> m_pages;
        size_t m_activePageIndex = 0;

        uint64_t m_totalCapacity = 0;
        uint64_t m_totalUsed = 0;
    };

    struct PerFrameBuffers {
        std::unique_ptr<LinearBufferAllocator> uploadAllocator;
        std::unique_ptr<LinearBufferAllocator> scratchAllocator;
        DynamicUploadStats stats;

        BufferPtr jointMatricesBuffer;
        BufferPtr morphStateBuffer;
        uint64_t morphStateOffset = 0;
        uint64_t morphStateDeviceAddr = 0;
        uint64_t jointMatricesDeviceAddr = 0;

        TransientAllocation indirectOpaqueAlloc;
        TransientAllocation indirectOpaqueDoubleSidedAlloc;
        TransientAllocation indirectTransmissionAlloc;
        TransientAllocation indirectTransmissionDoubleSidedAlloc;
        TransientAllocation indirectTransparentAlloc;

        GPUBufferSlice indirectOpaqueBuffer;
        GPUBufferSlice indirectOpaqueDoubleSidedBuffer;
        GPUBufferSlice indirectTransmissionBuffer;
        GPUBufferSlice indirectTransmissionDoubleSidedBuffer;
        GPUBufferSlice indirectTransparentBuffer;

        GPUBufferSlice opaqueCompactedSlice;
        GPUBufferSlice opaqueDoubleSidedCompactedSlice;

        BufferPtr gpuWorldBounds;
        BufferPtr gpuWorldBoundsDoubleSided;
        BufferPtr skinnedVertexBuffer;
        BufferPtr shadowTransformBuffer;
        void* mappedShadowData = nullptr;
        BufferPtr meshXformBuffer;
    };

    class FrameManager {
    public:
        FrameManager() = default;
        ~FrameManager() = default;

        void init(RHIRenderer* renderer, uint32_t framesInFlight);
        void shutdown();

        void beginFrame(uint32_t frameIndex);

        TransientAllocation allocateUpload(uint64_t size, uint64_t alignment = 16);

        TransientAllocation allocateScratch(uint64_t size, uint64_t alignment = 16);

        GPUBufferSlice allocateUploadSlice(uint64_t size, uint64_t alignment = 16);
        GPUBufferSlice allocateScratchSlice(uint64_t size, uint64_t alignment = 16);

        DynamicUploadStats getStats() const;
        DynamicUploadStats getDynamicUploadStats() const { return getStats(); }
        uint32_t getCurrentFrameIndex() const { return m_currentFrameIndex; }

        PerFrameBuffers& getCurrentFrameBuffers() { return m_frames[m_currentFrameIndex]; }
        PerFrameBuffers& getCurrentFrame() { return getCurrentFrameBuffers(); }

    private:
        RHIRenderer* m_renderer = nullptr;
        std::vector<PerFrameBuffers> m_frames;
        uint32_t m_currentFrameIndex = 0;
    };

}
