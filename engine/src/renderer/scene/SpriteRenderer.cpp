#include "pnkr/renderer/scene/SpriteRenderer.hpp"

#include "pnkr/core/logger.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/renderer/scene/Sprite.hpp"
#include "generated/sprite_billboard.vert.h"

#include <bit>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <string>
#include <vector>

namespace pnkr::renderer::scene
{
    namespace
    {
        struct SpriteInstanceGPU
        {
            glm::vec4 pos_space;
            glm::vec4 size_rot;
            glm::vec4 color;
            glm::uvec4 tex;
            glm::vec4 uvRect;
            glm::vec4 pivot_cutoff; // xy=pivot, z=alphaCutoff, w=unused
        };

        static_assert(sizeof(SpriteInstanceGPU) % 16 == 0, "SpriteInstanceGPU must be 16-byte aligned");

        constexpr uint32_t kFlagScreenSpace = 1u << 0u;
        constexpr size_t kDefaultFrameCount = 3;
        constexpr size_t kDefaultCapacity = 1024;

        static uint32_t floatToSortableUint(float f)
        {
            // Total ordering for IEEE754 floats:
            //  - flip sign bit for positives
            //  - invert all bits for negatives
            uint32_t u = std::bit_cast<uint32_t>(f);
            return (u & 0x80000000u) ? ~u : (u | 0x80000000u);
        }

        static uint32_t orderToKey(int16_t order)
        {
            return static_cast<uint32_t>(static_cast<uint16_t>(order + 32768));
        }
    }

    SpriteRenderer::SpriteRenderer(RHIRenderer& renderer)
        : m_renderer(renderer)
    {
        m_whiteTextureIndex = m_renderer.getTextureBindlessIndex(m_renderer.getWhiteTexture());
        m_defaultSamplerIndex = m_renderer.getBindlessSamplerIndex(rhi::SamplerAddressMode::Repeat);

        createQuadMesh();
        createPipelines();

        m_frames.resize(kDefaultFrameCount);
        // Pre-allocate with debug names
        for (uint32_t i = 0; i < m_frames.size(); ++i)
        {
            ensureFrameCapacity(m_frames[i], kDefaultCapacity, i);
        }
    }

