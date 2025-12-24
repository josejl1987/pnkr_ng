#include "pnkr/renderer/scene/SpriteRenderer.hpp"

#include "pnkr/core/logger.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/renderer/scene/Sprite.hpp"
#include "generated/sprite_billboard.vert.h"

#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <string>

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
            glm::vec4 pivot_pad;
        };

        static_assert(sizeof(SpriteInstanceGPU) % 16 == 0, "SpriteInstanceGPU must be 16-byte aligned");

        constexpr uint32_t kFlagScreenSpace = 1u << 0u;
        constexpr size_t kDefaultFrameCount = 3;
        constexpr size_t kDefaultCapacity = 1024;
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

        auto baseBuilder = rhi::RHIPipelineBuilder()
                               .setShaders(vertShader.get(), fragShader.get(), nullptr)
                               .useVertexType<Vertex>()
                               .setTopology(rhi::PrimitiveTopology::TriangleList)
                               .setCullMode(rhi::CullMode::None)
                               .enableDepthTest(false, rhi::CompareOp::LessOrEqual)
                               .setColorFormat(m_renderer.getDrawColorFormat())
                               .setDepthFormat(m_renderer.getDrawDepthFormat());

        auto alphaDesc = baseBuilder;
        alphaDesc.setAlphaBlend().setName("SpriteAlpha");
        m_alphaPipeline = m_renderer.createGraphicsPipeline(alphaDesc.buildGraphics());

        auto additiveDesc = baseBuilder;
        additiveDesc.setAdditiveBlend().setName("SpriteAdditive");
        m_additivePipeline = m_renderer.createGraphicsPipeline(additiveDesc.buildGraphics());

        auto premulDesc = baseBuilder;
        premulDesc.setAlphaBlend().setName("SpritePremultiplied");
        auto premul = premulDesc.buildGraphics();
        if (premul.blend.attachments.empty()) premul.blend.attachments.resize(1);
        auto& att = premul.blend.attachments[0];
        att.blendEnable = true;
        att.srcColorBlendFactor = rhi::BlendFactor::One;
        att.dstColorBlendFactor = rhi::BlendFactor::OneMinusSrcAlpha;
        att.colorBlendOp = rhi::BlendOp::Add;
        att.srcAlphaBlendFactor = rhi::BlendFactor::One;
        att.dstAlphaBlendFactor = rhi::BlendFactor::OneMinusSrcAlpha;
        att.alphaBlendOp = rhi::BlendOp::Add;
        m_premultipliedPipeline = m_renderer.createGraphicsPipeline(premul);

        auto* pipeline = m_renderer.getPipeline(m_alphaPipeline);
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

        std::vector<SpriteInstanceGPU> alphaBatch;
        std::vector<SpriteInstanceGPU> additiveBatch;
        std::vector<SpriteInstanceGPU> premulBatch;

        alphaBatch.reserve(sprites.size());
        additiveBatch.reserve(sprites.size() / 4);
        premulBatch.reserve(sprites.size() / 4);

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
            instance.pivot_pad = glm::vec4(sprite->pivot, 0.0F, 0.0F);

            switch (sprite->blend)
            {
            case SpriteBlendMode::Additive: additiveBatch.push_back(instance); break;
            case SpriteBlendMode::Premultiplied: premulBatch.push_back(instance); break;
            case SpriteBlendMode::Alpha: default: alphaBatch.push_back(instance); break;
            }
        }

        const size_t totalInstances = alphaBatch.size() + additiveBatch.size() + premulBatch.size();
        if (totalInstances == 0) return;

        // FIXED: Ring buffer logic
        size_t frameResIdx = frameIndex % m_frames.size();
        auto& frame = m_frames[frameResIdx];
        
        ensureFrameCapacity(frame, totalInstances, static_cast<uint32_t>(frameResIdx));

        if (!frame.instanceBuffer || !frame.descriptorSet) return;

        std::vector<SpriteInstanceGPU> packed;
        packed.reserve(totalInstances);
        const uint32_t alphaOffset = 0u;
        packed.insert(packed.end(), alphaBatch.begin(), alphaBatch.end());
        const uint32_t additiveOffset = static_cast<uint32_t>(packed.size());
        packed.insert(packed.end(), additiveBatch.begin(), additiveBatch.end());
        const uint32_t premulOffset = static_cast<uint32_t>(packed.size());
        packed.insert(packed.end(), premulBatch.begin(), premulBatch.end());

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

        drawBatch(m_alphaPipeline, static_cast<uint32_t>(alphaBatch.size()), alphaOffset);
        drawBatch(m_additivePipeline, static_cast<uint32_t>(additiveBatch.size()), additiveOffset);
        drawBatch(m_premultipliedPipeline, static_cast<uint32_t>(premulBatch.size()), premulOffset);
    }
} // namespace pnkr::renderer::scene
