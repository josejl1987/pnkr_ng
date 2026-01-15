#pragma once

#include "pnkr/rhi/rhi_types.hpp"
#include "pnkr/rhi/rhi_device.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/rhi/rhi_texture.hpp"
#include "pnkr/rhi/rhi_pipeline.hpp"
#include "pnkr/rhi/rhi_command_buffer.hpp"
#include "pnkr/core/Handle.h"
#include "pnkr/core/Pool.hpp"
#include <functional>
#include <memory>
#include <vector>
#include <span>
#include <shared_mutex>
#include <concurrentqueue/moodycamel/concurrentqueue.h>
#include "pnkr/core/StablePool.hpp"

namespace pnkr::renderer {
    class AssetManager;

    struct RHIDeferredDestruction {
        std::unique_ptr<rhi::RHIBuffer> buffer;
        std::shared_ptr<rhi::RHITexture> texture;
        std::unique_ptr<rhi::RHIPipeline> pipeline;
    };

    struct RHIMeshData {
        std::unique_ptr<rhi::RHIBuffer> m_vertexBuffer;
        std::unique_ptr<rhi::RHIBuffer> m_indexBuffer;
        uint32_t m_vertexCount;
        uint32_t m_indexCount;
        bool m_vertexPulling;

        RHIMeshData() = default;
        RHIMeshData(RHIMeshData&& other) noexcept
            : m_vertexBuffer(std::move(other.m_vertexBuffer))
            , m_indexBuffer(std::move(other.m_indexBuffer))
            , m_vertexCount(other.m_vertexCount)
            , m_indexCount(other.m_indexCount)
            , m_vertexPulling(other.m_vertexPulling) {}

        RHIMeshData& operator=(RHIMeshData&& other) noexcept {
            if (this != &other) {
                m_vertexBuffer = std::move(other.m_vertexBuffer);
                m_indexBuffer = std::move(other.m_indexBuffer);
                m_vertexCount = other.m_vertexCount;
                m_indexCount = other.m_indexCount;
                m_vertexPulling = other.m_vertexPulling;
            }
            return *this;
        }
    };

    struct RHITextureData {
        std::shared_ptr<rhi::RHITexture> texture;
        rhi::TextureBindlessHandle bindlessIndex;

        RHITextureData() = default;
        RHITextureData(RHITextureData&& other) noexcept
            : texture(std::move(other.texture))
            , bindlessIndex(other.bindlessIndex) {}

        RHITextureData& operator=(RHITextureData&& other) noexcept {
            if (this != &other) {
                texture = std::move(other.texture);
                bindlessIndex = other.bindlessIndex;
            }
            return *this;
        }
    };

    struct RHIBufferData {
        std::unique_ptr<rhi::RHIBuffer> buffer;
        rhi::BufferBindlessHandle bindlessIndex;

        RHIBufferData() = default;
        RHIBufferData(RHIBufferData&& other) noexcept
            : buffer(std::move(other.buffer))
            , bindlessIndex(other.bindlessIndex) {}

        RHIBufferData& operator=(RHIBufferData&& other) noexcept {
            if (this != &other) {
                buffer = std::move(other.buffer);
                bindlessIndex = other.bindlessIndex;
            }
            return *this;
        }
    };

    struct RHIPipelineData {
        std::unique_ptr<rhi::RHIPipeline> pipeline;

        RHIPipelineData() = default;
        RHIPipelineData(RHIPipelineData&& other) noexcept
            : pipeline(std::move(other.pipeline)) {}

        RHIPipelineData& operator=(RHIPipelineData&& other) noexcept {
            if (this != &other) {
                pipeline = std::move(other.pipeline);
            }
            return *this;
        }
    };

    class RHIResourceManager;

    template<typename T, typename Tag>
    class SmartHandle {
    public:
        SmartHandle() = default;
        SmartHandle(RHIResourceManager* manager, core::Handle<Tag> handle);

        ~SmartHandle() { releaseInternal(); }

        SmartHandle(const SmartHandle& other)
            : m_manager(other.m_manager), m_handle(other.m_handle) {
            addRefInternal();
        }

        SmartHandle& operator=(const SmartHandle& other) {
            if (this != &other) {
                releaseInternal();
                m_manager = other.m_manager;
                m_handle = other.m_handle;
                addRefInternal();
            }
            return *this;
        }

        SmartHandle(SmartHandle&& other) noexcept
            : m_manager(other.m_manager), m_handle(other.m_handle) {
            other.m_manager = nullptr;
            other.m_handle = {};
        }

