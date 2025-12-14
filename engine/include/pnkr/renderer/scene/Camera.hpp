//
// Created by Jose on 12/14/2025.
//

#ifndef PNKR_CAMERA_H
#define PNKR_CAMERA_H

#pragma once

#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace pnkr::renderer::scene {

    class Camera {
    public:
        void lookAt(const glm::vec3& eye,
                    const glm::vec3& center,
                    const glm::vec3& up = glm::vec3(0.f, 1.f, 0.f))
        {
            m_view = glm::lookAt(eye, center, up);
        }

        // fovyRad in radians
        void setPerspective(float fovyRad, float aspect, float zNear, float zFar)
        {
            m_proj = glm::perspective(fovyRad, aspect, zNear, zFar);
            // Vulkan NDC has inverted Y when using GLM's default conventions
            m_proj[1][1] *= -1.0f;
        }

        const glm::mat4& view() const noexcept { return m_view; }
        const glm::mat4& proj() const noexcept { return m_proj; }

        glm::mat4 viewProj() const noexcept { return m_proj * m_view; }

    private:
        glm::mat4 m_view{1.0f};
        glm::mat4 m_proj{1.0f};
    };

} // namespace pnkr::renderer


#endif //PNKR_CAMERA_H