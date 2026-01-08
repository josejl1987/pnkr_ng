#pragma once

#include "pnkr/rhi/rhi_device.hpp"
#include "pnkr/renderer/geometry/GeometryUtils.hpp"
#include "pnkr/rhi/rhi_types.hpp"
#include "pnkr/renderer/RHIResourceManager.hpp"
#include "pnkr/renderer/gpu_shared/PhysicsShared.h"

namespace pnkr::renderer::rhi { class RHIDescriptorSetLayout; }

namespace pnkr::renderer {
    class IndirectRenderer;
    class RenderResourceManager;
    namespace physics {
        struct ClothMesh;
    }
}

namespace pnkr::renderer::physics
{

    class ClothSystem
    {
    public:
        ClothSystem();
        ~ClothSystem();

        void init(rhi::RHIDevice* device, RHIResourceManager* resourceManager);
        void shutdown();

        ClothMesh* createClothMesh(const geometry::MeshData& sourceMesh);

        void update(rhi::RHICommandList* computeCmd, float dt);

        void setWindDirection(const glm::vec3& dir) { m_sceneData.windDirection = dir; }
        glm::vec3 getWindDirection() const { return m_sceneData.windDirection; }

        void setAirDensity(float density) { m_sceneData.airDensity = density; }
        float getAirDensity() const { return m_sceneData.airDensity; }

        void setSpringStiffness(float stiffness) { m_sceneData.springStiffness = stiffness; }
        float getSpringStiffness() const { return m_sceneData.springStiffness; }

        void setSpringDamping(float damping) { m_sceneData.springDamping = damping; }
        float getSpringDamping() const { return m_sceneData.springDamping; }

        void resetSimulation() { m_sceneData.resetSimulation = 1; }

    private:
        void createPipeline();

        rhi::RHIDevice* m_device = nullptr;
        RHIResourceManager* m_resourceManager = nullptr;

        std::vector<std::unique_ptr<ClothMesh>> m_clothMeshes;

        PipelineHandle m_simulationPipeline;
        std::unique_ptr<rhi::RHIDescriptorSetLayout> m_dsl;

        gpu::PhysicsSceneData m_sceneData{};
        std::unique_ptr<rhi::RHIBuffer> m_physicsSceneBuffer;

        void updateSceneBuffer(rhi::RHICommandList* cmd);
    };
}
