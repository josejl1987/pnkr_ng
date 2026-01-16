#include "pnkr/renderer/scene/ModelDOD.hpp"

#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/SceneGraph.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/rhi/rhi_buffer.hpp"

#include <glm/common.hpp>
#include <limits>

namespace pnkr::renderer::scene
{
    uint32_t ModelDOD::appendPrimitiveMeshData(const geometry::MeshData& primitiveData,
                                               uint32_t materialIndex,
                                               const std::string& name)
    {
        uint32_t meshId = m_assets.appendPrimitiveMeshData(primitiveData, materialIndex, name);
        m_state.morphStates().push_back({});
        return meshId;
    }

    void ModelDOD::uploadUnifiedBuffers(RHIRenderer& renderer)
    {
        m_assets.uploadUnifiedBuffers(renderer);
    }

    uint32_t ModelDOD::addPrimitiveToScene(RHIRenderer& renderer,
                                           const geometry::MeshData& primitiveData,
                                           uint32_t materialIndex,
                                           const glm::mat4& transform,
                                           const std::string& name)
    {
        if (primitiveData.vertices.empty() || primitiveData.indices.empty()) {
            return 0;
        }

        const uint32_t meshId = appendPrimitiveMeshData(primitiveData, materialIndex, name);

        renderer.device()->waitIdle();

        auto& sceneGraph = *m_scene;
        ecs::Entity parent = sceneGraph.root();
        ecs::Entity nodeId = sceneGraph.createNode(parent);

        sceneGraph.registry().get<LocalTransform>(nodeId).matrix = transform;
        sceneGraph.registry().emplace<MeshRenderer>(nodeId, static_cast<int32_t>(meshId));
        auto &lb = sceneGraph.registry().emplace<LocalBounds>(nodeId);
        if (meshId < m_assets.meshBounds().size()) {
            lb.aabb = m_assets.meshBounds()[meshId];
        }
        sceneGraph.registry().emplace<WorldBounds>(nodeId);
        sceneGraph.registry().emplace<Visibility>(nodeId);
        sceneGraph.registry().emplace<BoundsDirtyTag>(nodeId);

        if (!name.empty()) {
            sceneGraph.registry().emplace<Name>(nodeId, name);
        }

        sceneGraph.onHierarchyChanged();
        return nodeId;
    }

    void ModelDOD::addPrimitiveMeshes(RHIRenderer& renderer,
                                      const std::vector<geometry::MeshData>& primitives,
                                      const std::vector<std::string>& names,
                                      uint32_t materialIndex)
    {
        if (primitives.empty()) {
            return;
        }

        size_t appended = 0;
        for (size_t i = 0; i < primitives.size(); ++i) {
            const auto& data = primitives[i];
            if (data.vertices.empty() || data.indices.empty()) {
                continue;
            }
            const std::string name = (i < names.size()) ? names[i] : "Primitive";
            appendPrimitiveMeshData(data, materialIndex, name);
            ++appended;
        }

        if (appended == 0) {
            return;
        }

        renderer.device()->waitIdle();
    }

    void ModelDOD::dropCpuGeometry()
    {
        m_assets.dropCpuGeometry();
    }

    int32_t ModelDOD::addLight(const Light& light, const glm::mat4& transform, const std::string& name)
    {
        auto& sceneGraph = *m_scene;
        ecs::Entity parent = sceneGraph.root();

        if (sceneGraph.root() == ecs::kNullEntity) {
            return -1;
        }

        ecs::Entity nodeId = sceneGraph.createNode(parent);

        const auto lightIndex = static_cast<int32_t>(
            sceneGraph.registry().getPool<LightSource>().size());
        auto& ls = sceneGraph.registry().emplace<LightSource>(nodeId);
        ls.type = light.m_type;
        ls.color = light.m_color;
        ls.direction = light.m_direction;
        ls.intensity = light.m_intensity;
        ls.range = light.m_range;
        ls.innerConeAngle = light.m_innerConeAngle;
        ls.outerConeAngle = light.m_outerConeAngle;
        ls.debugDraw = light.m_debugDraw;

        sceneGraph.registry().get<LocalTransform>(nodeId).matrix = transform;

        if (!name.empty()) {
            sceneGraph.registry().emplace<Name>(nodeId, name);
        } else if (!light.m_name.empty()) {
            sceneGraph.registry().emplace<Name>(nodeId, light.m_name);
        }

        sceneGraph.onHierarchyChanged();

        return lightIndex;
    }

    void ModelDOD::removeLight(int32_t lightIndex)
    {
        const auto& lightPool = scene().registry().getPool<LightSource>();
        if (lightIndex < 0 ||
            static_cast<size_t>(lightIndex) >= lightPool.size()) {
            return;
        }

        auto& sceneGraph = *m_scene;
        auto lightView = sceneGraph.registry().view<LightSource>();

        ecs::Entity toDestroy = ecs::kNullEntity;
        uint32_t currentIdx = 0;

        lightView.each([&](ecs::Entity e, LightSource& ) {
            if (currentIdx == util::u32(lightIndex)) {
                toDestroy = e;
            }
            currentIdx++;
        });

        if (toDestroy != ecs::kNullEntity) {
            sceneGraph.destroyNode(toDestroy);
        }
    }
}