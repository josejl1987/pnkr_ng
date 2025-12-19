#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include "pipeline/Pipeline.h"
#include "pnkr/core/Handle.h"
#include "pnkr/core/profiler.hpp"
#include "pnkr/renderer/renderer_config.hpp"
#include "pnkr/renderer/vulkan/vulkan_command_buffer.hpp"
#include "pnkr/renderer/vulkan/vulkan_context.hpp"
#include "pnkr/renderer/vulkan/vulkan_device.hpp"
#include "pnkr/renderer/vulkan/vulkan_pipeline.hpp"
#include "pnkr/renderer/vulkan/vulkan_render_target.h"
#include "pnkr/renderer/vulkan/vulkan_swapchain.hpp"
#include "pnkr/renderer/vulkan/vulkan_sync_manager.h"

#include "geometry/mesh.h"
#include "pnkr/renderer/vulkan/pipeline/compute_pipeline.hpp"
#include "geometry/Vertex.h"
#include "pnkr/core/common.hpp"
#include "pnkr/core/logger.hpp"
#include "vulkan/bindless/bindless_manager.hpp"
#include "vulkan/image/vulkan_image.hpp"
#include "vulkan/image/vulkan_sampler.hpp"

namespace pnkr::renderer
{
    class VulkanCommandBuffer;
    class VulkanContext;
    class VulkanDevice;
    class VulkanPipeline;
    class VulkanSwapchain;
    class VulkanSyncManager;
    class VulkanRenderTarget;
    class VulkanDescriptorAllocator;
    class VulkanDescriptorLayoutCache;
    class VulkanImage;
    class VulkanSampler;
}

namespace pnkr::renderer
{
    using RecordFunc = std::function<void(const RenderFrameContext&)>;

    class Renderer
    {
    public:
        explicit Renderer(platform::Window& window,
                          [[maybe_unused]] const RendererConfig& config);
        void createTextureDescriptorSetLayout();
        TextureHandle createTextureFromPixels(const unsigned char* pixels,
                                              int width,
                                              int height,
                                              int channels,
                                              bool srgb = true);
        uint32_t getTextureBindlessIndex(TextureHandle handle) const;

        explicit Renderer(platform::Window& window)
            : Renderer(window, RendererConfig{})
        {
        }

        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;
        Renderer(Renderer&&) = delete;
        Renderer& operator=(Renderer&&) = delete;

        void bindMesh(vk::CommandBuffer cmd, MeshHandle handle) const;
        void drawMesh(vk::CommandBuffer cmd, MeshHandle handle) const;

        void beginFrame(float deltaTime);
        void drawFrame();
        void endFrame();
        void resize(int width, int height);

        MeshHandle createMesh(const std::vector<Vertex>& vertices,
                              const std::vector<uint32_t>& indices);
        PipelineHandle createPipeline(const VulkanPipeline::Config& cfg);
        void setRecordFunc(const RecordFunc& callback);
        void bindPipeline(vk::CommandBuffer cmd, PipelineHandle handle) const;
        vk::PipelineLayout pipelineLayout(PipelineHandle handle) const;
        TextureHandle loadTexture(const std::filesystem::path& filepath,
                                  bool srgb = true);
        vk::DescriptorSet getTextureDescriptor(TextureHandle handle) const;
        vk::DescriptorSetLayout getTextureDescriptorLayout() const;

        template <typename T>
        void pushConstants(vk::CommandBuffer cmd,
                           PipelineHandle pipe,
                           vk::ShaderStageFlags stages,
                           const T& data,
                           uint32_t offset = 0) const
        {
            static_assert(std::is_trivially_copyable_v<T>,
                          "pushConstants<T>: T must be trivially copyable");
            cmd.pushConstants(pipelineLayout(pipe),
                              stages,
                              offset,
                              util::u32(sizeof(T)),
                              &data);
        }

        void pushConstantsRaw(vk::CommandBuffer cmd,
                              PipelineHandle pipe,
                              vk::ShaderStageFlags stages,
                              uint32_t offset,
                              uint32_t size,
                              const void* data) const
        {
            cmd.pushConstants(pipelineLayout(pipe), stages, offset, size, data);
        }

        [[nodiscard]] vk::Format getDrawColorFormat() const { return m_mainTarget->colorImage().format(); }
        [[nodiscard]] vk::Format getDrawDepthFormat() const { return m_mainTarget->depthImage().format(); }
        [[nodiscard]] vk::Device device() const { return m_device->device(); }
        [[nodiscard]] vk::PhysicalDevice physicalDevice() const { return m_device->physicalDevice(); }
        [[nodiscard]] vk::Instance instance() const { return m_context->instance(); }

        [[nodiscard]] vk::Queue graphicsQueue() const { return m_device->graphicsQueue(); }
        [[nodiscard]] uint32_t graphicsQueueFamilyIndex() const { return m_device->graphicsQueueFamily(); }

