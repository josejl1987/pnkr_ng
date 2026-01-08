#include "pnkr/renderer/physics/ClothSystem.hpp"
#include "pnkr/renderer/physics/ClothMesh.hpp"
#include "pnkr/renderer/RHIResourceManager.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/renderer/gpu_shared/PhysicsShared.h"
#include "pnkr/renderer/passes/RenderPassUtils.hpp"

namespace pnkr::renderer::physics {

    ClothSystem::ClothSystem() = default;
    ClothSystem::~ClothSystem() {
        shutdown();
    }

    void ClothSystem::init(RHIDevice* device, RHIResourceManager* resourceManager) {
        m_device = device;
        m_resourceManager = resourceManager;

        createPipeline();

        m_physicsSceneBuffer = m_device->createBuffer("PhysicsSceneBuffer", {
            .size = sizeof(gpu::PhysicsSceneData),
            .usage = rhi::BufferUsage::UniformBuffer | rhi::BufferUsage::TransferDst,
            .memoryUsage = rhi::MemoryUsage::CPUToGPU,
            .debugName = "PhysicsSceneBuffer"
        });

        m_sceneData.windDirection = glm::vec3(1.0F, 0.0F, 0.0F);
        m_sceneData.airDensity = 1.0F;
        m_sceneData.springStiffness = 500.0F;
        m_sceneData.springDamping = 0.5F;
        m_sceneData.resetSimulation = 0;
    }

    void ClothSystem::shutdown() {
      if (m_simulationPipeline.isValid() && (m_resourceManager != nullptr)) {
        m_resourceManager->destroyDeferred(m_simulationPipeline);
        m_simulationPipeline = {};
      }
        m_clothMeshes.clear();
        m_physicsSceneBuffer.reset();
        m_dsl.reset();
    }

    void ClothSystem::createPipeline() {
        rhi::DescriptorSetLayout dslDesc{};
        dslDesc.bindings = {
            {.binding = 0,
             .type = rhi::DescriptorType::UniformBuffer,
             .count = 1,
             .stages = rhi::ShaderStage::Compute,
             .name = "Scene"},
            {.binding = 1,
             .type = rhi::DescriptorType::StorageBuffer,
             .count = 1,
             .stages = rhi::ShaderStage::Compute,
             .name = "Vertices"},
            {.binding = 2,
             .type = rhi::DescriptorType::StorageBuffer,
             .count = 1,
             .stages = rhi::ShaderStage::Compute,
             .name = "Positions"},
            {.binding = 3,
             .type = rhi::DescriptorType::StorageBuffer,
             .count = 1,
             .stages = rhi::ShaderStage::Compute,
             .name = "Normals"},
            {.binding = 4,
             .type = rhi::DescriptorType::StorageBuffer,
             .count = 1,
             .stages = rhi::ShaderStage::Compute,
             .name = "Indices"},
        };

        m_dsl = m_device->createDescriptorSetLayout(dslDesc);

        rhi::RHIPipelineBuilder builder;
        auto shader = rhi::Shader::load(rhi::ShaderStage::Compute, "shaders/cloth_simulation.spv");

        if (!shader) {
            return;
        }

        m_simulationPipeline = m_resourceManager->createComputePipeline(
            builder.setComputeShader(shader.get())
                   .setDescriptorSetLayouts({m_dsl->description()})
                   .setName("ClothSimulation")
                   .buildCompute()
        );
    }

    ClothMesh* ClothSystem::createClothMesh(const geometry::MeshData& sourceMesh) {
        auto mesh = std::make_unique<ClothMesh>();
        mesh->create(m_device, m_resourceManager, sourceMesh);

        mesh->descriptorSet = m_device->allocateDescriptorSet(m_dsl.get());

        auto* ds = mesh->descriptorSet.get();
        if (ds != nullptr) {
          ds->updateBuffer(0, m_physicsSceneBuffer.get(), 0,
                           sizeof(gpu::PhysicsSceneData));
          ds->updateBuffer(1, mesh->physicsVertexBuffer.get(), 0,
                           mesh->physicsVertexBuffer->size());
          ds->updateBuffer(2, mesh->positionBuffer.get(), 0,
                           mesh->positionBuffer->size());
          ds->updateBuffer(3, mesh->normalBuffer.get(), 0,
                           mesh->normalBuffer->size());
          ds->updateBuffer(4, mesh->indexBuffer.get(), 0,
                           mesh->indexBuffer->size());
        }

        ClothMesh* ptr = mesh.get();
        m_clothMeshes.push_back(std::move(mesh));
        return ptr;
    }

    void ClothSystem::updateSceneBuffer(rhi::RHICommandList * ) {
      m_physicsSceneBuffer->uploadData(
          {reinterpret_cast<const std::byte *>(&m_sceneData),
           sizeof(gpu::PhysicsSceneData)});
      if (m_sceneData.resetSimulation != 0u) {
        m_sceneData.resetSimulation = 0;
      }
    }

    void ClothSystem::update(rhi::RHICommandList *cmd, float ) {
      if (m_clothMeshes.empty() || !m_simulationPipeline.isValid()) {
        return;
      }

      using namespace pnkr::renderer::passes::utils;
      ScopedGpuMarker scope(cmd, "Cloth Simulation");

      updateSceneBuffer(cmd);

      cmd->pipelineBarrier(rhi::ShaderStage::Transfer,
                           rhi::ShaderStage::Compute,
                           {{.buffer = m_physicsSceneBuffer.get(),
                             .srcAccessStage = rhi::ShaderStage::Transfer,
                             .dstAccessStage = rhi::ShaderStage::Compute}});

      cmd->bindPipeline(m_resourceManager->getPipeline(m_simulationPipeline));

      for (auto &mesh : m_clothMeshes) {
        cmd->bindDescriptorSet(0, mesh->descriptorSet.get());

        struct PushConstants {
          uint32_t m_vertexCount;
          uint32_t m_indexCount;
        } push{};
        push.m_vertexCount = mesh->vertexCount;
        push.m_indexCount = mesh->indexCount;

        cmd->pushConstants(rhi::ShaderStage::Compute, push);

        uint32_t groupSize = 32;
        cmd->dispatch((push.m_vertexCount + groupSize - 1) / groupSize, 1, 1);
      }
    }
}
