#pragma once

#include "pnkr/rhi/rhi_command_buffer.hpp"
#include "pnkr/rhi/rhi_pipeline.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/rhi/rhi_types.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/passes/HandleTraits.hpp"
#include <memory>
#include <optional>
#include <vector>

#include "pnkr/renderer/GPUBufferSlice.hpp"
#include "pnkr/renderer/gpu_shared/CullingShared.h"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"

namespace gpu
{
	struct IndirectPushConstants;
}

namespace pnkr::renderer {
	struct RenderPassContext;
}

namespace pnkr::renderer::passes::utils
{

	struct ShaderLoadResult
	{
		std::shared_ptr<rhi::Shader> vertex;
		std::shared_ptr<rhi::Shader> fragment;
		std::shared_ptr<rhi::Shader> compute;
		bool success;
	};

	ShaderLoadResult loadComputeShader(
		const char* compPath,
		const char* passName);

	ShaderLoadResult loadGraphicsShaders(
		const char* vertPath,
		const char* fragPath,
		const char* passName);

	class ScopedGpuMarker
	{
	public:
		ScopedGpuMarker(rhi::RHICommandList* cmd, const char* name);
		~ScopedGpuMarker();

		ScopedGpuMarker(const ScopedGpuMarker&) = delete;
		ScopedGpuMarker& operator=(const ScopedGpuMarker&) = delete;

	private:
		rhi::RHICommandList* m_cmd;
	};

	class ScopedDebugLabel
	{
	public:
		ScopedDebugLabel(rhi::RHICommandList* cmd, const char* label);
		ScopedDebugLabel(rhi::RHICommandList* cmd, const char* label,
			float r, float g, float b, float a);
		~ScopedDebugLabel();

		ScopedDebugLabel(const ScopedDebugLabel&) = delete;
		ScopedDebugLabel& operator=(const ScopedDebugLabel&) = delete;

	private:
		rhi::RHICommandList* m_cmd;
	};

	class ScopedPassMarkers
	{
	public:
		ScopedPassMarkers(rhi::RHICommandList* cmd,
			const char* passName,
			float r, float g, float b, float a);
		~ScopedPassMarkers();

	private:
		ScopedGpuMarker m_gpuMarker;
		ScopedDebugLabel m_debugLabel;
	};

	template<typename HandleT>
	void createTextureAttachment(
		RHIRenderer* renderer,
		HandleT& handle,
		uint32_t width,
		uint32_t height,
		rhi::Format format,
		rhi::TextureUsageFlags usage,
		const char* debugName,
		uint32_t samples = 1);

	template<typename HandleT>
	void recreateTextureIfNeeded(
		RHIRenderer* renderer,
		HandleT& handle,
		const rhi::TextureDescriptor& newDesc,
		const char* debugName);

	template<typename HandleT>
	void recreateBufferIfNeeded(
		RHIRenderer* renderer,
		HandleT& handle,
		const rhi::BufferDescriptor& newDesc,
		const char* debugName);

	void populateBaseIndirectPushConstants(
		const renderer::RenderPassContext& ctx,
		gpu::IndirectPushConstants& pc,
		RHIRenderer* renderer);

	struct IndirectDrawCall {
		PipelineHandle pipeline;
		GPUBufferSlice indirectBuffer;
		uint32_t drawCount;
		uint32_t commandSize = sizeof(gpu::DrawIndexedIndirectCommandGPU);
	};

	template<typename PushConstantsT>
	void executeIndirectDraw(
		RHIRenderer* renderer,
		rhi::RHICommandList* cmd,
		const IndirectDrawCall& call,
		const PushConstantsT& pc,
		core::Flags<rhi::ShaderStage> stages = rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment);

	template<typename PushConstantsT>
	void dispatchCompute(
		RHIRenderer* renderer,
		rhi::RHICommandList* cmd,
		PipelineHandle pipeline,
		const PushConstantsT& pc,
		uint32_t width,
		uint32_t height,
		uint32_t groupSizeXY = 16);

	struct GraphicsPassState
	{
		rhi::Format colorFormat;
		rhi::Format depthFormat;
		rhi::CompareOp depthCompareOp = rhi::CompareOp::LessOrEqual;
		bool enableDepthTest = true;
		bool enableDepthWrite = true;
		rhi::CullMode cullMode = rhi::CullMode::Back;
		bool frontFaceCounterClockwise = false;
		uint32_t msaaSamples = 1;
		bool enableBlend = false;
	};

	rhi::RHIPipelineBuilder createGraphicsPipelineBuilder(
		const GraphicsPassState& state,
		const std::shared_ptr<rhi::Shader>& vertShader,
		const std::shared_ptr<rhi::Shader>& fragShader);

	inline void setFullViewport(
		rhi::RHICommandList* cmd,
		uint32_t width,
		uint32_t height)
	{
		cmd->setViewport({0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1});
		cmd->setScissor({0, 0, width, height});
	}

	class RenderingInfoBuilder
	{
	public:
		RenderingInfoBuilder() = default;
		RenderingInfoBuilder(const RenderingInfoBuilder&) = delete;
		RenderingInfoBuilder& operator=(const RenderingInfoBuilder&) = delete;

		RenderingInfoBuilder& setRenderArea(uint32_t width, uint32_t height);
		RenderingInfoBuilder& addColorAttachment(
			rhi::RHITexture* texture,
			rhi::LoadOp loadOp = rhi::LoadOp::Clear,
			rhi::StoreOp storeOp = rhi::StoreOp::Store,
			rhi::RHITexture* resolveTexture = nullptr);
		RenderingInfoBuilder& setDepthAttachment(
			rhi::RHITexture* texture,
			rhi::LoadOp loadOp = rhi::LoadOp::Clear,
			rhi::StoreOp storeOp = rhi::StoreOp::Store,
			rhi::RHITexture* resolveTexture = nullptr);

		const rhi::RenderingInfo& get();

	private:
		rhi::RenderingInfo m_info{};
		std::vector<rhi::RenderingAttachment> m_colorAttachments;
		rhi::RenderingAttachment m_depthAttachment{};
		bool m_hasDepthAttachment = false;
	};

}
