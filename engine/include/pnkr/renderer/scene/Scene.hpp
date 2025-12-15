#pragma once
#include <vector>
#include <vulkan/vulkan.hpp>

#include "pnkr/core/Handle.h"
#include "pnkr/renderer/pipeline/Pipeline.h"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/CameraController.hpp"
#include "pnkr/renderer/scene/transform.hpp"
#include "pnkr/platform/Input.hpp"

namespace pnkr::renderer {
    class Renderer;
}

namespace pnkr::renderer::scene {

    struct Renderable {
        Transform xform;
        MeshHandle mesh{};
        PipelineHandle pipe{};
    };

    class Scene {
    public:
        void onResize(vk::Extent2D ext);
        void update(float dt, vk::Extent2D ext, const platform::Input& input);
        void record(const RenderFrameContext& ctx, const Renderer& r) const;

        Camera& camera() { return m_camera; }
        const Camera& camera() const { return m_camera; }

        CameraController& cameraController() { return m_cameraController; }
        const CameraController& cameraController() const { return m_cameraController; }

        std::vector<Renderable>& objects() { return m_objects; }
        const std::vector<Renderable>& objects() const { return m_objects; }

    private:
        Camera m_camera;
        CameraController m_cameraController;
        std::vector<Renderable> m_objects;
        vk::Extent2D m_lastExtent{0, 0};
    };

}
