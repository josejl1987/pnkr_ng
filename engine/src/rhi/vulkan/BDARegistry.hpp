#pragma once

#include "pnkr/core/common.hpp"
#include <vulkan/vulkan.hpp>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

namespace pnkr::renderer::rhi::vulkan
{
    struct BDAAllocationInfo
    {
        uint64_t baseAddress;
        uint64_t size;
        std::string debugName;
        uint64_t frameCreated;
        uint64_t frameFreed;
        bool isAlive;
    };

    class BDARegistry
    {
    public:
        struct ObjectRef
        {
            VkObjectType type = VK_OBJECT_TYPE_UNKNOWN;
            uint64_t handle = 0;
            std::string name;
        };

        struct RangeEvent
        {
            uint64_t base = 0;
            uint64_t size = 0;

            VkDeviceAddressBindingTypeEXT bindingType = VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT;
            VkDeviceAddressBindingFlagsEXT flags = 0;
            std::vector<ObjectRef> objects;

            uint64_t sequence = 0;
            bool alive = false;
        };

        void registerBuffer(uint64_t address, uint64_t size, const std::string& name, uint64_t currentFrame);
        void unregisterBuffer(uint64_t address, uint64_t currentFrame);

        const BDAAllocationInfo* findAllocation(uint64_t address) const;

        void onDeviceAddressBinding(VkDeviceAddressBindingTypeEXT type,
                                    uint64_t address,
                                    uint64_t size,
                                    VkDeviceAddressBindingFlagsEXT flags,
                                    const ObjectRef* objects,
                                    uint32_t objectCount);

        std::vector<RangeEvent> snapshot() const;

    private:
        static bool overlaps(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1)
        {
            return (a0 < b1) && (b0 < a1);
        }

        mutable std::shared_mutex m_mutex;

        std::map<uint64_t, BDAAllocationInfo> m_allocations;
        std::vector<RangeEvent> m_ranges;
        uint64_t m_seq = 0;
    };
}
