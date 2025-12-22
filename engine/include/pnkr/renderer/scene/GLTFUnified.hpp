#pragma once
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/Model.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace pnkr::renderer::scene {

enum class SortingType : uint32_t {
    Opaque = 0,
    Transmission = 1,
    Transparent = 2
};

// Must match GLSL std430 packing rules.
struct GLTFTransformGPU {
    glm::mat4 model;
    glm::mat4 normalMatrix;
    uint32_t  nodeIndex;
    uint32_t  primIndex;
    uint32_t  materialIndex;
    uint32_t  sortingType;
};

struct GLTFFrameDataGPU {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 cameraPos;
};

struct GLTFUnifiedContext {
    RHIRenderer* renderer = nullptr;
    std::unique_ptr<Model> model;

    // CPU-side lists
    std::vector<GLTFTransformGPU> transforms;
    std::vector<uint32_t> opaque;
    std::vector<uint32_t> transmission;
    std::vector<uint32_t> transparent;

    bool volumetricMaterial = false;

    // GPU buffers
    BufferHandle perFrameBuffer = INVALID_BUFFER_HANDLE;
    BufferHandle transformBuffer = INVALID_BUFFER_HANDLE;
    BufferHandle materialBuffer = INVALID_BUFFER_HANDLE;
    BufferHandle environmentBuffer = INVALID_BUFFER_HANDLE;

    // Pipelines
    PipelineHandle pipelineSolid = INVALID_PIPELINE_HANDLE;
    PipelineHandle pipelineTransparent = INVALID_PIPELINE_HANDLE;

    // Optional: offscreen for screen-copy refraction
    TextureHandle offscreen[3] = { INVALID_TEXTURE_HANDLE, INVALID_TEXTURE_HANDLE, INVALID_TEXTURE_HANDLE };
    uint32_t currentOffscreen = 0;
};

void loadGLTF(GLTFUnifiedContext& ctx,
              RHIRenderer& renderer,
              const std::filesystem::path& path);

void buildTransformsList(GLTFUnifiedContext& ctx);

void sortTransparentNodes(GLTFUnifiedContext& ctx, const glm::vec3& cameraPos);

} // namespace pnkr::renderer::scene
