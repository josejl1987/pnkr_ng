#include "pnkr/renderer/passes/RenderPassUtils.hpp"
#include "pnkr/renderer/passes/IRenderPass.hpp"
#include "pnkr/renderer/gpu_shared/SceneShared.h"
#include "pnkr/renderer/gpu_shared/OITShared.h"
#include "pnkr/renderer/gpu_shared/PostProcessShared.h"
#include "pnkr/core/logger.hpp"

namespace pnkr::renderer::passes::utils
{

	ShaderLoadResult loadComputeShader(const char* compPath, const char* passName)
	{
		ShaderLoadResult result{};
		result.compute = rhi::Shader::load(rhi::ShaderStage::Compute, compPath);
		result.success = (result.compute != nullptr);

		if (!result.success)
		{
			core::Logger::Render.error("{}: Failed to load compute shader '{}'.", passName, compPath);
		}

		return result;
	}

	ShaderLoadResult loadGraphicsShaders(const char* vertPath, const char* fragPath, const char* passName)
	{
		ShaderLoadResult result{};
		result.vertex = rhi::Shader::load(rhi::ShaderStage::Vertex, vertPath);
		result.fragment = rhi::Shader::load(rhi::ShaderStage::Fragment, fragPath);
		result.success = (result.vertex != nullptr && result.fragment != nullptr);

		if (!result.success)
		{
			core::Logger::Render.error("{}: Failed to load shader(s).", passName);
		}

		return result;
	}

	ScopedGpuMarker::ScopedGpuMarker(rhi::RHICommandList* cmd, const char* name)
		: m_cmd(cmd)
	{
          if (m_cmd != nullptr) {
            m_cmd->pushGPUMarker(name);
          }
        }

	ScopedGpuMarker::~ScopedGpuMarker()
	{
          if (m_cmd != nullptr) {
            m_cmd->popGPUMarker();
          }
        }

	ScopedDebugLabel::ScopedDebugLabel(rhi::RHICommandList* cmd, const char* label)
		: m_cmd(cmd)
	{
          if (m_cmd != nullptr) {
            m_cmd->beginDebugLabel(label);
          }
        }

	ScopedDebugLabel::ScopedDebugLabel(rhi::RHICommandList* cmd, const char* label,
		float r, float g, float b, float a)
		: m_cmd(cmd)
	{
          if (m_cmd != nullptr) {
            m_cmd->beginDebugLabel(label, r, g, b, a);
          }
        }

	ScopedDebugLabel::~ScopedDebugLabel()
	{
          if (m_cmd != nullptr) {
            m_cmd->endDebugLabel();
          }
        }

	ScopedPassMarkers::ScopedPassMarkers(rhi::RHICommandList* cmd,
		const char* passName,
		float r, float g, float b, float a)
		: m_gpuMarker(cmd, passName)
		, m_debugLabel(cmd, passName, r, g, b, a)
	{
	}

	ScopedPassMarkers::~ScopedPassMarkers() = default;

	template<typename HandleT>
	void createTextureAttachment(
		RHIRenderer* renderer,
		HandleT& handle,
		uint32_t width,
		uint32_t height,
		rhi::Format format,
		rhi::TextureUsageFlags usage,
		const char* debugName,
		uint32_t samples)
	{
		rhi::TextureDescriptor desc{};
                desc.extent = {.width = width, .height = height, .depth = 1};
                desc.format = format;
		desc.usage = usage;
		desc.sampleCount = samples;
		desc.debugName = debugName;
		recreateTextureIfNeeded(renderer, handle, desc, debugName);
	}

	template<typename HandleT>
	void recreateTextureIfNeeded(
		RHIRenderer* renderer,
		HandleT& handle,
		const rhi::TextureDescriptor& newDesc,
		const char* debugName)
	{
		bool needsRecreation = !isHandleValid(handle);

		if (!needsRecreation)
		{
			auto* existingTexture = renderer->getTexture(toRawHandle(handle));
			if (!existingTexture)
			{
				needsRecreation = true;
			}
			else
			{
				const auto& currentExtent = existingTexture->extent();
				needsRecreation = (currentExtent.width != newDesc.extent.width ||
					currentExtent.height != newDesc.extent.height ||
					currentExtent.depth != newDesc.extent.depth ||
					existingTexture->sampleCount() != newDesc.sampleCount ||
					existingTexture->format() != newDesc.format);
			}
		}

		if (needsRecreation)
		{
			destroyHandleIfNeeded(renderer, handle);
			assignHandle(handle, renderer->createTexture(debugName, newDesc));
		}
	}

