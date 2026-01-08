#pragma once
#include "pnkr/rhi/rhi_command_buffer.hpp"
#include "pnkr/renderer/framegraph/FrameGraph.hpp"
#include "pnkr/renderer/scene/SceneGraph.hpp"
#include "pnkr/renderer/RenderSettings.hpp"
#include "pnkr/renderer/FrameManager.hpp"
#include "pnkr/renderer/scene/ModelDOD.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/GLTFUnifiedDOD.hpp"
#include <functional>
 
namespace pnkr::renderer {
 
    class ShaderHotReloader;
 
    struct RenderGraphResources {
        TextureHandle sceneColor = INVALID_TEXTURE_HANDLE;
        TextureHandle sceneDepth = INVALID_TEXTURE_HANDLE;
        TextureHandle msaaColor = INVALID_TEXTURE_HANDLE;
        TextureHandle msaaDepth = INVALID_TEXTURE_HANDLE;
        TextureHandle shadowMap = INVALID_TEXTURE_HANDLE;
        TextureHandle ssaoOutput = INVALID_TEXTURE_HANDLE;
        TextureHandle transmissionTexture = INVALID_TEXTURE_HANDLE;
 
        GPUBufferSlice opaqueCompactedSlice{};
        GPUBufferSlice opaqueDoubleSidedCompactedSlice{};
        BufferHandle transmissionCompactedBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle transparentCompactedBuffer = INVALID_BUFFER_HANDLE;
 
        TextureHandle brdfLut = INVALID_TEXTURE_HANDLE;
        TextureHandle irradianceMap = INVALID_TEXTURE_HANDLE;
        TextureHandle prefilterMap = INVALID_TEXTURE_HANDLE;
        TextureHandle skyboxCubemap = INVALID_TEXTURE_HANDLE;
 
        rhi::ResourceLayout sceneColorLayout = rhi::ResourceLayout::Undefined;
        rhi::ResourceLayout sceneDepthLayout = rhi::ResourceLayout::Undefined;
        rhi::ResourceLayout msaaColorLayout = rhi::ResourceLayout::Undefined;
        rhi::ResourceLayout msaaDepthLayout = rhi::ResourceLayout::Undefined;
        rhi::ResourceLayout shadowLayout = rhi::ResourceLayout::Undefined;
        rhi::ResourceLayout ssaoLayout = rhi::ResourceLayout::Undefined;
        rhi::ResourceLayout transmissionLayout = rhi::ResourceLayout::Undefined;
 
        uint32_t shadowMapBindlessIndex = 0xFFFFFFFF;
        int shadowCasterIndex = -1;
        uint32_t effectiveMsaaSamples = 1;
        GPUBufferSlice shadowIndirectOpaqueBuffer;
        GPUBufferSlice shadowIndirectOpaqueDoubleSidedBuffer;
        const scene::DrawLists* drawLists = nullptr;
    };
 
    struct RenderPassContext {
        rhi::RHICommandList* cmd;
        const scene::ModelDOD* model;
        const scene::Camera* camera;
        const scene::Camera* mainCamera;
        PerFrameBuffers& frameBuffers;
        FrameManager& frameManager;
        RenderGraphResources& resources;
        const RenderSettings& settings;
        uint32_t viewportWidth;
        uint32_t viewportHeight;
        uint32_t frameIndex;
        uint32_t msaaSamples = 1;
        float dt;
        std::function<void(rhi::RHICommandList*)> uiRender;
        glm::mat4 cullingViewProj = glm::mat4(1.0f);
        uint64_t cameraDataAddr = 0;
        uint64_t sceneDataAddr = 0;
        uint64_t transformAddr = 0;
        uint64_t lightAddr = 0;
        uint32_t lightCount = 0;
        uint64_t materialAddr = 0;
        uint64_t environmentAddr = 0;
        uint64_t shadowDataAddr = 0;
        uint64_t instanceXformAddr = 0;
 
        const FrameGraphResources* fg = nullptr;
        FGHandle fgSceneColorCopy{};
        FGHandle fgDepthResolved{};
        FGHandle fgSSAORaw{};
        FGHandle fgSSAOBlur{};
        FGHandle fgOITHeads{};
        FGHandle fgPPBright{};
        FGHandle fgPPLuminance{};
        FGHandle fgPPBloom0{};
        FGHandle fgPPBloom1{};
        FGHandle fgPPMeteredLum{};
        FGHandle fgPPAdaptedLum{};
        FGHandle fgPPPrevAdaptedLum{};
        FGHandle fgSceneColor{};
        FGHandle fgSceneDepth{};
        FGHandle fgMsaaColor{};
        FGHandle fgMsaaDepth{};
        FGHandle fgShadowMap{};
 
        scene::GLTFUnifiedDODContext dodContext;
        scene::GLTFUnifiedDODContext shadowDodContext;
    };
 
    class IRenderPass {
    public:
        virtual ~IRenderPass() = default;
 
        virtual void init(RHIRenderer* renderer, uint32_t width, uint32_t height,
                          ShaderHotReloader* hotReloader) = 0;
        virtual void resize(uint32_t width, uint32_t height, const MSAASettings& msaa) = 0;
        virtual void execute(const RenderPassContext& ctx) = 0;
        virtual const char* getName() const = 0;
    };
}
