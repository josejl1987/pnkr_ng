#pragma once

/**
 * @file renderer.hpp
 * @brief Public rendering facade for PNKR applications
 */

#include <memory>
#include <vector>

#include "pipeline/Pipeline.h"
#include "pnkr/core/Handle.h"
#include "pnkr/platform/window.hpp"
#include "pnkr/renderer/renderer_config.hpp"
#include "pnkr/renderer/vulkan/vulkan_command_buffer.hpp"
#include "pnkr/renderer/vulkan/vulkan_context.hpp"
#include "pnkr/renderer/vulkan/vulkan_device.hpp"
#include "pnkr/renderer/vulkan/vulkan_pipeline.hpp"
#include "pnkr/renderer/vulkan/vulkan_swapchain.hpp"
#include "pnkr/renderer/vulkan/vulkan_sync_manager.h"
#include "vulkan/geometry/mesh.h"
#include "vulkan/geometry/Vertex.h"
#include "vulkan/image/vulkan_image.hpp"
#include "vulkan/image/vulkan_sampler.hpp"

namespace pnkr::renderer {
    class VulkanCommandBuffer;
    class VulkanContext;
    class VulkanDevice;
    class VulkanPipeline;
    class VulkanSwapchain;
    class VulkanSyncManager;
    class VulkanDescriptorAllocator;
    class VulkanDescriptorLayoutCache;
    class VulkanImage;
    class VulkanSampler;
}


namespace pnkr::renderer
{
    using RecordFunc = std::function<void(const RenderFrameContext&)>;
    using TextureHandle = Handle;
    /**
     * @brief High-level renderer entry point exposed to applications
     *
     * Internals are owned via unique_ptr to keep Vulkan details private.
     */
    class Renderer
    {
    public:
        explicit Renderer(platform::Window& window,
                          [[maybe_unused]] const RendererConfig& config);
        void createTextureDescriptorSetLayout();

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
        void drawFrame() const;
        void endFrame();
        void resize(int width, int height);

        MeshHandle createMesh(const std::vector<Vertex>& vertices,
                              const std::vector<uint32_t>& indices);
        PipelineHandle createPipeline(const VulkanPipeline::Config& cfg);
        void setRecordFunc(const RecordFunc& callback);
        void bindPipeline(vk::CommandBuffer cmd, PipelineHandle handle) const;
        vk::PipelineLayout pipelineLayout(PipelineHandle handle) const;
        TextureHandle loadTexture(const std::filesystem::path& filepath);
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
                              static_cast<uint32_t>(sizeof(T)),
                              &data);
        }

        // Optional non-template overload (useful later if you pack bytes dynamically)
        void pushConstantsRaw(vk::CommandBuffer cmd,
                              PipelineHandle pipe,
                              vk::ShaderStageFlags stages,
                              uint32_t offset,
                              uint32_t size,
                              const void* data) const
        {
            cmd.pushConstants(pipelineLayout(pipe), stages, offset, size, data);
        }

    private:
        platform::Window& m_window;
        std::unique_ptr<VulkanContext> m_context;
        std::unique_ptr<VulkanDevice> m_device;
        std::unique_ptr<VulkanSwapchain> m_swapchain;
        std::unique_ptr<VulkanCommandBuffer> m_commandBuffer;
        std::unique_ptr<VulkanSyncManager> m_sync;
        std::vector<std::unique_ptr<Mesh>> m_meshes;

        std::unique_ptr<VulkanDescriptorAllocator> m_descriptorAllocator;
        std::unique_ptr<VulkanDescriptorLayoutCache> m_descriptorLayoutCache;
        std::unique_ptr<VulkanSampler> m_defaultSampler;
        std::vector<std::unique_ptr<VulkanImage>> m_textures;
        std::vector<vk::DescriptorSet> m_textureDescriptors;
        vk::DescriptorSetLayout m_textureSetLayout{};


        std::vector<std::unique_ptr<VulkanPipeline>> m_pipelines;
        const VulkanPipeline& pipeline(PipelineHandle handle) const;
        RecordFunc m_recordCallback; // must be set by app

        // State
        uint32_t m_imageIndex = 0;
        bool m_frameInProgress = false;
        float m_deltaTime = 0.0f;
    };
} // namespace pnkr::renderer
