#include "pnkr/renderer/RHIResourceManager.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/rhi/BindlessManager.hpp"
#include "pnkr/renderer/AssetManager.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/core/profiler.hpp"
#include <iostream>

namespace pnkr::renderer {

    namespace {
        struct GPUVertexInstance {
          glm::vec4 m_pos;
          glm::vec4 m_color;
          glm::vec4 m_normal;
          glm::vec4 m_uv;
          glm::vec4 m_tangent;
        };
    }

    RHIResourceManager::RHIResourceManager(rhi::RHIDevice* device, uint32_t framesInFlight)
        : m_device(device), m_renderThreadId(std::this_thread::get_id()) {
        m_deferredDestructionQueues.resize(framesInFlight);
        m_meshes.setRenderThreadId(m_renderThreadId);
        m_textures.setRenderThreadId(m_renderThreadId);
        m_buffers.setRenderThreadId(m_renderThreadId);
        m_pipelines.setRenderThreadId(m_renderThreadId);
    }

    RHIResourceManager::~RHIResourceManager() {
        clear();
    }

    ResourceStats RHIResourceManager::getResourceStats() const {
      ResourceStats stats{};
      stats.texturesAlive = m_textures.size();
      stats.buffersAlive = m_buffers.size();
      stats.meshesAlive = m_meshes.size();
      stats.pipelinesAlive = m_pipelines.size();

      for (const auto &queue : m_deferredDestructionQueues) {
        for (const auto &item : queue) {
          if (item.texture) stats.texturesDeferred++;
          if (item.buffer) stats.buffersDeferred++;
          if (item.pipeline) stats.pipelinesDeferred++;
        }
      }
      return stats;
    }

    void RHIResourceManager::dumpLeaks(std::ostream& out) const {
#ifndef NDEBUG
      bool hasLeaks = false;
      m_textures.for_each([&](const RHITextureData &, TextureHandle h) {
        if (auto* slot = m_textures.getSlotPtr(h.index)) {
             uint32_t rc = slot->refCount.load();
             if (rc > 0) {
                 out << "[LEAK] Texture index=" << h.index << " (gen=" << h.generation << ") refCount=" << rc << "\n";
                 hasLeaks = true;
             }
        }
      });
      // Similar for buffers, meshes, pipelines... 
      // Process all other resource types to check for leaks

      m_buffers.for_each([&](const RHIBufferData &, BufferHandle h) {
        if (auto* slot = m_buffers.getSlotPtr(h.index)) {
             uint32_t rc = slot->refCount.load();
             if (rc > 0) {
                 out << "[LEAK] Buffer index=" << h.index << " (gen=" << h.generation << ") refCount=" << rc << "\n";
                 hasLeaks = true;
             }
        }
      });
      m_meshes.for_each([&](const RHIMeshData &, MeshHandle h) {
        if (auto* slot = m_meshes.getSlotPtr(h.index)) {
             uint32_t rc = slot->refCount.load();
             if (rc > 0) {
                 out << "[LEAK] Mesh index=" << h.index << " (gen=" << h.generation << ") refCount=" << rc << "\n";
                 hasLeaks = true;
             }
        }
      });
      m_pipelines.for_each([&](const RHIPipelineData &, PipelineHandle h) {
        if (auto* slot = m_pipelines.getSlotPtr(h.index)) {
             uint32_t rc = slot->refCount.load();
             if (rc > 0) {
                 out << "[LEAK] Pipeline index=" << h.index << " (gen=" << h.generation << ") refCount=" << rc << "\n";
                 hasLeaks = true;
             }
        }
      });

      if (!hasLeaks) {
        out << "RHIResourceManager: No leaks detected.\n";
      }
#endif
    }

    void RHIResourceManager::reportToTracy() const {
#ifdef TRACY_ENABLE
      PNKR_TRACY_PLOT("RHI Textures", static_cast<int64_t>(m_textures.size()));
      PNKR_TRACY_PLOT("RHI Buffers", static_cast<int64_t>(m_buffers.size()));
      PNKR_TRACY_PLOT("RHI Meshes", static_cast<int64_t>(m_meshes.size()));
      PNKR_TRACY_PLOT("RHI Pipelines", static_cast<int64_t>(m_pipelines.size()));

      uint32_t deferredTotal = 0;
      for (const auto &q : m_deferredDestructionQueues) {
        deferredTotal += static_cast<uint32_t>(q.size());
      }
      PNKR_TRACY_PLOT("RHI Deferred Destruction", static_cast<int64_t>(deferredTotal));
#endif
    }

