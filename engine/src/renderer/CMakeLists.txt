# ============================================================================
# Renderer Module
# ============================================================================

target_sources(pnkr_engine
  PRIVATE
    # Main renderer files
    AssetManager.cpp
    AsyncLoader.cpp
    AsyncLoaderStagingManager.cpp
    BRDFLutGenerator.cpp
    CommandListPool.cpp
    FrameManager.cpp
    GlobalResourcePool.cpp
    IndirectRenderer.cpp
    IndirectPipeline.cpp
    rhi_renderer.cpp
    RHIDeviceContext.cpp
    RenderDevice.cpp
    RenderContext.cpp
    RHIResourceManager.cpp
    RHISwapchainManager.cpp
    RHIPipelineCache.cpp
    ktx_utils.cpp
    SystemMeshes.cpp
    ShaderHotReloader.cpp
    ShaderCompiler.cpp
    ShaderCache.cpp

    # Debug
    debug/DebugLayer.cpp
    debug/DebugUtils.cpp
    debug/LineCanvas3D.cpp

    # Environment
    environment/EnvironmentProcessor.cpp
    environment/EnvironmentUploader.cpp

    # Framegraph
    framegraph/FrameGraph.cpp
    framegraph/BarrierSolver.cpp
    framegraph/FrameGraphResourcePool.cpp

    # Geometry
    geometry/GeometryUtils.cpp

    # IO
    io/GLTFLoader.cpp
    io/ModelSerializer.cpp
    io/ModelUploader.cpp

    # Lighting
    lighting/LightUploader.cpp

    # Material
    material/GlobalMaterialHeap.cpp

    # Passes
    passes/CullingPass.cpp
    passes/GeometryPass.cpp
    passes/OITPass.cpp
    passes/PostProcessPass.cpp
    passes/RenderPassUtils.cpp
    passes/SSAOPass.cpp
    passes/ShadowPass.cpp
    passes/TransmissionPass.cpp

    # Profiling
    profiling/gpu_profiler.cpp
    profiling/gpu_profiler_imgui.cpp

    # Physics
    physics/ClothSystem.cpp
    physics/ClothMesh.cpp

    # Scene
    scene/AnimationSystem.cpp
    scene/Bounds.cpp
    scene/GLTFUnifiedDOD.cpp
    scene/GLTFUtils.cpp
    scene/InfiniteGrid.cpp
    scene/ModelDOD.cpp
    scene/RenderBatcher.cpp
    scene/SceneAssetDatabase.cpp
    scene/SceneBufferPacker.cpp
    scene/SceneGraph.cpp
    scene/SceneUploader.cpp
    scene/Skybox.cpp
    scene/SpriteRenderer.cpp
    scene/SpriteSystem.cpp
    SceneUniformProvider.cpp
    TextureStreamer.cpp

    # Skinning
    skinning/GlobalJointBuffer.cpp

    # UI
    ui/imgui_layer.cpp

    # Utils
    utils/FullScreenPass.cpp

  PUBLIC FILE_SET headers BASE_DIRS "${CMAKE_SOURCE_DIR}/engine/include" FILES
    # Main renderer headers
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/app/Application.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/engine.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/AssetManager.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/AsyncLoader.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/AsyncLoaderStagingManager.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/BRDFLutGenerator.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/CommandListPool.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/FrameManager.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/GPUBufferSlice.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/GlobalResourcePool.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/IndirectDrawContext.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/IndirectPipeline.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/IndirectRenderer.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/IndirectUtils.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/RenderPipeline.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/RenderDevice.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/RenderContext.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/RenderResourceManager.h"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/RenderSettings.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/SceneUniformProvider.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/SystemMeshes.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/UploadSlice.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/TextureStreamer.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/ktx_utils.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/rhi_renderer.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/RHIDeviceContext.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/RHIResourceManager.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/RHISwapchainManager.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/RHIPipelineCache.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/shader_payload_helpers.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/ShaderHotReloader.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/ShaderCompiler.hpp"

    # Debug
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/debug/DebugLayer.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/debug/DebugUtils.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/debug/LineCanvas3D.hpp"

    # Environment
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/environment/EnvironmentProcessor.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/environment/EnvironmentUploader.hpp"

    # Framegraph
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/framegraph/FrameGraph.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/framegraph/BarrierSolver.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/framegraph/FGTypes.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/framegraph/FrameGraphResourcePool.hpp"

    # Geometry
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/geometry/Frustum.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/geometry/GeometryUtils.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/geometry/Vertex.h"

    # GPU Shared Structures
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/gpu_shared/CullingShared.h"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/gpu_shared/GridShared.h"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/gpu_shared/LightingCameraShared.h"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/gpu_shared/LineCanvasShared.h"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/gpu_shared/OITShared.h"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/gpu_shared/PostProcessShared.h"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/gpu_shared/SceneShared.h"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/gpu_shared/SkinningShared.h"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/gpu_shared/SkyboxShared.h"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/gpu_shared/SlangCppBridge.h"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/gpu_shared/SpriteShared.h"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/gpu_shared/VertexShared.h"

    # IO
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/io/GLTFLoader.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/io/ModelSerializer.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/io/ModelUploader.hpp"

    # Lighting
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/lighting/LightUploader.hpp"

    # Material
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/material/GlobalMaterialHeap.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/material/Material.hpp"

    # Passes
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/passes/CullingPass.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/passes/GeometryPass.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/passes/IRenderPass.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/passes/OITPass.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/passes/PostProcessPass.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/passes/RenderPassUtils.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/passes/SSAOPass.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/passes/ShadowPass.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/passes/TransmissionPass.hpp"

    # Profiling
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/profiling/gpu_profiler.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/profiling/gpu_profiler_imgui.hpp"

    # Renderer Config
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/renderer_config.hpp"

    # Scene
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/Animation.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/AnimationSystem.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/Bounds.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/Camera.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/CameraController.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/Components.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/GLTFUnifiedDOD.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/GLTFUtils.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/GltfCamera.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/InfiniteGrid.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/Light.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/MaterialPipelineMap.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/MaterialType.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/ModelAsset.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/ModelDOD.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/RenderBatcher.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/SceneAssetDatabase.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/SceneBufferPacker.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/SceneState.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/Node.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/SceneGraph.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/SceneUploader.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/Skybox.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/Sprite.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/SpriteRenderer.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/SpriteSystem.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/scene/transform.hpp"

    # Skinning
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/skinning/GlobalJointBuffer.hpp"

    # UI
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/ui/imgui_layer.hpp"

    # Utils
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/utils/FullScreenPass.hpp"
)
