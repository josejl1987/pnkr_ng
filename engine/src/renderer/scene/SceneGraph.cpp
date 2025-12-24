#include "pnkr/renderer/scene/SceneGraph.hpp"

#include <fastgltf/types.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

namespace pnkr::renderer::scene
{
    void SceneGraphDOD::appendChild(uint32_t parent, uint32_t child)
    {
        auto& ph = hierarchy[parent];

        hierarchy[child].parent = (int32_t)parent;
        hierarchy[child].nextSibling = -1;

        if (ph.firstChild < 0)
        {
            ph.firstChild = (int32_t)child;
            ph.lastChild = (int32_t)child;
        }
        else
        {
            hierarchy[(uint32_t)ph.lastChild].nextSibling = (int32_t)child;
            ph.lastChild = (int32_t)child;
        }
    }

    static glm::mat4 getLocalMatrix(const fastgltf::Node& node)
    {
        return std::visit(fastgltf::visitor{
                              [&](const fastgltf::TRS& trs)
                              {
                                  const glm::vec3 t = glm::make_vec3(trs.translation.data());
                                  const auto& q = trs.rotation; // glTF: [x,y,z,w]
                                  const glm::quat r(q[3], q[0], q[1], q[2]); // GLM: (w,x,y,z)
                                  const glm::vec3 s = glm::make_vec3(trs.scale.data());
                                  return glm::translate(glm::mat4(1.0f), t) * glm::toMat4(r) * glm::scale(
                                      glm::mat4(1.0f), s);
                              },
                              [&](const fastgltf::math::fmat4x4& m)
                              {
                                  return glm::make_mat4(m.data());
                              }
                          }, node.transform);
    }


    void SceneGraphDOD::buildFromFastgltf(const fastgltf::Asset& gltf, size_t sceneIndex)
    {
        clear();

        const uint32_t N = (uint32_t)gltf.nodes.size();
        const uint32_t ROOT = 0; // synthetic root
        const uint32_t BASE = 1; // glTF node i -> (BASE + i)

        root = ROOT;

        // Allocate fixed-size arrays
        local.resize(N + 1, glm::mat4(1.0f));
        global.resize(N + 1, glm::mat4(1.0f));
        hierarchy.resize(N + 1);

        meshIndex.assign(N + 1, -1);
        lightIndex.assign(N + 1, -1);
        nameId.assign(N + 1, -1);

        // Root defaults
        hierarchy[ROOT] = {};
        hierarchy[ROOT].parent = -1;
        hierarchy[ROOT].level = 0;

        // 1) Fill per-node components
        for (uint32_t i = 0; i < N; ++i)
        {
            const auto& n = gltf.nodes[i];
            const uint32_t id = BASE + i;

            local[id] = getLocalMatrix(n);

            if (n.meshIndex.has_value())
                meshIndex[id] = (int32_t)n.meshIndex.value();

            if (n.lightIndex.has_value())
                lightIndex[id] = (int32_t)n.lightIndex.value();

            if (!n.name.empty())
            {
                const int32_t sid = (int32_t)names.size();
                names.emplace_back(n.name);
                nameId[id] = sid;
            }
        }

        // 2) Wire parent/children using glTF children lists (stable order)
        for (uint32_t p = 0; p < N; ++p)
        {
            const uint32_t pid = BASE + p;
            const auto& children = gltf.nodes[p].children;
            if (children.empty()) continue;

            hierarchy[pid].firstChild = (int32_t)(BASE + (uint32_t)children.front());

            int32_t prev = -1;
            for (size_t k = 0; k < children.size(); ++k)
            {
                const uint32_t c = (uint32_t)children[k];
                const uint32_t cid = BASE + c;

                hierarchy[cid].parent = (int32_t)pid;
                if (prev != -1) hierarchy[(uint32_t)prev].nextSibling = (int32_t)cid;
                prev = (int32_t)cid;
            }
            hierarchy[pid].lastChild = prev;
        }

        // 3) Attach glTF scene roots under synthetic root
        if (sceneIndex >= gltf.scenes.size()) sceneIndex = 0;
        const auto& sc = gltf.scenes[sceneIndex];

        roots.clear();
        roots.reserve(sc.nodeIndices.size());

        for (size_t r = 0; r < sc.nodeIndices.size(); ++r)
        {
            const uint32_t gltfRoot = (uint32_t)sc.nodeIndices[r];
            const uint32_t rid = BASE + gltfRoot;
            roots.push_back(rid);
            appendChild(ROOT, rid);
        }

        // 4) Build topoOrder + levels via BFS from ROOT
        topoOrder.clear();
        topoOrder.reserve(N + 1);

        std::vector<uint32_t> queue;
        queue.reserve(N + 1);
        queue.push_back(ROOT);
        hierarchy[ROOT].level = 0;

        for (size_t qi = 0; qi < queue.size(); ++qi)
        {
            const uint32_t n = queue[qi];
            topoOrder.push_back(n);

            const uint16_t nextLevel = (uint16_t)(hierarchy[n].level + 1);
            for (int32_t ch = hierarchy[n].firstChild; ch != -1; ch = hierarchy[(uint32_t)ch].nextSibling)
            {
                hierarchy[(uint32_t)ch].level = nextLevel;
                queue.push_back((uint32_t)ch);
            }
        }

        // 5) Compute globals once
        recalculateGlobalTransforms();
    }


    void SceneGraphDOD::recalculateGlobalTransforms()
    {
        if (local.size() != global.size() || hierarchy.size() != local.size()) return;
        if (topoOrder.empty()) return;

        // Ensure first is root if the builder is used
        global[root] = local[root];

        for (size_t k = 1; k < topoOrder.size(); ++k) {
            const uint32_t n = topoOrder[k];
            const int32_t p  = hierarchy[n].parent;
            if (p < 0) { global[n] = local[n]; continue; } // defensive
            global[n] = global[(uint32_t)p] * local[n];
        }
    }
} // namespace pnkr::renderer::scene
