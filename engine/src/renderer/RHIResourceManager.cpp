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
        : m_device(device) {
        m_deferredDestructionQueues.resize(framesInFlight);
    }

    RHIResourceManager::~RHIResourceManager() {
        clear();
    }

    ResourceStats RHIResourceManager::getResourceStats() const {
      std::scoped_lock lock(m_mutex);
      ResourceStats stats{};
      stats.texturesAlive = static_cast<uint32_t>(m_textures.size());
      stats.buffersAlive = static_cast<uint32_t>(m_buffers.size());
      stats.meshesAlive = static_cast<uint32_t>(m_meshes.size());
      stats.pipelinesAlive = static_cast<uint32_t>(m_pipelines.size());

      for (const auto &queue : m_deferredDestructionQueues) {
        for (const auto &item : queue) {
          if (item.texture) {
            stats.texturesDeferred++;
          }
          if (item.buffer) {
            stats.buffersDeferred++;
          }
          if (item.pipeline) {
            stats.pipelinesDeferred++;
          }
        }
        }
        return stats;
    }

    void RHIResourceManager::dumpLeaks(std::ostream& out) const {
#ifndef NDEBUG
      std::scoped_lock lock(m_mutex);
      bool hasLeaks = false;
      m_textures.for_each([&](const RHITextureData &data, TextureHandle h) {
        if (data.refCount.load() > 0) {
          out << "[LEAK] Texture index=" << h.index << " (gen=" << h.generation
              << ") refCount=" << data.refCount.load() << "\n";
          hasLeaks = true;
        }
      });
      m_buffers.for_each([&](const RHIBufferData &data, BufferHandle h) {
        if (data.refCount.load() > 0) {
          out << "[LEAK] Buffer index=" << h.index << " (gen=" << h.generation
              << ") refCount=" << data.refCount.load() << "\n";
          hasLeaks = true;
        }
      });
      m_meshes.for_each([&](const RHIMeshData &data, MeshHandle h) {
        if (data.refCount.load() > 0) {
          out << "[LEAK] Mesh index=" << h.index << " (gen=" << h.generation
              << ") refCount=" << data.refCount.load() << "\n";
          hasLeaks = true;
        }
      });
      m_pipelines.for_each([&](const RHIPipelineData &data, PipelineHandle h) {
        if (data.refCount.load() > 0) {
          out << "[LEAK] Pipeline index=" << h.index << " (gen=" << h.generation
              << ") refCount=" << data.refCount.load() << "\n";
          hasLeaks = true;
        }
      });

      if (!hasLeaks) {
        out << "RHIResourceManager: No leaks detected.\n";
      }
#endif
    }

    void RHIResourceManager::reportToTracy() const {
#ifdef TRACY_ENABLE
      std::scoped_lock lock(m_mutex);
      PNKR_TRACY_PLOT("RHI Textures", static_cast<int64_t>(m_textures.size()));
      PNKR_TRACY_PLOT("RHI Buffers", static_cast<int64_t>(m_buffers.size()));
      PNKR_TRACY_PLOT("RHI Meshes", static_cast<int64_t>(m_meshes.size()));
      PNKR_TRACY_PLOT("RHI Pipelines",
                      static_cast<int64_t>(m_pipelines.size()));

      uint32_t deferredTotal = 0;
      for (const auto &q : m_deferredDestructionQueues) {
        deferredTotal += static_cast<uint32_t>(q.size());
      }
      PNKR_TRACY_PLOT("RHI Deferred Destruction",
                      static_cast<int64_t>(deferredTotal));
#endif
    }

    TexturePtr RHIResourceManager::createTexture(const char* name, const rhi::TextureDescriptor& desc, bool useBindless) {
      std::scoped_lock lock(m_mutex);
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

        return { this, m_textures.emplace(std::move(texData)) };
    }

    TexturePtr RHIResourceManager::createTextureView(const char* name, TextureHandle parent, const rhi::TextureViewDescriptor& desc, bool useBindless) {
      std::scoped_lock lock(m_mutex);
      if (!m_textures.validate(parent) || !m_textures.get(parent)->texture) {
        return {};
      }
        auto parentShared = m_textures.get(parent)->texture;
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

        return { this, m_textures.emplace(std::move(texData)) };
    }

    void RHIResourceManager::replaceTexture(TextureHandle handle, TextureHandle source, uint32_t frameIndex, bool useBindless) {
      std::scoped_lock lock(m_mutex);
      if (!m_textures.validate(handle) || !m_textures.validate(source)) {
        core::Logger::Render.error("replaceTexture: invalid handle(s)");
        return;
      }

        auto* dstData = m_textures.get(handle);
        auto* srcData = m_textures.get(source);

        if ((dstData != nullptr) && (srcData != nullptr)) {
          core::Logger::Render.trace("RHIResourceManager: Replacing texture "
                                     "handle {} with content from handle {}",
                                     (uint32_t)handle.index,
                                     (uint32_t)source.index);

          rhi::TextureType oldType = rhi::TextureType::Texture2D;
          if (dstData->texture) {
            oldType = dstData->texture->type();
            if (dstData->texture.use_count() == 1) {
              dstData->texture->setBindlessHandle(
                  rhi::TextureBindlessHandle::Invalid);
            }
            const uint32_t frameSlot =
                frameIndex %
                static_cast<uint32_t>(m_deferredDestructionQueues.size());
            m_deferredDestructionQueues[frameSlot].push_back(
                {.buffer = nullptr,
                 .texture = std::move(dstData->texture),
                 .pipeline = nullptr});
          }

          dstData->texture = srcData->texture;

          if (useBindless && dstData->bindlessIndex.isValid()) {
            auto *bindless = m_device->getBindlessManager();
            if (bindless != nullptr) {
              rhi::TextureType newType = dstData->texture
                                             ? dstData->texture->type()
                                             : rhi::TextureType::Texture2D;

              if (oldType != newType) {
                core::Logger::Render.warn(
                    "RHIResourceManager: Texture type changed ({} -> {}) for "
                    "handle {}. Re-allocating bindless.",
                    (int)oldType, (int)newType, (uint32_t)handle.index);

                if (oldType == rhi::TextureType::TextureCube) {
                  bindless->releaseCubemap(dstData->bindlessIndex);
                } else {
                  bindless->releaseTexture(dstData->bindlessIndex);
                }

                if (newType == rhi::TextureType::TextureCube) {
                  dstData->bindlessIndex =
                      bindless->registerCubemapImage(dstData->texture.get());
                } else {
                  dstData->bindlessIndex =
                      bindless->registerTexture2D(dstData->texture.get());
                }

                core::Logger::Render.info(
                    "RHIResourceManager: Re-allocated bindless index: {}",
                    dstData->bindlessIndex.index());
              } else {
                core::Logger::Render.trace(
                    "RHIResourceManager: Updating bindless index {} for "
                    "texture handle {}",
                    dstData->bindlessIndex.index(), (uint32_t)handle.index);
                bindless->updateTexture(dstData->bindlessIndex,
                                        dstData->texture.get());
              }
              if (dstData->texture) {
                dstData->texture->setBindlessHandle(dstData->bindlessIndex);
              }
            }
          } else {
            if (useBindless) {
              core::Logger::Render.warn(
                  "RHIResourceManager: Texture replaced but bindless update "
                  "skipped (Index Invalid). Handle: {}",
                  (uint32_t)handle.index);
            }
          }
        }
    }

    BufferPtr RHIResourceManager::createBuffer(const char* name, const rhi::BufferDescriptor& desc) {
        RHIBufferData bufferData{};
        bufferData.buffer = m_device->createBuffer(name, desc);
        bufferData.bindlessIndex = bufferData.buffer->getBindlessHandle();
        return { this, m_buffers.emplace(std::move(bufferData)) };
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

        return { this, m_meshes.emplace(std::move(mesh)) };
    }

    PipelinePtr RHIResourceManager::createGraphicsPipeline(const rhi::GraphicsPipelineDescriptor& desc) {
        RHIPipelineData data{};
        data.pipeline = m_device->createGraphicsPipeline(desc);
        return { this, m_pipelines.emplace(std::move(data)) };
    }

    PipelinePtr RHIResourceManager::createComputePipeline(const rhi::ComputePipelineDescriptor& desc) {
        RHIPipelineData data{};
        data.pipeline = m_device->createComputePipeline(desc);
        return { this, m_pipelines.emplace(std::move(data)) };
    }

    MeshPtr RHIResourceManager::loadNoVertexPulling(std::span<const struct Vertex> vertices, std::span<const uint32_t> indices) {
        return createMesh(vertices, indices, false);
    }

    MeshPtr RHIResourceManager::loadVertexPulling(std::span<const struct Vertex> vertices, std::span<const uint32_t> indices) {
        return createMesh(vertices, indices, true);
    }

    void RHIResourceManager::destroyTexture(TextureHandle handle, uint32_t frameIndex) {
      if (!m_textures.validate(handle)) {
        return;
      }
        auto* data = m_textures.get(handle);

        if (data->texture) {
            const uint32_t frameSlot = frameIndex % static_cast<uint32_t>(m_deferredDestructionQueues.size());
            m_deferredDestructionQueues[frameSlot].push_back({ .buffer = nullptr, .texture = std::move(data->texture), .pipeline = nullptr });
        }
        m_textures.erase(handle);
    }

    void RHIResourceManager::destroyBuffer(BufferHandle handle, uint32_t frameIndex) {
      if (!m_buffers.validate(handle)) {
        return;
      }
        auto* data = m_buffers.get(handle);
        if (data->buffer) {
            const uint32_t frameSlot = frameIndex % static_cast<uint32_t>(m_deferredDestructionQueues.size());
            m_deferredDestructionQueues[frameSlot].push_back({ .buffer = std::move(data->buffer), .texture = nullptr, .pipeline = nullptr });
        }
        m_buffers.erase(handle);
    }

    void RHIResourceManager::destroyMesh(MeshHandle handle) {
      if (!m_meshes.validate(handle)) {
        return;
      }
        m_meshes.erase(handle);
    }

    void RHIResourceManager::flush(uint32_t frameSlot) {
        uint32_t slot = frameSlot % static_cast<uint32_t>(m_deferredDestructionQueues.size());
        m_deferredDestructionQueues[slot].clear();
    }

    void RHIResourceManager::clear() {
        for (auto& queue : m_deferredDestructionQueues) {
            queue.clear();
        }

        m_meshes.clear();
        m_textures.clear();
        m_buffers.clear();
        m_pipelines.clear();
    }

    rhi::RHITexture* RHIResourceManager::getTexture(TextureHandle handle) const {
      if (!m_textures.validate(handle)) {
        return nullptr;
      }
        return m_textures.get(handle)->texture.get();
    }

    rhi::RHIBuffer* RHIResourceManager::getBuffer(BufferHandle handle) const {
      if (!m_buffers.validate(handle)) {
        return nullptr;
      }
        return m_buffers.get(handle)->buffer.get();
    }

    rhi::RHIPipeline* RHIResourceManager::getPipeline(PipelineHandle handle) const {
      if (!m_pipelines.validate(handle)) {
        return nullptr;
      }
        return m_pipelines.get(handle)->pipeline.get();
    }

    const RHIMeshData* RHIResourceManager::getMesh(MeshHandle handle) const {
      if (!m_meshes.validate(handle)) {
        return nullptr;
      }
        return m_meshes.get(handle);
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
        if (!m_pipelines.validate(handle)) {
            core::Logger::Render.error("Invalid pipeline handle for hot-swap");
            return;
        }

        auto* slot = m_pipelines.get(handle);

        if (slot->pipeline) {
             const uint32_t frameSlot = m_currentFrameIndex % static_cast<uint32_t>(m_deferredDestructionQueues.size());
             m_deferredDestructionQueues[frameSlot].push_back({ .buffer = nullptr, .texture = nullptr, .pipeline = std::move(slot->pipeline) });
        }

        slot->pipeline = m_device->createGraphicsPipeline(desc);

        core::Logger::Render.trace("Pipeline {} hot-swapped", (uint32_t)handle.index);
    }

    void RHIResourceManager::hotSwapPipeline(PipelineHandle handle, const rhi::ComputePipelineDescriptor& desc) {
        if (!m_pipelines.validate(handle)) {
            core::Logger::Render.error("Invalid pipeline handle for hot-swap");
            return;
        }

        auto* slot = m_pipelines.get(handle);

        if (slot->pipeline) {
             const uint32_t frameSlot = m_currentFrameIndex % static_cast<uint32_t>(m_deferredDestructionQueues.size());
             m_deferredDestructionQueues[frameSlot].push_back({ .buffer = nullptr, .texture = nullptr, .pipeline = std::move(slot->pipeline) });
        }

        slot->pipeline = m_device->createComputePipeline(desc);

        core::Logger::Render.trace("Pipeline {} hot-swapped", (uint32_t)handle.index);
    }

}