        SmartHandle& operator=(SmartHandle&& other) noexcept {
            if (this != &other) {
                releaseInternal();
                m_manager = other.m_manager;
                m_handle = other.m_handle;
                other.m_manager = nullptr;
                other.m_handle = {};
            }
            return *this;
        }

        [[nodiscard]] bool isValid() const { return m_handle.isValid(); }
        [[nodiscard]] core::Handle<Tag> handle() const { return m_handle; }

        [[nodiscard]] core::Handle<Tag> release() {
            core::Handle<Tag> h = m_handle;
            m_handle = {};
            m_manager = nullptr;
            return h;
        }

        operator core::Handle<Tag>() const { return m_handle; }
        operator bool() const { return isValid(); }

        bool operator==(const SmartHandle& other) const { return m_handle == other.m_handle; }
        bool operator!=(const SmartHandle& other) const { return m_handle != other.m_handle; }

        void reset() {
            releaseInternal();
            m_handle = {};
            m_manager = nullptr;
        }

    private:
        void addRefInternal();
        void releaseInternal();

        RHIResourceManager* m_manager = nullptr;
        core::Handle<Tag> m_handle{};
    };

    using TexturePtr = SmartHandle<RHITextureData, core::TextureTag>;
    using BufferPtr = SmartHandle<RHIBufferData, core::BufferTag>;
    using MeshPtr = SmartHandle<RHIMeshData, core::MeshTag>;
    using PipelinePtr = SmartHandle<RHIPipelineData, core::PipelineTag>;

    struct ResourceStats {
        uint32_t texturesAlive = 0;
        uint32_t buffersAlive = 0;
        uint32_t meshesAlive = 0;
        uint32_t pipelinesAlive = 0;
        uint32_t texturesDeferred = 0;
        uint32_t buffersDeferred = 0;
        uint32_t pipelinesDeferred = 0;
    };

    struct DestroyEvent {
        enum class Kind : uint8_t { Texture, Buffer, Mesh, Pipeline };
        Kind kind;
        uint32_t index : 20;
        uint32_t generation : 12;

        template<typename Tag>
        static DestroyEvent fromHandle(core::Handle<Tag> handle, Kind kind) {
            return { kind, handle.index, handle.generation };
        }
    };

    class RHIResourceManager {
    public:
        explicit RHIResourceManager(rhi::RHIDevice* device, uint32_t framesInFlight);
        ~RHIResourceManager();

        ResourceStats getResourceStats() const;
        void dumpLeaks(std::ostream& out) const;
        void reportToTracy() const;

        TexturePtr createTexture(const char* name, const rhi::TextureDescriptor& desc, bool useBindless);
        TexturePtr createTextureView(const char* name, TextureHandle parent, const rhi::TextureViewDescriptor& desc, bool useBindless);
        BufferPtr createBuffer(const char* name, const rhi::BufferDescriptor& desc);
        MeshPtr createMesh(std::span<const struct Vertex> vertices, std::span<const uint32_t> indices, bool enableVertexPulling);
        PipelinePtr createGraphicsPipeline(const rhi::GraphicsPipelineDescriptor& desc);
        PipelinePtr createComputePipeline(const rhi::ComputePipelineDescriptor& desc);

        MeshPtr loadNoVertexPulling(std::span<const struct Vertex> vertices, std::span<const uint32_t> indices);
        MeshPtr loadVertexPulling(std::span<const struct Vertex> vertices, std::span<const uint32_t> indices);

        TextureHandle createWhiteTexture();
        TextureHandle createBlackTexture();
        TextureHandle createFlatNormalTexture();

        void replaceTexture(TextureHandle handle, TextureHandle source, uint32_t frameIndex, bool useBindless);

        void hotSwapPipeline(PipelineHandle handle, const rhi::GraphicsPipelineDescriptor& desc);
        void hotSwapPipeline(PipelineHandle handle, const rhi::ComputePipelineDescriptor& desc);

        void destroyTexture(TextureHandle handle, uint32_t frameIndex);
        void destroyBuffer(BufferHandle handle, uint32_t frameIndex);
        void destroyMesh(MeshHandle handle);
        void destroyPipeline(PipelineHandle handle, uint32_t frameIndex);

        void processDestroyEvents();
        void flushDeferred(uint32_t frameSlot);

        // Deprecated: use processDestroyEvents() + flushDeferred()
        void flush(uint32_t frameSlot);
        void clear();

        [[nodiscard]] rhi::RHITexture* getTexture(TextureHandle handle) const;
        [[nodiscard]] rhi::RHIBuffer* getBuffer(BufferHandle handle) const;
        [[nodiscard]] rhi::RHIPipeline* getPipeline(PipelineHandle handle) const;
        [[nodiscard]] const RHIMeshData* getMesh(MeshHandle handle) const;