    TexturePtr RHIResourceManager::createTexture(const char* name, const rhi::TextureDescriptor& desc, bool useBindless) {
      auto textureUnique = m_device->createTexture(name, desc);
      std::shared_ptr<rhi::RHITexture> texture = std::move(textureUnique);

      if (useBindless && !desc.skipBindless &&
          desc.usage.has(rhi::TextureUsage::Sampled)) {
        rhi::TextureBindlessHandle bindlessHandle{};
        auto *bindless = m_device->getBindlessManager();
        if (bindless != nullptr) {
          if (desc.sampleCount > 1) {
            bindlessHandle = bindless->registerMSTexture2D(texture.get());
          } else if (desc.type == rhi::TextureType::TextureCube) {
            bindlessHandle = bindless->registerCubemapImage(texture.get());
          } else {
            bindlessHandle = bindless->registerTexture2D(texture.get());
          }
          texture->setBindlessHandle(bindlessHandle);
        }
      }

      RHITextureData texData{};
      texData.texture = std::move(texture);
      texData.bindlessIndex = texData.texture->getBindlessHandle();

      PNKR_ASSERT(isRenderThread(), "Must be called on Render Thread");
      uint32_t index = 0;
      auto handle = m_textures.emplace(std::move(texData));
      return { this, handle };
    }

    TexturePtr RHIResourceManager::createTextureView(const char* name, TextureHandle parent, const rhi::TextureViewDescriptor& desc, bool useBindless) {
      PNKR_ASSERT(isRenderThread(), "Must be called on Render Thread");
      auto* parentData = m_textures.get(parent);
      if (!parentData || !parentData->texture) {
        return {};
      }
      auto parentShared = parentData->texture;

        auto* parentTex = parentShared.get();

        auto textureUnique = m_device->createTextureView(name, parentTex, desc);
        std::shared_ptr<rhi::RHITexture> texture = std::move(textureUnique);
        texture->setParent(parentShared);

        if (useBindless) {
            auto* bindless = m_device->getBindlessManager();
            if (bindless != nullptr) {
              auto bindlessHandle = bindless->registerTexture2D(texture.get());
              texture->setBindlessHandle(bindlessHandle);
            }
        }

        RHITextureData texData{};
        texData.texture = std::move(texture);
        texData.bindlessIndex = texData.texture->getBindlessHandle();

        PNKR_ASSERT(isRenderThread(), "Must be called on Render Thread");
        auto handle = m_textures.emplace(std::move(texData));
        return { this, handle };
    }

    void RHIResourceManager::replaceTexture(TextureHandle handle, TextureHandle source, uint32_t frameIndex, bool useBindless) {
      PNKR_ASSERT(isRenderThread(), "Must be called on Render Thread");
      auto* srcData = m_textures.get(source);
      auto* dstData = m_textures.get(handle);
      if (!srcData || !dstData) {
          core::Logger::Render.error("replaceTexture: invalid handle(s)");
          return;
      }
      
      auto srcTexture = srcData->texture;
      auto oldTexture = std::move(dstData->texture);
      auto oldBindlessIndex = dstData->bindlessIndex;
      dstData->texture = srcTexture;

      if (oldTexture) {
          if (oldTexture.use_count() == 1) {
              oldTexture->setBindlessHandle(rhi::TextureBindlessHandle::Invalid);
          }
          const uint32_t frameSlot = frameIndex % static_cast<uint32_t>(m_deferredDestructionQueues.size());
          m_deferredDestructionQueues[frameSlot].push_back({ .buffer = nullptr, .texture = std::move(oldTexture), .pipeline = nullptr });
      }

      if (useBindless && oldBindlessIndex.isValid()) {
          auto* bindless = m_device->getBindlessManager();
          if (bindless != nullptr) {
              rhi::TextureType oldType = oldTexture ? oldTexture->type() : rhi::TextureType::Texture2D;
              rhi::TextureType newType = srcTexture ? srcTexture->type() : rhi::TextureType::Texture2D;

              if (oldType != newType) {
                  if (oldType == rhi::TextureType::TextureCube) {
                      bindless->releaseCubemap(oldBindlessIndex);
                  } else {
                      bindless->releaseTexture(oldBindlessIndex);
                  }

                  rhi::TextureBindlessHandle newBindless;
                  if (newType == rhi::TextureType::TextureCube) {
                      newBindless = bindless->registerCubemapImage(srcTexture.get());
                  } else {
                      newBindless = bindless->registerTexture2D(srcTexture.get());
                  }

                  if (auto* dstData = m_textures.get(handle)) {
                      dstData->bindlessIndex = newBindless;
                      if (dstData->texture) {
                          dstData->texture->setBindlessHandle(newBindless);
                      }
                  }
              } else {
                  bindless->updateTexture(oldBindlessIndex, srcTexture.get());
                  if (srcTexture) {
                      srcTexture->setBindlessHandle(oldBindlessIndex);
                  }
              }
          }
      }
    }

