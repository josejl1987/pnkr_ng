#include "pnkr/renderer/renderer.hpp"
#include "pnkr/core/common.hpp"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include "pnkr/platform/window.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/renderer/vulkan/vulkan_command_buffer.hpp"
#include "pnkr/renderer/vulkan/vulkan_context.hpp"
#include "pnkr/renderer/vulkan/vulkan_device.hpp"
#include "pnkr/renderer/vulkan/vulkan_pipeline.hpp"
#include "pnkr/renderer/vulkan/vulkan_swapchain.hpp"
#include "pnkr/renderer/vulkan/vulkan_sync_manager.h"
#include "pnkr/renderer/vulkan/vulkan_descriptor.hpp"
#include "pnkr/renderer/vulkan/vulkan_render_target.h"
#include <array>
#include <cstddef>

using namespace pnkr::util;

namespace pnkr::renderer
{
    Renderer::Renderer(platform::Window& window,
                       [[maybe_unused]] const RendererConfig& config)
        : m_window(window)
          , m_useBindlessForCurrentFrame(config.m_useBindless)
    {
        m_context = std::make_unique<VulkanContext>(window);
        m_device = std::make_unique<VulkanDevice>(*m_context);

        m_swapchain = std::make_unique<VulkanSwapchain>(
            m_device->physicalDevice(), m_device->device(), m_context->surface(),
            m_device->queueFamilies().graphics, m_device->queueFamilies().present,
            window, m_device->allocator());

        m_commandBuffer = std::make_unique<VulkanCommandBuffer>(*m_device);

        // Tracy GPU context calibration using a one-time command buffer
        {
            vk::CommandBufferAllocateInfo allocInfo{};
            allocInfo.commandPool = m_commandBuffer->commandPool();
            allocInfo.level = vk::CommandBufferLevel::ePrimary;
            allocInfo.commandBufferCount = 1;

            vk::CommandBuffer tmpCmd = m_device->device().allocateCommandBuffers(allocInfo)[0];

            vk::CommandBufferBeginInfo beginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
            tmpCmd.begin(beginInfo);

            m_tracyCtx = PNKR_PROFILE_GPU_CONTEXT(
                static_cast<VkPhysicalDevice>(m_device->physicalDevice()),
                static_cast<VkDevice>(m_device->device()),
                static_cast<VkQueue>(m_device->graphicsQueue()),
                static_cast<VkCommandBuffer>(tmpCmd)
            );

            tmpCmd.end();

            vk::SubmitInfo submitInfo{};
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &tmpCmd;

            m_device->graphicsQueue().submit(submitInfo, nullptr);
            m_device->device().waitIdle();

            m_device->device().freeCommandBuffers(m_commandBuffer->commandPool(), tmpCmd);
        }

        m_mainTarget = std::make_unique<VulkanRenderTarget>(
            m_device->allocator(),
            m_device->device(),
            m_swapchain->extent().width,
            m_swapchain->extent().height,
            vk::Format::eR16G16B16A16Sfloat, // HDR Color
            vk::Format::eD32Sfloat // High precision depth
        );


        if (config.m_enableBindless) {
            m_bindless = std::make_unique<BindlessManager>(
                m_device->device(),
                m_device->physicalDevice()
            );
            core::Logger::info("[Renderer] Bindless support AVAILABLE (toggle at runtime)");
        } else {
            core::Logger::info("[Renderer] Bindless support DISABLED (set config.enableBindless=true to enable)");
        }

        m_sync = std::make_unique<VulkanSyncManager>(
            m_device->device(), m_device->framesInFlight(),
            u32(m_swapchain->images().size()));

        m_descriptorAllocator = std::make_unique<VulkanDescriptorAllocator>(m_device->device());
        m_descriptorLayoutCache = std::make_unique<VulkanDescriptorLayoutCache>(m_device->device());
        m_defaultSampler = std::make_unique<VulkanSampler>(m_device->device());
        createTextureDescriptorSetLayout();

        // Default 1x1 white texture to safely bind when materials reference no texture
        const unsigned char white[4] = {255, 255, 255, 255};
        m_whiteTexture = createTextureFromPixels(white, 1, 1, 4, true);
    }

