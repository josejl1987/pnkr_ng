#include "rhi/vulkan/BDARegistry.hpp"
#include "pnkr/core/logger.hpp"

namespace pnkr::renderer::rhi::vulkan
{
    void BDARegistry::registerBuffer(uint64_t address, uint64_t size, const std::string& name, uint64_t currentFrame)
    {
        std::unique_lock lock(m_mutex);

        BDAAllocationInfo info{};
        info.baseAddress = address;
        info.size = size;
        info.debugName = name;
        info.frameCreated = currentFrame;
        info.frameFreed = 0;
        info.isAlive = true;

        m_allocations[address] = info;

        core::Logger::RHI.trace("BDA Register: {:#x} - {} (size: {} bytes, frame: {})", address, name, size, currentFrame);
    }

    void BDARegistry::unregisterBuffer(uint64_t address, uint64_t currentFrame)
    {
        std::unique_lock lock(m_mutex);

        auto it = m_allocations.find(address);
        if (it == m_allocations.end())
        {
            core::Logger::RHI.warn("BDARegistry: Attempted to unregister unknown buffer at address {:#x}", address);
            return;
        }

        it->second.isAlive = false;
        it->second.frameFreed = currentFrame;

        core::Logger::RHI.trace("BDA Unregister: {:#x} - {} (freed at frame: {})",
            address, it->second.debugName, currentFrame);
    }

    const BDAAllocationInfo* BDARegistry::findAllocation(uint64_t address) const
    {
        std::shared_lock lock(m_mutex);

        auto it = m_allocations.upper_bound(address);
        if (it == m_allocations.begin()) {
          return nullptr;
        }

        --it;

        const auto& info = it->second;
        if (address >= info.baseAddress &&
            address < (info.baseAddress + info.size)) {
          return &info;
        }

        return nullptr;
    }

    void BDARegistry::onDeviceAddressBinding(VkDeviceAddressBindingTypeEXT type,
                                            uint64_t address,
                                            uint64_t size,
                                            VkDeviceAddressBindingFlagsEXT flags,
                                            const ObjectRef* objects,
                                            uint32_t objectCount)
    {
      if (address == 0 || size == 0) {
        return;
      }

        const uint64_t begin = address;
        const uint64_t end = address + size;

        std::unique_lock lock(m_mutex);

        auto addBindRecord = [&]()
        {
            RangeEvent ev{};
            ev.base = begin;
            ev.size = size;
            ev.bindingType = type;
            ev.flags = flags;
            ev.sequence = ++m_seq;
            ev.alive = true;

            if (objects && objectCount)
            {
                ev.objects.assign(objects, objects + objectCount);
            }

            m_ranges.emplace_back(std::move(ev));
        };

        auto killExactMatches = [&]() -> bool
        {
            bool any = false;
            for (auto& r : m_ranges)
            {
              if (!r.alive) {
                continue;
              }

                if (r.base == begin && r.size == size)
                {
                    r.alive = false;
                    r.bindingType = type;
                    r.flags = flags;
                    r.sequence = ++m_seq;
                    any = true;
                }
            }
            return any;
        };

        auto unbindByOverlapAndSplit = [&]() -> bool
        {
            bool any = false;

            struct SplitOp
            {
              std::optional<RangeEvent> m_left;
              std::optional<RangeEvent> m_right;
            };
            std::vector<SplitOp> splits;

            for (auto &r : m_ranges) {
              if (!r.alive) {
                continue;
              }

              const uint64_t r0 = r.base;
              const uint64_t r1 = r.base + r.size;

              if (!overlaps(r0, r1, begin, end)) {
                continue;
              }

              any = true;

              SplitOp op{};

              if (r0 < begin) {
                RangeEvent left = r;
                left.base = r0;
                left.size = begin - r0;
                left.alive = true;
                left.sequence = ++m_seq;
                op.m_left = std::move(left);
              }

              if (end < r1) {
                RangeEvent right = r;
                right.base = end;
                right.size = r1 - end;
                right.alive = true;
                right.sequence = ++m_seq;
                op.m_right = std::move(right);
              }

              r.alive = false;
              r.bindingType = type;
              r.flags = flags;
              r.sequence = ++m_seq;

              splits.push_back(std::move(op));
            }

            for (auto& op : splits)
            {
              if (op.m_left) {
                m_ranges.emplace_back(std::move(*op.m_left));
              }
              if (op.m_right) {
                m_ranges.emplace_back(std::move(*op.m_right));
              }
            }

            return any;
        };

        if (type == VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT)
        {
            addBindRecord();
            return;
        }

        if (type == VK_DEVICE_ADDRESS_BINDING_TYPE_UNBIND_EXT)
        {
            for (const auto& pair : m_allocations)
            {
                const auto& info = pair.second;
                if (!info.isAlive) {
                  continue;
                }

                const uint64_t a0 = info.baseAddress;
                const uint64_t a1 = info.baseAddress + info.size;
                if (overlaps(a0, a1, begin, end))
                {
                    core::Logger::RHI.warn(
                        "BDA Registry: Driver reported UNBIND for address range <{:#x} - {:#x}> (size {}) overlapping engine buffer '{}' <{:#x} - {:#x}>. This may indicate incorrect lifetime management or imminent use-after-free.",
                        begin, end, size, info.debugName, info.baseAddress, info.baseAddress + info.size
                    );
                    break;
                }
            }

            if (killExactMatches()) {
              return;
            }

            if (unbindByOverlapAndSplit()) {
              return;
            }

            RangeEvent tomb{};
            tomb.base = begin;
            tomb.size = size;
            tomb.bindingType = type;
            tomb.flags = flags;
            tomb.sequence = ++m_seq;
            tomb.alive = false;
            if ((objects != nullptr) && (objectCount != 0u)) {
              tomb.objects.assign(objects, objects + objectCount);
            }
            m_ranges.emplace_back(std::move(tomb));
            return;
        }
    }

    std::vector<BDARegistry::RangeEvent> BDARegistry::snapshot() const
    {
        std::shared_lock lock(m_mutex);
        return m_ranges;
    }
}

