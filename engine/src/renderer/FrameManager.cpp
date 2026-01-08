#include "pnkr/renderer/FrameManager.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/profiler.hpp"

namespace pnkr::renderer {

    LinearBufferAllocator::LinearBufferAllocator(RHIRenderer* renderer, rhi::BufferUsageFlags usage, rhi::MemoryUsage memUsage, uint64_t blockSize, std::string debugName)
        : m_renderer(renderer)
        , m_usage(usage)
        , m_memUsage(memUsage)
        , m_blockSize(blockSize)
        , m_debugName(std::move(debugName))
    {
    }

    LinearBufferAllocator::~LinearBufferAllocator() {
        for (auto& page : m_pages) {
            if (page.handle.isValid()) {
              if (page.mappedPtr != nullptr) {
                page.rhiBuffer->unmap();
              }
                m_renderer->destroyBuffer(page.handle.handle());
            }
        }
        m_pages.clear();
    }

    void LinearBufferAllocator::reset() {
        m_activePageIndex = 0;
        m_totalUsed = 0;
        for (auto& page : m_pages) {
            page.cursor = 0;
        }
    }

    void LinearBufferAllocator::allocateNewPage(uint64_t minSize) {
        uint64_t size = std::max(m_blockSize, minSize);

        std::string name = m_debugName + "_Page" + std::to_string(m_pages.size());

        rhi::BufferUsageFlags finalUsage = m_usage | rhi::BufferUsage::ShaderDeviceAddress;

        BufferPtr handle = m_renderer->createBuffer(name.c_str(), {
            .size = size,
            .usage = finalUsage,
            .memoryUsage = m_memUsage,
            .debugName = name
        });

        rhi::RHIBuffer* buffer = m_renderer->getBuffer(handle.handle());
        std::byte* ptr = nullptr;

        if (m_memUsage == rhi::MemoryUsage::CPUToGPU || m_memUsage == rhi::MemoryUsage::CPUOnly) {
            ptr = buffer->map();
        }

        m_pages.push_back({
            .handle = std::move(handle),
            .rhiBuffer = buffer,
            .size = size,
            .cursor = 0,
            .mappedPtr = ptr,
            .baseAddress = buffer->getDeviceAddress()
        });

        m_totalCapacity += size;
        m_activePageIndex = m_pages.size() - 1;

        core::Logger::Render.info("FrameManager: Allocated new page for '{}' ({} MB). Total pages: {}",
            m_debugName, size / (1024.0 * 1024.0), m_pages.size());
    }

    TransientAllocation LinearBufferAllocator::allocate(uint64_t size, uint64_t alignment) {
        if (m_pages.empty()) {
            allocateNewPage(size);
        }

        Page* current = &m_pages[m_activePageIndex];

        uint64_t alignedOffset = (current->cursor + (alignment - 1)) & ~(alignment - 1);

        if (alignedOffset + size > current->size) {
            if (m_activePageIndex + 1 < m_pages.size()) {
                m_activePageIndex++;
                current = &m_pages[m_activePageIndex];
                alignedOffset = (current->cursor + (alignment - 1)) & ~(alignment - 1);
            }

            if (alignedOffset + size > current->size) {
                allocateNewPage(size);
                current = &m_pages.back();
                m_activePageIndex = m_pages.size() - 1;
                alignedOffset = 0;
            }
        }

        TransientAllocation alloc;
        alloc.buffer = current->handle;
        alloc.offset = alignedOffset;
        alloc.size = size;
        alloc.deviceAddress = current->baseAddress + alignedOffset;
        if (current->mappedPtr != nullptr) {
          alloc.mappedPtr = current->mappedPtr + alignedOffset;
        }

        current->cursor = alignedOffset + size;
        m_totalUsed += size;

        return alloc;
    }

    void FrameManager::init(RHIRenderer* renderer, uint32_t framesInFlight) {
        m_renderer = renderer;
        m_frames.resize(framesInFlight);

        for (uint32_t i = 0; i < framesInFlight; ++i) {
            m_frames[i].uploadAllocator = std::make_unique<LinearBufferAllocator>(
                renderer,
                rhi::BufferUsage::UniformBuffer | rhi::BufferUsage::StorageBuffer |
                rhi::BufferUsage::IndirectBuffer | rhi::BufferUsage::TransferSrc | rhi::BufferUsage::VertexBuffer | rhi::BufferUsage::IndexBuffer,
                rhi::MemoryUsage::CPUToGPU,
                64 * 1024 * 1024,
                "Frame_Upload_" + std::to_string(i)
            );

            m_frames[i].scratchAllocator = std::make_unique<LinearBufferAllocator>(
                renderer,
                rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::IndirectBuffer |
                rhi::BufferUsage::TransferDst | rhi::BufferUsage::VertexBuffer | rhi::BufferUsage::TransferSrc,
                rhi::MemoryUsage::GPUOnly,
                32 * 1024 * 1024,
                "Frame_Scratch_" + std::to_string(i)
            );
        }
    }

    void FrameManager::shutdown() {
        m_frames.clear();
    }

    void FrameManager::beginFrame(uint32_t frameIndex) {
        m_currentFrameIndex = frameIndex % m_frames.size();
        auto& f = m_frames[m_currentFrameIndex];
        (*f.uploadAllocator).reset();
        (*f.scratchAllocator).reset();
        f.stats = {};

        f.jointMatricesBuffer = {};
        f.morphStateBuffer = {};
        f.morphStateOffset = 0;
        f.indirectOpaqueAlloc = {};
        f.indirectTransmissionAlloc = {};
        f.indirectTransparentAlloc = {};
    }

    TransientAllocation FrameManager::allocateUpload(uint64_t size, uint64_t alignment) {
        auto& f = m_frames[m_currentFrameIndex];
        return f.uploadAllocator->allocate(size, alignment);
    }

    TransientAllocation FrameManager::allocateScratch(uint64_t size, uint64_t alignment) {
        auto& f = m_frames[m_currentFrameIndex];
        return f.scratchAllocator->allocate(size, alignment);
    }

    GPUBufferSlice FrameManager::allocateUploadSlice(uint64_t size, uint64_t alignment) {
        auto alloc = allocateUpload(size, alignment);
        return makeSlice(*m_renderer, alloc.buffer, alloc.offset, alloc.size);
    }

    GPUBufferSlice FrameManager::allocateScratchSlice(uint64_t size, uint64_t alignment) {
        auto alloc = allocateScratch(size, alignment);
        return makeSlice(*m_renderer, alloc.buffer, alloc.offset, alloc.size);
    }

    DynamicUploadStats FrameManager::getStats() const {
        const auto& f = m_frames[m_currentFrameIndex];
        DynamicUploadStats s;
        s.uploadBytesUsed = f.uploadAllocator->used();
        s.uploadBytesCapacity = f.uploadAllocator->capacity();
        s.scratchBytesUsed = f.scratchAllocator->used();
        s.scratchBytesCapacity = f.scratchAllocator->capacity();
        return s;
    }

}
