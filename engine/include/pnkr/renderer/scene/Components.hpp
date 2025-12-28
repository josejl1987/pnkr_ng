#pragma once
#include <glm/glm.hpp>
#include <string>
#include "pnkr/core/ECS.hpp"

namespace pnkr::renderer::scene {

    enum class LightType
    {
        Directional = 0,
        Point = 1,
        Spot = 2
    };

    // 1. Hierarchy Component
    // Maintains the tree structure
    struct Relationship {
        ecs::Entity parent = ecs::NULL_ENTITY;
        ecs::Entity firstChild = ecs::NULL_ENTITY;
        ecs::Entity prevSibling = ecs::NULL_ENTITY;
        ecs::Entity nextSibling = ecs::NULL_ENTITY;
        ecs::Entity lastChild = ecs::NULL_ENTITY;
        uint16_t level = 0;
    };

    // 2. Transform Components
    struct LocalTransform {
        glm::mat4 matrix{1.0f};
    };

    struct WorldTransform {
        glm::mat4 matrix{1.0f};
    };

    // 3. Logic Tags
    struct DirtyTag {};          // Legacy
    struct TransformDirtyTag {}; // Marker for nodes needing world transform update
    struct VisibleTag {};        // Entity should be rendered
    struct StaticTag {};         // Hint that transform won't change
    struct CastShadowTag {};     // Entity casts shadows

    // 4. Content References (Indices into ModelDOD arrays)
    struct MeshRenderer {
        int32_t meshID = -1;     // Index into ModelDOD::m_meshes
    };

    struct LightSource {
        LightType type = LightType::Directional;
        glm::vec3 color{1.0f};
        glm::vec3 direction{0.0f, 0.0f, -1.0f};
        float intensity{1.0f};
        float range{0.0f}; // 0 = infinite
        float innerConeAngle{0.0f}; // Radians
        float outerConeAngle{0.785398f}; // Radians (45 degrees)
        bool debugDraw = false;
    };

    struct CameraComponent {
        int32_t cameraID = -1;   // Index into ModelDOD::m_cameras
    };

    struct SkinComponent {
        int32_t skinID = -1;     // Index into ModelDOD::m_skins
    };
    
    struct Name {
        std::string str;
    };
}