    void SpriteRenderer::createQuadMesh()
    {
        std::vector<Vertex> vertices(4);
        vertices[0].m_texCoord0 = {0.0F, 0.0F}; vertices[1].m_texCoord0 = {1.0F, 0.0F};
        vertices[2].m_texCoord0 = {1.0F, 1.0F}; vertices[3].m_texCoord0 = {0.0F, 1.0F};
        vertices[0].m_texCoord1 = {-0.5F, -0.5F}; vertices[1].m_texCoord1 = {0.5F, -0.5F};
        vertices[2].m_texCoord1 = {0.5F, 0.5F}; vertices[3].m_texCoord1 = {-0.5F, 0.5F};
        std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};
        m_quadMesh = m_renderer.createMesh(vertices, indices, false);
    }

    void SpriteRenderer::createPipelines()
    {
        auto vertShader = rhi::Shader::load(rhi::ShaderStage::Vertex, "shaders/sprite_billboard.vert.spv");
        auto fragShader = rhi::Shader::load(rhi::ShaderStage::Fragment, "shaders/sprite_billboard.frag.spv");

        if (!vertShader || !fragShader) { core::Logger::error("SpriteRenderer: failed to load shaders."); return; }

        auto base = rhi::RHIPipelineBuilder()
                               .setShaders(vertShader.get(), fragShader.get(), nullptr)
                               .useVertexType<Vertex>()
                               .setTopology(rhi::PrimitiveTopology::TriangleList)
                               .setCullMode(rhi::CullMode::None)
                               .setColorFormat(m_renderer.getDrawColorFormat())
                               .setDepthFormat(m_renderer.getDrawDepthFormat());

        // World pipelines
        {
            auto worldCutout = base;
            worldCutout.enableDepthTest(true, rhi::CompareOp::LessOrEqual)
                       .setNoBlend()
                       .setName("SpriteWorldCutout");
            m_worldCutoutPipeline = m_renderer.createGraphicsPipeline(worldCutout.buildGraphics());

            auto worldAlpha = base;
            worldAlpha.enableDepthTest(false, rhi::CompareOp::LessOrEqual)
                      .setAlphaBlend()
                      .setName("SpriteWorldAlpha");
            m_worldAlphaPipeline = m_renderer.createGraphicsPipeline(worldAlpha.buildGraphics());

            auto worldAdd = base;
            worldAdd.enableDepthTest(false, rhi::CompareOp::LessOrEqual)
                    .setAdditiveBlend()
                    .setName("SpriteWorldAdditive");
            m_worldAdditivePipeline = m_renderer.createGraphicsPipeline(worldAdd.buildGraphics());

            auto worldPremul = base;
            worldPremul.enableDepthTest(false, rhi::CompareOp::LessOrEqual)
                       .setAlphaBlend()
                       .setName("SpriteWorldPremultiplied");
            auto premulDesc = worldPremul.buildGraphics();
            if (premulDesc.blend.attachments.empty()) premulDesc.blend.attachments.resize(1);
            auto& att = premulDesc.blend.attachments[0];
            att.blendEnable = true;
            att.srcColorBlendFactor = rhi::BlendFactor::One;
            att.dstColorBlendFactor = rhi::BlendFactor::OneMinusSrcAlpha;
            att.colorBlendOp = rhi::BlendOp::Add;
            att.srcAlphaBlendFactor = rhi::BlendFactor::One;
            att.dstAlphaBlendFactor = rhi::BlendFactor::OneMinusSrcAlpha;
            att.alphaBlendOp = rhi::BlendOp::Add;
            m_worldPremultipliedPipeline = m_renderer.createGraphicsPipeline(premulDesc);
        }

        // UI pipelines (depth off)
        {
            auto uiAlpha = base;
            uiAlpha.disableDepthTest()
                   .setAlphaBlend()
                   .setName("SpriteUIAlpha");
            m_uiAlphaPipeline = m_renderer.createGraphicsPipeline(uiAlpha.buildGraphics());

            auto uiAdd = base;
            uiAdd.disableDepthTest()
                 .setAdditiveBlend()
                 .setName("SpriteUIAdditive");
            m_uiAdditivePipeline = m_renderer.createGraphicsPipeline(uiAdd.buildGraphics());

            auto uiPremul = base;
            uiPremul.disableDepthTest()
                    .setAlphaBlend()
                    .setName("SpriteUIPremultiplied");
            auto premulDesc = uiPremul.buildGraphics();
            if (premulDesc.blend.attachments.empty()) premulDesc.blend.attachments.resize(1);
            auto& att = premulDesc.blend.attachments[0];
            att.blendEnable = true;
            att.srcColorBlendFactor = rhi::BlendFactor::One;
            att.dstColorBlendFactor = rhi::BlendFactor::OneMinusSrcAlpha;
            att.colorBlendOp = rhi::BlendOp::Add;
            att.srcAlphaBlendFactor = rhi::BlendFactor::One;
            att.dstAlphaBlendFactor = rhi::BlendFactor::OneMinusSrcAlpha;
            att.alphaBlendOp = rhi::BlendOp::Add;
            m_uiPremultipliedPipeline = m_renderer.createGraphicsPipeline(premulDesc);
        }

        auto* pipeline = m_renderer.getPipeline(m_worldAlphaPipeline);
        if (pipeline != nullptr) m_instanceLayout = pipeline->descriptorSetLayout(0);
    }

    void SpriteRenderer::ensureFrameCapacity(FrameResources& frame, size_t requiredInstances, uint32_t frameIndex)
    {
        bool recreated = false;
        if (requiredInstances > frame.capacityInstances || !frame.instanceBuffer)
        {
            size_t newCapacity = std::max(requiredInstances, frame.capacityInstances * 2u);
            if (newCapacity < kDefaultCapacity) newCapacity = kDefaultCapacity;

            // Debug Name for RenderDoc
            std::string debugName = "SpriteBuffer_F" + std::to_string(frameIndex);

            frame.instanceBuffer = m_renderer.device()->createBuffer({
                .size = newCapacity * sizeof(SpriteInstanceGPU),
                .usage = rhi::BufferUsage::StorageBuffer,
                .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                .debugName = debugName.c_str()
            });

            frame.capacityInstances = newCapacity;
            recreated = true;
            
            // Log reallocation for visibility
            core::Logger::debug("SpriteRenderer: Resized buffer Frame[{}] to {} sprites", frameIndex, newCapacity);
        }

        if (!frame.descriptorSet && m_instanceLayout != nullptr)
        {
            frame.descriptorSet = m_renderer.device()->allocateDescriptorSet(m_instanceLayout);
        }

        if ((recreated || frame.descriptorSet) && frame.instanceBuffer)
        {
            frame.descriptorSet->updateBuffer(0, frame.instanceBuffer.get(), 0, frame.instanceBuffer->size());
        }
    }

    void SpriteRenderer::uploadAndDraw(rhi::RHICommandBuffer* cmd,
                                       const Camera& camera,
                                       uint32_t viewportW,
                                       uint32_t viewportH,
                                       uint32_t frameIndex,
                                       std::span<const Sprite*> sprites)
    {
        if (sprites.empty() || !m_quadMesh) return;

        struct DrawItem
        {
            uint64_t key = 0;
            SpriteInstanceGPU inst{};
        };

        // Buckets:
        //  - WorldCutout (no blending)
        //  - WorldTranslucent: alpha/additive/premul
        //  - UI: alpha/additive/premul
        std::vector<DrawItem> worldCutout;
        std::vector<DrawItem> worldAlpha;
        std::vector<DrawItem> worldAdd;
        std::vector<DrawItem> worldPremul;
        std::vector<DrawItem> uiAlpha;
        std::vector<DrawItem> uiAdd;
        std::vector<DrawItem> uiPremul;

        worldCutout.reserve(sprites.size() / 4);
        worldAlpha.reserve(sprites.size());
        worldAdd.reserve(sprites.size() / 4);
        worldPremul.reserve(sprites.size() / 4);
        uiAlpha.reserve(sprites.size() / 2);
        uiAdd.reserve(sprites.size() / 8);
        uiPremul.reserve(sprites.size() / 8);

        uint32_t seq = 0;

        for (const Sprite* sprite : sprites)
        {
            if (!sprite || !sprite->alive) continue;

            uint32_t textureIndex = (sprite->textureBindlessIndex == 0xFFFFFFFFu) ? m_whiteTextureIndex : sprite->textureBindlessIndex;
            uint32_t samplerIndex = (sprite->samplerIndex == 0xFFFFFFFFu) ? m_defaultSamplerIndex : sprite->samplerIndex;
            uint32_t flags = (sprite->space == SpriteSpace::Screen) ? kFlagScreenSpace : 0u;

            SpriteInstanceGPU instance{};
            instance.pos_space = glm::vec4(sprite->position, 0.0F);
            instance.size_rot = glm::vec4(sprite->size, sprite->rotation, 0.0F);
            instance.color = sprite->color;
            instance.tex = glm::uvec4(textureIndex, samplerIndex, flags, 0u);
            instance.uvRect = glm::vec4(sprite->uvMin, sprite->uvMax);
            instance.pivot_cutoff = glm::vec4(sprite->pivot, sprite->alphaCutoff, 0.0F);

            // Resolve effective pass (Auto behavior)
            SpritePass pass = sprite->pass;
            if (pass == SpritePass::Auto)
            {
                pass = (sprite->space == SpriteSpace::Screen) ? SpritePass::UI : SpritePass::WorldTranslucent;
            }

            // Build sort keys
            const uint32_t orderKey = orderToKey(sprite->order);
            uint64_t key = 0;

            if (pass == SpritePass::UI)
            {
                // UI: primary (layer, order), then stable seq
                key = (uint64_t(sprite->layer) << 48) |
                      (uint64_t(orderKey) << 32) |
                      uint64_t(seq);
            }
            else
            {
                // World: depth sort using clip-space z/w (0..1 in Vulkan depth convention)
                const glm::vec4 clip = camera.viewProj() * glm::vec4(sprite->position, 1.0f);
                float ndcZ = 0.0f;
                if (clip.w != 0.0f) ndcZ = clip.z / clip.w;
                const uint32_t depthKey = floatToSortableUint(ndcZ);

                if (pass == SpritePass::WorldCutout)
                {
                    // Front-to-back to reduce overdraw (smaller depth first)
                    key = (uint64_t(depthKey) << 32) | uint64_t(seq);
                }
                else
                {
                    // Back-to-front for translucency (larger depth first)
                    const uint32_t invDepth = 0xFFFFFFFFu - depthKey;
                    // Include layer/order as tie-breaker (optional but useful)
                    key = (uint64_t(invDepth) << 32) | uint64_t(seq);
                }
            }

            DrawItem item{};
            item.key = key;
            item.inst = instance;

            // Route to bucket/pipeline
            if (pass == SpritePass::WorldCutout)
            {
                worldCutout.push_back(item);
            }
            else if (pass == SpritePass::UI)
            {
                switch (sprite->blend)
                {
                case SpriteBlendMode::Additive: uiAdd.push_back(item); break;
                case SpriteBlendMode::Premultiplied: uiPremul.push_back(item); break;
                case SpriteBlendMode::Alpha: default: uiAlpha.push_back(item); break;
                }
            }
            else
            {
                switch (sprite->blend)
                {
                case SpriteBlendMode::Additive: worldAdd.push_back(item); break;
                case SpriteBlendMode::Premultiplied: worldPremul.push_back(item); break;
                case SpriteBlendMode::Alpha: default: worldAlpha.push_back(item); break;
                }
            }

            ++seq;
        }

        auto sortByKey = [](std::vector<DrawItem>& v)
        {
            std::sort(v.begin(), v.end(), [](const DrawItem& a, const DrawItem& b) { return a.key < b.key; });
        };

        sortByKey(worldCutout);
        sortByKey(worldAlpha);
        sortByKey(worldAdd);
        sortByKey(worldPremul);
        sortByKey(uiAlpha);
        sortByKey(uiAdd);
        sortByKey(uiPremul);

        const size_t totalInstances =
            worldCutout.size() +
            worldAlpha.size() + worldAdd.size() + worldPremul.size() +
            uiAlpha.size() + uiAdd.size() + uiPremul.size();
        if (totalInstances == 0) return;

        // FIXED: Ring buffer logic
        size_t frameResIdx = frameIndex % m_frames.size();
        auto& frame = m_frames[frameResIdx];
        
        ensureFrameCapacity(frame, totalInstances, static_cast<uint32_t>(frameResIdx));

        if (!frame.instanceBuffer || !frame.descriptorSet) return;

        std::vector<SpriteInstanceGPU> packed;
        packed.reserve(totalInstances);

        auto append = [&](const std::vector<DrawItem>& src)
        {
            for (const auto& it : src) packed.push_back(it.inst);
        };

        const uint32_t worldCutoutOffset = 0u;
        append(worldCutout);
        const uint32_t worldAlphaOffset = static_cast<uint32_t>(packed.size());
        append(worldAlpha);
        const uint32_t worldAddOffset = static_cast<uint32_t>(packed.size());
        append(worldAdd);
        const uint32_t worldPremulOffset = static_cast<uint32_t>(packed.size());
        append(worldPremul);
        const uint32_t uiAlphaOffset = static_cast<uint32_t>(packed.size());
        append(uiAlpha);
        const uint32_t uiAddOffset = static_cast<uint32_t>(packed.size());
        append(uiAdd);
        const uint32_t uiPremulOffset = static_cast<uint32_t>(packed.size());
        append(uiPremul);

        frame.instanceBuffer->uploadData(packed.data(), packed.size() * sizeof(SpriteInstanceGPU), 0);

        glm::mat4 invView = glm::inverse(camera.view());
        glm::vec3 camRight = glm::vec3(invView[0]);
        glm::vec3 camUp = glm::vec3(invView[1]);

        uint32_t safeW = viewportW > 0 ? viewportW : 1u;
        uint32_t safeH = viewportH > 0 ? viewportH : 1u;

        ShaderGen::sprite_billboard_vert::SpritePushConstants pc{};
        pc.viewProj = camera.viewProj();
        pc.camRight = glm::vec4(camRight, 0.0F);
        pc.camUp = glm::vec4(camUp, 0.0F);
        pc.viewport = glm::vec4(static_cast<float>(safeW),
                                static_cast<float>(safeH),
                                1.0F / static_cast<float>(safeW),
                                1.0F / static_cast<float>(safeH));

        auto drawBatch = [&](PipelineHandle pipeline,
                             uint32_t instanceCount,
                             uint32_t firstInstance)
        {
            if (instanceCount == 0) return;

            m_renderer.bindPipeline(cmd, pipeline);
            m_renderer.bindMesh(cmd, m_quadMesh);
            m_renderer.bindDescriptorSet(cmd, pipeline, 0, frame.descriptorSet.get());
            m_renderer.pushConstants(cmd, pipeline, rhi::ShaderStage::Vertex, pc);

            cmd->drawIndexed(6, instanceCount, 0, 0, firstInstance);
        };

        drawBatch(m_worldCutoutPipeline, static_cast<uint32_t>(worldCutout.size()), worldCutoutOffset);
        drawBatch(m_worldAlphaPipeline, static_cast<uint32_t>(worldAlpha.size()), worldAlphaOffset);
        drawBatch(m_worldAdditivePipeline, static_cast<uint32_t>(worldAdd.size()), worldAddOffset);
        drawBatch(m_worldPremultipliedPipeline, static_cast<uint32_t>(worldPremul.size()), worldPremulOffset);
        drawBatch(m_uiAlphaPipeline, static_cast<uint32_t>(uiAlpha.size()), uiAlphaOffset);
        drawBatch(m_uiAdditivePipeline, static_cast<uint32_t>(uiAdd.size()), uiAddOffset);
        drawBatch(m_uiPremultipliedPipeline, static_cast<uint32_t>(uiPremul.size()), uiPremulOffset);
    }
} // namespace pnkr::renderer::scene
