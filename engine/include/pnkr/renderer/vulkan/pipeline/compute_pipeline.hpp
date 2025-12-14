//
// Created by Jose on 12/14/2025.
//

#ifndef PNKR_COMPUTE_PIPELINE_HPP
#define PNKR_COMPUTE_PIPELINE_HPP
#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>
#include <filesystem>
#include <memory>

namespace pnkr::renderer {

    class Renderer;

    class ComputePipeline {
    public:
        ComputePipeline(vk::Device device, vk::Pipeline pipeline, vk::PipelineLayout layout);
        ~ComputePipeline();

        [[nodiscard]] vk::Pipeline pipeline() const { return m_pipeline; }
        [[nodiscard]] vk::PipelineLayout layout() const { return m_layout; }

    private:
        vk::Device m_device;
        vk::Pipeline m_pipeline;
        vk::PipelineLayout m_layout;
    };

    class ComputePipelineBuilder {
    public:
        explicit ComputePipelineBuilder(Renderer& renderer);

        ComputePipelineBuilder& setShader(const std::filesystem::path& path);
        ComputePipelineBuilder& addDescriptorSetLayout(vk::DescriptorSetLayout layout);
        ComputePipelineBuilder& setPushConstantSize(uint32_t size);

        std::unique_ptr<ComputePipeline> build();

    private:
        Renderer& m_renderer;
        std::filesystem::path m_shaderPath;
        std::vector<vk::DescriptorSetLayout> m_layouts;
        uint32_t m_pushConstantSize = 0;
    };

}
#endif //PNKR_COMPUTE_PIPELINE_HPP