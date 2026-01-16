#pragma once
#include <glm/glm.hpp>
#include <string>
#include "pnkr/core/ECS.hpp"
#include "pnkr/renderer/SystemMeshes.hpp"
#include "pnkr/renderer/scene/ModelAsset.hpp"
#include <memory>

namespace pnkr::renderer::scene {

    enum class LightType
    {
        Directional,
        Point,
        Spot
    };

    class Relationship {
    public:
        ecs::Entity parent() const noexcept { return m_parent; }
        ecs::Entity firstChild() const noexcept { return m_firstChild; }
        ecs::Entity prevSibling() const noexcept { return m_prevSibling; }
        ecs::Entity nextSibling() const noexcept { return m_nextSibling; }
        ecs::Entity lastChild() const noexcept { return m_lastChild; }
        uint16_t level() const noexcept { return m_level; }

        void setParent(ecs::Entity parent) noexcept { m_parent = parent; }
        void setFirstChild(ecs::Entity child) noexcept { m_firstChild = child; }
        void setPrevSibling(ecs::Entity sibling) noexcept { m_prevSibling = sibling; }
        void setNextSibling(ecs::Entity sibling) noexcept { m_nextSibling = sibling; }
        void setLastChild(ecs::Entity child) noexcept { m_lastChild = child; }
        void setLevel(uint16_t level) noexcept { m_level = level; }

    private:
        ecs::Entity m_parent = ecs::kNullEntity;
        ecs::Entity m_firstChild = ecs::kNullEntity;
        ecs::Entity m_prevSibling = ecs::kNullEntity;
        ecs::Entity m_nextSibling = ecs::kNullEntity;
        ecs::Entity m_lastChild = ecs::kNullEntity;
        uint16_t m_level = 0;
    };

    struct LocalTransform {
        glm::mat4 matrix{1.0f};
    };

    struct WorldTransform {
        glm::mat4 matrix{1.0f};
    };

    struct DirtyTag {};
    struct TransformDirtyTag {};
    struct VisibleTag {};
    struct StaticTag {};
    struct CastShadowTag {};

    struct MeshRenderer {
        int32_t meshID = -1;
        int32_t materialOverride = -1;

        constexpr MeshRenderer() = default;
        constexpr explicit MeshRenderer(int32_t mesh)
            : meshID(mesh) {}
        constexpr MeshRenderer(int32_t mesh, int32_t material)
            : meshID(mesh), materialOverride(material) {}
    };

    struct SystemMeshRenderer {
        SystemMeshType type = SystemMeshType::Cube;
        int32_t materialOverride = -1;
    };

    struct SkinnedMeshRenderer {

        ModelAssetPtr asset;
        int32_t skinIndex = -1;
        uint32_t jointOffset = 0;
        uint32_t jointCount = 0;
        int32_t materialOverride = -1;
    };

    struct LightSource {
        LightType type = LightType::Directional;
        glm::vec3 color{1.0f};
        glm::vec3 direction{0.0f, 0.0f, -1.0f};
        float intensity{1.0f};
        float range{0.0f};
        float innerConeAngle{0.0f};
        float outerConeAngle{0.785398f};
        bool debugDraw = false;
    };

    struct CameraComponent {
        int32_t cameraID = -1;
    };

    struct SkinComponent {
        int32_t skinID = -1;
    };

    struct Name {
        std::string str;
    };
}
