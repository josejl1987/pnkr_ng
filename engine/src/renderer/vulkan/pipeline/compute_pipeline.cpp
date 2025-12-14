//
// Created by Jose on 12/14/2025.
//

#include "pnkr/renderer/vulkan/pipeline/compute_pipeline.hpp"
#include "pnkr/renderer/renderer.hpp"
#include "pnkr/core/logger.hpp"
#include <fstream>

namespace pnkr::renderer {

    // --- Helper: Read File ---
    static std::vector<char> readFile(const std::filesystem::path& filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filename.string());
        }
        size_t fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        return buffer;
    }

    // --- ComputePipeline ---
    ComputePipeline::ComputePipeline(vk::Device device, vk::Pipeline pipeline, vk::PipelineLayout layout)
        : m_device(device), m_pipeline(pipeline), m_layout(layout) {}

    ComputePipeline::~ComputePipeline() {
        if (m_device) {
            m_device.destroyPipeline(m_pipeline);
            m_device.destroyPipelineLayout(m_layout);
        }
    }

    // --- ComputePipelineBuilder ---
    ComputePipelineBuilder::ComputePipelineBuilder(Renderer& renderer) : m_renderer(renderer) {}

    ComputePipelineBuilder& ComputePipelineBuilder::setShader(const std::filesystem::path& path) {
        m_shaderPath = path;
        return *this;
    }

    ComputePipelineBuilder& ComputePipelineBuilder::addDescriptorSetLayout(vk::DescriptorSetLayout layout) {
        m_layouts.push_back(layout);
        return *this;
    }

    ComputePipelineBuilder& ComputePipelineBuilder::setPushConstantSize(uint32_t size) {
        m_pushConstantSize = size;
        return *this;
    }

    std::unique_ptr<ComputePipeline> ComputePipelineBuilder::build() {
        auto device = m_renderer.device();

        // 1. Create Shader Module
        auto code = readFile(m_shaderPath);
        vk::ShaderModuleCreateInfo createInfo{};
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        vk::ShaderModule shaderModule = device.createShaderModule(createInfo);

        // 2. Pipeline Layout
        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(m_layouts.size());
        layoutInfo.pSetLayouts = m_layouts.data();

        vk::PushConstantRange pcRange{};
        if (m_pushConstantSize > 0) {
            pcRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
            pcRange.offset = 0;
            pcRange.size = m_pushConstantSize;
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pcRange;
        }

        vk::PipelineLayout pipelineLayout = device.createPipelineLayout(layoutInfo);

        // 3. Compute Pipeline
        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.stage.stage = vk::ShaderStageFlagBits::eCompute;
        pipelineInfo.stage.module = shaderModule;
        pipelineInfo.stage.pName = "main";

        auto result = device.createComputePipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to create compute pipeline");
        }

        device.destroyShaderModule(shaderModule);

        return std::make_unique<ComputePipeline>(device, result.value, pipelineLayout);
    }
}