        [[nodiscard]] vk::CommandPool commandPool() const
        {
            return m_commandBuffer->commandPool();
        }

        [[nodiscard]] vk::Format getSwapchainColorFormat() const { return m_swapchain->imageFormat(); }

        static void bindPipeline(vk::CommandBuffer cmd, const ComputePipeline& pipeline);
        static void dispatch(vk::CommandBuffer cmd, uint32_t groupX, uint32_t groupY, uint32_t groupZ);

        using PostProcessCallback = std::function<void(vk::CommandBuffer cmd, uint32_t swapchainImageIndex,
                                                       const vk::Extent2D& extent)>;
        void setPostProcessCallback(PostProcessCallback callback) { m_postProcessCallback = callback; }

        [[nodiscard]] vk::ImageView getSwapchainImageView(uint32_t index) const
        {
            return m_swapchain->imageViews()[index];
        }

        [[nodiscard]] uint32_t getSwapchainImageCount() const
        {
            return static_cast<uint32_t>(m_swapchain->images().size());
        }

        [[nodiscard]] const VulkanImage& getOffscreenTexture() const { return m_mainTarget->colorImage(); }

        [[nodiscard]] BindlessIndex registerBindlessStorageBuffer(vk::Buffer buffer,
                                                                  vk::DeviceSize offset,
                                                                  vk::DeviceSize range) const
        {
            if (!m_bindless) {
                core::Logger::error("Bindless not initialized");
                return INVALID_BINDLESS_INDEX;
            }
            return m_bindless->registerStorageBuffer(buffer, offset, range);
        }

        // Overload for common case where offset = 0
        [[nodiscard]] BindlessIndex registerBindlessStorageBuffer(vk::Buffer buffer,
                                                                  vk::DeviceSize range) const
        {
            return registerBindlessStorageBuffer(buffer, 0, range);
        }

        [[nodiscard]] BindlessIndex registerBindlessSampledImage(vk::ImageView view, vk::Sampler sampler) const
        {
            return m_bindless->registerSampledImage(view, sampler);
        }

        [[nodiscard]] BindlessIndex registerBindlessStorageImage(vk::ImageView view) const
        {
            return m_bindless->registerStorageImage(view);
        }

        [[nodiscard]] vk::DescriptorSetLayout getBindlessLayout() const
        {
            return m_bindless->getLayout();
        }

        [[nodiscard]] vk::DescriptorSet getBindlessDescriptorSet() const
        {
            return m_bindless->getDescriptorSet();
        }

        void setBindlessEnabled(bool enabled) {
            if (enabled && !m_bindless) {
                core::Logger::warn("Cannot enable bindless: not initialized at startup");
                return;
            }
            m_useBindlessForCurrentFrame = enabled;
            core::Logger::info("Bindless rendering: {}", enabled ? "ENABLED" : "DISABLED");
        }

        [[nodiscard]] bool isBindlessEnabled() const noexcept {
            return m_useBindlessForCurrentFrame;
        }

        [[nodiscard]] bool hasBindlessSupport() const noexcept {
            return m_bindless != nullptr;
        }


    private:
        platform::Window& m_window;
        std::unique_ptr<VulkanContext> m_context;
        std::unique_ptr<VulkanDevice> m_device;
        std::unique_ptr<VulkanSwapchain> m_swapchain;
        std::unique_ptr<VulkanCommandBuffer> m_commandBuffer;
        std::unique_ptr<VulkanSyncManager> m_sync;
        TracyContext m_tracyCtx{nullptr};
        std::vector<std::unique_ptr<Mesh>> m_meshes;
        std::unique_ptr<VulkanRenderTarget> m_mainTarget;
        std::unique_ptr<VulkanDescriptorAllocator> m_descriptorAllocator;
        std::unique_ptr<VulkanDescriptorLayoutCache> m_descriptorLayoutCache;
        std::unique_ptr<VulkanSampler> m_defaultSampler;
        std::vector<std::unique_ptr<VulkanImage>> m_textures;
        std::vector<vk::DescriptorSet> m_textureDescriptors;
        vk::DescriptorSetLayout m_textureSetLayout{};
        PostProcessCallback m_postProcessCallback = nullptr;
        std::unique_ptr<BindlessManager> m_bindless;
        bool m_useBindlessForCurrentFrame = false;
        std::vector<std::unique_ptr<VulkanPipeline>> m_pipelines;
        std::vector<BindlessIndex> m_textureBindlessIndices;
        const VulkanPipeline& pipeline(PipelineHandle handle) const;
        RecordFunc m_recordCallback;

        uint32_t m_imageIndex = 0;
        bool m_frameInProgress = false;
        float m_deltaTime = 0.0f;
        TextureHandle m_whiteTexture{INVALID_TEXTURE_HANDLE};
    };
}
