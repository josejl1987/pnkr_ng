#include "pnkr/renderer/scene/SpriteRenderer.hpp"

#include "pnkr/core/logger.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/renderer/passes/RenderPassUtils.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/renderer/scene/Sprite.hpp"
#include "pnkr/renderer/gpu_shared/SpriteShared.h"

#include <algorithm>
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
        static_assert(sizeof(gpu::SpriteInstanceGPU) % 16 == 0, "SpriteInstanceGPU must be 16-byte aligned");

        constexpr uint32_t K_FLAG_SCREEN_SPACE = 1U << 0U;
        constexpr size_t K_DEFAULT_FRAME_COUNT = 3;
        constexpr size_t kDefaultCapacity = 1024;

        uint32_t floatToSortableUint(float f) {

          auto u = std::bit_cast<uint32_t>(f);
          return ((u & 0x80000000U) != 0u) ? ~u : (u | 0x80000000U);
        }

        uint32_t orderToKey(int16_t order) {
          return static_cast<uint16_t>(order + 32768);
        }
    }

    SpriteRenderer::SpriteRenderer(RHIRenderer& renderer)
        : m_renderer(renderer)
    {
        m_whiteTextureIndex = m_renderer.getTextureBindlessIndex(m_renderer.getWhiteTexture());
        m_defaultSamplerIndex = m_renderer.getBindlessSamplerIndex(rhi::SamplerAddressMode::Repeat);

        createQuadMesh();
        createPipelines();

        m_frames.resize(K_DEFAULT_FRAME_COUNT);

        for (uint32_t i = 0; i < m_frames.size(); ++i)
        {
            ensureFrameCapacity(m_frames[i], kDefaultCapacity, i);
        }
    }

    void SpriteRenderer::createQuadMesh()
    {
        std::vector<Vertex> vertices(4);
        vertices[0].uv0 = {0.0F, 0.0F}; vertices[1].uv0 = {1.0F, 0.0F};
        vertices[2].uv0 = {1.0F, 1.0F}; vertices[3].uv0 = {0.0F, 1.0F};
        vertices[0].uv1 = {-0.5F, -0.5F}; vertices[1].uv1 = {0.5F, -0.5F};
        vertices[2].uv1 = {0.5F, 0.5F}; vertices[3].uv1 = {-0.5F, 0.5F};
        std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};
        m_quadMesh = m_renderer.createMesh(vertices, indices, false);
    }

    void SpriteRenderer::createPipelines()
    {
        using namespace passes::utils;
        auto shaders = loadGraphicsShaders("shaders/sprite_billboard.vert.spv", "shaders/sprite_billboard.frag.spv", "SpriteRenderer");
        if (!shaders.success) { return; }

        auto base = rhi::RHIPipelineBuilder()
                               .setShaders(shaders.vertex.get(), shaders.fragment.get(), nullptr)
                               .useVertexType<Vertex>()
                               .setTopology(rhi::PrimitiveTopology::TriangleList)
                               .setCullMode(rhi::CullMode::None)
                               .setColorFormat(m_renderer.getDrawColorFormat())
                               .setDepthFormat(m_renderer.getDrawDepthFormat());

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
            if (premulDesc.blend.attachments.empty()) {
              premulDesc.blend.attachments.resize(1);
            }
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
            if (premulDesc.blend.attachments.empty()) {
              premulDesc.blend.attachments.resize(1);
            }
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
        if (pipeline != nullptr) {
          m_instanceLayout = pipeline->descriptorSetLayout(0);
        }
    }

    void SpriteRenderer::ensureFrameCapacity(FrameResources& frame, size_t requiredInstances, uint32_t frameIndex)
    {
        bool recreated = false;
        if (requiredInstances > frame.capacityInstances || !frame.instanceBuffer)
        {
          size_t newCapacity =
              std::max(requiredInstances, frame.capacityInstances * 2U);
          newCapacity = std::max(newCapacity, kDefaultCapacity);

          std::string debugName = "SpriteBuffer_F" + std::to_string(frameIndex);

          frame.instanceBuffer = m_renderer.device()->createBuffer(
              {.size = newCapacity * sizeof(gpu::SpriteInstanceGPU),
               .usage = rhi::BufferUsage::StorageBuffer,
               .memoryUsage = rhi::MemoryUsage::CPUToGPU,
               .debugName = debugName});

          frame.capacityInstances = newCapacity;
          recreated = true;

          core::Logger::Scene.debug(
              "SpriteRenderer: Resized buffer Frame[{}] to {} sprites",
              frameIndex, newCapacity);
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

    void SpriteRenderer::uploadAndDraw(rhi::RHICommandList* cmd,
                                       const Camera& camera,
                                       uint32_t viewportW,
                                       uint32_t viewportH,
                                       uint32_t frameIndex,
                                       std::span<const Sprite*> sprites)
    {
      if (sprites.empty() || !m_quadMesh) {
        return;
      }

        struct DrawItem
        {
          uint64_t m_key = 0;
          gpu::SpriteInstanceGPU m_inst{};
        };

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
          if ((sprite == nullptr) || !sprite->alive) {
            continue;
          }

            uint32_t textureIndex = (!sprite->textureBindlessIndex.isValid()) ? util::u32(m_whiteTextureIndex) : util::u32(sprite->textureBindlessIndex);
            uint32_t samplerIndex = (!sprite->samplerIndex.isValid()) ? util::u32(m_defaultSamplerIndex) : util::u32(sprite->samplerIndex);

            uint32_t flags = (sprite->space == SpriteSpace::Screen)
                                 ? K_FLAG_SCREEN_SPACE
                                 : 0U;

            gpu::SpriteInstanceGPU instance{};
            instance.pos_space = glm::vec4(sprite->position, 0.0F);
            instance.size_rot = glm::vec4(sprite->size, sprite->rotation, 0.0F);
            instance.color = sprite->color;
            instance.tex = glm::uvec4(textureIndex, samplerIndex, flags, 0U);
            instance.uvRect = glm::vec4(sprite->uvMin, sprite->uvMax);
            instance.pivot_cutoff = glm::vec4(sprite->pivot, sprite->alphaCutoff, 0.0F);

            SpritePass pass = sprite->pass;
            if (pass == SpritePass::Auto)
            {
                pass = (sprite->space == SpriteSpace::Screen) ? SpritePass::UI : SpritePass::WorldTranslucent;
            }

            const uint32_t orderKey = orderToKey(sprite->order);
            uint64_t key = 0;

            if (pass == SpritePass::UI)
            {

                key = (uint64_t(sprite->layer) << 48) |
                      (uint64_t(orderKey) << 32) |
                      uint64_t(seq);
            }
            else
            {

              const glm::vec4 clip =
                  camera.viewProj() * glm::vec4(sprite->position, 1.0F);
              float ndcZ = 0.0F;
              if (clip.w != 0.0F) {
                ndcZ = clip.z / clip.w;
              }
                const uint32_t depthKey = floatToSortableUint(ndcZ);

                if (pass == SpritePass::WorldCutout)
                {

                    key = (uint64_t(depthKey) << 32) | uint64_t(seq);
                }
                else
                {

                    const uint32_t invDepth = BINDLESS_INVALID_ID - depthKey;

                    key = (uint64_t(invDepth) << 32) | uint64_t(seq);
                }
            }

            DrawItem item{};
            item.m_key = key;
            item.m_inst = instance;

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

        auto sortByKey = [](std::vector<DrawItem> &v) {
          std::ranges::sort(v, [](const DrawItem &a, const DrawItem &b) {
            return a.m_key < b.m_key;
          });
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
        if (totalInstances == 0) {
          return;
        }

        size_t frameResIdx = frameIndex % m_frames.size();
        auto& frame = m_frames[frameResIdx];

        ensureFrameCapacity(frame, totalInstances, util::u32(frameResIdx));

        if (!frame.instanceBuffer || !frame.descriptorSet) {
          return;
        }

        std::vector<gpu::SpriteInstanceGPU> packed;
        packed.reserve(totalInstances);

        auto append = [&](const std::vector<DrawItem> &src) {
          for (const auto &it : src) {
            packed.push_back(it.m_inst);
          }
        };

        const uint32_t worldCutoutOffset = 0U;
        append(worldCutout);
        const uint32_t worldAlphaOffset = util::u32(packed.size());
        append(worldAlpha);
        const uint32_t worldAddOffset = util::u32(packed.size());
        append(worldAdd);
        const uint32_t worldPremulOffset = util::u32(packed.size());
        append(worldPremul);
        const uint32_t uiAlphaOffset = util::u32(packed.size());
        append(uiAlpha);
        const uint32_t uiAddOffset = util::u32(packed.size());
        append(uiAdd);
        const uint32_t uiPremulOffset = util::u32(packed.size());
        append(uiPremul);

        frame.instanceBuffer->uploadData(std::span<const std::byte>(reinterpret_cast<const std::byte*>(packed.data()), packed.size() * sizeof(gpu::SpriteInstanceGPU)), 0);

        glm::mat4 invView = glm::inverse(camera.view());
        auto camRight = glm::vec3(invView[0]);
        auto camUp = glm::vec3(invView[1]);

        uint32_t safeW = viewportW > 0 ? viewportW : 1U;
        uint32_t safeH = viewportH > 0 ? viewportH : 1U;

        gpu::SpritePushConstants pc{};
        pc.viewProj = camera.viewProj();
        pc.camRight = glm::vec4(camRight, 0.0F);
        pc.camUp = glm::vec4(camUp, 0.0F);
        pc.viewport = glm::vec4(static_cast<float>(safeW),
                                static_cast<float>(safeH),
                                1.0F / static_cast<float>(safeW),
                                1.0F / static_cast<float>(safeH));

        auto drawBatch = [&](PipelineHandle pipeline, uint32_t instanceCount,
                             uint32_t firstInstance) {
          if (instanceCount == 0) {
            return;
          }

          cmd->bindPipeline(m_renderer.getPipeline(pipeline));
          auto meshView = m_renderer.getMeshView(m_quadMesh);
          if (!meshView) {
            return;
          }
          if (!meshView->vertexPulling) {
            cmd->bindVertexBuffer(0, meshView->vertexBuffer, 0);
          }
          cmd->bindIndexBuffer(meshView->indexBuffer, 0, false);
          cmd->bindDescriptorSet(0, frame.descriptorSet.get());
          cmd->pushConstants(rhi::ShaderStage::Vertex, pc);

          cmd->drawIndexed(6, instanceCount, 0, 0, firstInstance);
        };

        drawBatch(m_worldCutoutPipeline, util::u32(worldCutout.size()), worldCutoutOffset);
        drawBatch(m_worldAlphaPipeline, util::u32(worldAlpha.size()), worldAlphaOffset);
        drawBatch(m_worldAdditivePipeline, util::u32(worldAdd.size()), worldAddOffset);
        drawBatch(m_worldPremultipliedPipeline, util::u32(worldPremul.size()), worldPremulOffset);
        drawBatch(m_uiAlphaPipeline, util::u32(uiAlpha.size()), uiAlphaOffset);
        drawBatch(m_uiAdditivePipeline, util::u32(uiAdd.size()), uiAddOffset);
        drawBatch(m_uiPremultipliedPipeline, util::u32(uiPremul.size()), uiPremulOffset);
    }
}
