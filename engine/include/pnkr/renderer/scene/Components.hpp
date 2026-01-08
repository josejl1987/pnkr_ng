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
        ecs::Entity parent() const noexcept { return parent_; }
        ecs::Entity firstChild() const noexcept { return firstChild_; }
        ecs::Entity prevSibling() const noexcept { return prevSibling_; }
        ecs::Entity nextSibling() const noexcept { return nextSibling_; }
        ecs::Entity lastChild() const noexcept { return lastChild_; }
        uint16_t level() const noexcept { return level_; }

        void setParent(ecs::Entity parent) noexcept { parent_ = parent; }
        void setFirstChild(ecs::Entity child) noexcept { firstChild_ = child; }
        void setPrevSibling(ecs::Entity sibling) noexcept { prevSibling_ = sibling; }
        void setNextSibling(ecs::Entity sibling) noexcept { nextSibling_ = sibling; }
        void setLastChild(ecs::Entity child) noexcept { lastChild_ = child; }
        void setLevel(uint16_t level) noexcept { level_ = level; }

    private:
        ecs::Entity parent_ = ecs::NULL_ENTITY;
        ecs::Entity firstChild_ = ecs::NULL_ENTITY;
        ecs::Entity prevSibling_ = ecs::NULL_ENTITY;
        ecs::Entity nextSibling_ = ecs::NULL_ENTITY;
        ecs::Entity lastChild_ = ecs::NULL_ENTITY;
        uint16_t level_ = 0;
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