	template<typename HandleT>
	void recreateBufferIfNeeded(
		RHIRenderer* renderer,
		HandleT& handle,
		const rhi::BufferDescriptor& newDesc,
		const char* debugName)
	{
		bool needsRecreation = !isHandleValid(handle);

		if (!needsRecreation)
		{
			auto* existingBuffer = renderer->getBuffer(toRawHandle(handle));
			if (!existingBuffer)
			{
				needsRecreation = true;
			}
			else
			{
				needsRecreation = (existingBuffer->size() != newDesc.size);
			}
		}

		if (needsRecreation)
		{
			destroyHandleIfNeeded(renderer, handle);
			assignHandle(handle, renderer->createBuffer(debugName, newDesc));
		}
	}

	template void createTextureAttachment(RHIRenderer*, TexturePtr&, uint32_t, uint32_t, rhi::Format, rhi::TextureUsageFlags, const char*, uint32_t);
	template void createTextureAttachment(RHIRenderer*, TextureHandle&, uint32_t, uint32_t, rhi::Format, rhi::TextureUsageFlags, const char*, uint32_t);

	template void recreateTextureIfNeeded(RHIRenderer*, TexturePtr&, const rhi::TextureDescriptor&, const char*);
	template void recreateTextureIfNeeded(RHIRenderer*, TextureHandle&, const rhi::TextureDescriptor&, const char*);

	template void recreateBufferIfNeeded(RHIRenderer*, BufferPtr&, const rhi::BufferDescriptor&, const char*);
	template void recreateBufferIfNeeded(RHIRenderer*, BufferHandle&, const rhi::BufferDescriptor&, const char*);

	void populateBaseIndirectPushConstants(
		const RenderPassContext& ctx,
		gpu::IndirectPushConstants& pc,
		RHIRenderer* renderer)
	{
		pc.cameraData = ctx.sceneDataAddr;
		pc.instances = ctx.instanceXformAddr;
		pc.materials = ctx.materialAddr;
		pc.lights = ctx.lightAddr;
		pc.lightCount = ctx.lightCount;
		pc.shadowData = ctx.shadowDataAddr;
		pc.envMapData = ctx.environmentAddr;

		const bool hasSkinning = ctx.frameBuffers.jointMatricesBuffer.isValid();
		const bool hasMorphing = (ctx.model->morphVertexBuffer() != INVALID_BUFFER_HANDLE &&
		                          ctx.frameBuffers.morphStateBuffer.isValid());

		pc.vertices = (hasSkinning || hasMorphing)
			? renderer->getBuffer(ctx.frameBuffers.skinnedVertexBuffer)->getDeviceAddress()
			: renderer->getBuffer(ctx.model->vertexBuffer())->getDeviceAddress();
	}

	template<typename PushConstantsT>
	void executeIndirectDraw(
		RHIRenderer* renderer,
		rhi::RHICommandList* cmd,
		const IndirectDrawCall& call,
		const PushConstantsT& pc,
		core::Flags<rhi::ShaderStage> stages)
	{
          if (!call.indirectBuffer.buffer.isValid() || call.drawCount == 0) {
            return;
          }

                cmd->bindPipeline(renderer->getPipeline(call.pipeline));
		cmd->pushConstants(stages, pc);
		cmd->drawIndexedIndirect(
			renderer->getBuffer(call.indirectBuffer.buffer),
			call.indirectBuffer.offset + 16,
			call.drawCount,
			call.commandSize);
	}

	template void executeIndirectDraw(RHIRenderer*, rhi::RHICommandList*, const IndirectDrawCall&, const gpu::IndirectPushConstants&, core::Flags<rhi::ShaderStage>);
	template void executeIndirectDraw(RHIRenderer*, rhi::RHICommandList*, const IndirectDrawCall&, const gpu::OITPushConstants&, core::Flags<rhi::ShaderStage>);