    void Renderer::createTextureDescriptorSetLayout()
    {
        vk::DescriptorSetLayoutBinding samplerBinding{};
        samplerBinding.binding = 0;
        samplerBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        samplerBinding.descriptorCount = 1;
        samplerBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &samplerBinding;

        m_textureSetLayout = m_device->device().createDescriptorSetLayout(layoutInfo);
    }


    TextureHandle Renderer::createTextureFromPixels(const unsigned char* pixels,
                                                    int width,
                                                    int height,
                                                    int channels,
                                                    bool srgb)
    {
        // 1. Validate inputs
        if (width <= 0 || height <= 0 || (pixels == nullptr))
        {
            throw std::runtime_error("Invalid texture data");
        }

        std::vector<unsigned char> rgbaPixels;

        const unsigned char* srcData = pixels;

        // Normalize to RGBA to match staging buffer size expectations
        if (channels != 4)
        {
            if (channels <= 0)
            {
                throw std::runtime_error("Unsupported channel count for texture");
            }

            rgbaPixels.resize(sz(width * height * 4));
            for (int i = 0; i < width * height; ++i)
            {
                const unsigned char r = pixels[(i * channels) + 0];
                const unsigned char g = (channels >= 2) ? pixels[(i * channels) + 1] : r;
                const unsigned char b = (channels >= 3) ? pixels[(i * channels) + 2] : r;
                const unsigned char a = (channels >= 4) ? pixels[(i * channels) + 3] : 255;

                rgbaPixels[(i * 4) + 0] = r;
                rgbaPixels[(i * 4) + 1] = g;
                rgbaPixels[(i * 4) + 2] = b;
                rgbaPixels[(i * 4) + 3] = a;
            }
            srcData = rgbaPixels.data();
        }


        auto texture = std::make_unique<VulkanImage>(
            VulkanImage::createFromMemory(*m_device, srcData, width, height, srgb)
        );
        BindlessIndex bindlessIndex = m_bindless->registerSampledImage(
            texture->view(),
            m_defaultSampler->sampler()
        );
        // 4. Create Descriptor
        vk::DescriptorImageInfo imageInfo{};
        imageInfo.sampler = m_defaultSampler->sampler();
        imageInfo.imageView = texture->view();
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::DescriptorSet descriptorSet;
        VulkanDescriptorBuilder::begin(m_descriptorLayoutCache.get(), m_descriptorAllocator.get())
            .bindImage(0, &imageInfo, vk::DescriptorType::eCombinedImageSampler,
                       vk::ShaderStageFlagBits::eFragment)
            .build(descriptorSet);

        TextureHandle handle{u32(m_textures.size())};
        m_textures.push_back(std::move(texture));
        m_textureDescriptors.push_back(descriptorSet);
        m_textureBindlessIndices.push_back(bindlessIndex);

        return handle;
    }

    uint32_t Renderer::getTextureBindlessIndex(TextureHandle handle) const
    {
        if (false) {
            // Return 0 as dummy - caller should check isBindlessEnabled()
            return 0;
        }

        if (!handle || handle.id >= m_textureBindlessIndices.size()) {
            // Fallback to white texture
            return m_textureBindlessIndices[m_whiteTexture.id].raw();
        }

        return m_textureBindlessIndices[handle.id].raw();
    }