    BufferPtr RHIResourceManager::createBuffer(const char* name, const rhi::BufferDescriptor& desc) {
        RHIBufferData bufferData{};
        bufferData.buffer = m_device->createBuffer(name, desc);
        bufferData.bindlessIndex = bufferData.buffer->getBindlessHandle();
        
        PNKR_ASSERT(isRenderThread(), "Must be called on Render Thread");
        core::Logger::Render.trace("RHIResourceManager::createBuffer: {}", name ? name : "unnamed");
        auto handle = m_buffers.emplace(std::move(bufferData));
        core::Logger::Render.trace("RHIResourceManager::createBuffer: Created handle {}:{}", (uint32_t)handle.index, (uint32_t)handle.generation);
        return { this, handle };
    }

    MeshPtr RHIResourceManager::createMesh(std::span<const Vertex> vertices, std::span<const uint32_t> indices, bool enableVertexPulling) {
        RHIMeshData mesh{};
        mesh.m_vertexPulling = enableVertexPulling;

        if (enableVertexPulling) {
            std::vector<GPUVertexInstance> gpuVertices;
            gpuVertices.reserve(vertices.size());
            for (const auto& v : vertices) {
                GPUVertexInstance gv{};
                gv.m_pos = v.position;
                gv.m_color = v.color;
                gv.m_normal = v.normal;
                gv.m_tangent = v.tangent;
                gv.m_uv.x = v.uv0.x;
                gv.m_uv.y = v.uv0.y;
                gv.m_uv.z = v.uv1.x;
                gv.m_uv.w = v.uv1.y;
                gpuVertices.push_back(gv);
            }

            uint64_t vertexBufferSize = gpuVertices.size() * sizeof(GPUVertexInstance);
            mesh.m_vertexBuffer = m_device->createBuffer("VertexPullingVertexBuffer", {
                .size = vertexBufferSize,
                .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress | rhi::BufferUsage::TransferDst,
                .memoryUsage = rhi::MemoryUsage::GPUOnly,
                .data = gpuVertices.data(),
                .debugName = "VertexPullingVertexBuffer"
            });
        } else {
            uint64_t vertexBufferSize = vertices.size() * sizeof(Vertex);
            mesh.m_vertexBuffer = m_device->createBuffer("VertexBuffer", {
                .size = vertexBufferSize,
                .usage = rhi::BufferUsage::VertexBuffer | rhi::BufferUsage::TransferDst | rhi::BufferUsage::ShaderDeviceAddress,
                .memoryUsage = rhi::MemoryUsage::GPUOnly,
                .data = vertices.data(),
                .debugName = "VertexBuffer"
            });
        }

        uint64_t indexBufferSize = indices.size() * sizeof(uint32_t);
        mesh.m_indexBuffer = m_device->createBuffer("IndexBuffer", {
            .size = indexBufferSize,
            .usage = rhi::BufferUsage::IndexBuffer | rhi::BufferUsage::TransferDst | rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = rhi::MemoryUsage::GPUOnly,
            .data = indices.data(),
            .debugName = "IndexBuffer"
        });

        mesh.m_vertexCount = static_cast<uint32_t>(vertices.size());
        mesh.m_indexCount = static_cast<uint32_t>(indices.size());

        PNKR_ASSERT(isRenderThread(), "Must be called on Render Thread");
        auto handle = m_meshes.emplace(std::move(mesh));
        return { this, handle };
    }

