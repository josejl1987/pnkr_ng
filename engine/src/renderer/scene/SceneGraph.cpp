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
        hierarchy.assign(N + 1, HierarchyDOD{});

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

            for (auto childIdx : children)
            {
                const uint32_t cid = BASE + (uint32_t)childIdx;
                appendChild(pid, cid);
            }
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
        recalculateGlobalTransformsFull();
        initDirtyTracking();
    }


    void SceneGraphDOD::recalculateGlobalTransformsFull()
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

    void SceneGraphDOD::initDirtyTracking()
    {
        // Determine maxLevel (already computed in buildFromFastgltf() usually).
        maxLevel = 0;
        for (const auto& h : hierarchy) maxLevel = std::max<uint16_t>(maxLevel, h.level);

        changedAtThisFrame.clear();
        changedAtThisFrame.resize((size_t)maxLevel + 1);

        dirtyFlag.assign(hierarchy.size(), 0);
        dirtyNodes.clear();
        dirtyNodes.reserve(hierarchy.size());
        maxDirtyLevelThisFrame = 0;
    }

    void SceneGraphDOD::beginFrameDirty()
    {
        // Reset flags from last frame cheaply (no O(N) clear).
        for (uint32_t n : dirtyNodes) dirtyFlag[n] = 0;
        dirtyNodes.clear();

        // Clear per-level lists scaling with what changed.
        const uint16_t lim = std::min<uint16_t>(
            maxDirtyLevelThisFrame,
            changedAtThisFrame.empty() ? (uint16_t)0 : (uint16_t)(changedAtThisFrame.size() - 1));
        
        for (uint16_t i = 0; i <= lim && i < changedAtThisFrame.size(); ++i)
            changedAtThisFrame[i].clear();

        maxDirtyLevelThisFrame = 0;
    }

    void SceneGraphDOD::markAsChanged(uint32_t node)
    {
        if (node >= hierarchy.size()) return;
        if (changedAtThisFrame.empty()) initDirtyTracking();

        // Iterative DFS: mark node + descendants.
        std::vector<uint32_t> stack;
        stack.push_back(node);

        while (!stack.empty()) {
            const uint32_t n = stack.back();
            stack.pop_back();

            if (n >= hierarchy.size()) continue;
            if (dirtyFlag[n]) continue;

            dirtyFlag[n] = 1;
            dirtyNodes.push_back(n);

            const uint16_t lvl = hierarchy[n].level;
            if ((size_t)lvl >= changedAtThisFrame.size()) {
                // Defensive: if level is out-of-range, expand.
                changedAtThisFrame.resize((size_t)lvl + 1);
            }
            changedAtThisFrame[lvl].push_back(n);
            maxDirtyLevelThisFrame = std::max(maxDirtyLevelThisFrame, lvl);

            for (int32_t ch = hierarchy[n].firstChild; ch != -1; ch = hierarchy[(uint32_t)ch].nextSibling) {
                stack.push_back((uint32_t)ch);
            }
        }
    }

    void SceneGraphDOD::recalculateGlobalTransformsDirty()
    {
        if (changedAtThisFrame.empty()) return;
        if (local.size() != global.size() || hierarchy.size() != local.size()) return;

        // Always ensure root is correct (future-proof if root becomes editable).
        global[root] = local[root];

        // Level 0: roots (synthetic root is usually the only one at level 0).
        if (!changedAtThisFrame[0].empty()) {
            for (uint32_t c : changedAtThisFrame[0]) {
                global[c] = local[c];
            }
            changedAtThisFrame[0].clear();
        }

        // IMPORTANT: do NOT break on first empty level (gaps are legal).
        for (uint16_t lvl = 1; lvl <= maxDirtyLevelThisFrame && (size_t)lvl < changedAtThisFrame.size(); ++lvl) {
            auto& list = changedAtThisFrame[lvl];
            if (list.empty()) continue;

            for (uint32_t c : list) {
                const int32_t p = hierarchy[c].parent;
                if (p < 0) { global[c] = local[c]; continue; }
                global[c] = global[(uint32_t)p] * local[c];
            }
            list.clear();
        }

        // Clear dirty flags now (so multiple recalc calls in same frame still work).
        for (uint32_t n : dirtyNodes) dirtyFlag[n] = 0;
        dirtyNodes.clear();
        maxDirtyLevelThisFrame = 0;
    }

    void SceneGraphDOD::onHierarchyChanged()
    {
        // Recompute levels + topoOrder from current hierarchy (BFS from root).
        topoOrder.clear();
        topoOrder.reserve(hierarchy.size());

        std::vector<uint32_t> queue;
        queue.reserve(hierarchy.size());
        queue.push_back(root);
        hierarchy[root].level = 0;

        maxLevel = 0;

        for (size_t qi = 0; qi < queue.size(); ++qi)
        {
            const uint32_t n = queue[qi];
            topoOrder.push_back(n);

            const uint16_t nextLevel = (uint16_t)(hierarchy[n].level + 1);
            for (int32_t ch = hierarchy[n].firstChild; ch != -1; ch = hierarchy[(uint32_t)ch].nextSibling)
            {
                hierarchy[(uint32_t)ch].level = nextLevel;
                maxLevel = std::max<uint16_t>(maxLevel, nextLevel);
                queue.push_back((uint32_t)ch);
            }
        }

        recalculateGlobalTransformsFull();
        initDirtyTracking();
        hierarchyDirty = false;
    }
} // namespace pnkr::renderer::scene