    TextureHandle Renderer::loadTexture(const std::filesystem::path& filepath, bool srgb)
    {
        auto texture = std::make_unique<VulkanImage>(
            VulkanImage::createFromFile(*m_device, filepath, srgb)
        );


        BindlessIndex bindlessIndex = m_bindless->registerSampledImage(
            texture->view(),
            m_defaultSampler->sampler()
        );


        // Create descriptor set for this texture
        vk::DescriptorImageInfo imageInfo{};
        imageInfo.sampler = m_defaultSampler->sampler();
        imageInfo.imageView = texture->view();
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::DescriptorSet descriptorSet;
        VulkanDescriptorBuilder::begin(m_descriptorLayoutCache.get(), m_descriptorAllocator.get())
            .bindImage(0, &imageInfo, vk::DescriptorType::eCombinedImageSampler,
                       vk::ShaderStageFlagBits::eFragment)
            .build(descriptorSet);

        TextureHandle handle{u32(m_textures.size())};
        m_textures.push_back(std::move(texture));
        m_textureDescriptors.push_back(descriptorSet);
        m_textureBindlessIndices.push_back(bindlessIndex); // NEW
        core::Logger::info("Loaded texture: {} (handle={})", filepath.string(), handle.id);
        return handle;
    }

    vk::DescriptorSet Renderer::getTextureDescriptor(TextureHandle handle) const
    {
        const bool fallback = (!handle) || (handle.id >= m_textureDescriptors.size());
        if (fallback)
        {
            if (m_textureDescriptors.empty() ||
                !m_whiteTexture ||
                m_whiteTexture.id >= m_textureDescriptors.size())
            {
                throw std::runtime_error("Default texture descriptor not initialized");
            }
            return m_textureDescriptors[m_whiteTexture.id];
        }
        return m_textureDescriptors[handle.id];
    }

    vk::DescriptorSetLayout Renderer::getTextureDescriptorLayout() const
    {
        return m_textureSetLayout;
    }


    PipelineHandle Renderer::createPipeline(const VulkanPipeline::Config& cfg)
    {
        PipelineHandle handle{u32(m_pipelines.size())};

        PipelineConfig pipelineCfg = cfg;

        m_pipelines.push_back(std::make_unique<VulkanPipeline>(
            m_device->device(), pipelineCfg));

        pnkr::core::Logger::info("[Renderer] Created pipeline handle={}", handle.id);
        return handle;
    }

    const VulkanPipeline& Renderer::pipeline(PipelineHandle handle) const
    {
        if (handle.id >= m_pipelines.size())
        {
            throw std::runtime_error("[Renderer] Invalid pipeline handle: " +
                std::to_string(handle.id));
        }
        return *m_pipelines[handle.id];
    }

    vk::PipelineLayout Renderer::pipelineLayout(PipelineHandle handle) const
    {
        if (handle.id >= m_pipelines.size()) {
            throw std::runtime_error("[Renderer] Invalid pipeline handle");
}
        return m_pipelines[handle.id]->layout();
    }

    MeshHandle Renderer::createMesh(const std::vector<Vertex>& vertices,
                                    const std::vector<uint32_t>& indices)
    {
        if (vertices.empty() || indices.empty()) {
            throw std::runtime_error("[Renderer] createMesh: empty data");
}

        MeshHandle handle{u32(m_meshes.size())};

        m_meshes.push_back(std::make_unique<Mesh>(*m_device, vertices, indices));

        pnkr::core::Logger::info("[Renderer] Created mesh handle={}", handle.id);
        return handle;
    }

    void Renderer::bindMesh(vk::CommandBuffer cmd, MeshHandle handle) const
    {
        if (handle.id >= m_meshes.size()) {
            throw std::runtime_error("[Renderer] Invalid mesh handle: out of range");
}

        const auto& mesh = m_meshes[handle.id];
        if (!mesh) {
            throw std::runtime_error("[Renderer] Invalid mesh handle: null mesh slot");
}

        mesh->bind(cmd);
    }

    void Renderer::drawMesh(vk::CommandBuffer cmd, MeshHandle handle) const
    {
        if (handle.id >= m_meshes.size()) {
            throw std::runtime_error("[Renderer] Invalid mesh handle: out of range");
}

        const auto& mesh = m_meshes[handle.id];
        if (!mesh) {
            throw std::runtime_error("[Renderer] Invalid mesh handle: null mesh slot");
}

        mesh->draw(cmd);
    }


