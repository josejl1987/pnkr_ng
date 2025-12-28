#pragma once

#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/core/common.hpp"
#include <vector>
#include <functional>
#include <cstring>
#include <string>

namespace pnkr::renderer {

    // Matches DrawIndexedIndirectCommand in shaders/C++
    using IndirectCommand = rhi::DrawIndexedIndirectCommand;

    // Wrapper for VKIndirectBuffer11
    class IndirectDrawBuffer {
    public:
        // Changed: Now accepts maxFramesInFlight to create ring-buffered staging memory
        IndirectDrawBuffer(RHIRenderer* renderer, uint32_t maxCommands, uint32_t maxFramesInFlight) 
            : m_renderer(renderer), m_maxCommands(maxCommands) 
        {
            // Layout: [Count (4 bytes)] [Padding (12 bytes)] [Command 0] [Command 1] ...
            // We pad to 16 bytes for proper alignment on all GPUs.
            size_t size = 16 + maxCommands * sizeof(IndirectCommand);
            
            // Main buffer in device-local memory
            m_buffer = m_renderer->createBuffer({
                .size = size,
                .usage = rhi::BufferUsage::IndirectBuffer | rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::TransferDst, 
                .memoryUsage = rhi::MemoryUsage::GPUOnly,
                .debugName = "IndirectDrawBuffer_GPU"
            });

            // Create per-frame staging buffers for async upload
            m_stagingBuffers.resize(maxFramesInFlight);
            m_stagingNames.reserve(maxFramesInFlight);
            for(uint32_t i = 0; i < maxFramesInFlight; ++i) {
                m_stagingNames.push_back("IndirectStaging_" + std::to_string(i));
                m_stagingBuffers[i] = m_renderer->createBuffer({
                    .size = size,
                    .usage = rhi::BufferUsage::TransferSrc,
                    .memoryUsage = rhi::MemoryUsage::CPUToGPU, // Host Visible
                    .debugName = m_stagingNames.back().c_str()
                });
            }
            
            m_commands.reserve(maxCommands);
        }

        ~IndirectDrawBuffer() {
            if (m_buffer != INVALID_BUFFER_HANDLE) {
                m_renderer->destroyBuffer(m_buffer);
            }
            for (auto& handle : m_stagingBuffers) {
                if (handle != INVALID_BUFFER_HANDLE) {
                    m_renderer->destroyBuffer(handle);
                }
            }
        }

        void clear() {
            m_commands.clear();
        }

        void addCommand(const IndirectCommand& cmd) {
            if (m_commands.size() < m_maxCommands) {
                m_commands.push_back(cmd);
            }
        }

        void addCommands(const std::vector<IndirectCommand>& cmds) {
            size_t toCopy = std::min((size_t)cmds.size(), (size_t)(m_maxCommands - m_commands.size()));
            if (toCopy > 0) {
                m_commands.insert(m_commands.end(), cmds.begin(), cmds.begin() + toCopy);
            }
        }

        // Optimized: Records copy command to the main command buffer. No waiting.
        // MUST BE CALLED OUTSIDE RENDER PASS.
        void upload(rhi::RHICommandBuffer* cmd, uint32_t frameIndex) {
            if (m_buffer == INVALID_BUFFER_HANDLE || m_stagingBuffers.empty()) return;
            
            // 1. Select the staging buffer for the current frame
            uint32_t safeFrameIdx = frameIndex % (uint32_t)m_stagingBuffers.size();
            BufferHandle currentStaging = m_stagingBuffers[safeFrameIdx];
            
            auto* stagingInfo = m_renderer->getBuffer(currentStaging);
            auto* gpuBuffer = m_renderer->getBuffer(m_buffer);

            if (!stagingInfo || !gpuBuffer) return;

            // 2. Map & Copy CPU -> Staging (Fast, Host Coherent)
            uint32_t count = static_cast<uint32_t>(m_commands.size());
            size_t uploadSize = 16 + count * sizeof(IndirectCommand);

            uint8_t* ptr = static_cast<uint8_t*>(stagingInfo->map());
            if (ptr) {
                std::memset(ptr, 0, 16); // Clear padding/count header
                std::memcpy(ptr, &count, sizeof(uint32_t));
                
                if (count > 0) {
                    std::memcpy(ptr + 16, m_commands.data(), count * sizeof(IndirectCommand));
                }
                stagingInfo->unmap();
            }

            // 3. Record Async Copy (GPU Staging -> GPU Local)
            cmd->copyBuffer(stagingInfo, gpuBuffer, 0, 0, uploadSize);

            // 4. Pipeline Barrier: Ensure Transfer finishes before Indirect Draw reads
            rhi::RHIMemoryBarrier barrier{};
            barrier.buffer = gpuBuffer;
            barrier.srcAccessStage = rhi::ShaderStage::Transfer;
            barrier.dstAccessStage = rhi::ShaderStage::DrawIndirect | rhi::ShaderStage::Compute;
            
            cmd->pipelineBarrier(
                rhi::ShaderStage::Transfer, 
                rhi::ShaderStage::DrawIndirect | rhi::ShaderStage::Compute, 
                { barrier }
            );
        }

        BufferHandle handle() const { return m_buffer; }
        uint32_t maxCommands() const { return m_maxCommands; }
        const std::vector<IndirectCommand>& commands() const { return m_commands; }

    private:
        RHIRenderer* m_renderer;
        BufferHandle m_buffer;
        std::vector<BufferHandle> m_stagingBuffers; // Ring buffer
        std::vector<std::string> m_stagingNames;
        uint32_t m_maxCommands;
        std::vector<IndirectCommand> m_commands;
    };

    struct IndirectPipeline {
        PipelineHandle solid = INVALID_PIPELINE_HANDLE;
        PipelineHandle wireframe = INVALID_PIPELINE_HANDLE;
    };

}
