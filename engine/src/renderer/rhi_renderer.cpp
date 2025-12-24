#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/ktx_utils.hpp"
#include "pnkr/rhi/rhi_factory.hpp"
#include "pnkr/rhi/vulkan/vulkan_device.hpp"
#include "pnkr/rhi/vulkan/vulkan_swapchain.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/common.hpp"

#include <ktx.h>
#include <stb_image.h>
#include <algorithm>
#include <cstddef>
#include <string>

using namespace pnkr::util;

namespace pnkr::renderer
{
    namespace
    {
        struct GPUVertex
        {
            glm::vec4 pos;
            glm::vec4 color;
            glm::vec4 normal;
            glm::vec4 uv;
            glm::vec4 tangent;
        };
    }

    RHIRenderer::RHIRenderer(platform::Window& window, const RendererConfig& config)
        : m_window(window)
    {
        core::Logger::info("Creating RHI Renderer");

        // Create device
        rhi::DeviceDescriptor deviceDesc{};
        deviceDesc.enableValidation = true;
        deviceDesc.enableBindless = config.m_enableBindless;
        deviceDesc.requiredExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME
        };

        if (config.m_enableBindless)
        {
            deviceDesc.requiredExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
        }

        m_device = rhi::RHIFactory::createDeviceAuto(rhi::RHIBackend::Vulkan, deviceDesc);

        if (!m_device)
        {
            throw cpptrace::runtime_error("Failed to create RHI device");
        }

        // Check bindless support
        m_bindlessSupported = m_device->physicalDevice().capabilities().bindlessTextures;
        m_useBindless = m_bindlessSupported && config.m_enableBindless;

        // Create swapchain (WSI)
        m_swapchain = rhi::RHIFactory::createSwapchain(
            m_device.get(),
            m_window,
            rhi::Format::B8G8R8A8_UNORM);

        if (!m_swapchain)
        {
            throw cpptrace::runtime_error("Failed to create RHI swapchain");
        }

        // Create per-frame command buffers.
        const uint32_t framesInFlight = std::max(1U, m_swapchain->framesInFlight());
        m_commandBuffers.reserve(framesInFlight);
        for (uint32_t i = 0; i < framesInFlight; ++i)
        {
            m_commandBuffers.push_back(m_device->createCommandBuffer());
        }

        // Create default sampler
        m_defaultSampler = m_device->createSampler(
            rhi::Filter::Linear,
            rhi::Filter::Linear,
            rhi::SamplerAddressMode::Repeat
        );
        if (m_useBindless)
        {
            m_repeatSamplerIndex = m_device->registerBindlessSampler(m_defaultSampler.get()).index;
            m_clampSampler = m_device->createSampler(
                rhi::Filter::Linear,
                rhi::Filter::Linear,
                rhi::SamplerAddressMode::ClampToEdge
            );
            m_mirrorSampler = m_device->createSampler(
                rhi::Filter::Linear,
                rhi::Filter::Linear,
                rhi::SamplerAddressMode::MirroredRepeat
            );
            m_clampSamplerIndex = m_device->registerBindlessSampler(m_clampSampler.get()).index;
            m_mirrorSamplerIndex = m_device->registerBindlessSampler(m_mirrorSampler.get()).index;
        }

        // Create render targets
        createRenderTargets();

        // Create global lighting layout
        rhi::DescriptorSetLayout lightingLayoutDesc{};
        lightingLayoutDesc.bindings = {
            {0, rhi::DescriptorType::CombinedImageSampler, 1, rhi::ShaderStage::Fragment}, // Irradiance
            {1, rhi::DescriptorType::CombinedImageSampler, 1, rhi::ShaderStage::Fragment}, // Prefilter
            {2, rhi::DescriptorType::CombinedImageSampler, 1, rhi::ShaderStage::Fragment}  // BRDF LUT
        };
        m_globalLightingLayout = m_device->createDescriptorSetLayout(lightingLayoutDesc);
        m_globalLightingSet = m_device->allocateDescriptorSet(m_globalLightingLayout.get());

        // Create default resources
        createDefaultResources();

