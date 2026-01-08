#pragma once

#include "pnkr/engine.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/ModelDOD.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/CameraController.hpp"
#include "pnkr/renderer/debug/DebugLayer.hpp"
#include "pnkr/app/Application.hpp"
#include "pnkr/renderer/IndirectRenderer.hpp"

#include <memory>
#include <ImGuizmo.h>

class SceneEditorApp : public pnkr::app::Application {
public:
    SceneEditorApp();
    ~SceneEditorApp() override;

protected:
    void onInit() override;
    void onUpdate(float dt) override;
    void onRecord(const pnkr::renderer::RHIFrameContext& ctx) override;
    void onImGui() override;
    void onEvent(const SDL_Event& event) override;

private:
    std::shared_ptr<pnkr::renderer::scene::ModelDOD> m_model;
    std::unique_ptr<pnkr::renderer::IndirectRenderer> m_indirectRenderer;
    std::unique_ptr<pnkr::renderer::debug::DebugLayer> m_debugLines;

    pnkr::renderer::scene::Camera m_camera;
    pnkr::renderer::scene::CameraController m_cameraController{{-19.261f, 8.465f, -7.317f}};

    int32_t m_selectedNode = -1;
    bool m_sceneDirty = false;
    bool m_drawWireframe = false;
    bool m_drawShadowFrustum = true;

    void tryPick();
};
