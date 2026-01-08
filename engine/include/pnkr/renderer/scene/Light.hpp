#pragma once
#include <glm/glm.hpp>
#include <string>
#include "pnkr/renderer/scene/Components.hpp"

namespace pnkr::renderer::scene {

    struct Light
    {
        std::string m_name;
        LightType m_type = LightType::Directional;
        glm::vec3 m_color{1.0f};
        glm::vec3 m_direction{0.0f, 0.0f, -1.0f};
        glm::vec3 m_position{0.0f, 0.0f, 0.0f};
        float m_intensity{1.0f};
        float m_range{0.0f};
        float m_innerConeAngle{0.0f};
        float m_outerConeAngle{0.785398f};
        bool m_debugDraw = false;
    };

}
