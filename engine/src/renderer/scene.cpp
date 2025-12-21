#include "pnkr/renderer/scene/Scene.hpp"
#include "pnkr/core/common.hpp"
#include <glm/gtc/constants.hpp>
#include <glm/gtx/quaternion.hpp>

using namespace pnkr::util;

namespace pnkr::renderer::scene
{
    void Scene::onResize(vk::Extent2D ext)
    {
        if (ext.width == 0 || ext.height == 0)
        {
            return;
        }
        if (ext.width == m_lastExtent.width && ext.height == m_lastExtent.height)
        {
            return;
        }

        m_lastExtent = ext;
        float aspect = toFloat(ext.width) / toFloat(ext.height);
        m_camera.setPerspective(glm::radians(60.0F), aspect, 0.1F, 100.0F);
    }

    void Scene::update(float dt, vk::Extent2D ext, const platform::Input& input)
    {
        onResize(ext);

        // Update camera controller
        m_cameraController.update(input, dt);
        m_cameraController.applyToCamera(m_camera);

        // Example animation: spin first object
        if (!m_objects.empty())
        {
            auto& rot = m_objects[0].xform.m_rotation;
            rot = glm::normalize(glm::rotate(rot, dt, glm::vec3(0.0F, 1.0F, 0.0F)));
        }
    }

    void Scene::record(const RenderFrameContext& ctx, const Renderer& r) const
    {
        (void)ctx;
        (void)r;
    }
} // namespace pnkr::renderer::scene
