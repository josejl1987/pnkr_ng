#pragma once

#include "pnkr/rhi/rhi_descriptor.hpp"
#include "pnkr/rhi/BindlessManager.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include <array>

#include <mutex>
#include <string>

namespace pnkr::renderer::rhi::vulkan
{
    class VulkanRHIDevice;

    class BindlessResourceManager {
    public:
        struct PendingRelease {
            uint32_t index;
            uint64_t frameIndex;
        };

        void init(uint32_t maxCapacity) {
            m_maxCapacity = maxCapacity;
            m_highWaterMark = 0;
            m_freeList.clear();
            m_pendingReleases.clear();
            m_slots.clear();
            m_slots.resize(maxCapacity);
        }

        uint32_t allocate() {
            if (!m_freeList.empty()) {
                uint32_t id = m_freeList.back();
                m_freeList.pop_back();
                return id;
            }

            if (m_highWaterMark >= m_maxCapacity) {
                return kInvalidBindlessIndex;
            }
            return m_highWaterMark++;
        }

        void markOccupied(uint32_t index, const BindlessSlotInfo& info) {
            if (index < m_slots.size()) {
                m_slots[index] = info;
                m_slots[index].isOccupied = true;
            }
        }

        void free(uint32_t id) {
            if (id != kInvalidBindlessIndex) {
                m_freeList.push_back(id);
                if (id < m_slots.size()) m_slots[id].isOccupied = false;
            }
        }

        void freeDeferred(uint32_t id, uint64_t frameIndex) {
            if (id != kInvalidBindlessIndex) {
                m_pendingReleases.push_back({id, frameIndex});
            }
        }

        void update(uint64_t completedFrame) {
            auto it = m_pendingReleases.begin();
            while (it != m_pendingReleases.end()) {
                if (it->frameIndex <= completedFrame) {
                    uint32_t id = it->index;
                    m_freeList.push_back(id);
                    if (id < m_slots.size()) m_slots[id].isOccupied = false;
                    it = m_pendingReleases.erase(it);
                } else {
                    ++it;
                }
            }
        }

        uint32_t maxCapacity() const { return m_maxCapacity; }
        uint32_t highWaterMark() const { return m_highWaterMark; }
        uint32_t freeListSize() const { return (uint32_t)m_freeList.size(); }
        const std::vector<BindlessSlotInfo>& slots() const { return m_slots; }

    private:
        uint32_t m_maxCapacity = 0;
        uint32_t m_highWaterMark = 0;
        std::vector<uint32_t> m_freeList;
        std::vector<PendingRelease> m_pendingReleases;
        std::vector<BindlessSlotInfo> m_slots;
    };

    class BindlessDescriptorManager : public BindlessManager
    {
    public:
        BindlessDescriptorManager(vk::Device device, vk::PhysicalDevice physicalDevice);
        ~BindlessDescriptorManager() override;

        void init(VulkanRHIDevice* rhiDevice);

        TextureBindlessHandle registerTexture(RHITexture* texture, RHISampler* sampler) override;
        TextureBindlessHandle registerCubemap(RHITexture* texture, RHISampler* sampler) override;
        TextureBindlessHandle registerTexture2D(RHITexture* texture) override;
        TextureBindlessHandle registerCubemapImage(RHITexture* texture) override;
        SamplerBindlessHandle registerSampler(RHISampler* sampler) override;
        SamplerBindlessHandle registerShadowSampler(RHISampler* sampler) override;
        TextureBindlessHandle registerStorageImage(RHITexture* texture) override;
        BufferBindlessHandle registerBuffer(RHIBuffer* buffer) override;
        TextureBindlessHandle registerShadowTexture2D(RHITexture* texture) override;
        TextureBindlessHandle registerMSTexture2D(RHITexture* texture) override;

        void updateTexture(TextureBindlessHandle handle, RHITexture* texture) override;

        void releaseTexture(TextureBindlessHandle handle) override;
        void releaseCubemap(TextureBindlessHandle handle) override;
        void releaseSampler(SamplerBindlessHandle handle) override;
        void releaseShadowSampler(SamplerBindlessHandle handle) override;
        void releaseStorageImage(TextureBindlessHandle handle) override;
        void releaseBuffer(BufferBindlessHandle handle) override;
        void releaseShadowTexture2D(TextureBindlessHandle handle) override;
        void releaseMSTexture2D(TextureBindlessHandle handle) override;

        void update(uint64_t completedFrame);

        BindlessStatistics getStatistics() const override;

        RHIDescriptorSet* getDescriptorSet() const { return (RHIDescriptorSet*)m_bindlessSetWrapper.get(); }
        RHIDescriptorSetLayout* getDescriptorSetLayout() const { return (RHIDescriptorSetLayout*)m_bindlessLayout.get(); }

        vk::DescriptorSet vkDescriptorSet() const { return m_bindlessSet; }

    private:
        void updateSampler(TextureBindlessHandle imageHandle, RHISampler* sampler);

        mutable std::mutex m_mutex;

        VulkanRHIDevice* m_rhiDevice = nullptr;
        vk::Device m_device;
        vk::PhysicalDevice m_physicalDevice;
        vk::DescriptorPool m_bindlessPool;
        vk::DescriptorSet m_bindlessSet;
        std::unique_ptr<class VulkanRHIDescriptorSetLayout> m_bindlessLayout;
        std::unique_ptr<class VulkanRHIDescriptorSet> m_bindlessSetWrapper;

        std::unique_ptr<RHITexture> m_dummyTexture;
        std::unique_ptr<RHITexture> m_dummyCube;
        std::unique_ptr<RHITexture> m_dummyStorageImage;
        std::unique_ptr<RHIBuffer> m_dummyBuffer;
        std::unique_ptr<RHISampler> m_dummySampler;

        BindlessResourceManager m_textureManager;
        BindlessResourceManager m_samplerManager;
        BindlessResourceManager m_shadowTextureManager;
        BindlessResourceManager m_shadowSamplerManager;
        BindlessResourceManager m_bufferManager;
        BindlessResourceManager m_cubemapManager;
        BindlessResourceManager m_storageImageManager;
        BindlessResourceManager m_msaaTextureManager;

        static constexpr uint32_t MAX_BINDLESS_RESOURCES = 100000;
        static constexpr uint32_t MAX_SAMPLERS = 200;
    };

}