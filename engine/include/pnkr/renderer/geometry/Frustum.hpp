#pragma once

#include <glm/glm.hpp>
#include <array>
#include <limits>
#include "pnkr/renderer/scene/Bounds.hpp"

namespace pnkr::renderer::geometry {

    struct Frustum {
        glm::vec4 planes[6];
        glm::vec4 corners[8];
    };

    inline void getFrustumPlanes(const glm::mat4& vp, glm::vec4* planes) {
        const glm::mat4 t = glm::transpose(vp);
        planes[0] = t[3] + t[0];
        planes[1] = t[3] - t[0];
        planes[2] = t[3] + t[1];
        planes[3] = t[3] - t[1];
        planes[4] = t[3] + t[2];
        planes[5] = t[3] - t[2];

        for (int i = 0; i < 6; i++) {
            planes[i] /= glm::length(glm::vec3(planes[i]));
        }
    }

    inline void getFrustumCorners(const glm::mat4& vp, glm::vec4* points) {
        const glm::vec4 ndcCorners[8] = {
            {-1, -1, 0, 1}, { 1, -1, 0, 1},
            { 1,  1, 0, 1}, {-1,  1, 0, 1},
            {-1, -1, 1, 1}, { 1, -1, 1, 1},
            { 1,  1, 1, 1}, {-1,  1, 1, 1}
        };

        const glm::mat4 invVP = glm::inverse(vp);
        for (int i = 0; i < 8; i++) {
            const glm::vec4 q = invVP * ndcCorners[i];
            points[i] = q / q.w;
        }
    }

    inline Frustum createFrustum(const glm::mat4& vp) {
        Frustum f;
        getFrustumPlanes(vp, f.planes);
        getFrustumCorners(vp, f.corners);
        return f;
    }

    inline scene::BoundingBox transformBox(const scene::BoundingBox& box, const glm::mat4& m) {
        glm::vec3 min = box.m_min;
        glm::vec3 max = box.m_max;

        glm::vec3 corners[8] = {
            {min.x, min.y, min.z}, {max.x, min.y, min.z},
            {min.x, max.y, min.z}, {max.x, max.y, min.z},
            {min.x, min.y, max.z}, {max.x, min.y, max.z},
            {min.x, max.y, max.z}, {max.x, max.y, max.z}
        };

        scene::BoundingBox out;
        out.m_min = glm::vec3(std::numeric_limits<float>::max());
        out.m_max = glm::vec3(std::numeric_limits<float>::lowest());

        for(int i=0; i<8; ++i) {
            glm::vec3 p = glm::vec3(m * glm::vec4(corners[i], 1.0f));
            out.m_min = glm::min(out.m_min, p);
            out.m_max = glm::max(out.m_max, p);
        }
        return out;
    }

    inline bool isBoxInFrustum(const Frustum& f, const scene::BoundingBox& b) {

        for (int i = 0; i < 6; i++) {
            int out = 0;
            out += (glm::dot(f.planes[i], glm::vec4(b.m_min.x, b.m_min.y, b.m_min.z, 1.0f)) < 0.0) ? 1 : 0;
            out += (glm::dot(f.planes[i], glm::vec4(b.m_max.x, b.m_min.y, b.m_min.z, 1.0f)) < 0.0) ? 1 : 0;
            out += (glm::dot(f.planes[i], glm::vec4(b.m_min.x, b.m_max.y, b.m_min.z, 1.0f)) < 0.0) ? 1 : 0;
            out += (glm::dot(f.planes[i], glm::vec4(b.m_max.x, b.m_max.y, b.m_min.z, 1.0f)) < 0.0) ? 1 : 0;
            out += (glm::dot(f.planes[i], glm::vec4(b.m_min.x, b.m_min.y, b.m_max.z, 1.0f)) < 0.0) ? 1 : 0;
            out += (glm::dot(f.planes[i], glm::vec4(b.m_max.x, b.m_min.y, b.m_max.z, 1.0f)) < 0.0) ? 1 : 0;
            out += (glm::dot(f.planes[i], glm::vec4(b.m_min.x, b.m_max.y, b.m_max.z, 1.0f)) < 0.0) ? 1 : 0;
            out += (glm::dot(f.planes[i], glm::vec4(b.m_max.x, b.m_max.y, b.m_max.z, 1.0f)) < 0.0) ? 1 : 0;
            if (out == 8) return false;
        }

        int r = 0;
        r=0; for(int i=0; i<8; i++) r += (f.corners[i].x > b.m_max.x) ? 1 : 0; if(r==8) return false;
        r=0; for(int i=0; i<8; i++) r += (f.corners[i].x < b.m_min.x) ? 1 : 0; if(r==8) return false;
        r=0; for(int i=0; i<8; i++) r += (f.corners[i].y > b.m_max.y) ? 1 : 0; if(r==8) return false;
        r=0; for(int i=0; i<8; i++) r += (f.corners[i].y < b.m_min.y) ? 1 : 0; if(r==8) return false;
        r=0; for(int i=0; i<8; i++) r += (f.corners[i].z > b.m_max.z) ? 1 : 0; if(r==8) return false;
        r=0; for(int i=0; i<8; i++) r += (f.corners[i].z < b.m_min.z) ? 1 : 0; if(r==8) return false;

        return true;
    }
}
