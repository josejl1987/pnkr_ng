#include "pnkr/renderer/renderer.hpp"
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

namespace pnkr::renderer
{
    Renderer::Renderer(platform::Window& window,
                       [[maybe_unused]] const RendererConfig& config)
        : m_window(window)
    {
        m_context = std::make_unique<VulkanContext>(window);
        m_device = std::make_unique<VulkanDevice>(*m_context);

        m_swapchain = std::make_unique<VulkanSwapchain>(
            m_device->physicalDevice(), m_device->device(), m_context->surface(),
            m_device->queueFamilies().graphics, m_device->queueFamilies().present,
            window, m_device->allocator());

        m_commandBuffer = std::make_unique<VulkanCommandBuffer>(*m_device);

        m_mainTarget = std::make_unique<VulkanRenderTarget>(
            m_device->allocator(),
            m_device->device(),
            m_swapchain->extent().width,
            m_swapchain->extent().height,
            vk::Format::eR16G16B16A16Sfloat, // HDR Color
            vk::Format::eD32Sfloat // High precision depth
        );

        m_sync = std::make_unique<VulkanSyncManager>(
            m_device->device(), m_device->framesInFlight(),
            static_cast<uint32_t>(m_swapchain->images().size()));

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
        if (width <= 0 || height <= 0 || !pixels)
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

            rgbaPixels.resize(width * height * 4);
            for (int i = 0; i < width * height; ++i)
            {
                const unsigned char r = pixels[i * channels + 0];
                const unsigned char g = (channels >= 2) ? pixels[i * channels + 1] : r;
                const unsigned char b = (channels >= 3) ? pixels[i * channels + 2] : r;
                const unsigned char a = (channels >= 4) ? pixels[i * channels + 3] : 255;

                rgbaPixels[i * 4 + 0] = r;
                rgbaPixels[i * 4 + 1] = g;
                rgbaPixels[i * 4 + 2] = b;
                rgbaPixels[i * 4 + 3] = a;
            }
            srcData = rgbaPixels.data();
        }


        auto texture = std::make_unique<VulkanImage>(
            VulkanImage::createFromMemory(*m_device, srcData, width, height, srgb)
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

        TextureHandle handle{static_cast<uint32_t>(m_textures.size())};
        m_textures.push_back(std::move(texture));
        m_textureDescriptors.push_back(descriptorSet);

        return handle;
    }


    TextureHandle Renderer::loadTexture(const std::filesystem::path& filepath, bool srgb)
    {
        auto texture = std::make_unique<VulkanImage>(
            VulkanImage::createFromFile(*m_device, filepath, srgb)
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

        TextureHandle handle{static_cast<uint32_t>(m_textures.size())};
        m_textures.push_back(std::move(texture));
        m_textureDescriptors.push_back(descriptorSet);

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
        PipelineHandle handle{static_cast<uint32_t>(m_pipelines.size())};

        PipelineConfig pipelineCfg = cfg;

        m_pipelines.push_back(std::make_unique<VulkanPipeline>(
            m_device->device(),  pipelineCfg));

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
        if (handle.id >= m_pipelines.size())
            throw std::runtime_error("[Renderer] Invalid pipeline handle");
        return m_pipelines[handle.id]->layout();
    }

    MeshHandle Renderer::createMesh(const std::vector<Vertex>& vertices,
                                    const std::vector<uint32_t>& indices)
    {
        if (vertices.empty() || indices.empty())
            throw std::runtime_error("[Renderer] createMesh: empty data");

        MeshHandle handle{static_cast<uint32_t>(m_meshes.size())};

        m_meshes.push_back(std::make_unique<Mesh>(*m_device, vertices, indices));

        pnkr::core::Logger::info("[Renderer] Created mesh handle={}", handle.id);
        return handle;
    }

    void Renderer::bindMesh(vk::CommandBuffer cmd, MeshHandle handle) const
    {
        if (handle.id >= m_meshes.size())
            throw std::runtime_error("[Renderer] Invalid mesh handle: out of range");

        const auto& mesh = m_meshes[handle.id];
        if (!mesh)
            throw std::runtime_error("[Renderer] Invalid mesh handle: null mesh slot");

        mesh->bind(cmd);
    }

    void Renderer::drawMesh(vk::CommandBuffer cmd, MeshHandle handle) const
    {
        if (handle.id >= m_meshes.size())
            throw std::runtime_error("[Renderer] Invalid mesh handle: out of range");

        const auto& mesh = m_meshes[handle.id];
        if (!mesh)
            throw std::runtime_error("[Renderer] Invalid mesh handle: null mesh slot");

        mesh->draw(cmd);
    }


    void Renderer::setRecordFunc(const RecordFunc& callback)
    {
        m_recordCallback = callback;
    }

    void Renderer::bindPipeline(vk::CommandBuffer cmd,
                                PipelineHandle handle) const
    {
        if (handle.id >= m_pipelines.size())
            throw std::runtime_error("[Renderer] Invalid pipeline handle");

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,
                         m_pipelines[handle.id]->pipeline());
    }

