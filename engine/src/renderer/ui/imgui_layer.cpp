#include "pnkr/ui/imgui_layer.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <cpptrace/cpptrace.hpp>

#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/platform/window.hpp"
#include "pnkr/rhi/rhi_device.hpp"
#include "pnkr/rhi/rhi_imgui.hpp"
#include "pnkr/rhi/rhi_texture.hpp"
#include "pnkr/rhi/rhi_sampler.hpp"

namespace pnkr::ui {

void ImGuiLayer::init(renderer::RHIRenderer* renderer, platform::Window* window) {
    m_renderer = renderer;

    m_backend = m_renderer->device()->createImGuiRenderer();
    if (!m_backend) {
        throw cpptrace::runtime_error("[ImGuiLayer] Failed to create RHI ImGui backend!");
    }

    m_uiSampler = m_renderer->device()->createSampler(
        renderer::rhi::Filter::Linear,
        renderer::rhi::Filter::Linear,
        renderer::rhi::SamplerAddressMode::ClampToEdge
    );

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForVulkan(window->get());

    m_backend->init(
        window->get(),
        m_renderer->getDrawColorFormat(),
        m_renderer->getDrawDepthFormat(),
        m_renderer->getSwapchain()->framesInFlight()
    );

    m_initialized = true;
}

void ImGuiLayer::shutdown() {
  if ((m_renderer != nullptr) && m_initialized) {
    m_renderer->device()->waitIdle();

    for (const auto &entry : m_textureCache) {
      m_backend->removeTexture(reinterpret_cast<void *>(entry.second.id));
    }
    m_textureCache.clear();

    m_backend->shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    m_uiSampler.reset();
    m_backend.reset();

    m_renderer = nullptr;
    m_initialized = false;
  }
}

void ImGuiLayer::handleEvent(const SDL_Event& event) {
    ImGui_ImplSDL3_ProcessEvent(&event);
}

void ImGuiLayer::beginFrame() {
    m_backend->beginFrame(m_renderer->getFrameIndex());
    garbageCollectTextureCache();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::endFrame() {
    ImGui::Render();
}

void ImGuiLayer::render(renderer::rhi::RHICommandList* cmd) {
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData != nullptr) {
      m_backend->renderDrawData(cmd, drawData);
    }
}

ImTextureID ImGuiLayer::getTextureID(TextureHandle handle) {
  if (handle == INVALID_TEXTURE_HANDLE) {
    return 0;
  }

    auto it = m_textureCache.find(handle.index);
    if (it != m_textureCache.end()) {
        auto* tex = m_renderer->getTexture(handle);
        if (tex == nullptr) {
          releaseTexture(handle);
          return 0;
        }

        auto viewId = reinterpret_cast<uint64_t>(tex->nativeView());
        if (it->second.view == viewId) {
            return it->second.id;
        }

        releaseTexture(handle);
        it = m_textureCache.end();
    }

    auto* tex = m_renderer->getTexture(handle);
    if (tex == nullptr) {
      return 0;
    }

    void* imageView = tex->nativeView();
    void* sampler = m_uiSampler->nativeHandle();

    void* ds = m_backend->registerTexture(imageView, sampler);

    CachedTexture cached{};
    cached.id = reinterpret_cast<ImTextureID>(ds);
    cached.view = reinterpret_cast<uint64_t>(imageView);
    m_textureCache[handle.index] = cached;
    return cached.id;
}

void ImGuiLayer::releaseTexture(TextureHandle handle)
{
  if (!m_initialized || (m_renderer == nullptr) ||
      handle == INVALID_TEXTURE_HANDLE) {
    return;
  }

    auto it = m_textureCache.find(handle.index);
    if (it == m_textureCache.end()) {
      return;
    }

    m_backend->removeTexture(reinterpret_cast<void*>(it->second.id));
    m_textureCache.erase(it);
}

void ImGuiLayer::drawGPUProfiler()
{
  if (!m_initialized || (m_renderer == nullptr)) {
    return;
  }

    auto* profiler = m_renderer->device()->gpuProfiler();
    auto extent = m_renderer->getSwapchain()->extent();
    m_gpuProfilerUI.setTextureResolver([this](TextureHandle handle) {
        return getTextureID(handle);
    });
    m_gpuProfilerUI.setBindlessManager(m_renderer->device()->getBindlessManager());
    m_gpuProfilerUI.draw("GPU Profiler", profiler, m_renderer->getFrameIndex(),
                         extent.width, extent.height);
}

void ImGuiLayer::garbageCollectTextureCache()
{
  if (!m_initialized || (m_renderer == nullptr)) {
    return;
  }

    for (auto it = m_textureCache.begin(); it != m_textureCache.end(); )
    {
        TextureHandle handle{it->first, 0};
        auto* tex = m_renderer->getTexture(handle);
        bool drop = false;
        if (tex == nullptr) {
          drop = true;
        } else {
          auto viewId = reinterpret_cast<uint64_t>(tex->nativeView());
          drop = (viewId != it->second.view);
        }

        if (drop)
        {
            m_backend->removeTexture(reinterpret_cast<void*>(it->second.id));
            it = m_textureCache.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

}
