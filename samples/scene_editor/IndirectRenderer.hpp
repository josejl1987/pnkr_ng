#pragma once

#include "pnkr/engine.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/ModelDOD.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include <glm/glm.hpp>

using namespace pnkr;
using namespace pnkr::renderer;

namespace indirect {

    // Matches VkDrawIndexedIndirectCommand (20 bytes)
    struct IndirectCommand {
        uint32_t indexCount;
        uint32_t instanceCount;
        uint32_t firstIndex;
        int32_t  vertexOffset;
        uint32_t firstInstance; // We use this as an index into DrawInstanceData
    };

    // Data fetched per draw call via BDA
    struct DrawInstanceData {
        uint32_t transformIndex; // Index into global transform array
        uint32_t materialIndex;  // Index into material array
        uint32_t _pad0;
        uint32_t _pad1;
    };

    // Push Constants for the pipeline
    struct PushConstants {
        glm::mat4 viewProj;
        uint64_t transformBufferAddr; // BDA
        uint64_t instanceBufferAddr;  // BDA
        uint64_t vertexBufferAddr;    // BDA
        uint64_t materialBufferAddr;  // BDA
    };

    class IndirectRenderer {
    public:
        void init(RHIRenderer* renderer, std::shared_ptr<scene::ModelDOD> model);
        
        // Updates
        void updateGlobalTransforms(); // Re-uploads the entire transform buffer
        void updateMaterial(uint32_t materialIndex); // Partial update of one material
        
        // Drawing
        void draw(rhi::RHICommandBuffer* cmd, const scene::Camera& camera);
        void setWireframe(bool enabled);

    private:
        void createPipeline();
        void buildBuffers();
        void uploadMaterialData();

        RHIRenderer* m_renderer = nullptr;
        std::shared_ptr<scene::ModelDOD> m_model;

        // GPU Resources
        PipelineHandle m_pipeline = INVALID_PIPELINE_HANDLE;
        PipelineHandle m_pipelineWireframe = INVALID_PIPELINE_HANDLE;
        
        // Buffers
        BufferHandle m_indirectBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle m_instanceBuffer = INVALID_BUFFER_HANDLE; 
        BufferHandle m_transformBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle m_materialBuffer = INVALID_BUFFER_HANDLE;

        uint32_t m_drawCount = 0;
        bool m_drawWireframe = false;
    };

} // namespace indirect
