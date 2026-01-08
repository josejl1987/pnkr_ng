#pragma once

#include <cstdint>
#include "rhi_pipeline.hpp"

namespace pnkr::renderer::rhi
{
    class RHIBuffer;
    class RHITexture;
    class RHISampler;

    class RHIDescriptorSetLayout
    {
    public:
        virtual ~RHIDescriptorSetLayout() = default;
        virtual void* nativeHandle() const = 0;
        virtual const DescriptorSetLayout& description() const = 0;
    };

    class RHIDescriptorSet
    {
    public:
        virtual ~RHIDescriptorSet() = default;

        virtual void updateBuffer(uint32_t binding,
                                  RHIBuffer* buffer,
                                  uint64_t offset,
                                  uint64_t range) = 0;
        virtual void updateTexture(uint32_t binding,
                                   RHITexture* texture,
                                   RHISampler* sampler) = 0;

        virtual void* nativeHandle() const = 0;
    };
}