        [[nodiscard]] rhi::RHITexture* getTexture(const TexturePtr& ptr) const;
        [[nodiscard]] rhi::RHIBuffer* getBuffer(const BufferPtr& ptr) const;
        [[nodiscard]] rhi::RHIPipeline* getPipeline(const PipelinePtr& ptr) const;
        [[nodiscard]] const RHIMeshData* getMesh(const MeshPtr& ptr) const;

        [[nodiscard]] bool isRenderThread() const {
            return m_renderThreadId == std::thread::id() || std::this_thread::get_id() == m_renderThreadId;
        }

        core::StablePool<RHITextureData, core::TextureTag>& textures() { return m_textures; }
        core::StablePool<RHIBufferData, core::BufferTag>& buffers() { return m_buffers; }

        void setCurrentFrameIndex(uint32_t index) { m_currentFrameIndex = index; }
        [[nodiscard]] uint32_t currentFrameIndex() const { return m_currentFrameIndex; }

        std::shared_mutex& getMutex() const { return m_mutex; }

        template<typename Tag>
        auto* getPoolSlot(core::Handle<Tag> handle) {
            if constexpr (std::is_same_v<Tag, core::TextureTag>) return m_textures.getSlotPtr(handle.index);
            else if constexpr (std::is_same_v<Tag, core::BufferTag>) return m_buffers.getSlotPtr(handle.index);
            else if constexpr (std::is_same_v<Tag, core::MeshTag>) return m_meshes.getSlotPtr(handle.index);
            else if constexpr (std::is_same_v<Tag, core::PipelineTag>) return m_pipelines.getSlotPtr(handle.index);
            else return (void*)nullptr;
        }

        template<typename Tag>
        void destroyDeferred(core::Handle<Tag> handle) {
            DestroyEvent::Kind kind;
            if constexpr (std::is_same_v<Tag, core::TextureTag>) kind = DestroyEvent::Kind::Texture;
            else if constexpr (std::is_same_v<Tag, core::BufferTag>) kind = DestroyEvent::Kind::Buffer;
            else if constexpr (std::is_same_v<Tag, core::MeshTag>) kind = DestroyEvent::Kind::Mesh;
            else if constexpr (std::is_same_v<Tag, core::PipelineTag>) kind = DestroyEvent::Kind::Pipeline;

            m_destroyQueue.enqueue(DestroyEvent::fromHandle(handle, kind));
        }

    private:
        rhi::RHIDevice* m_device;
        uint32_t m_currentFrameIndex = 0;
        std::thread::id m_renderThreadId;

        core::StablePool<RHIMeshData, core::MeshTag> m_meshes;
        core::StablePool<RHITextureData, core::TextureTag> m_textures;
        core::StablePool<RHIBufferData, core::BufferTag> m_buffers;
        core::StablePool<RHIPipelineData, core::PipelineTag> m_pipelines;

        std::vector<std::vector<RHIDeferredDestruction>> m_deferredDestructionQueues;
        moodycamel::ConcurrentQueue<DestroyEvent> m_destroyQueue;
        mutable std::shared_mutex m_mutex;
    };

    template<typename T, typename Tag>
    SmartHandle<T, Tag>::SmartHandle(RHIResourceManager* manager, core::Handle<Tag> handle)
        : m_manager(manager), m_handle(handle) {
        addRefInternal();
    }

    template<typename T, typename Tag>
    void SmartHandle<T, Tag>::addRefInternal() {
        if (m_manager && m_handle.isValid()) {
            if (auto* slot = m_manager->template getPoolSlot<Tag>(m_handle)) {
                // Lock-free validation: state must be Alive and generation must match
                // Note: getPoolSlot only returns slot if basics match, but we double-check state
                if (slot->state.load(std::memory_order_acquire) == core::SlotState::Alive && 
                    slot->generation.load(std::memory_order_acquire) == m_handle.generation) {
                    slot->refCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    }

    template<typename T, typename Tag>
    void SmartHandle<T, Tag>::releaseInternal() {
        if (m_manager && m_handle.isValid()) {
            if (auto* slot = m_manager->template getPoolSlot<Tag>(m_handle)) {
                // Relaxed check: we don't strictly need Alive for decrement, 
                // but generation MUST match to be the right resource.
                if (slot->generation.load(std::memory_order_acquire) == m_handle.generation) {
                    uint32_t val = slot->refCount.fetch_sub(1, std::memory_order_acq_rel);
                    if (val == 1) {
                        m_manager->destroyDeferred<Tag>(m_handle);
                    }
                }
            }
        }
    }

}