    PipelinePtr RHIResourceManager::createGraphicsPipeline(const rhi::GraphicsPipelineDescriptor& desc) {
        PNKR_ASSERT(isRenderThread(), "Must be called on Render Thread");
        RHIPipelineData data{};
        data.pipeline = m_device->createGraphicsPipeline(desc);
        
        auto handle = m_pipelines.emplace(std::move(data));
        return { this, handle };
    }

    PipelinePtr RHIResourceManager::createComputePipeline(const rhi::ComputePipelineDescriptor& desc) {
        PNKR_ASSERT(isRenderThread(), "Must be called on Render Thread");
        RHIPipelineData data{};
        data.pipeline = m_device->createComputePipeline(desc);
        
        auto handle = m_pipelines.emplace(std::move(data));
        return { this, handle };
    }

    MeshPtr RHIResourceManager::loadNoVertexPulling(std::span<const struct Vertex> vertices, std::span<const uint32_t> indices) {
        return createMesh(vertices, indices, false);
    }

    MeshPtr RHIResourceManager::loadVertexPulling(std::span<const struct Vertex> vertices, std::span<const uint32_t> indices) {
        return createMesh(vertices, indices, true);
    }

    void RHIResourceManager::destroyTexture(TextureHandle handle, uint32_t frameIndex) {
        PNKR_ASSERT(isRenderThread(), "Must be called on Render Thread");
        auto* slot = m_textures.getSlotPtr(handle.index);
        if (!slot) return;
        
        // Use acquire here to ensure we see the latest T
        if (slot->state.load(std::memory_order_acquire) == core::SlotState::Alive && 
            slot->generation.load(std::memory_order_relaxed) == handle.generation) {
            
            auto& data = *slot->get();
            const uint32_t frameSlot = frameIndex % static_cast<uint32_t>(m_deferredDestructionQueues.size());
            m_deferredDestructionQueues[frameSlot].push_back({ .buffer = nullptr, .texture = std::move(data.texture), .pipeline = nullptr });
            
            m_textures.retire(handle);
            m_textures.freeSlot(handle.index);
        }
    }

    void RHIResourceManager::destroyBuffer(BufferHandle handle, uint32_t frameIndex) {
        PNKR_ASSERT(isRenderThread(), "Must be called on Render Thread");
        auto* slot = m_buffers.getSlotPtr(handle.index);
        if (!slot) return;

        if (slot->state.load(std::memory_order_acquire) == core::SlotState::Alive && 
            slot->generation.load(std::memory_order_relaxed) == handle.generation) {
            
            auto& data = *slot->get();
            const uint32_t frameSlot = frameIndex % static_cast<uint32_t>(m_deferredDestructionQueues.size());
            m_deferredDestructionQueues[frameSlot].push_back({ .buffer = std::move(data.buffer), .texture = nullptr, .pipeline = nullptr });
            
            m_buffers.retire(handle);
            m_buffers.freeSlot(handle.index);
        }
    }

    void RHIResourceManager::destroyMesh(MeshHandle handle) {
        PNKR_ASSERT(isRenderThread(), "Must be called on Render Thread");
        auto* slot = m_meshes.getSlotPtr(handle.index);
        if (!slot) return;

        if (slot->state.load(std::memory_order_acquire) == core::SlotState::Alive && 
            slot->generation.load(std::memory_order_relaxed) == handle.generation) {
            
            m_meshes.retire(handle);
            m_meshes.freeSlot(handle.index);
        }
    }

    void RHIResourceManager::destroyPipeline(PipelineHandle handle, uint32_t frameIndex) {
        PNKR_ASSERT(isRenderThread(), "Must be called on Render Thread");
        auto* slot = m_pipelines.getSlotPtr(handle.index);
        if (!slot) return;

        if (slot->state.load(std::memory_order_acquire) == core::SlotState::Alive && 
            slot->generation.load(std::memory_order_relaxed) == handle.generation) {
            
            auto& data = *slot->get();
            const uint32_t frameSlot = frameIndex % static_cast<uint32_t>(m_deferredDestructionQueues.size());
            m_deferredDestructionQueues[frameSlot].push_back({ .buffer = nullptr, .texture = nullptr, .pipeline = std::move(data.pipeline) });
            
            m_pipelines.retire(handle);
            m_pipelines.freeSlot(handle.index);
        }
    }