    void Renderer::setRecordFunc(const RecordFunc& callback)
    {
        m_recordCallback = callback;
    }

    void Renderer::bindPipeline(vk::CommandBuffer cmd,
                                PipelineHandle handle) const
    {
        if (handle.id >= m_pipelines.size()) {
            throw std::runtime_error("[Renderer] Invalid pipeline handle");
}

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,
                         m_pipelines[handle.id]->pipeline());
    }

    Renderer::~Renderer()
    {
        if (m_device && m_device->device())
        {
            m_device->device().waitIdle();
        }

        if (m_tracyCtx != nullptr)
        {
            PNKR_PROFILE_GPU_DESTROY(m_tracyCtx);
            m_tracyCtx = nullptr;
        }

        // 1) Pipelines first (they may reference descriptor set layouts)
        m_pipelines.clear();

        // 2) Texture descriptors + textures + sampler
        m_textureDescriptors.clear(); // optional, but keeps vectors tidy
        m_textures.clear(); // triggers VulkanImage destructors
        m_defaultSampler.reset(); // destroys vk::Sampler

        if (m_device && m_textureSetLayout)
        {
            m_device->device().destroyDescriptorSetLayout(m_textureSetLayout);
            m_textureSetLayout = nullptr;
        }

        if (m_descriptorLayoutCache)
        {
            m_descriptorLayoutCache->cleanup(); // if you implemented this
        }

        m_descriptorAllocator = nullptr;
        m_descriptorLayoutCache.reset();

        m_meshes.clear();
        m_swapchain.reset();
        m_sync.reset();
        m_commandBuffer.reset();
        m_device.reset();
        m_context.reset();
    }

    void Renderer::beginFrame(float deltaTime)
    {
        PNKR_PROFILE_FUNCTION();

        if (m_frameInProgress) {
            return;
}
        if (!m_recordCallback)
        {
            throw std::runtime_error(
                "[Renderer] No record callback set (call setRecordFunc first)");
        }

        const uint32_t frame = m_commandBuffer->currentFrame();
        m_deltaTime = deltaTime;
        m_sync->waitForFrame(frame);

        const vk::Result acq = m_swapchain->acquireNextImage(
            UINT64_MAX, m_sync->imageAvailableSemaphore(frame), nullptr,
            m_imageIndex);

        if (acq == vk::Result::eErrorOutOfDateKHR)
        {
            resize(m_window.width(), m_window.height());
            return;
        }
        if (acq != vk::Result::eSuccess && acq != vk::Result::eSuboptimalKHR)
        {
            pnkr::core::Logger::error("[Renderer] acquireNextImage failed: {}",
                                      vk::to_string(acq));
            return;
        }

        m_sync->resetFrame(frame);

        (void)m_commandBuffer->begin(frame);

        vk::CommandBuffer cmd = m_commandBuffer->cmd(frame);
        PNKR_PROFILE_GPU_COLLECT(m_tracyCtx, cmd);

        m_frameInProgress = true;
    }

    void Renderer::bindPipeline(vk::CommandBuffer cmd, const ComputePipeline& pipeline)
    {
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.pipeline());
    }

    void Renderer::dispatch(vk::CommandBuffer cmd, uint32_t groupX, uint32_t groupY, uint32_t groupZ)
    {
        cmd.dispatch(groupX, groupY, groupZ);
    }


    void Renderer::drawFrame()
    {
        PNKR_PROFILE_FUNCTION();

        if (!m_frameInProgress) {
            return;
}

        const uint32_t frameIndex = m_commandBuffer->currentFrame();
        vk::CommandBuffer cmd = m_commandBuffer->cmd(frameIndex);

        {
            PNKR_PROFILE_GPU_ZONE(m_tracyCtx, cmd, "Main Render Pass");

            // Render scene into HDR target
            m_mainTarget->transitionToAttachment(cmd);

            vk::ClearValue colorClear{vk::ClearColorValue{std::array<float, 4>{0.0F, 0.0F, 0.0F, 1.0F}}};
            vk::ClearValue depthClear{vk::ClearDepthStencilValue{1.0F, 0}};
            m_mainTarget->beginRendering(cmd, colorClear, depthClear);

            if (m_recordCallback)
            {
                RenderFrameContext ctx{};
                ctx.m_cmd = cmd;
                ctx.m_frameIndex = frameIndex;
                ctx.m_imageIndex = m_imageIndex;
                ctx.m_extent = m_swapchain->extent();
                ctx.m_deltaTime = m_deltaTime;
                m_recordCallback(ctx);
            }

            m_mainTarget->endRendering(cmd);
        }

        const vk::Image hdrImage = m_mainTarget->colorImage().image();
        const vk::Image swapImage = m_swapchain->images()[m_imageIndex];

        // Post-Process (Compute) OR Blit (Fallback)
        if (m_postProcessCallback)
        {
            vk::ImageLayout oldSwapLayout = m_swapchain->imageLayout(m_imageIndex);

            vk::ImageMemoryBarrier2 preBarriers[2]{};

            // HDR: color attachment -> shader read for compute sampling
            preBarriers[0].srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            preBarriers[0].srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
            preBarriers[0].dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
            preBarriers[0].dstAccessMask = vk::AccessFlagBits2::eShaderRead;
            preBarriers[0].oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
            preBarriers[0].newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            preBarriers[0].image = hdrImage;
            preBarriers[0].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

            // Swapchain: previous layout -> general for storage writes
            preBarriers[1].srcStageMask = (oldSwapLayout == vk::ImageLayout::eUndefined)
                                              ? vk::PipelineStageFlagBits2::eTopOfPipe
                                              : vk::PipelineStageFlagBits2::eAllCommands;
            preBarriers[1].srcAccessMask = (oldSwapLayout == vk::ImageLayout::eUndefined)
                                               ? vk::AccessFlagBits2::eNone
                                               : vk::AccessFlagBits2::eMemoryRead;
            preBarriers[1].dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
            preBarriers[1].dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
            preBarriers[1].oldLayout = oldSwapLayout;
            preBarriers[1].newLayout = vk::ImageLayout::eGeneral;
            preBarriers[1].image = swapImage;
            preBarriers[1].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

            vk::DependencyInfo preDep{};
            preDep.imageMemoryBarrierCount = 2;
            preDep.pImageMemoryBarriers = preBarriers;
            cmd.pipelineBarrier2(preDep);

            m_swapchain->imageLayout(m_imageIndex) = vk::ImageLayout::eGeneral;

            // C. Execute Callback
            m_postProcessCallback(cmd, m_imageIndex, m_swapchain->extent());

            // D. Prepare images for UI rendering
            vk::ImageMemoryBarrier2 postBarriers[2]{};

            postBarriers[0].srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
            postBarriers[0].srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
            postBarriers[0].dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            postBarriers[0].dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite |
                vk::AccessFlagBits2::eColorAttachmentRead;
            postBarriers[0].oldLayout = vk::ImageLayout::eGeneral;
            postBarriers[0].newLayout = vk::ImageLayout::eColorAttachmentOptimal;
            postBarriers[0].image = swapImage;
            postBarriers[0].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

            postBarriers[1].srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
            postBarriers[1].srcAccessMask = vk::AccessFlagBits2::eShaderRead;
            postBarriers[1].dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            postBarriers[1].dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
            postBarriers[1].oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            postBarriers[1].newLayout = vk::ImageLayout::eColorAttachmentOptimal;
            postBarriers[1].image = hdrImage;
            postBarriers[1].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

            vk::DependencyInfo postDep{};
            postDep.imageMemoryBarrierCount = 2;
            postDep.pImageMemoryBarriers = postBarriers;
            cmd.pipelineBarrier2(postDep);

            m_swapchain->imageLayout(m_imageIndex) = vk::ImageLayout::eColorAttachmentOptimal;
        }
        else
        {
            vk::ImageLayout oldSwapLayout = m_swapchain->imageLayout(m_imageIndex);

            vk::ImageMemoryBarrier2 barriers[2]{};

            // HDR src for blit
            barriers[0].srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            barriers[0].srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
            barriers[0].dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
            barriers[0].dstAccessMask = vk::AccessFlagBits2::eTransferRead;
            barriers[0].oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
            barriers[0].newLayout = vk::ImageLayout::eTransferSrcOptimal;
            barriers[0].image = hdrImage;
            barriers[0].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

            // Swapchain dst for blit
            barriers[1].srcStageMask = (oldSwapLayout == vk::ImageLayout::eUndefined)
                                           ? vk::PipelineStageFlagBits2::eTopOfPipe
                                           : vk::PipelineStageFlagBits2::eAllCommands;
            barriers[1].srcAccessMask = (oldSwapLayout == vk::ImageLayout::eUndefined)
                                            ? vk::AccessFlagBits2::eNone
                                            : vk::AccessFlagBits2::eMemoryRead;
            barriers[1].dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
            barriers[1].dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
            barriers[1].oldLayout = oldSwapLayout;
            barriers[1].newLayout = vk::ImageLayout::eTransferDstOptimal;
            barriers[1].image = swapImage;
            barriers[1].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

            vk::DependencyInfo dep{};
            dep.imageMemoryBarrierCount = 2;
            dep.pImageMemoryBarriers = barriers;
            cmd.pipelineBarrier2(dep);

            // Blit full image
            const vk::Extent2D srcExtent = m_mainTarget->extent();
            const vk::Extent2D dstExtent = m_swapchain->extent();

            vk::ImageBlit2 blit{};
            blit.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
            blit.srcOffsets[0] = vk::Offset3D{0, 0, 0};
            blit.srcOffsets[1] = vk::Offset3D{
                static_cast<int32_t>(srcExtent.width), static_cast<int32_t>(srcExtent.height), 1
            };
            blit.dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
            blit.dstOffsets[0] = vk::Offset3D{0, 0, 0};
            blit.dstOffsets[1] = vk::Offset3D{
                static_cast<int32_t>(dstExtent.width), static_cast<int32_t>(dstExtent.height), 1
            };

            vk::BlitImageInfo2 blitInfo{};
            blitInfo.srcImage = hdrImage;
            blitInfo.srcImageLayout = vk::ImageLayout::eTransferSrcOptimal;
            blitInfo.dstImage = swapImage;
            blitInfo.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
            blitInfo.filter = vk::Filter::eLinear;
            blitInfo.regionCount = 1;
            blitInfo.pRegions = &blit;

            cmd.blitImage2(blitInfo);

            // Restore layouts for UI rendering
            vk::ImageMemoryBarrier2 postBarriers[2]{};

            postBarriers[0].srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
            postBarriers[0].srcAccessMask = vk::AccessFlagBits2::eTransferRead;
            postBarriers[0].dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            postBarriers[0].dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
            postBarriers[0].oldLayout = vk::ImageLayout::eTransferSrcOptimal;
            postBarriers[0].newLayout = vk::ImageLayout::eColorAttachmentOptimal;
            postBarriers[0].image = hdrImage;
            postBarriers[0].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

            postBarriers[1].srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
            postBarriers[1].srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
            postBarriers[1].dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            postBarriers[1].dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
            postBarriers[1].oldLayout = vk::ImageLayout::eTransferDstOptimal;
            postBarriers[1].newLayout = vk::ImageLayout::eColorAttachmentOptimal;
            postBarriers[1].image = swapImage;
            postBarriers[1].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

            vk::DependencyInfo postDep{};
            postDep.imageMemoryBarrierCount = 2;
            postDep.pImageMemoryBarriers = postBarriers;
            cmd.pipelineBarrier2(postDep);

            m_swapchain->imageLayout(m_imageIndex) = vk::ImageLayout::eColorAttachmentOptimal;
        }

        // 6) ImGui pass using dynamic rendering into the swapchain image (LoadOp=LOAD)
        {
            vk::RenderingAttachmentInfo colorAttach{};
            colorAttach.imageView = m_swapchain->imageViews()[m_imageIndex];
            colorAttach.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            colorAttach.loadOp = vk::AttachmentLoadOp::eLoad; // Keep previously blitted scene
            colorAttach.storeOp = vk::AttachmentStoreOp::eStore;

            vk::RenderingInfo renderInfo{};
            renderInfo.renderArea = vk::Rect2D({0, 0}, m_swapchain->extent());
            renderInfo.layerCount = 1;
            renderInfo.colorAttachmentCount = 1;
            renderInfo.pColorAttachments = &colorAttach;

            cmd.beginRendering(renderInfo);

            // Make sure you've called ImGui::Render() before this, e.g. in beginImGuiFrame()/UI build path.
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

            cmd.endRendering();
        }

        // 7) Transition swapchain image to PRESENT
        {
            vk::ImageMemoryBarrier2 presentBarrier{};
            presentBarrier.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            presentBarrier.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
            presentBarrier.dstStageMask = vk::PipelineStageFlagBits2::eBottomOfPipe; // or none
            presentBarrier.dstAccessMask = vk::AccessFlagBits2::eNone;
            presentBarrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
            presentBarrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
            presentBarrier.image = m_swapchain->images()[m_imageIndex];
            presentBarrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

            vk::DependencyInfo dep{};
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &presentBarrier;
            cmd.pipelineBarrier2(dep);

            m_swapchain->imageLayout(m_imageIndex) = vk::ImageLayout::ePresentSrcKHR;
        }
    }


    void Renderer::endFrame()
    {
        if (!m_frameInProgress) {
            return;
}

        const uint32_t frame = m_commandBuffer->currentFrame();

        m_commandBuffer->end(frame);

        // Submit
        m_commandBuffer->submit(
            frame, m_device->graphicsQueue(), m_sync->imageAvailableSemaphore(frame),
            m_sync->renderFinishedSemaphore(m_imageIndex), // Image-specific semaphore
            m_sync->inFlightFence(frame),
            vk::PipelineStageFlagBits::eColorAttachmentOutput);

        // Present
        const vk::Result pres =
            m_swapchain->present(m_device->presentQueue(), m_imageIndex,
                                 m_sync->renderFinishedSemaphore(
                                     m_imageIndex)); // Image-specific semaphore

        if (pres == vk::Result::eErrorOutOfDateKHR ||
            pres == vk::Result::eSuboptimalKHR)
        {
            resize(m_window.width(), m_window.height());
        }
        else if (pres != vk::Result::eSuccess)
        {
            pnkr::core::Logger::error("[Renderer] present failed: {}",
                                      vk::to_string(pres));
        }

        m_commandBuffer->advanceFrame();
        m_frameInProgress = false;
    }

    void Renderer::resize(int /*width*/, int /*height*/)
    {
        if (!m_swapchain) {
            return;
}

        m_device->device().waitIdle();

        const vk::Format oldFmt = m_swapchain->imageFormat();

        m_swapchain->recreate(m_device->physicalDevice(), m_device->device(),
                              m_context->surface(),
                              m_device->queueFamilies().graphics,
                              m_device->queueFamilies().present, m_window);

        m_mainTarget = std::make_unique<VulkanRenderTarget>(
            m_device->allocator(),
            m_device->device(),
            m_swapchain->extent().width,
            m_swapchain->extent().height,
            vk::Format::eR16G16B16A16Sfloat,
            vk::Format::eD32Sfloat
        );

        m_sync->updateSwapchainSize(
            u32(m_swapchain->images().size()));

        if (m_swapchain->imageFormat() != oldFmt)
        {
            for (auto& pipe : m_pipelines)
            {
                if (!pipe) {
                    continue;
}
                auto cfg = pipe->config();
                pipe = std::make_unique<VulkanPipeline>(m_device->device(),
                                                        cfg);
            }
        }
    }
} // namespace pnkr::renderer
