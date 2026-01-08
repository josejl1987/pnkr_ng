

#ifndef PNKR_CAMERA_H
#define PNKR_CAMERA_H

#pragma once

#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace pnkr::renderer::scene {

    class Camera {
    public:
        void setViewMatrix(const glm::mat4& view)
        {
            m_view = view;
            m_eye = glm::vec3(glm::affineInverse(view)[3]);
        }

        void setProjMatrix(const glm::mat4& proj)
        {
            m_proj = proj;
        }

        void lookAt(const glm::vec3& eye,
                    const glm::vec3& center,
                    const glm::vec3& up = glm::vec3(0.f, 1.f, 0.f))
        {
            m_view = glm::lookAt(eye, center, up);

            m_eye = eye;
            m_center = center;
            m_up = up;
        }

        void setPerspective(float fovyRad, float aspect, float zNear, float zFar)
        {
            m_proj = glm::perspective(fovyRad, aspect, zNear, zFar);
            m_zNear = zNear;
            m_zFar = zFar;
        }

        void setOrthographic(float left, float right, float bottom, float top, float zNear, float zFar)
        {
            m_proj = glm::ortho(left, right, bottom, top, zNear, zFar);
            m_zNear = zNear;
            m_zFar = zFar;
        }

        const glm::mat4& view() const noexcept { return m_view; }
        const glm::mat4& proj() const noexcept { return m_proj; }

        glm::mat4 viewProj() const noexcept { return m_proj * m_view; }

        const glm::vec3& position() const noexcept { return m_eye; }
        const glm::vec3& target() const noexcept { return m_center; }
        const glm::vec3& up() const noexcept { return m_up; }

        glm::vec3 direction() const noexcept { return glm::normalize(m_center - m_eye); }

        float zNear() const noexcept { return m_zNear; }
        float zFar() const noexcept { return m_zFar; }

    private:
        glm::mat4 m_view{1.0f};
        glm::mat4 m_proj{1.0f};

        glm::vec3 m_up;

        glm::vec3 m_center;

        glm::vec3 m_eye;

        float m_zNear = 0.1f;
        float m_zFar = 1000.0f;
    };

}

#endif