        core::Logger::info("RHI Renderer created successfully");
        core::Logger::info("Bindless rendering: {}", m_useBindless ? "ENABLED" : "DISABLED");
    }

    RHIRenderer::~RHIRenderer()
    {
        if (m_device)
        {
            m_device->waitIdle();
        }

        core::Logger::info("RHI Renderer destroyed");
    }

    void RHIRenderer::beginFrame(float deltaTime)
    {
        if (m_frameInProgress)
        {
            core::Logger::warn("beginFrame called while frame already in progress");
            return;
        }

        if (!m_swapchain)
        {
            core::Logger::error("beginFrame: swapchain not created");
            return;
        }

        m_deltaTime = deltaTime;
        m_frameInProgress = true;

        if (m_commandBuffers.empty())
        {
            core::Logger::error("beginFrame: no command buffers available");
            m_frameInProgress = false;
            return;
        }

        const uint32_t frameSlot = m_frameIndex % u32(m_commandBuffers.size());
        m_activeCommandBuffer = m_commandBuffers[frameSlot].get();

        if (auto* vkDevice = dynamic_cast<rhi::vulkan::VulkanRHIDevice*>(m_device.get()))
        {
            const uint32_t framesInFlight = m_swapchain->framesInFlight();
            const uint64_t currentFrame = vkDevice->getFrameCount();
            if (currentFrame >= framesInFlight)
            {
                const uint64_t waitValue = currentFrame - framesInFlight + 1;
                vkDevice->waitForTimelineValue(waitValue);
            }
        }

        // Acquire swapchain image and transition it to ColorAttachment.
        if (!m_swapchain->beginFrame(m_frameIndex, m_activeCommandBuffer, m_currentFrame))
        {
            // Swapchain may have been recreated; skip this frame.
            m_activeCommandBuffer = nullptr;
            m_frameInProgress = false;
            return;
        }

        m_backbuffer = m_currentFrame.color;
    }


    void RHIRenderer::drawFrame()
    {
        if (!m_frameInProgress)
        {
            core::Logger::error("drawFrame called without beginFrame");
            return;
        }

        if (!m_recordCallback && !m_computeRecordCallback)
        {
            core::Logger::warn("No record callback set (m_recordCallback={}, m_computeRecordCallback={})", 
                               (bool)m_recordCallback, (bool)m_computeRecordCallback);
            return;
        }

        if (m_activeCommandBuffer == nullptr)
        {
            core::Logger::error("drawFrame: command buffer not available");
            return;
        }

        RHIFrameContext context{};
        context.commandBuffer = m_activeCommandBuffer;
        context.frameIndex = m_frameIndex;
        context.deltaTime = m_deltaTime;

        if (m_computeRecordCallback)
        {
            m_computeRecordCallback(context);
        }

        if (!m_recordCallback)
        {
            return;
        }

        if ((m_backbuffer == nullptr) || !m_depthTarget)
        {
            core::Logger::error("drawFrame: missing backbuffer or depth target");
            return;
        }

        // Transition depth target to attachment layout (track layout across frames).
        if (m_depthLayout != rhi::ResourceLayout::DepthStencilAttachment)
        {
            rhi::RHIMemoryBarrier depthBarrier{};
            depthBarrier.texture = m_depthTarget.get();
            depthBarrier.srcAccessStage = rhi::ShaderStage::None;
            depthBarrier.dstAccessStage = rhi::ShaderStage::DepthStencilAttachment;
            depthBarrier.oldLayout = m_depthLayout;
            depthBarrier.newLayout = rhi::ResourceLayout::DepthStencilAttachment;

            m_activeCommandBuffer->pipelineBarrier(
                rhi::ShaderStage::None,
                rhi::ShaderStage::DepthStencilAttachment,
                {depthBarrier}
            );

            m_depthLayout = rhi::ResourceLayout::DepthStencilAttachment;
        }

        // Begin rendering into the swapchain backbuffer.
        rhi::RenderingInfo renderingInfo{};
        renderingInfo.renderArea = rhi::Rect2D{
            .x = 0, .y = 0,
            .width = m_swapchain->extent().width,
            .height = m_swapchain->extent().height
        };

        // Color attachment (swapchain image)
        rhi::RenderingAttachment colorAttachment{};
        colorAttachment.texture = m_backbuffer;
        colorAttachment.loadOp = rhi::LoadOp::Clear;
        colorAttachment.storeOp = rhi::StoreOp::Store;
        colorAttachment.clearValue.isDepthStencil = false;
        colorAttachment.clearValue.color.float32[0] = 1.0F;
        colorAttachment.clearValue.color.float32[1] = 1.0F;
        colorAttachment.clearValue.color.float32[2] = 1.0F;
        colorAttachment.clearValue.color.float32[3] = 1.0F;
        renderingInfo.colorAttachments.push_back(colorAttachment);

        // Depth attachment
        rhi::RenderingAttachment depthAttachment{};
        depthAttachment.texture = m_depthTarget.get();
        depthAttachment.loadOp = rhi::LoadOp::Clear;
        depthAttachment.storeOp = rhi::StoreOp::Store;
        depthAttachment.clearValue.isDepthStencil = true;
        depthAttachment.clearValue.depthStencil.depth = 1.0F;
        depthAttachment.clearValue.depthStencil.stencil = 0;
        renderingInfo.depthAttachment = &depthAttachment;

        m_activeCommandBuffer->beginRendering(renderingInfo);

        // Viewport/scissor
        rhi::Viewport viewport{};
        viewport.x = 0.0F;
        viewport.y = 0.0F;
        viewport.width = toFloat(m_swapchain->extent().width);
        viewport.height = toFloat(m_swapchain->extent().height);
        viewport.minDepth = 0.0F;
        viewport.maxDepth = 1.0F;
        m_activeCommandBuffer->setViewport(viewport);

        rhi::Rect2D scissor{};
        scissor.x = 0;
        scissor.y = 0;
        scissor.width = m_swapchain->extent().width;
        scissor.height = m_swapchain->extent().height;
        m_activeCommandBuffer->setScissor(scissor);

        // Call user record callback
        m_recordCallback(context);

        m_activeCommandBuffer->endRendering();
    }

    void RHIRenderer::endFrame()
    {
        if (!m_frameInProgress)
        {
            core::Logger::error("endFrame called without beginFrame");
            return;
        }

        if (!m_swapchain)
        {
            core::Logger::error("endFrame: swapchain not created");
            if (m_activeCommandBuffer != nullptr)
            {
                m_activeCommandBuffer->end();
            }
            m_frameInProgress = false;
            m_activeCommandBuffer = nullptr;
            return;
        }

        // Transition to Present, end, submit, and present.
        if (m_activeCommandBuffer != nullptr)
        {
            const bool ready = m_swapchain->endFrame(m_frameIndex, m_activeCommandBuffer);
            if (ready)
            {
                auto* vkDevice = dynamic_cast<rhi::vulkan::VulkanRHIDevice*>(m_device.get());
                auto* vkSwapchain = dynamic_cast<rhi::vulkan::VulkanRHISwapchain*>(m_swapchain.get());
                if (vkDevice && vkSwapchain)
                {
                    const uint64_t signalValue = vkDevice->advanceFrame();

                    std::vector<vk::Semaphore> waits = { vkSwapchain->getCurrentAcquireSemaphore() };
                    
                    // FIXED: Wait at AllCommands to ensure layout transitions (which happen at the 
                    // start of the command buffer) are blocked until the image is acquired.
                    // 'eColorAttachmentOutput' is insufficient because it allows Transfer/Barriers 
                    // to execute before the stage is reached, causing the WRITE_AFTER_PRESENT hazard.
                    std::vector<vk::PipelineStageFlags> waitStages = {
                        vk::PipelineStageFlagBits::eAllCommands
                    };
                    
                    std::vector<vk::Semaphore> signals = { vkSwapchain->getCurrentRenderFinishedSemaphore() };

                    vkDevice->submitCommands(
                        m_activeCommandBuffer,
                        waits,
                        waitStages,
                        signals,
                        signalValue);

                    (void)m_swapchain->present(m_frameIndex);
                }
                else
                {
                    core::Logger::error("endFrame: Vulkan device/swapchain not available for submission");
                }
            }
        }

        m_frameInProgress = false;
        m_activeCommandBuffer = nullptr;
        m_frameIndex++;
    }

    void RHIRenderer::resize(int width, int height)
    {
        core::Logger::info("Resizing renderer to {}x{}", width, height);

        if (!m_swapchain)
        {
            return;
        }

        m_device->waitIdle();

        m_swapchain->recreate(u32(width), u32(height));

        // Recreate depth target for new extent.
        createRenderTargets();
    }


    MeshHandle RHIRenderer::loadNoVertexPulling(const std::vector<Vertex>& vertices,
                                                const std::vector<uint32_t>& indices)
    {
        MeshData mesh{};

        // Create vertex buffer
        uint64_t vertexBufferSize = vertices.size() * sizeof(Vertex);
        mesh.m_vertexBuffer = m_device->createBuffer({
            .size = vertexBufferSize,
            .usage = rhi::BufferUsage::VertexBuffer | rhi::BufferUsage::TransferDst | rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = rhi::MemoryUsage::GPUOnly,
            .data = vertices.data(),
            .debugName = "VertexBuffer"
        });

        // Create index buffer
        uint64_t indexBufferSize = indices.size() * sizeof(uint32_t);
        mesh.m_indexBuffer = m_device->createBuffer({
            .size = indexBufferSize,
            .usage = rhi::BufferUsage::IndexBuffer | rhi::BufferUsage::TransferDst,
            .memoryUsage = rhi::MemoryUsage::GPUOnly,
            .data = indices.data(),
            .debugName = "IndexBuffer"
        });

        mesh.m_vertexCount = u32(vertices.size());
        mesh.m_indexCount = u32(indices.size());

        auto handle = static_cast<MeshHandle>(m_meshes.size());
        m_meshes.push_back(std::move(mesh));

        core::Logger::info("Created mesh: {} vertices, {} indices",
                           mesh.m_vertexCount, mesh.m_indexCount);

        return handle;
    }

    MeshHandle RHIRenderer::loadVertexPulling(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
    {
        MeshData mesh{};

        mesh.m_vertexPulling = true;
        std::vector<GPUVertex> gpuVertices;
        gpuVertices.reserve(vertices.size());

        for (const auto& v : vertices)
        {
            GPUVertex gv{};
            gv.pos = glm::vec4(v.m_position, 1.0f);
            gv.color = glm::vec4(v.m_color, 1.0f);
            gv.normal = glm::vec4(v.m_normal, 0.0f);
            gv.tangent = v.m_tangent;
            gv.uv.x = v.m_texCoord0.x;
            gv.uv.y = v.m_texCoord0.y;
            gv.uv.z = v.m_texCoord1.x;
            gv.uv.w = v.m_texCoord1.y;
            gpuVertices.push_back(gv);
        }

        // Create vertex buffer
        uint64_t vertexBufferSize = gpuVertices.size() * sizeof(GPUVertex);
        mesh.m_vertexBuffer = m_device->createBuffer({
            .size = vertexBufferSize,
            .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress | rhi::BufferUsage::TransferDst,
            .memoryUsage = rhi::MemoryUsage::GPUOnly,
            .data = gpuVertices.data(),
            .debugName = "VertexPullingVertexBuffer"
        });

        // Create index buffer
        uint64_t indexBufferSize = indices.size() * sizeof(uint32_t);
        mesh.m_indexBuffer = m_device->createBuffer({
            .size = indexBufferSize,
            .usage = rhi::BufferUsage::IndexBuffer | rhi::BufferUsage::TransferDst,
            .memoryUsage = rhi::MemoryUsage::GPUOnly,
            .data = indices.data(),
            .debugName = "VertexPullingIndexBuffer"
        });

        mesh.m_vertexCount = u32(vertices.size());
        mesh.m_indexCount = u32(indices.size());

        auto handle = static_cast<MeshHandle>(m_meshes.size());
        m_meshes.push_back(std::move(mesh));

        core::Logger::info("Created mesh: {} vertices, {} indices",
                           mesh.m_vertexCount, mesh.m_indexCount);


        return handle;
    }

    MeshHandle RHIRenderer::createMesh(const std::vector<Vertex>& vertices,
                                       const std::vector<uint32_t>& indices, bool enableVertexPulling)
    {
        auto meshHandle = enableVertexPulling
                              ? loadVertexPulling(vertices, indices)
                              : loadNoVertexPulling(vertices, indices);
        return meshHandle;
    }


    TextureHandle RHIRenderer::createTexture(const unsigned char* data,
                                             int width, int height, int channels,
                                             bool srgb, bool isSigned)
    {
        rhi::Format format;
        switch (channels)
        {
        case 1: format = isSigned ? rhi::Format::R8_SNORM : rhi::Format::R8_UNORM;
            break;
        case 2: format = isSigned ? rhi::Format::R8G8_SNORM : rhi::Format::R8G8_UNORM;
            break;
        case 3: format = isSigned ? rhi::Format::R8G8B8_SNORM :  rhi::Format::R8G8B8_UNORM;
            break;
        case 4: format =  srgb ? rhi::Format::R8G8B8A8_SRGB : isSigned ? rhi::Format::R8G8B8A8_SNORM : rhi::Format::R8G8B8A8_UNORM;
            break;
        default:
            core::Logger::error("Unsupported channel count: {}", channels);
            return INVALID_TEXTURE_HANDLE;
        }

        auto texture = m_device->createTexture(
            rhi::Extent3D{.width = u32(width), .height = u32(height), .depth = 1},
            format,
            rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst,
            1, 1
        );

        // Upload texture data
        uint64_t imageSize = static_cast<uint64_t>(width * height * channels);
        texture->uploadData(data, imageSize);

        TextureData texData{};
        texData.texture = std::move(texture);
        texData.bindlessIndex = 0;

        if (m_useBindless && m_device)
        {
            auto bindlessHandle = m_device->registerBindlessTexture2D(
                texData.texture.get()
            );
            texData.bindlessIndex = bindlessHandle.index;
        }

        auto handle = static_cast<TextureHandle>(m_textures.size());
        m_textures.push_back(std::move(texData));

        core::Logger::info("Created texture: {}x{}, {} channels", width, height, channels);

        return handle;
    }

    TextureHandle RHIRenderer::createTexture(const rhi::TextureDescriptor& desc)
    {
        auto texture = m_device->createTexture(desc);

        TextureData texData{};
        texData.texture = std::move(texture);
        texData.bindlessIndex = 0;

        if (m_useBindless && m_device)
        {
            auto bindlessHandle = m_device->registerBindlessTexture2D(
                texData.texture.get()
            );
            texData.bindlessIndex = bindlessHandle.index;
        }

        auto handle = static_cast<TextureHandle>(m_textures.size());
        m_textures.push_back(std::move(texData));

        core::Logger::info("Created texture (desc): {}x{} mips={}", desc.extent.width, desc.extent.height, desc.mipLevels);

        return handle;
    }

    TextureHandle RHIRenderer::loadTexture(const std::filesystem::path& filepath, bool srgb)
    {
        int width;
        int height;
        int channels;
        stbi_set_flip_vertically_on_load(1);

        unsigned char* data = stbi_load(filepath.string().c_str(),
                                        &width, &height, &channels, 0);

        if (data == nullptr)
        {
            core::Logger::error("Failed to load texture: {}", filepath.string());
            return INVALID_TEXTURE_HANDLE;
        }

        TextureHandle handle = createTexture(data, width, height, channels, srgb);

        stbi_image_free(data);

        core::Logger::info("Loaded texture from: {}", filepath.string());

        return handle;
    }

    TextureHandle RHIRenderer::loadTextureKTX(const std::filesystem::path& filepath, bool srgb)
    {
        KTXTextureData ktxData{};
        std::string error;
        if (!KTXUtils::loadFromFile(filepath, ktxData, &error))
        {
            core::Logger::error("Failed to load KTX texture: {} ({})", filepath.string(), error);
            return INVALID_TEXTURE_HANDLE;
        }

        struct DestroyGuard {
            KTXTextureData* d;
            ~DestroyGuard() { if (d) KTXUtils::destroy(*d); }
        } guard{ &ktxData };

        if (ktxData.type == rhi::TextureType::TextureCube && ktxData.numFaces != 6)
        {
            core::Logger::error("KTX cubemap must have 6 faces: {}", filepath.string());
            return INVALID_TEXTURE_HANDLE;
        }

        if (ktxData.type == rhi::TextureType::Texture3D && ktxData.numLayers > 1)
        {
            core::Logger::error("KTX 3D arrays are not supported: {}", filepath.string());
            return INVALID_TEXTURE_HANDLE;
        }

        rhi::Format finalFormat = ktxData.format;
        if (srgb)
        {
            if (finalFormat == rhi::Format::R8G8B8A8_UNORM) finalFormat = rhi::Format::R8G8B8A8_SRGB;
            else if (finalFormat == rhi::Format::B8G8R8A8_UNORM) finalFormat = rhi::Format::B8G8R8A8_SRGB;
            else if (finalFormat == rhi::Format::BC1_RGB_UNORM) finalFormat = rhi::Format::BC1_RGB_SRGB;
            else if (finalFormat == rhi::Format::BC3_UNORM) finalFormat = rhi::Format::BC3_SRGB;
            else if (finalFormat == rhi::Format::BC7_UNORM) finalFormat = rhi::Format::BC7_SRGB;
        }
        else
        {
            if (finalFormat == rhi::Format::R8G8B8A8_SRGB) finalFormat = rhi::Format::R8G8B8A8_UNORM;
            else if (finalFormat == rhi::Format::B8G8R8A8_SRGB) finalFormat = rhi::Format::B8G8R8A8_UNORM;
            else if (finalFormat == rhi::Format::BC1_RGB_SRGB) finalFormat = rhi::Format::BC1_RGB_UNORM;
            else if (finalFormat == rhi::Format::BC3_SRGB) finalFormat = rhi::Format::BC3_UNORM;
            else if (finalFormat == rhi::Format::BC7_SRGB) finalFormat = rhi::Format::BC7_UNORM;
        }

        rhi::TextureDescriptor desc{};
        desc.extent = ktxData.extent;
        desc.format = finalFormat;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst;
        desc.mipLevels = ktxData.mipLevels;
        desc.type = ktxData.type;

        // Use the flattened arrayLayers (numLayers * numFaces) from KTXUtils
        desc.arrayLayers = ktxData.arrayLayers;

        if (desc.type == rhi::TextureType::TextureCube && (desc.arrayLayers % 6u) != 0u)
        {
            core::Logger::error("KTX cube arrayLayers must be multiple of 6: {}", filepath.string());
            return INVALID_TEXTURE_HANDLE;
        }

        auto texture = m_device->createTexture(desc);

        if (!texture)
        {
            core::Logger::error("Failed to create RHI texture for: {}", filepath.string());
            return INVALID_TEXTURE_HANDLE;
        }

        const auto* srcData = ktxData.data.data();
        const auto dataSize = ktxData.data.size();
        const uint32_t numLayers = ktxData.numLayers;
        const uint32_t numFaces = ktxData.numFaces;

        for (uint32_t level = 0; level < ktxData.mipLevels; ++level)
        {
            const ktx_size_t imageSize = ktxTexture_GetImageSize(ktxData.texture, level);

            for (uint32_t layer = 0; layer < numLayers; ++layer)
            {
                for (uint32_t face = 0; face < numFaces; ++face)
                {
                    ktx_size_t offset = 0;
                    if (ktxTexture_GetImageOffset(ktxData.texture, level, layer, face, &offset) != KTX_SUCCESS)
                    {
                        core::Logger::error("KTX offset query failed: {}", filepath.string());
                        return INVALID_TEXTURE_HANDLE;
                    }

                    if (offset + imageSize > dataSize)
                    {
                        core::Logger::error("KTX data range out of bounds: {}", filepath.string());
                        return INVALID_TEXTURE_HANDLE;
                    }

                    rhi::TextureSubresource subresource{};
                    subresource.mipLevel = level;
                    subresource.arrayLayer = layer * numFaces + face; // matches desc.arrayLayers flattening

                    texture->uploadData(srcData + offset, imageSize, subresource);
                }
            }
        }

        TextureData texData{};
        texData.texture = std::move(texture);
        texData.bindlessIndex = 0;

        if (m_useBindless && m_device)
        {
            if (ktxData.type == rhi::TextureType::TextureCube)
            {
                auto bindlessHandle = m_device->registerBindlessCubemapImage(
                    texData.texture.get()
                );
                texData.bindlessIndex = bindlessHandle.index;
            }
            else
            {
                auto bindlessHandle = m_device->registerBindlessTexture2D(
                    texData.texture.get()
                );
                texData.bindlessIndex = bindlessHandle.index;
            }
        }

        auto handle = static_cast<TextureHandle>(m_textures.size());
        m_textures.push_back(std::move(texData));

        core::Logger::info("Loaded KTX texture: {}", filepath.string());

        return handle;
    }


    TextureHandle RHIRenderer::createCubemap(const std::vector<std::filesystem::path>& faces, bool srgb)
    {
        if (faces.size() != 6)
        {
            core::Logger::error("createCubemap: Exactly 6 face images required, got {}", faces.size());
            return INVALID_TEXTURE_HANDLE;
        }

        // Load all 6 faces first to validate they have the same dimensions
        std::vector<std::unique_ptr<unsigned char[], void(*)(void*)>> faceData;
        std::vector<int> widths;
        std::vector<int> heights;
        std::vector<int> channels;

        for (const auto& facePath : faces)
        {
            int w;
            int h;
            int c;
            stbi_set_flip_vertically_on_load(1); // Don't flip for cubemaps
            unsigned char* data = stbi_load(facePath.string().c_str(), &w, &h, &c, STBI_rgb_alpha);

            if (data == nullptr)
            {
                core::Logger::error("Failed to load cubemap face: {}", facePath.string());
                return INVALID_TEXTURE_HANDLE;
            }

            faceData.emplace_back(data, stbi_image_free);
            widths.push_back(w);
            heights.push_back(h);
            channels.push_back(STBI_rgb_alpha);
        }

        // Validate all faces have the same dimensions
        for (size_t i = 1; i < widths.size(); ++i)
        {
            if (widths[i] != widths[0] || heights[i] != heights[0])
            {
                core::Logger::error("All cubemap faces must have the same dimensions");
                return INVALID_TEXTURE_HANDLE;
            }
        }

        // Determine format
        rhi::Format format;
        switch (channels[0])
        {
        case 1: format = rhi::Format::R8_UNORM;
            break;
        case 2: format = rhi::Format::R8G8_UNORM;
            break;
        case 3: format = rhi::Format::R8G8B8_UNORM;
            break;
        case 4: format = srgb ? rhi::Format::R8G8B8A8_SRGB : rhi::Format::R8G8B8A8_UNORM;
            break;
        default:
            core::Logger::error("Unsupported channel count: {}", channels[0]);
            return INVALID_TEXTURE_HANDLE;
        }

        // Create cubemap texture
        auto texture = m_device->createCubemap(
            rhi::Extent3D{.width = u32(widths[0]), .height = u32(heights[0]), .depth = 1},
            format,
            rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst,
            1 // mipLevels
        );

        // Upload each face
        uint64_t faceSize = static_cast<uint64_t>(widths[0] * heights[0] * 4);
        // Always use RGBA after STBI_rgb_alpha conversion
        for (uint32_t i = 0; i < 6; ++i)
        {
            rhi::TextureSubresource subresource{};
            subresource.mipLevel = 0;
            subresource.arrayLayer = i; // Each face is a different array layer

            texture->uploadData(faceData[i].get(), faceSize, subresource);
        }

        TextureData texData{};
        texData.texture = std::move(texture);
        texData.bindlessIndex = 0;

        if (m_useBindless && m_device)
        {
            auto bindlessHandle = m_device->registerBindlessCubemapImage(
                texData.texture.get()
            );
            texData.bindlessIndex = bindlessHandle.index;
        }

        auto handle = static_cast<TextureHandle>(m_textures.size());
        m_textures.push_back(std::move(texData));

        core::Logger::info("Created cubemap: {}x{}, {} faces", widths[0], heights[0], 6);

        return handle;
    }

    BufferHandle RHIRenderer::createBuffer(const rhi::BufferDescriptor& desc)
    {
        BufferData data{};

        // 1. Create the hardware resource via device
        data.buffer = m_device->createBuffer(desc);

        // 2. Automatically register with Bindless if it's a Storage Buffer
        if (m_useBindless && hasFlag(desc.usage, rhi::BufferUsage::StorageBuffer)) {
            auto bindlessHandle = m_device->registerBindlessBuffer(data.buffer.get());
            data.bindlessIndex = bindlessHandle.index;
        }

        // 3. Store and return handle
        BufferHandle handle{static_cast<uint32_t>(m_buffers.size())};
        m_buffers.push_back(std::move(data));

        core::Logger::info("Created Buffer: handle={}, size={}, bindless={}",
                           handle.id, desc.size, data.bindlessIndex);
        return handle;
    }

    PipelineHandle RHIRenderer::createGraphicsPipeline(const rhi::GraphicsPipelineDescriptor& desc)
    {
        auto pipeline = m_device->createGraphicsPipeline(desc);

        auto handle = static_cast<PipelineHandle>(m_pipelines.size());
        m_pipelines.push_back(std::move(pipeline));

        core::Logger::info("Created graphics pipeline");

        return handle;
    }

    PipelineHandle RHIRenderer::createComputePipeline(const rhi::ComputePipelineDescriptor& desc)
    {
        auto pipeline = m_device->createComputePipeline(desc);

        auto handle = static_cast<PipelineHandle>(m_pipelines.size());
        m_pipelines.push_back(std::move(pipeline));

        core::Logger::info("Created compute pipeline");

        return handle;
    }

    void RHIRenderer::setRecordFunc(const RHIRecordFunc& callback)
    {
        core::Logger::info("RHIRenderer: Record callback set.");
        m_recordCallback = callback;
    }

    void RHIRenderer::bindPipeline(rhi::RHICommandBuffer* cmd, PipelineHandle handle)
    {
        if (handle.id >= m_pipelines.size())
        {
            core::Logger::error("Invalid pipeline handle: {}", handle.id);
            return;
        }

        cmd->bindPipeline(m_pipelines[handle.id].get());
    }

    void RHIRenderer::bindMesh(rhi::RHICommandBuffer* cmd, MeshHandle handle)
    {
        if (handle.id >= m_meshes.size())
        {
            core::Logger::error("Invalid mesh handle: {}", handle.id);
            return;
        }

        const auto& mesh = m_meshes[handle.id];
        if (!mesh.m_vertexPulling)
        {
            cmd->bindVertexBuffer(0, mesh.m_vertexBuffer.get(), 0);
        }
        cmd->bindIndexBuffer(mesh.m_indexBuffer.get(), 0, false);
    }

    void RHIRenderer::drawMesh(rhi::RHICommandBuffer* cmd, MeshHandle handle)
    {
        if (handle.id >= m_meshes.size())
        {
            core::Logger::error("Invalid mesh handle.id: {}", handle.id);
            return;
        }

        const auto& mesh = m_meshes[handle.id];
        cmd->drawIndexed(mesh.m_indexCount, 1, 0, 0, 0);
    }

    void RHIRenderer::drawMeshInstanced(rhi::RHICommandBuffer* cmd, MeshHandle handle, uint32_t instanceCount)
    {
        if (handle.id >= m_meshes.size())
        {
            core::Logger::error("Invalid mesh handle.id: {}", handle.id);
            return;
        }

        const auto& mesh = m_meshes[handle.id];
        cmd->drawIndexed(mesh.m_indexCount, instanceCount, 0, 0, 0);
    }

    void RHIRenderer::drawMeshBaseInstance(rhi::RHICommandBuffer* cmd, MeshHandle handle, uint32_t firstInstance)
    {
        if (handle.id >= m_meshes.size())
        {
            core::Logger::error("Invalid mesh handle.id: {}", handle.id);
            return;
        }

        const auto& mesh = m_meshes[handle.id];
        cmd->drawIndexed(mesh.m_indexCount, 1, 0, 0, firstInstance);
    }

    void RHIRenderer::bindDescriptorSet(rhi::RHICommandBuffer* cmd,
                                        PipelineHandle handle,
                                        uint32_t setIndex,
                                        rhi::RHIDescriptorSet* descriptorSet)
    {
        auto* pipeline = getPipeline(handle);
        if (pipeline == nullptr)
        {
            core::Logger::error("Invalid pipeline handle: {}", handle.id);
            return;
        }

        cmd->bindDescriptorSet(pipeline, setIndex, descriptorSet);
    }

    rhi::RHITexture* RHIRenderer::getTexture(TextureHandle handle) const
    {
        if (handle.id >= m_textures.size())
        {
            core::Logger::error("Invalid texture handle: {}", handle.id);
            return nullptr;
        }

        return m_textures[handle.id].texture.get();
    }

    uint32_t RHIRenderer::getTextureBindlessIndex(TextureHandle handle) const
    {
        if (handle.id >= m_textures.size())
        {
            core::Logger::error("Invalid texture handle: {}", handle.id);
            return 0xFFFFFFFFU;
        }

        return m_textures[handle.id].bindlessIndex;
    }

    uint32_t RHIRenderer::getBindlessSamplerIndex(rhi::SamplerAddressMode addressMode) const
    {
        switch (addressMode)
        {
        case rhi::SamplerAddressMode::ClampToEdge:
        case rhi::SamplerAddressMode::ClampToBorder:
            return m_clampSamplerIndex;
        case rhi::SamplerAddressMode::MirroredRepeat:
            return m_mirrorSamplerIndex;
        case rhi::SamplerAddressMode::Repeat:
        default:
            return m_repeatSamplerIndex;
        }
    }

    rhi::RHIBuffer* RHIRenderer::getBuffer(BufferHandle handle) const
    {
        if (handle.id >= m_buffers.size())
        {
            core::Logger::error("Invalid buffer handle: {}", handle.id);
            return nullptr;
        }

        return m_buffers[handle.id].buffer.get();
    }

    uint32_t RHIRenderer::getBufferBindlessIndex(BufferHandle handle) const
    {
        if (handle.id >= m_buffers.size())
        {
            core::Logger::error("Invalid buffer handle: {}", handle.id);
            return 0xFFFFFFFFU;
        }

        return m_buffers[handle.id].bindlessIndex;
    }

    uint32_t RHIRenderer::getMeshIndexCount(MeshHandle handle) const
    {
        if (handle.id >= m_meshes.size())
        {
            core::Logger::error("Invalid mesh handle: {}", handle.id);
            return 0;
        }
        return m_meshes[handle.id].m_indexCount;
    }

    uint64_t RHIRenderer::getMeshVertexBufferAddress(MeshHandle handle) const
    {
        return m_meshes[handle.id].m_vertexBuffer->getDeviceAddress();
    }

    rhi::Format RHIRenderer::getDrawColorFormat() const
    {
        return m_swapchain ? m_swapchain->colorFormat() : rhi::Format::Undefined;
    }

    rhi::Format RHIRenderer::getDrawDepthFormat() const
    {
        return m_depthTarget->format();
    }

    rhi::Format RHIRenderer::getSwapchainColorFormat() const
    {
        return m_swapchain ? m_swapchain->colorFormat() : rhi::Format::Undefined;
    }

    void RHIRenderer::setBindlessEnabled(bool enabled)
    {
        if (enabled && !m_bindlessSupported)
        {
            core::Logger::warn("Cannot enable bindless: not supported");
            return;
        }

        m_useBindless = enabled;
        core::Logger::info("Bindless rendering: {}", enabled ? "ENABLED" : "DISABLED");
    }

    rhi::RHIPipeline* RHIRenderer::pipeline(PipelineHandle handle)
    {
        return getPipeline(handle);
    }

    void RHIRenderer::createRenderTargets()
    {
        if (!m_swapchain)
        {
            throw cpptrace::runtime_error("createRenderTargets: swapchain is null");
        }

        const auto scExtent = m_swapchain->extent();

        // Depth target (device-owned; backbuffer comes from the swapchain).
        m_depthTarget = m_device->createTexture(
            rhi::Extent3D{.width = scExtent.width, .height = scExtent.height, .depth = 1},
            rhi::Format::D32_SFLOAT,
            rhi::TextureUsage::DepthStencilAttachment,
            1, 1
        );
        m_depthLayout = rhi::ResourceLayout::Undefined;

        core::Logger::info("Created swapchain/depth targets: {}x{}", scExtent.width, scExtent.height);
    }

    void RHIRenderer::createDefaultResources()
    {
        // Create white texture (1x1 white pixel)
        m_whiteTexture = createWhiteTexture();
        m_blackTexture = createBlackTexture();
        m_flatNormalTexture = createFlatNormalTexture();
    }

    rhi::RHIPipeline* RHIRenderer::getPipeline(PipelineHandle handle)
    {
        if (handle.id >= m_pipelines.size())
        {
            return nullptr;
        }
        return m_pipelines[handle.id].get();
    }

    TextureHandle RHIRenderer::createWhiteTexture()
    {
        unsigned char white[4] = {255, 255, 255, 255};
        return createTexture(white, 1, 1, 4, false);
    }

    TextureHandle RHIRenderer::createBlackTexture()
    {
        unsigned char black[4] = {0, 0, 0, 255};
        return createTexture(black, 1, 1, 4, false);
    }

    TextureHandle RHIRenderer::createFlatNormalTexture()
    {
        unsigned char flatNormal[4] = {128, 128, 255, 255};
        return createTexture(flatNormal, 1, 1, 4, false);
    }

    void RHIRenderer::setVsync(bool enabled)
    {
        if (m_swapchain) {
            m_device->waitIdle();
            m_swapchain->setVsync(enabled);
            // Force recreate
            m_swapchain->recreate(m_window.width(), m_window.height());
        }
    }

    void RHIRenderer::uploadToBuffer(rhi::RHIBuffer* target, const void* data, uint64_t size)
    {
        if ((target == nullptr) || (data == nullptr) || size == 0)
        {
            core::Logger::error("uploadToBuffer: invalid target/data/size");
            return;
        }

        auto staging = m_device->createBuffer({
            .size = size,
            .usage = rhi::BufferUsage::TransferSrc,
            .memoryUsage = rhi::MemoryUsage::CPUToGPU,
            .data = data,
            .debugName = "UploadToBufferStaging"
        });

        auto cmd = m_device->createCommandBuffer();
        cmd->begin();
        cmd->copyBuffer(staging.get(), target, 0, 0, size);
        cmd->end();
        m_device->submitCommands(cmd.get());
        m_device->waitIdle();
    }

    void RHIRenderer::setGlobalIBL(TextureHandle irradiance, TextureHandle prefilter, TextureHandle brdfLut)
    {
        auto* irrTex = getTexture(irradiance);
        auto* prefTex = getTexture(prefilter);
        auto* brdfTex = getTexture(brdfLut);

        if (!irrTex || !prefTex || !brdfTex)
        {
            core::Logger::error("setGlobalIBL: One or more textures are invalid");
            return;
        }

        m_globalLightingSet->updateTexture(0, irrTex, m_defaultSampler.get());
        m_globalLightingSet->updateTexture(1, prefTex, m_defaultSampler.get());
        m_globalLightingSet->updateTexture(2, brdfTex, m_defaultSampler.get());
    }
} // namespace pnkr::renderer