    void RHIResourceManager::processDestroyEvents() {
        DestroyEvent event;
        while (m_destroyQueue.try_dequeue(event)) {
            switch (event.kind) {
                case DestroyEvent::Kind::Texture:
                    destroyTexture(TextureHandle(event.index, event.generation), m_currentFrameIndex);
                    break;
                case DestroyEvent::Kind::Buffer:
                    destroyBuffer(BufferHandle(event.index, event.generation), m_currentFrameIndex);
                    break;
                case DestroyEvent::Kind::Mesh:
                    destroyMesh(MeshHandle(event.index, event.generation));
                    break;
                case DestroyEvent::Kind::Pipeline:
                    destroyPipeline(PipelineHandle(event.index, event.generation), m_currentFrameIndex);
                    break;
            }
        }
    }

    void RHIResourceManager::flushDeferred(uint32_t frameSlot) {
        PNKR_ASSERT(isRenderThread(), "Must be called on Render Thread");
        if (m_deferredDestructionQueues.empty()) return;
        uint32_t slot = frameSlot % static_cast<uint32_t>(m_deferredDestructionQueues.size());
        auto toDestroy = std::move(m_deferredDestructionQueues[slot]);
        m_deferredDestructionQueues[slot].clear();
        // unique_ptrs/shared_ptrs in toDestroy will be destroyed here on Render Thread
    }

    void RHIResourceManager::flush(uint32_t frameSlot) {
        // Fix: Flush the deferred queue *before* processing new events.
        // If we process events first, the newly added items (which must live for FramesInFlight frames)
        // would be immediately flushed because they land in 'frameSlot'.
        // By flushing first, we clear items from 'FramesInFlight' frames ago.
        flushDeferred(frameSlot);
        processDestroyEvents();
    }

    void RHIResourceManager::clear() {
        PNKR_ASSERT(isRenderThread(), "Must be called on Render Thread");
        processDestroyEvents();

        for (auto& queue : m_deferredDestructionQueues) {
            queue.clear();
        }
        
        m_textures.clear();
        m_buffers.clear();
        m_meshes.clear();
        m_pipelines.clear();
    }

    rhi::RHITexture* RHIResourceManager::getTexture(TextureHandle handle) const {
        if (!handle.isValid()) return nullptr;
        // Not strictly thread-safe without owning handle, but valid for Render Thread
        auto* slot = m_textures.getSlotPtr(handle.index);
        if (slot && slot->state.load(std::memory_order_acquire) == core::SlotState::Alive && 
            slot->generation.load(std::memory_order_relaxed) == handle.generation) {
            return slot->get()->texture.get();
        }
        return nullptr;
    }

    rhi::RHIBuffer* RHIResourceManager::getBuffer(BufferHandle handle) const {
        if (!handle.isValid()) return nullptr;
        auto* slot = m_buffers.getSlotPtr(handle.index);
        if (slot && slot->state.load(std::memory_order_acquire) == core::SlotState::Alive && 
            slot->generation.load(std::memory_order_relaxed) == handle.generation) {
            return slot->get()->buffer.get();
        }
        return nullptr;
    }

    rhi::RHIPipeline* RHIResourceManager::getPipeline(PipelineHandle handle) const {
        if (!handle.isValid()) return nullptr;
        auto* slot = m_pipelines.getSlotPtr(handle.index);
        if (slot && slot->state.load(std::memory_order_acquire) == core::SlotState::Alive && 
            slot->generation.load(std::memory_order_relaxed) == handle.generation) {
            return slot->get()->pipeline.get();
        }
        return nullptr;
    }

    const RHIMeshData* RHIResourceManager::getMesh(MeshHandle handle) const {
        if (!handle.isValid()) return nullptr;
        auto* slot = m_meshes.getSlotPtr(handle.index);
        if (slot && slot->state.load(std::memory_order_acquire) == core::SlotState::Alive && 
            slot->generation.load(std::memory_order_relaxed) == handle.generation) {
            return slot->get();
        }
        return nullptr;
    }

