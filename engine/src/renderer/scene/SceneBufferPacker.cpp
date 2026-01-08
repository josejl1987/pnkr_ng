#include "pnkr/renderer/scene/SceneBufferPacker.hpp"
#include "pnkr/renderer/scene/SceneUploader.hpp"
#include "pnkr/renderer/gpu_shared/SceneShared.h"
#include <vector>
#include <span>

namespace pnkr::renderer::scene
{
    void SceneBufferPacker::uploadMaterials(RHIRenderer& renderer, 
                                            const SceneAssetDatabase& assets, 
                                            BufferPtr& outBuffer)
    {
        auto gpuData = SceneUploader::packMaterials(assets.materials(), renderer);

        const uint64_t bytes = gpuData.size() * sizeof(gpu::MaterialDataGPU);
        if (!outBuffer.isValid())
        {
            outBuffer = renderer.createBuffer("GLTF_Materials", {
                .size = bytes,
                .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                .debugName = "GLTF DOD Materials"
            });
        }

        renderer.getBuffer(outBuffer.handle())->uploadData(std::as_bytes(std::span(gpuData)));
    }

    void SceneBufferPacker::uploadEnvironment(RHIRenderer& renderer,
                                              TextureHandle env, 
                                              TextureHandle irr, 
                                              TextureHandle brdf,
                                              BufferPtr& outBuffer)
    {
      auto gpuEnv =
          SceneUploader::packEnvironment(renderer, env, irr, brdf, 1.0F);

      if (!outBuffer.isValid()) {
        outBuffer = renderer.createBuffer(
            "GLTF_Environment", {.size = sizeof(gpu::EnvironmentMapDataGPU),
                                 .usage = rhi::BufferUsage::UniformBuffer |
                                          rhi::BufferUsage::ShaderDeviceAddress,
                                 .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                                 .debugName = "GLTF Environment"});
      }

        renderer.getBuffer(outBuffer.handle())->uploadData(std::as_bytes(std::span(&gpuEnv, 1)));
    }

    uint32_t SceneBufferPacker::uploadLights(RHIRenderer& renderer,
                                         const SceneGraphDOD& scene,
                                         BufferPtr& outBuffer)
    {
        auto lightResult = SceneUploader::packLights(scene);
        uint32_t activeLightCount = util::u32(lightResult.lights.size());

        if (activeLightCount == 0) {
          return 0;
        }

        const uint64_t bytes = lightResult.lights.size() * sizeof(gpu::LightDataGPU);
        if (!outBuffer.isValid())
        {
            outBuffer = renderer.createBuffer("GLTF_Lights", {
                .size = bytes,
                .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                .debugName = "GLTF Lights"
            });
        }

        renderer.getBuffer(outBuffer.handle())->uploadData(std::as_bytes(std::span(lightResult.lights)));
        return activeLightCount;
    }

    void SceneBufferPacker::uploadIndirectCommands(RHIRenderer& renderer,
                                                   const char* debugName,
                                                   const void* data,
                                                   uint32_t count,
                                                   uint32_t stride,
                                                   BufferPtr& outBuffer)
    {
        const uint64_t bytes = static_cast<uint64_t>(count) * stride;
        if (bytes == 0) {
            return;
        }

        if (!outBuffer.isValid() ||
            renderer.getBuffer(outBuffer.handle())->size() < bytes) {
            if (outBuffer.isValid()) {
                renderer.destroyBuffer(outBuffer.handle());
            }

            outBuffer = renderer.createBuffer(debugName, {
                .size = bytes,
                .usage = rhi::BufferUsage::IndirectBuffer,
                .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                .debugName = debugName
            });
        }

        // We assume data is a contiguous array of 'stride' size elements
        renderer.getBuffer(outBuffer.handle())->uploadData(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(data), bytes));
    }

    void SceneBufferPacker::uploadTransforms(RHIRenderer& renderer,
                                             const void* data,
                                             uint32_t count,
                                             BufferPtr& outBuffer)
    {
        const uint64_t xfBytes = static_cast<uint64_t>(count) * sizeof(gpu::InstanceData);
        if (!outBuffer.isValid() ||
            renderer.getBuffer(outBuffer.handle())->size() < xfBytes)
        {
            if (outBuffer.isValid()) {
                renderer.destroyBuffer(outBuffer.handle());
            }

            outBuffer = renderer.createBuffer("GLTF_TransformsBuffer", {
                .size = xfBytes,
                .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                .debugName = "GLTF Transforms DOD"
            });
        }
        renderer.getBuffer(outBuffer.handle())->uploadData(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(data), xfBytes));
    }
}