    Renderer::~Renderer()
    {
        if (m_device && m_device->device())
        {
            m_device->device().waitIdle();
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

        m_descriptorAllocator.reset();
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
        if (m_frameInProgress)
            return;
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
        m_frameInProgress = true;
    }

    void Renderer::drawFrame() const
    {
        if (!m_frameInProgress)
            return;
        if (!m_recordCallback)
        {
            throw std::runtime_error(
                "[Renderer] No record callback set (call setRecordFunc first)");
        }

        const uint32_t frame = m_commandBuffer->currentFrame();
        vk::CommandBuffer cmd = m_commandBuffer->cmd(frame);

        // --- PASS 1: Render Scene to Offscreen Target ---

        m_mainTarget->transitionToAttachment(cmd);

        m_mainTarget->beginRendering(cmd);

        // Call User Record Function
        RenderFrameContext ctx{
            cmd,
            frame,
            m_imageIndex,
            m_mainTarget->extent(), // Changed from swapchain->extent()
            m_deltaTime
        };
        m_recordCallback(ctx);

        m_mainTarget->endRendering(cmd);


        // --- PASS 2: Blit to Swapchain ---

        const vk::Image swapImg = m_swapchain->images()[m_imageIndex];
        const vk::Extent2D swapExt = m_swapchain->extent();

        // 1. Transition Offscreen Target to Transfer Source
        m_mainTarget->transitionToRead(cmd);

        // 2. Transition Swapchain Image to Transfer Destination

        vk::ImageMemoryBarrier2 swapDstBarrier{};
        swapDstBarrier.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        swapDstBarrier.srcAccessMask = vk::AccessFlagBits2::eNone;
        swapDstBarrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
        swapDstBarrier.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
        swapDstBarrier.oldLayout = vk::ImageLayout::eUndefined;
        swapDstBarrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
        swapDstBarrier.image = swapImg;
        swapDstBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        swapDstBarrier.subresourceRange.baseMipLevel = 0;
        swapDstBarrier.subresourceRange.levelCount = 1;
        swapDstBarrier.subresourceRange.baseArrayLayer = 0;
        swapDstBarrier.subresourceRange.layerCount = 1;

        vk::DependencyInfo depSwapDst{};
        depSwapDst.imageMemoryBarrierCount = 1;
        depSwapDst.pImageMemoryBarriers = &swapDstBarrier;
        cmd.pipelineBarrier2(depSwapDst);

        // 3. Perform Blit
        vk::ImageBlit blitRegion{};
        blitRegion.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blitRegion.srcSubresource.layerCount = 1;
        blitRegion.srcOffsets[0] = vk::Offset3D{0, 0, 0};
        blitRegion.srcOffsets[1] = vk::Offset3D{
            (int32_t)m_mainTarget->extent().width,
            (int32_t)m_mainTarget->extent().height,
            1
        };
        blitRegion.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blitRegion.dstSubresource.layerCount = 1;
        blitRegion.dstOffsets[0] = vk::Offset3D{0, 0, 0};
        blitRegion.dstOffsets[1] = vk::Offset3D{
            (int32_t)swapExt.width,
            (int32_t)swapExt.height,
            1
        };

        // Use Linear filter if size differs, Nearest if identical (faster/cleaner)
        vk::Filter blitFilter = (m_mainTarget->extent() == swapExt) ? vk::Filter::eNearest : vk::Filter::eLinear;

        cmd.blitImage(
            m_mainTarget->colorImage().image(), vk::ImageLayout::eTransferSrcOptimal,
            swapImg, vk::ImageLayout::eTransferDstOptimal,
            1, &blitRegion, blitFilter
        );

        // 4. Transition Swapchain to Present
        // (Matches the final barrier in your base code, but src is now Transfer)
        vk::ImageMemoryBarrier2 toPresent{};
        toPresent.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        toPresent.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        toPresent.dstStageMask = vk::PipelineStageFlagBits2::eNone;
        toPresent.dstAccessMask = vk::AccessFlagBits2::eNone; // Present engine handles visibility
        toPresent.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        toPresent.newLayout = vk::ImageLayout::ePresentSrcKHR;
        toPresent.image = swapImg;
        toPresent.subresourceRange = swapDstBarrier.subresourceRange;

        vk::DependencyInfo depPresent{};
        depPresent.imageMemoryBarrierCount = 1;
        depPresent.pImageMemoryBarriers = &toPresent;
        cmd.pipelineBarrier2(depPresent);
    }


    void Renderer::endFrame()
    {
        if (!m_frameInProgress)
            return;

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
        if (!m_swapchain)
            return;

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
            static_cast<uint32_t>(m_swapchain->images().size()));

        if (m_swapchain->imageFormat() != oldFmt)
        {
            for (auto& pipe : m_pipelines)
            {
                if (!pipe)
                    continue;
                auto cfg = pipe->config();
                pipe = std::make_unique<VulkanPipeline>(m_device->device(),
                                                         cfg);
            }
        }
    }
} // namespace pnkr::renderer