	template<typename PushConstantsT>
	void dispatchCompute(
		RHIRenderer* renderer,
		rhi::RHICommandList* cmd,
		PipelineHandle pipeline,
		const PushConstantsT& pc,
		uint32_t width,
		uint32_t height,
		uint32_t groupSizeXY)
	{
		cmd->bindPipeline(renderer->getPipeline(pipeline));
		cmd->pushConstants(rhi::ShaderStage::Compute, pc);
		cmd->dispatch(
			(width + groupSizeXY - 1) / groupSizeXY,
			(height + groupSizeXY - 1) / groupSizeXY,
			1);
	}

	template void dispatchCompute(RHIRenderer*, rhi::RHICommandList*, PipelineHandle, const gpu::SSAOParams&, uint32_t, uint32_t, uint32_t);
	template void dispatchCompute(RHIRenderer*, rhi::RHICommandList*, PipelineHandle, const gpu::BlurParams&, uint32_t, uint32_t, uint32_t);
	template void dispatchCompute(RHIRenderer*, rhi::RHICommandList*, PipelineHandle, const gpu::ResolveConstants&, uint32_t, uint32_t, uint32_t);

	rhi::RHIPipelineBuilder createGraphicsPipelineBuilder(
		const GraphicsPassState& state,
		const std::shared_ptr<rhi::Shader>& vertShader,
		const std::shared_ptr<rhi::Shader>& fragShader)
	{
		rhi::RHIPipelineBuilder builder;
                builder.setShaders(vertShader.get(), fragShader.get())
                    .setTopology(rhi::PrimitiveTopology::TriangleList)
                    .setPolygonMode(rhi::PolygonMode::Fill)
                    .setCullMode(state.cullMode,
                                 state.frontFaceCounterClockwise, false)
                    .setMultisampling(state.msaaSamples, state.msaaSamples > 1,
                                      0.25F)
                    .enableDepthTest(state.enableDepthWrite,
                                     state.depthCompareOp)
                    .setDepthFormat(state.depthFormat)
                    .setColorFormat(state.colorFormat);

                if (state.enableBlend)
		{
			builder.setAlphaBlend();
		}
		else
		{
			builder.setNoBlend();
		}

		return builder;
	}

	RenderingInfoBuilder& RenderingInfoBuilder::setRenderArea(uint32_t width, uint32_t height)
	{
          m_info.renderArea = {
              .x = 0, .y = 0, .width = width, .height = height};
          return *this;
	}

	RenderingInfoBuilder& RenderingInfoBuilder::addColorAttachment(
		rhi::RHITexture* texture,
		rhi::LoadOp loadOp,
		rhi::StoreOp storeOp,
		rhi::RHITexture* resolveTexture)
	{
		rhi::RenderingAttachment att{};
		att.texture = texture;
		att.resolveTexture = resolveTexture;
		att.loadOp = loadOp;
		att.storeOp = storeOp;
                att.clearValue.color.float32[0] = 0.0F;
                m_colorAttachments.push_back(att);
		return *this;
	}

	RenderingInfoBuilder& RenderingInfoBuilder::setDepthAttachment(
		rhi::RHITexture* texture,
		rhi::LoadOp loadOp,
		rhi::StoreOp storeOp,
		rhi::RHITexture* resolveTexture)
	{
		m_depthAttachment.texture = texture;
		m_depthAttachment.resolveTexture = resolveTexture;
		m_depthAttachment.loadOp = loadOp;
		m_depthAttachment.storeOp = storeOp;
                m_depthAttachment.clearValue.depthStencil.depth = 1.0F;
                m_depthAttachment.clearValue.isDepthStencil = true;
                m_hasDepthAttachment = true;
		return *this;
	}

	const rhi::RenderingInfo& RenderingInfoBuilder::get()
	{
		m_info.colorAttachments = m_colorAttachments;
		if (m_hasDepthAttachment)
		{
			m_info.depthAttachment = &m_depthAttachment;
		}
		return m_info;
	}

}
