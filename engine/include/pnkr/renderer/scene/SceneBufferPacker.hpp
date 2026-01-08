#pragma once

#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/SceneAssetDatabase.hpp"
#include "pnkr/renderer/scene/SceneGraph.hpp"
#include "pnkr/renderer/scene/ModelDOD.hpp"

namespace pnkr::renderer::scene
{
    class SceneBufferPacker
    {
    public:
        static void uploadMaterials(RHIRenderer& renderer, 
                                    const SceneAssetDatabase& assets, 
                                    BufferPtr& outBuffer);
                                    
        static void uploadEnvironment(RHIRenderer& renderer,
                                      TextureHandle env, 
                                      TextureHandle irr, 
                                      TextureHandle brdf,
                                      BufferPtr& outBuffer);
                                      
        static uint32_t uploadLights(RHIRenderer& renderer,
                                 const SceneGraphDOD& scene,
                                 BufferPtr& outBuffer);
                                 
        // Also move the Indirect Buffer upload helper here or similar utility
        static void uploadIndirectCommands(RHIRenderer& renderer,
                                           const char* debugName,
                                           const void* data,
                                           uint32_t count,
                                           uint32_t stride,
                                           BufferPtr& outBuffer);

        static void uploadTransforms(RHIRenderer& renderer,
                                     const void* data,
                                     uint32_t count,
                                     BufferPtr& outBuffer);
    };
}