    rhi::RHITexture* RHIResourceManager::getTexture(const TexturePtr& ptr) const {
        return getTexture(ptr.handle());
    }

    rhi::RHIBuffer* RHIResourceManager::getBuffer(const BufferPtr& ptr) const {
        return getBuffer(ptr.handle());
    }

    rhi::RHIPipeline* RHIResourceManager::getPipeline(const PipelinePtr& ptr) const {
        return getPipeline(ptr.handle());
    }

    const RHIMeshData* RHIResourceManager::getMesh(const MeshPtr& ptr) const {
        return getMesh(ptr.handle());
    }

    TextureHandle RHIResourceManager::createWhiteTexture() {
        rhi::TextureDescriptor desc{};
        desc.extent = {.width = 1, .height = 1, .depth = 1};
        desc.format = rhi::Format::R8G8B8A8_UNORM;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst;
        auto ptr = createTexture("WhiteTexture", desc, true);
        TextureHandle h = ptr.release();

        uint32_t data = BINDLESS_INVALID_ID;
        getTexture(h)->uploadData(std::as_bytes(std::span(&data, 1)));

        return h;
    }

    TextureHandle RHIResourceManager::createBlackTexture() {
        rhi::TextureDescriptor desc{};
        desc.extent = {.width = 1, .height = 1, .depth = 1};
        desc.format = rhi::Format::R8G8B8A8_UNORM;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst;
        auto ptr = createTexture("BlackTexture", desc, true);
        TextureHandle h = ptr.release();

        uint32_t data = 0xFF000000;
        getTexture(h)->uploadData(std::as_bytes(std::span(&data, 1)));

        return h;
    }

    TextureHandle RHIResourceManager::createFlatNormalTexture() {
        rhi::TextureDescriptor desc{};
        desc.extent = {.width = 1, .height = 1, .depth = 1};
        desc.format = rhi::Format::R8G8B8A8_UNORM;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst;
        auto ptr = createTexture("FlatNormalTexture", desc, true);
        TextureHandle h = ptr.release();

        uint32_t data = 0xFFFF8080;
        getTexture(h)->uploadData(std::as_bytes(std::span(&data, 1)));

        return h;
    }

    void RHIResourceManager::hotSwapPipeline(PipelineHandle handle, const rhi::GraphicsPipelineDescriptor& desc) {
        PNKR_ASSERT(isRenderThread(), "Must be called on Render Thread");
        auto newPipeline = m_device->createGraphicsPipeline(desc);

        auto* slot = m_pipelines.get(handle);
        if (!slot) {
            core::Logger::Render.error("Invalid pipeline handle for hot-swap");
            return;
        }

        if (slot->pipeline) {
             const uint32_t frameSlot = m_currentFrameIndex % static_cast<uint32_t>(m_deferredDestructionQueues.size());
             m_deferredDestructionQueues[frameSlot].push_back({ .buffer = nullptr, .texture = nullptr, .pipeline = std::move(slot->pipeline) });
        }

        slot->pipeline = std::move(newPipeline);
        core::Logger::Render.trace("Pipeline {} hot-swapped", (uint32_t)handle.index);
    }

    void RHIResourceManager::hotSwapPipeline(PipelineHandle handle, const rhi::ComputePipelineDescriptor& desc) {
        PNKR_ASSERT(isRenderThread(), "Must be called on Render Thread");
        auto newPipeline = m_device->createComputePipeline(desc);

        auto* slot = m_pipelines.get(handle);
        if (!slot) {
            core::Logger::Render.error("Invalid pipeline handle for hot-swap");
            return;
        }

        if (slot->pipeline) {
             const uint32_t frameSlot = m_currentFrameIndex % static_cast<uint32_t>(m_deferredDestructionQueues.size());
             m_deferredDestructionQueues[frameSlot].push_back({ .buffer = nullptr, .texture = nullptr, .pipeline = std::move(slot->pipeline) });
        }

        slot->pipeline = std::move(newPipeline);
        core::Logger::Render.trace("Pipeline {} hot-swapped", (uint32_t)handle.index);
    }

}
