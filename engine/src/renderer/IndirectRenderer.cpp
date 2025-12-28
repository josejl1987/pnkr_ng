#include "pnkr/renderer/IndirectRenderer.hpp"

#include "generated/indirect.frag.h"
#include "generated/indirect.vert.h"
#include "generated/skinning.comp.h"

#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/renderer/scene/GLTFUnifiedDOD.hpp"
#include "pnkr/renderer/scene/AnimationSystem.hpp"
#include "pnkr/renderer/geometry/GeometryUtils.hpp"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace pnkr::renderer
{
    using namespace pnkr::renderer::scene;

    struct MeshXformGPU
    {
        glm::mat4 invModel;
        glm::mat4 normalWorldToLocal;
    };

    IndirectRenderer::~IndirectRenderer()
    {
        if (m_renderer && m_shadowMapBindlessIndex != 0xFFFFFFFF)
        {
            m_renderer->device()->releaseBindlessShadowTexture2D({m_shadowMapBindlessIndex});
        }
        if (m_renderer && m_shadowMapDebugBindlessIndex != 0xFFFFFFFF)
        {
            m_renderer->device()->releaseBindlessTexture({m_shadowMapDebugBindlessIndex});
        }
        for (auto& frame : m_frames)
        {
            frame.lightBuffer = INVALID_BUFFER_HANDLE;
            frame.shadowDataBuffer = INVALID_BUFFER_HANDLE;
            frame.mappedLights = nullptr;
            frame.lightCount = 0;
        }
    }

    void IndirectRenderer::init(RHIRenderer* renderer, std::shared_ptr<scene::ModelDOD> model,
                                TextureHandle brdf, TextureHandle irradiance, TextureHandle prefilter)
    {
        m_renderer = renderer;
        m_model = model;

        if (m_renderer && m_model)
        {
            // Primitives are now automatically added by ModelDOD::load
        }

        m_skinOffsets.clear();
        uint32_t offset = 0;
        for (const auto& skin : m_model->skins())
        {
            m_skinOffsets.push_back(offset);
            offset += (uint32_t)skin.joints.size();
        }

        createComputePipeline();

        uint32_t flightCount = m_renderer->getSwapchain()->framesInFlight();
        m_frames.resize(flightCount);

        const uint32_t meshCount = (uint32_t)m_model->meshes().size();
        const uint32_t xformCount = (meshCount > 0) ? meshCount : 1u;

        // Allocate skinned buffer (same size as source)
        uint64_t skinnedVertexBufferSize = 0;
        if (m_model->vertexBuffer != INVALID_BUFFER_HANDLE)
        {
            skinnedVertexBufferSize = m_renderer->getBuffer(m_model->vertexBuffer)->size();
        }

        // Initialize per-frame resources
        for (auto& frame : m_frames)
        {
            if (skinnedVertexBufferSize > 0)
            {
                frame.skinnedVertexBuffer = m_renderer->createBuffer({
                    .size = skinnedVertexBufferSize,
                    .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::VertexBuffer |
                    rhi::BufferUsage::ShaderDeviceAddress,
                    .memoryUsage = rhi::MemoryUsage::GPUOnly,
                    .debugName = "SkinnedVertexBuffer"
                });
            }

            frame.meshXformsBuffer = m_renderer->createBuffer({
                .size = sizeof(MeshXformGPU) * xformCount,
                .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                .debugName = "MeshXformsBuffer"
            });
            frame.mappedMeshXforms = m_renderer->getBuffer(frame.meshXformsBuffer)->map();
        }

        createPipeline();

        // Shadow resources
        if (m_shadowMap != INVALID_TEXTURE_HANDLE && m_shadowMapBindlessIndex != 0xFFFFFFFF)
        {
            m_renderer->device()->releaseBindlessShadowTexture2D({m_shadowMapBindlessIndex});
            m_shadowMapBindlessIndex = 0xFFFFFFFF;
        }
        if (m_shadowMap != INVALID_TEXTURE_HANDLE && m_shadowMapDebugBindlessIndex != 0xFFFFFFFF)
        {
            m_renderer->device()->releaseBindlessTexture({m_shadowMapDebugBindlessIndex});
            m_shadowMapDebugBindlessIndex = 0xFFFFFFFF;
        }

        rhi::TextureDescriptor shadowDesc{};
        shadowDesc.extent = {m_shadowDim, m_shadowDim, 1};
        shadowDesc.format = rhi::Format::D32_SFLOAT;
        shadowDesc.usage = rhi::TextureUsage::DepthStencilAttachment | rhi::TextureUsage::Sampled;
        shadowDesc.debugName = "ShadowMap";
        m_shadowMap = m_renderer->createTexture(shadowDesc);
        m_shadowLayout = rhi::ResourceLayout::Undefined;

        if (m_renderer->isBindlessEnabled())
        {
            auto* shadowTex = m_renderer->getTexture(m_shadowMap);
            m_shadowMapBindlessIndex = m_renderer->device()->registerBindlessShadowTexture2D(shadowTex).index;
            m_shadowMapDebugBindlessIndex = m_renderer->device()->registerBindlessTexture2D(shadowTex).index;
        }

        for (auto& frame : m_frames)
        {
            frame.shadowDataBuffer = m_renderer->createBuffer({
                .size = sizeof(ShaderGen::indirect_frag::ShadowDataGPU),
                .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                .debugName = "ShadowDataBuffer"
            });
            frame.mappedShadowData = m_renderer->getBuffer(frame.shadowDataBuffer)->map();
        }

        auto shadowVert = rhi::Shader::load(rhi::ShaderStage::Vertex, "shaders/shadow.vert.spv");
        auto shadowFrag = rhi::Shader::load(rhi::ShaderStage::Fragment, "shaders/shadow.frag.spv");

        rhi::RHIPipelineBuilder shadowBuilder;
        shadowBuilder.setShaders(shadowVert.get(), shadowFrag.get())
                     .setTopology(rhi::PrimitiveTopology::TriangleList)
                     .setPolygonMode(rhi::PolygonMode::Fill)
                     .setDepthBiasEnable(true)
                     .enableDepthTest(true, rhi::CompareOp::LessOrEqual)
                     .setDynamicStates({
                         rhi::DynamicState::Viewport, rhi::DynamicState::Scissor, rhi::DynamicState::DepthBias
                     })
                     .setDepthFormat(rhi::Format::D32_SFLOAT)
                     .setColorFormats({})
                     .setName("ShadowPass");

        m_shadowPipeline = m_renderer->createGraphicsPipeline(shadowBuilder.buildGraphics());
        // Note: We don't call buildBuffers() here anymore; GLTFUnifiedDOD handles it in draw()

        // Initial upload of static data
        uploadMaterialData();
        uploadEnvironmentData(brdf, irradiance, prefilter);

        // Init offscreen targets
        resize(m_renderer->getSwapchain()->extent().width, m_renderer->getSwapchain()->extent().height);
    }

    void IndirectRenderer::createOffscreenResources(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0) return;

        // 1. Scene Color (High precision if needed, but matching swapchain for now)
        rhi::TextureDescriptor descRT{};
        descRT.extent = {width, height, 1};
        descRT.format = m_renderer->getSwapchainColorFormat();
        descRT.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::TransferSrc | rhi::TextureUsage::Sampled;
        descRT.mipLevels = 1;
        descRT.debugName = "SceneColor";
        m_sceneColor = m_renderer->createTexture(descRT);
        m_sceneColorLayout = rhi::ResourceLayout::Undefined;

        // 2. Transmission Copy (Needs mips for roughness blur)
        rhi::TextureDescriptor descCopy{};
        descCopy.extent = descRT.extent;
        descCopy.format = descRT.format;
        descCopy.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst | rhi::TextureUsage::TransferSrc;
        descCopy.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
        descCopy.debugName = "TransmissionCopy";
        m_transmissionTexture = m_renderer->createTexture(descCopy);
        m_transmissionLayout = rhi::ResourceLayout::Undefined;
    }

    void IndirectRenderer::resize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0) return;
        if (m_width == width && m_height == height && m_sceneColor != INVALID_TEXTURE_HANDLE) return;

        m_width = width;
        m_height = height;
        createOffscreenResources(width, height);
    }

    void IndirectRenderer::createComputePipeline()
    {
        auto comp = rhi::Shader::load(rhi::ShaderStage::Compute, "shaders/skinning.comp.spv");

        rhi::RHIPipelineBuilder builder;
        builder.setComputeShader(comp.get());
        m_skinningPipeline = m_renderer->createComputePipeline(builder.buildCompute());
    }

    void IndirectRenderer::update(float dt)
    {
        if (m_model)
        {
            m_currentFrameIndex = (m_currentFrameIndex + 1) % m_frames.size();
            auto& frame = m_frames[m_currentFrameIndex];

            scene::AnimationSystem::update(*m_model, dt);
            m_model->scene().updateTransforms();

            // Update Skinning Matrices
            auto jointMatrices = scene::AnimationSystem::updateSkinning(*m_model);
            if (!jointMatrices.empty())
            {
                size_t dataSize = jointMatrices.size() * sizeof(glm::mat4);
                if (frame.jointMatricesBuffer == INVALID_BUFFER_HANDLE ||
                    m_renderer->getBuffer(frame.jointMatricesBuffer)->size() < dataSize)
                {
                    frame.jointMatricesBuffer = m_renderer->createBuffer({
                        .size = dataSize,
                        .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                        .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                        .debugName = "JointMatrices"
                    });
                    frame.mappedJointMatrices = m_renderer->getBuffer(frame.jointMatricesBuffer)->map();
                }

                if (frame.mappedJointMatrices)
                {
                    std::memcpy(frame.mappedJointMatrices, jointMatrices.data(), dataSize);
                }
                else
                {
                    m_renderer->getBuffer(frame.jointMatricesBuffer)->uploadData(jointMatrices.data(), dataSize);
                }
            }

            // Update Morph States
            if (!m_model->morphStates().empty())
            {
                size_t size = m_model->morphStates().size() * sizeof(scene::MorphStateGPU);
                if (m_model->morphStateBuffer == INVALID_BUFFER_HANDLE ||
                    m_renderer->getBuffer(m_model->morphStateBuffer)->size() < size)
                {
                    m_model->morphStateBuffer = m_renderer->createBuffer({
                        .size = size,
                        .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                        .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                        .debugName = "MorphStateBuffer"
                    });
                }
                m_renderer->getBuffer(m_model->morphStateBuffer)->uploadData(m_model->morphStates().data(), size);
            }

            // Phase 2: update per-mesh xforms (pick one representative node per mesh; prefer skinned nodes).
            const auto& sc = m_model->scene();
            const uint32_t meshCount = (uint32_t)m_model->meshes().size();
            std::vector<MeshXformGPU> xforms;
            xforms.resize(meshCount > 0 ? meshCount : 1u);

            // Default identity
            for (auto& xf : xforms)
            {
                xf.invModel = glm::mat4(1.0f);
                xf.normalWorldToLocal = glm::mat4(1.0f);
            }

            // Fill from scene: first skinned node that references each mesh wins.
            std::vector<bool> filled(meshCount, false);
            auto meshPool = sc.registry.view<scene::MeshRenderer>();
            auto worldPool = sc.registry.view<scene::WorldTransform>();
            auto skinPool = sc.registry.view<scene::SkinComponent>();

            for (uint32_t nodeIdInt : sc.topoOrder)
            {
                ecs::Entity nodeId = static_cast<ecs::Entity>(nodeIdInt);
                if (!meshPool.reg.has<scene::MeshRenderer>(nodeId)) continue;
                const int32_t meshIdx = meshPool.reg.get<scene::MeshRenderer>(nodeId).meshID;
                if (meshIdx < 0 || (uint32_t)meshIdx >= meshCount) continue;

                bool hasSkin = skinPool.reg.has<scene::SkinComponent>(nodeId);
                if (!hasSkin) continue;
                if (filled[(uint32_t)meshIdx]) continue;

                const glm::mat4 model = worldPool.reg.get<scene::WorldTransform>(nodeId).matrix;
                const glm::mat4 invModel = glm::inverse(model);
                const glm::mat3 m3 = glm::mat3(model);

                MeshXformGPU xf{};
                xf.invModel = invModel;
                xf.normalWorldToLocal = glm::mat4(1.0f);
                xf.normalWorldToLocal[0] = glm::vec4(glm::transpose(m3)[0], 0.0f);
                xf.normalWorldToLocal[1] = glm::vec4(glm::transpose(m3)[1], 0.0f);
                xf.normalWorldToLocal[2] = glm::vec4(glm::transpose(m3)[2], 0.0f);

                xforms[(uint32_t)meshIdx] = xf;
                filled[(uint32_t)meshIdx] = true;
            }

            if (frame.meshXformsBuffer != INVALID_BUFFER_HANDLE)
            {
                const size_t bytes = sizeof(MeshXformGPU) * xforms.size();
                if (frame.mappedMeshXforms)
                {
                    std::memcpy(frame.mappedMeshXforms, xforms.data(), bytes);
                }
                else
                {
                    m_renderer->getBuffer(frame.meshXformsBuffer)->uploadData(xforms.data(), bytes);
                }
            }
        }

        updateGlobalTransforms();
    }

    void IndirectRenderer::updateGlobalTransforms()
    {
        // Update SceneGraph transforms
        const auto& scene = m_model->scene();
        auto worldPool = scene.registry.view<scene::WorldTransform>();

        if (scene.topoOrder.empty()) return;

        auto& frame = m_frames[m_currentFrameIndex];

        // Find max entity ID in topoOrder to size the buffer
        uint32_t maxEntityId = 0;
        for (ecs::Entity e : scene.topoOrder) maxEntityId = std::max(maxEntityId, (uint32_t)e);

        size_t nodeCount = maxEntityId + 1;
        size_t dataSize = nodeCount * sizeof(glm::mat4);

        if (frame.transformBuffer == INVALID_BUFFER_HANDLE ||
            m_renderer->getBuffer(frame.transformBuffer)->size() < dataSize)
        {
            // Reallocate
            frame.transformBuffer = m_renderer->createBuffer({
                .size = dataSize,
                .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                .debugName = "IndirectTransforms"
            });
            frame.mappedTransform = m_renderer->getBuffer(frame.transformBuffer)->map();
        }

        // Upload
        if (frame.mappedTransform)
        {
            glm::mat4* dst = static_cast<glm::mat4*>(frame.mappedTransform);
            // We must ensure the whole buffer is initialized if there are holes,
            // but here we just fill what we have.
            for (ecs::Entity e : scene.topoOrder)
            {
                dst[e] = worldPool.reg.get<scene::WorldTransform>(e).matrix;
            }
        }
        else
        {
            std::vector<glm::mat4> globals(nodeCount, glm::mat4(1.0f));
            for (ecs::Entity e : scene.topoOrder)
            {
                globals[e] = worldPool.reg.get<scene::WorldTransform>(e).matrix;
            }
            m_renderer->getBuffer(frame.transformBuffer)->uploadData(globals.data(), dataSize);
        }

        // Update Skins
        if (!m_model->skins().empty())
        {
            std::vector<glm::mat4> joints;
            // Estimate size
            uint32_t totalJoints = 0;
            if (!m_skinOffsets.empty())
            {
                totalJoints = m_skinOffsets.back() + (uint32_t)m_model->skins().back().joints.size();
            }
            joints.reserve(totalJoints);

            for (const auto& skin : m_model->skins())
            {
                for (size_t i = 0; i < skin.joints.size(); ++i)
                {
                    ecs::Entity entity = static_cast<ecs::Entity>(skin.joints[i]);
                    glm::mat4 global = worldPool.reg.has<scene::WorldTransform>(entity)
                                           ? worldPool.reg.get<scene::WorldTransform>(entity).matrix
                                           : glm::mat4(1.0f);
                    glm::mat4 inverseBind = skin.inverseBindMatrices[i];
                    joints.push_back(global * inverseBind);
                }
            }

            if (!joints.empty())
            {
                size_t size = joints.size() * sizeof(glm::mat4);
                if (frame.jointBuffer == INVALID_BUFFER_HANDLE || m_renderer->getBuffer(frame.jointBuffer)->size() <
                    size)
                {
                    frame.jointBuffer = m_renderer->createBuffer({
                        .size = size,
                        .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                        .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                        .debugName = "JointBuffer"
                    });
                    frame.mappedJoints = m_renderer->getBuffer(frame.jointBuffer)->map();
                }

                if (frame.mappedJoints)
                {
                    std::memcpy(frame.mappedJoints, joints.data(), size);
                }
                else
                {
                    m_renderer->getBuffer(frame.jointBuffer)->uploadData(joints.data(), size);
                }
            }
        }
    }

    void IndirectRenderer::updateLights()
    {
        if (!m_model || !m_renderer) return;

        auto& frame = m_frames[m_currentFrameIndex];
        auto& scene = m_model->scene();

        std::vector<ShaderGen::indirect_frag::LightDataGPU> gpuLights;
        gpuLights.reserve(scene.registry.getPool<LightSource>().size());

        m_shadowCasterIndex = -1;
        uint32_t currentLightIndex = 0;

        auto lightView = scene.registry.view<LightSource, WorldTransform>();
        lightView.each([&](ecs::Entity entity, LightSource& light, WorldTransform& world)
        {
            (void)entity;
            const glm::mat4& worldM = world.matrix;

            glm::vec3 baseDir = light.direction;
            if (glm::length(baseDir) < 0.0001f) baseDir = glm::vec3(0.0f, -1.0f, 0.0f);
            glm::vec3 dir = glm::normalize(glm::vec3(worldM * glm::vec4(baseDir, 0.0f)));

            ShaderGen::indirect_frag::LightDataGPU l{};
            l.direction = dir;
            l.color = light.color;
            l.intensity = light.intensity;
            l.range = (light.range <= 0.0f) ? 10000.0f : light.range;
            l.position = glm::vec3(worldM[3]);
            l.innerConeCos = std::cos(light.innerConeAngle);
            l.outerConeCos = std::cos(light.outerConeAngle);
            l.type = static_cast<uint32_t>(light.type);
            l.nodeId = entity;
            l._pad = 0;

            if (m_shadowCasterIndex == -1 &&
                (light.type == scene::LightType::Directional || light.type == scene::LightType::Spot))
            {
                m_shadowCasterIndex = currentLightIndex;
            }

            gpuLights.push_back(l);
            currentLightIndex++;
        });

        frame.lightCount = static_cast<uint32_t>(gpuLights.size());

        const size_t dataSize = gpuLights.size() * sizeof(ShaderGen::indirect_frag::LightDataGPU);

        if (dataSize > 0)
        {
            if (frame.lightBuffer == INVALID_BUFFER_HANDLE ||
                m_renderer->getBuffer(frame.lightBuffer)->size() < dataSize)
            {
                frame.lightBuffer = m_renderer->createBuffer({
                    .size = dataSize,
                    .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                    .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                    .debugName = "PerFrameLightBuffer"
                });
                frame.mappedLights = m_renderer->getBuffer(frame.lightBuffer)->map();
            }

            if (frame.mappedLights)
            {
                std::memcpy(frame.mappedLights, gpuLights.data(), dataSize);
            }
            else if (frame.lightBuffer != INVALID_BUFFER_HANDLE)
            {
                m_renderer->getBuffer(frame.lightBuffer)->uploadData(gpuLights.data(), dataSize);
            }
        }
    }

    void IndirectRenderer::buildBuffers()
    {
        // Legacy - no longer used as GLTFUnifiedDOD handles it
    }

    void IndirectRenderer::uploadEnvironmentData(TextureHandle brdf, TextureHandle irradiance, TextureHandle prefilter)
    {
        ShaderGen::indirect_frag::EnvironmentMapDataGPU envData{};

        // Initialize with INVALID_ID (~0u)
        envData.envMapTexture = ~0u;
        envData.envMapTextureIrradiance = ~0u;
        envData.texBRDF_LUT = ~0u;
        envData.envMapTextureCharlie = ~0u;

        // FIX: Use ClampToEdge sampler for IBL lookups to avoid edge artifacts
        uint32_t clampSampler = m_renderer->getBindlessSamplerIndex(rhi::SamplerAddressMode::ClampToEdge);

        if (prefilter != INVALID_TEXTURE_HANDLE)
        {
            envData.envMapTexture = m_renderer->getTextureBindlessIndex(prefilter);
            envData.envMapTextureSampler = clampSampler;

            // Charlie (Sheen) map - using prefilter as placeholder if specific map missing
            envData.envMapTextureCharlie = envData.envMapTexture;
            envData.envMapTextureCharlieSampler = clampSampler;
        }

        if (irradiance != INVALID_TEXTURE_HANDLE)
        {
            envData.envMapTextureIrradiance = m_renderer->getTextureBindlessIndex(irradiance);
            envData.envMapTextureIrradianceSampler = clampSampler;
        }

        if (brdf != INVALID_TEXTURE_HANDLE)
        {
            envData.texBRDF_LUT = m_renderer->getTextureBindlessIndex(brdf);
            envData.texBRDF_LUTSampler = clampSampler;
        }

        m_environmentBuffer = m_renderer->createBuffer({
            .size = sizeof(EnvironmentMapDataGPU),
            .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = rhi::MemoryUsage::CPUToGPU,
            .data = &envData,
            .debugName = "EnvironmentBuffer"
        });
    }

    void IndirectRenderer::uploadMaterialData()
    {
        if (!m_renderer || !m_model) return;
        m_materialsCPU = scene::packMaterialsGPU(*m_model, *m_renderer);
        uploadMaterialsToGPU();
    }

    void IndirectRenderer::createPipeline()
    {
        auto vert = rhi::Shader::load(rhi::ShaderStage::Vertex, "shaders/indirect.vert.spv");
        auto frag = rhi::Shader::load(rhi::ShaderStage::Fragment, "shaders/indirect.frag.spv");

        rhi::RHIPipelineBuilder builder;
        builder.setShaders(vert.get(), frag.get())
               .setTopology(rhi::PrimitiveTopology::TriangleList)
               .setColorFormat(m_renderer->getSwapchainColorFormat()) // Matches m_sceneColor format
               .setDepthFormat(m_renderer->getDrawDepthFormat());

        // Solid Pipeline (Writes Depth)
        builder.setPolygonMode(rhi::PolygonMode::Fill)
               .enableDepthTest(true, rhi::CompareOp::LessOrEqual)
               .setNoBlend()
               .setName("IndirectSolid");
        m_pipeline = m_renderer->createGraphicsPipeline(builder.buildGraphics());

        // Transparent Pipeline (No Depth Write, Blend)
        builder.enableDepthTest(false, rhi::CompareOp::LessOrEqual) // Keep test enabled, disable write
               .setAlphaBlend()
               .setName("IndirectTransparent");
        m_pipelineTransparent = m_renderer->createGraphicsPipeline(builder.buildGraphics());

        builder.setPolygonMode(rhi::PolygonMode::Line).setName("IndirectWireframe");
        m_pipelineWireframe = m_renderer->createGraphicsPipeline(builder.buildGraphics());
    }

    std::span<ShaderGen::indirect_frag::MetallicRoughnessDataGPU> IndirectRenderer::materialsCPU()
    {
        return m_materialsCPU;
    }

    void IndirectRenderer::uploadMaterialsToGPU()
    {
        if (!m_renderer || m_frames.empty() || m_materialsCPU.empty()) return;

        const uint64_t bytes = m_materialsCPU.size() * sizeof(ShaderGen::indirect_frag::MetallicRoughnessDataGPU);
        for (auto& frame : m_frames)
        {
            if (frame.materialBuffer == INVALID_BUFFER_HANDLE ||
                m_renderer->getBuffer(frame.materialBuffer)->size() < bytes)
            {
                frame.materialBuffer = m_renderer->createBuffer({
                    .size = bytes,
                    .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                    .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                    .debugName = "GLTF Materials"
                });
            }
            m_renderer->getBuffer(frame.materialBuffer)->uploadData(m_materialsCPU.data(), bytes);
        }
    }

    void IndirectRenderer::updateMaterial(uint32_t materialIndex)
    {
        if (materialIndex >= m_materialsCPU.size()) return;
        if (m_frames.empty()) return;

        // Repack if needed from model? Usually materialsCPU is already updated.
        // Assuming m_materialsCPU[materialIndex] is what we want to upload.
        uint64_t offset = materialIndex * sizeof(ShaderGen::indirect_frag::MetallicRoughnessDataGPU);
        for (auto& frame : m_frames)
        {
            if (frame.materialBuffer == INVALID_BUFFER_HANDLE) continue;
            m_renderer->getBuffer(frame.materialBuffer)->uploadData(&m_materialsCPU[materialIndex],
                                                                    sizeof(m_materialsCPU[materialIndex]), offset);
        }
    }

    void IndirectRenderer::repackMaterialsFromModel()
    {
        if (!m_renderer || !m_model) return;
        m_materialsCPU = scene::packMaterialsGPU(*m_model, *m_renderer);
    }

    void IndirectRenderer::dispatchSkinning(rhi::RHICommandBuffer* cmd)
    {
        if (m_model->meshes().empty()) return;

        auto& frame = m_frames[m_currentFrameIndex];

        // 1. Dispatch Skinning/Morphing if needed
        bool hasSkinning = (frame.jointMatricesBuffer != INVALID_BUFFER_HANDLE);
        bool hasMorphing = (m_model->morphVertexBuffer != INVALID_BUFFER_HANDLE && m_model->morphStateBuffer !=
            INVALID_BUFFER_HANDLE);

        if ((hasSkinning || hasMorphing) && m_skinningPipeline != INVALID_PIPELINE_HANDLE)
        {
            cmd->bindPipeline(m_renderer->getPipeline(m_skinningPipeline));

            struct SkinPushConstants
            {
                uint64_t inBuffer;
                uint64_t outBuffer;
                uint64_t jointMatrices;
                uint64_t morphDeltas;
                uint64_t morphStates;
                uint64_t meshXforms;
                uint32_t count;
                uint32_t hasSkinning;
                uint32_t hasMorphing;
            } skinPC{};

            auto* srcBuf = m_renderer->getBuffer(m_model->vertexBuffer);
            auto* dstBuf = m_renderer->getBuffer(frame.skinnedVertexBuffer);

            skinPC.inBuffer = srcBuf->getDeviceAddress();
            skinPC.outBuffer = dstBuf->getDeviceAddress();
            if (hasSkinning)
            {
                skinPC.jointMatrices = m_renderer->getBuffer(frame.jointMatricesBuffer)->getDeviceAddress();
                skinPC.hasSkinning = 1;
            }
            if (hasMorphing)
            {
                skinPC.morphDeltas = m_renderer->getBuffer(m_model->morphVertexBuffer)->getDeviceAddress();
                skinPC.morphStates = m_renderer->getBuffer(m_model->morphStateBuffer)->getDeviceAddress();
                skinPC.hasMorphing = 1;
            }
            if (frame.meshXformsBuffer != INVALID_BUFFER_HANDLE)
            {
                skinPC.meshXforms = m_renderer->getBuffer(frame.meshXformsBuffer)->getDeviceAddress();
            }

            skinPC.count = (uint32_t)(srcBuf->size() / sizeof(pnkr::renderer::Vertex));

            m_renderer->pushConstants(cmd, m_skinningPipeline, rhi::ShaderStage::Compute, skinPC);

            cmd->dispatch((skinPC.count + 63) / 64, 1, 1);

            // Barrier: Compute Write -> Vertex Attribute Read
            rhi::RHIMemoryBarrier barrier;
            barrier.buffer = dstBuf;
            barrier.srcAccessStage = rhi::ShaderStage::Compute;
            barrier.dstAccessStage = rhi::ShaderStage::Vertex;
            cmd->pipelineBarrier(rhi::ShaderStage::Compute, rhi::ShaderStage::Vertex, {barrier});
        }
    }

    void IndirectRenderer::draw(rhi::RHICommandBuffer* cmd, const scene::Camera& camera, uint32_t width,
                                uint32_t height)
    {
        if (m_pipeline == INVALID_PIPELINE_HANDLE) return;

        resize(width, height);

        // 1. PREPARE DRAW LISTS (Sorting & Culling)
        scene::GLTFUnifiedDODContext ctx;
        ctx.renderer = m_renderer;
        ctx.model = m_model.get();

        // Assign frame-specific buffers to the context
        auto& frame = m_frames[m_currentFrameIndex];

        // Assign existing buffers
        ctx.transformBuffer = frame.transformBuffer;
        ctx.indirectOpaqueBuffer = frame.indirectOpaqueBuffer;
        ctx.indirectTransmissionBuffer = frame.indirectTransmissionBuffer;
        ctx.indirectTransparentBuffer = frame.indirectTransparentBuffer;

        updateLights();

        // Build lists
        scene::GLTFUnifiedDOD::buildDrawLists(ctx, camera.position());

        // Update handles back to frame (in case they were recreated)
        frame.transformBuffer = ctx.transformBuffer;
        frame.indirectOpaqueBuffer = ctx.indirectOpaqueBuffer;
        frame.indirectTransmissionBuffer = ctx.indirectTransmissionBuffer;
        frame.indirectTransparentBuffer = ctx.indirectTransparentBuffer;

        // 2. STOP DEFAULT PASS
        cmd->endRendering();

        // 3. COMMON DATA
        ShaderGen::indirect_vert::PerFrameData pc{};
        pc.drawable.view = camera.view();
        pc.drawable.proj = camera.proj();
        pc.drawable.cameraPos = glm::vec4(camera.position(), 1.0f);

        // Bind Unified Index Buffer
        cmd->bindIndexBuffer(m_renderer->getBuffer(m_model->indexBuffer), 0, false);

        // Skinning / Vertex Address
        bool hasSkinning = (frame.jointMatricesBuffer != INVALID_BUFFER_HANDLE);
        bool hasMorphing = (m_model->morphVertexBuffer != INVALID_BUFFER_HANDLE);
        if (frame.skinnedVertexBuffer != INVALID_BUFFER_HANDLE && (hasSkinning || hasMorphing))
        {
            pc.drawable.vertexBufferPtr = m_renderer->getBuffer(frame.skinnedVertexBuffer)->getDeviceAddress();
        }
        else
        {
            pc.drawable.vertexBufferPtr = m_renderer->getBuffer(m_model->vertexBuffer)->getDeviceAddress();
        }

        pc.drawable.transformBufferPtr = m_renderer->getBuffer(frame.transformBuffer)->getDeviceAddress();
        pc.drawable.materialBufferPtr = (frame.materialBuffer != INVALID_BUFFER_HANDLE)
                                            ? m_renderer->getBuffer(frame.materialBuffer)->getDeviceAddress()
                                            : 0;
        pc.drawable.environmentBufferPtr = m_renderer->getBuffer(m_environmentBuffer)->getDeviceAddress();
        pc.drawable.lightBufferPtr = (frame.lightBuffer != INVALID_BUFFER_HANDLE)
                                         ? m_renderer->getBuffer(frame.lightBuffer)->getDeviceAddress()
                                         : 0;
        pc.drawable.lightCount = frame.lightCount;
        pc.drawable.shadowDataPtr = (frame.shadowDataBuffer != INVALID_BUFFER_HANDLE)
                                        ? m_renderer->getBuffer(frame.shadowDataBuffer)->getDeviceAddress()
                                        : 0;

        auto* sceneColorTex = m_renderer->getTexture(m_sceneColor);
        auto* depthTex = m_renderer->getDepthTexture();

        // --- PHASE 0: Shadow Pass ---
        // Prepare default invalid shadow data
        ShaderGen::indirect_frag::ShadowDataGPU shadowData{};
        shadowData.shadowMapTexture = 0xFFFFFFFFu; // Invalid texture ID disables shadows in shader
        shadowData.shadowMapSampler = 0u;
        shadowData.lightViewProjRaw = glm::mat4(1.0f);
        shadowData.lightViewProjBiased = glm::mat4(1.0f);
        shadowData.shadowMapTexelSize = glm::vec2(0.0f);
        shadowData.shadowBias = 0.0f;

        if (m_shadowPipeline != INVALID_PIPELINE_HANDLE && !ctx.indirectOpaque.empty() && m_shadowCasterIndex != -1)
        {
            glm::vec3 lightDir = glm::vec3(0.0f, -1.0f, 0.0f);
            glm::vec3 lightPos = glm::vec3(0.0f);
            scene::LightType lightType = scene::LightType::Directional;

            const auto& scene = m_model->scene();
            auto lightView = scene.registry.view<LightSource, WorldTransform>();
            uint32_t currentLightIdx = 0;
            lightView.each([&](ecs::Entity, LightSource& ls, WorldTransform& world)
            {
                if (static_cast<int>(currentLightIdx) == m_shadowCasterIndex)
                {
                    const glm::mat4& worldM = world.matrix;
                    lightPos = glm::vec3(worldM[3]);
                    lightType = ls.type;
                    glm::vec3 baseDir = ls.direction;
                    if (glm::length(baseDir) < 0.0001f) baseDir = glm::vec3(0.0f, -1.0f, 0.0f);
                    lightDir = glm::normalize(glm::vec3(worldM * glm::vec4(baseDir, 0.0f)));
                }
                currentLightIdx++;
            });

            glm::mat4 lightViewMat(1.0f), lightProjMat(1.0f);
            const float s = (m_shadowSettings.orthoSize > 0.01f) ? m_shadowSettings.orthoSize : 40.0f;

            if (lightType == scene::LightType::Directional)
            {
                glm::vec3 eye = lightPos - lightDir * m_shadowSettings.distFromCam;
                lightViewMat = glm::lookAt(eye, lightPos, glm::vec3(0, 1, 0));
                lightProjMat = glm::orthoRH_ZO(-s, s, -s, s, m_shadowSettings.nearPlane, m_shadowSettings.farPlane);

                const float texelWorldSize = (2.0f * s) / static_cast<float>(m_shadowDim);
                glm::vec4 centerLS = lightViewMat * glm::vec4(lightPos, 1.0f);
                const float snappedX = std::floor(centerLS.x / texelWorldSize) * texelWorldSize;
                const float snappedY = std::floor(centerLS.y / texelWorldSize) * texelWorldSize;
                const glm::vec3 snapDelta(snappedX - centerLS.x, snappedY - centerLS.y, 0.0f);
                lightViewMat = glm::translate(glm::mat4(1.0f), snapDelta) * lightViewMat;
            }
            else
            {
                const float fov = (m_shadowSettings.fov > 0.01f) ? m_shadowSettings.fov : 45.0f;
                lightViewMat = glm::lookAt(lightPos, lightPos + lightDir, glm::vec3(0, 1, 0));
                lightProjMat = glm::perspective(glm::radians(fov), 1.0f, m_shadowSettings.nearPlane,
                                                m_shadowSettings.farPlane);
            }

            const glm::mat4 lightViewProjRaw = lightProjMat * lightViewMat;
            const glm::mat4 scaleBias(
                0.5f, 0.0f, 0.0f, 0.0f,
                0.0f, 0.5f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.5f, 0.5f, 0.0f, 1.0f
            );

            shadowData.lightViewProjRaw = lightViewProjRaw;
            shadowData.lightViewProjBiased = scaleBias * lightViewProjRaw;
            shadowData.shadowMapTexture = m_shadowMapBindlessIndex;
            shadowData.shadowMapSampler = m_renderer->getShadowSamplerIndex();
            shadowData.shadowMapTexelSize = glm::vec2(1.0f / static_cast<float>(m_shadowDim));
            shadowData.shadowBias = m_shadowSettings.biasConst * 0.0001f;

            // Perform Shadow Rendering
            auto* shadowTex = m_renderer->getTexture(m_shadowMap);

            rhi::RHIMemoryBarrier bShadow{};
            bShadow.texture = shadowTex;
            bShadow.oldLayout = m_shadowLayout;
            bShadow.newLayout = rhi::ResourceLayout::DepthStencilAttachment;
            bShadow.srcAccessStage = rhi::ShaderStage::All;
            bShadow.dstAccessStage = rhi::ShaderStage::DepthStencilAttachment;

            cmd->pipelineBarrier(rhi::ShaderStage::All, rhi::ShaderStage::DepthStencilAttachment, {bShadow});
            m_shadowLayout = rhi::ResourceLayout::DepthStencilAttachment;

            rhi::RenderingInfo info{};
            info.renderArea = {0, 0, m_shadowDim, m_shadowDim};

            rhi::RenderingAttachment depthAtt{};
            depthAtt.texture = shadowTex;
            depthAtt.loadOp = rhi::LoadOp::Clear;
            depthAtt.storeOp = rhi::StoreOp::Store;
            depthAtt.clearValue.isDepthStencil = true;
            depthAtt.clearValue.depthStencil.depth = 1.0f;
            info.depthAttachment = &depthAtt;

            cmd->beginRendering(info);
            cmd->setViewport({0, 0, static_cast<float>(m_shadowDim), static_cast<float>(m_shadowDim), 0, 1});
            cmd->setScissor({0, 0, m_shadowDim, m_shadowDim});
            cmd->setDepthBias(m_shadowSettings.biasConst, 0.0f, m_shadowSettings.biasSlope);

            cmd->bindPipeline(m_renderer->getPipeline(m_shadowPipeline));

            ShaderGen::indirect_vert::PerFrameData shadowPC = pc;
            shadowPC.drawable.view = lightViewMat;
            shadowPC.drawable.proj = lightProjMat;
            m_renderer->pushConstants(cmd, m_shadowPipeline, rhi::ShaderStage::Vertex, shadowPC);

            auto* ib = m_renderer->getBuffer(frame.indirectOpaqueBuffer);
            cmd->drawIndexedIndirect(ib, 0, (uint32_t)ctx.indirectOpaque.size(),
                                     sizeof(rhi::DrawIndexedIndirectCommand));

            cmd->endRendering();

            bShadow.oldLayout = rhi::ResourceLayout::DepthStencilAttachment;
            bShadow.newLayout = rhi::ResourceLayout::ShaderReadOnly;
            bShadow.srcAccessStage = rhi::ShaderStage::DepthStencilAttachment;
            bShadow.dstAccessStage = rhi::ShaderStage::Fragment;
            cmd->pipelineBarrier(rhi::ShaderStage::DepthStencilAttachment, rhi::ShaderStage::Fragment, {bShadow});
            m_shadowLayout = rhi::ResourceLayout::ShaderReadOnly;
        }

        // Always upload shadow data (either valid or invalid) to prevent reading garbage
        if (frame.mappedShadowData)
        {
            std::memcpy(frame.mappedShadowData, &shadowData, sizeof(shadowData));
        }
        else if (frame.shadowDataBuffer != INVALID_BUFFER_HANDLE)
        {
            m_renderer->getBuffer(frame.shadowDataBuffer)->uploadData(&shadowData, sizeof(shadowData));
        }

        // --- PHASE 1: Opaque Pass ---
        {
            // Barriers
            std::vector<rhi::RHIMemoryBarrier> barriers;
            rhi::RHIMemoryBarrier bColor{};
            bColor.texture = sceneColorTex;
            bColor.oldLayout = m_sceneColorLayout;
            bColor.newLayout = rhi::ResourceLayout::ColorAttachment;
            bColor.srcAccessStage = rhi::ShaderStage::All;
            bColor.dstAccessStage = rhi::ShaderStage::RenderTarget;
            barriers.push_back(bColor);

            // Ensure Depth is ready
            if (m_depthLayout != rhi::ResourceLayout::DepthStencilAttachment)
            {
                rhi::RHIMemoryBarrier bDepth{};
                bDepth.texture = depthTex;
                bDepth.oldLayout = m_depthLayout;
                bDepth.newLayout = rhi::ResourceLayout::DepthStencilAttachment;
                bDepth.srcAccessStage = rhi::ShaderStage::All;
                bDepth.dstAccessStage = rhi::ShaderStage::DepthStencilAttachment;
                barriers.push_back(bDepth);
                m_depthLayout = rhi::ResourceLayout::DepthStencilAttachment;
            }
            cmd->pipelineBarrier(rhi::ShaderStage::All, rhi::ShaderStage::RenderTarget, barriers);
            m_sceneColorLayout = rhi::ResourceLayout::ColorAttachment;

            // Render Info
            rhi::RenderingInfo info{};
            info.renderArea = {0, 0, width, height};
            rhi::RenderingAttachment attColor{};
            attColor.texture = sceneColorTex;
            attColor.loadOp = rhi::LoadOp::Clear;
            attColor.storeOp = rhi::StoreOp::Store;
            attColor.clearValue.isDepthStencil = false;
            attColor.clearValue.color.float32[0] = 0.0f; // Black clear
            info.colorAttachments.push_back(attColor);

            rhi::RenderingAttachment attDepth{};
            attDepth.texture = depthTex;
            attDepth.loadOp = rhi::LoadOp::Clear;
            attDepth.storeOp = rhi::StoreOp::Store;
            attDepth.clearValue.isDepthStencil = true;
            attDepth.clearValue.depthStencil.depth = 1.0f;
            info.depthAttachment = &attDepth;

            cmd->beginRendering(info);
            cmd->setViewport({0, 0, (float)width, (float)height, 0, 1});
            cmd->setScissor({0, 0, width, height});

            PipelineHandle opaquePipeline = m_drawWireframe ? m_pipelineWireframe : m_pipeline;

            if (!ctx.indirectOpaque.empty())
            {
                cmd->bindPipeline(m_renderer->getPipeline(opaquePipeline));
                m_renderer->pushConstants(cmd, opaquePipeline, rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
                                          pc);

                auto* ib = m_renderer->getBuffer(frame.indirectOpaqueBuffer);
                cmd->drawIndexedIndirect(ib, 0, (uint32_t)ctx.indirectOpaque.size(),
                                         sizeof(rhi::DrawIndexedIndirectCommand));
            }
            cmd->endRendering();
        }

        // --- PHASE 2: Copy Opaque to Transmission ---
        if (!ctx.indirectTransmission.empty())
        {
            auto* transTex = m_renderer->getTexture(m_transmissionTexture);

            // Barrier: SceneColor -> TransferSrc, TransTex -> TransferDst
            std::vector<rhi::RHIMemoryBarrier> barriers;
            rhi::RHIMemoryBarrier bSrc{};
            bSrc.texture = sceneColorTex;
            bSrc.oldLayout = m_sceneColorLayout;
            bSrc.newLayout = rhi::ResourceLayout::TransferSrc;
            bSrc.srcAccessStage = rhi::ShaderStage::RenderTarget;
            bSrc.dstAccessStage = rhi::ShaderStage::Transfer;
            barriers.push_back(bSrc);

            rhi::RHIMemoryBarrier bDst{};
            bDst.texture = transTex;
            bDst.oldLayout = m_transmissionLayout;
            bDst.newLayout = rhi::ResourceLayout::TransferDst;
            bDst.srcAccessStage = rhi::ShaderStage::All;
            bDst.dstAccessStage = rhi::ShaderStage::Transfer;
            barriers.push_back(bDst);

            cmd->pipelineBarrier(rhi::ShaderStage::All, rhi::ShaderStage::Transfer, barriers);
            m_sceneColorLayout = rhi::ResourceLayout::TransferSrc;
            m_transmissionLayout = rhi::ResourceLayout::TransferDst;

            // Copy
            rhi::TextureCopyRegion region{};
            region.extent = {width, height, 1};
            cmd->copyTexture(sceneColorTex, transTex, region);

            // Mips (Transitions to ShaderReadOnly)
            transTex->generateMipmaps(cmd);
            m_transmissionLayout = rhi::ResourceLayout::ShaderReadOnly;

            // Restore SceneColor
            rhi::RHIMemoryBarrier bRest{};
            bRest.texture = sceneColorTex;
            bRest.oldLayout = rhi::ResourceLayout::TransferSrc;
            bRest.newLayout = rhi::ResourceLayout::ColorAttachment;
            bRest.srcAccessStage = rhi::ShaderStage::Transfer;
            bRest.dstAccessStage = rhi::ShaderStage::RenderTarget;
            cmd->pipelineBarrier(rhi::ShaderStage::Transfer, rhi::ShaderStage::RenderTarget, {bRest});
            m_sceneColorLayout = rhi::ResourceLayout::ColorAttachment;

            // Update PC for transmission lookup
            pc.drawable.transmissionFramebuffer = m_renderer->getTextureBindlessIndex(m_transmissionTexture);
            pc.drawable.transmissionFramebufferSampler = m_renderer->getBindlessSamplerIndex(
                rhi::SamplerAddressMode::ClampToEdge);
        }
        else
        {
            // Fallback if no transmission needed
            pc.drawable.transmissionFramebuffer = m_renderer->getTextureBindlessIndex(m_renderer->getWhiteTexture());
            pc.drawable.transmissionFramebufferSampler = 0;
        }

        // --- PHASE 3: Transmission & Transparent ---
        {
            rhi::RenderingInfo info{};
            info.renderArea = {0, 0, width, height};
            rhi::RenderingAttachment attColor{};
            attColor.texture = sceneColorTex;
            attColor.loadOp = rhi::LoadOp::Load; // Keep opaque pixels
            attColor.storeOp = rhi::StoreOp::Store;
            info.colorAttachments.push_back(attColor);

            rhi::RenderingAttachment attDepth{};
            attDepth.texture = depthTex;
            attDepth.loadOp = rhi::LoadOp::Load; // Keep depth
            attDepth.storeOp = rhi::StoreOp::Store;
            info.depthAttachment = &attDepth;

            cmd->beginRendering(info);
            cmd->setViewport({0, 0, (float)width, (float)height, 0, 1});
            cmd->setScissor({0, 0, width, height});

            PipelineHandle opaquePipeline = m_drawWireframe ? m_pipelineWireframe : m_pipeline;

            // Transmission (using Opaque pipeline state usually, but with transmission texture bound)
            if (!ctx.indirectTransmission.empty())
            {
                cmd->bindPipeline(m_renderer->getPipeline(opaquePipeline)); // Rebind
                m_renderer->pushConstants(cmd, opaquePipeline, rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
                                          pc);

                auto* ib = m_renderer->getBuffer(frame.indirectTransmissionBuffer);
                cmd->drawIndexedIndirect(ib, 0, (uint32_t)ctx.indirectTransmission.size(),
                                         sizeof(rhi::DrawIndexedIndirectCommand));
            }

            // Transparent
            if (!ctx.indirectTransparent.empty())
            {
                cmd->bindPipeline(m_renderer->getPipeline(m_pipelineTransparent));
                m_renderer->pushConstants(cmd, m_pipelineTransparent,
                                          rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, pc);

                auto* ib = m_renderer->getBuffer(frame.indirectTransparentBuffer);
                cmd->drawIndexedIndirect(ib, 0, (uint32_t)ctx.indirectTransparent.size(),
                                         sizeof(rhi::DrawIndexedIndirectCommand));
            }
            cmd->endRendering();
        }

        // --- PHASE 4: Final Blit to Swapchain ---
        {
            auto* backbuffer = m_renderer->getBackbuffer();

            // Barriers
            std::vector<rhi::RHIMemoryBarrier> barriers;
            rhi::RHIMemoryBarrier bSrc{};
            bSrc.texture = sceneColorTex;
            bSrc.oldLayout = m_sceneColorLayout;
            bSrc.newLayout = rhi::ResourceLayout::TransferSrc;
            bSrc.srcAccessStage = rhi::ShaderStage::RenderTarget;
            bSrc.dstAccessStage = rhi::ShaderStage::Transfer;
            barriers.push_back(bSrc);

            rhi::RHIMemoryBarrier bDst{};
            bDst.texture = backbuffer;
            // Backbuffer comes in as ColorAttachment from beginFrame
            bDst.oldLayout = rhi::ResourceLayout::ColorAttachment;
            bDst.newLayout = rhi::ResourceLayout::TransferDst;
            bDst.srcAccessStage = rhi::ShaderStage::RenderTarget;
            bDst.dstAccessStage = rhi::ShaderStage::Transfer;
            barriers.push_back(bDst);

            cmd->pipelineBarrier(rhi::ShaderStage::RenderTarget, rhi::ShaderStage::Transfer, barriers);
            m_sceneColorLayout = rhi::ResourceLayout::TransferSrc;

            // Blit
            rhi::TextureCopyRegion region{};
            region.extent = {width, height, 1};
            cmd->copyTexture(sceneColorTex, backbuffer, region);

            // Restore Backbuffer for UI (ColorAttachment)
            rhi::RHIMemoryBarrier bRest{};
            bRest.texture = backbuffer;
            bRest.oldLayout = rhi::ResourceLayout::TransferDst;
            bRest.newLayout = rhi::ResourceLayout::ColorAttachment;
            bRest.srcAccessStage = rhi::ShaderStage::Transfer;
            bRest.dstAccessStage = rhi::ShaderStage::RenderTarget;
            cmd->pipelineBarrier(rhi::ShaderStage::Transfer, rhi::ShaderStage::RenderTarget, {bRest});
        }

        // --- PHASE 5: Restart Swapchain Pass for ImGui ---
        {
            rhi::RenderingInfo info{};
            info.renderArea = {0, 0, width, height};
            rhi::RenderingAttachment attColor{};
            attColor.texture = m_renderer->getBackbuffer();
            attColor.loadOp = rhi::LoadOp::Load; // Keep blitted image
            attColor.storeOp = rhi::StoreOp::Store;
            info.colorAttachments.push_back(attColor);

            rhi::RenderingAttachment attDepth{};
            attDepth.texture = m_renderer->getDepthTexture();
            attDepth.loadOp = rhi::LoadOp::Load;
            attDepth.storeOp = rhi::StoreOp::Store;
            attDepth.clearValue.isDepthStencil = true;
            info.depthAttachment = &attDepth;

            cmd->beginRendering(info);
            cmd->setViewport({0, 0, (float)width, (float)height, 0, 1});
            cmd->setScissor({0, 0, width, height});
        }
    }

    void IndirectRenderer::setWireframe(bool enabled)
    {
        m_drawWireframe = enabled;
    }
} // namespace pnkr::renderer
