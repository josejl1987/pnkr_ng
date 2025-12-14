#include "pnkr/renderer/scene/Scene.hpp"
#include "pnkr/renderer/renderer.hpp"
#include "pnkr/renderer/vulkan/PushConstants.h"
#include <glm/gtc/constants.hpp>
#include <glm/gtx/quaternion.hpp>

namespace pnkr::renderer::scene {

    void Scene::onResize(vk::Extent2D ext) {
        if (ext.width == 0 || ext.height == 0) return;
        if (ext.width == m_lastExtent.width && ext.height == m_lastExtent.height) return;

        m_lastExtent = ext;
        float aspect = static_cast<float>(ext.width) / static_cast<float>(ext.height);
        m_camera.setPerspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
    }

    void Scene::update(float dt, vk::Extent2D ext, const platform::Input& input) {
        onResize(ext);

        // Update camera controller
        m_cameraController.update(input, dt);
        m_cameraController.applyToCamera(m_camera);

        // Example animation: spin first object
        if (!m_objects.empty()) {
            auto& rot = m_objects[0].xform.m_rotation;
            rot = glm::normalize(glm::rotate(rot, dt, glm::vec3(0.0f, 1.0f, 0.0f)));
        }
    }

    void Scene::record(const RenderFrameContext& ctx, const Renderer& r) const {
        for (const auto& obj : m_objects) {
            PushConstants pc{};
            pc.m_model = obj.xform.mat4();
            pc.m_viewProj = m_camera.viewProj();

            r.pushConstants(ctx.m_cmd, obj.pipe, vk::ShaderStageFlagBits::eVertex, pc);
            r.bindPipeline(ctx.m_cmd, obj.pipe);
            r.bindMesh(ctx.m_cmd, obj.mesh);
            r.drawMesh(ctx.m_cmd, obj.mesh);
        }
    }

} // namespace pnkr::renderer::scene
