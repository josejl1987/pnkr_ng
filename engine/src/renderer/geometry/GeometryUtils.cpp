#include "pnkr/renderer/geometry/GeometryUtils.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

namespace pnkr::renderer::geometry {

    namespace {
        uint32_t clampMin(uint32_t value, uint32_t minValue) {
            return std::max(value, minValue);
        }
    }

    void GeometryUtils::pushVertex(MeshData& data, const glm::vec3& pos, const glm::vec3& norm,
                                   const glm::vec2& uv, const glm::vec4& tan)
    {
        Vertex v{};
        v.position = glm::vec4(pos, 1.0F);
        v.normal = glm::vec4(norm, 0.0F);
        v.uv0 = uv;
        v.uv1 = uv;
        v.tangent = tan;
        v.color = glm::vec4(1.0F);
        v.joints = glm::uvec4(0);
        v.weights = glm::vec4(0.0F);
        v.meshIndex = 0;
        v.localIndex = 0;
        data.vertices.push_back(v);
    }

    MeshData GeometryUtils::getCube(float size)
    {
        MeshData data;
        const float h = size * 0.5F;

        auto buildFace = [&](const glm::vec3& normal, const glm::vec3& up, const glm::vec3& right) {
            const glm::vec3 center = normal * h;
            const glm::vec3 bl = center - right * h - up * h;
            const glm::vec3 br = center + right * h - up * h;
            const glm::vec3 tr = center + right * h + up * h;
            const glm::vec3 tl = center - right * h + up * h;

            const glm::vec4 tan(right, 1.0F);
            const auto base = static_cast<uint32_t>(data.vertices.size());

            pushVertex(data, bl, normal, {0.0F, 0.0F}, tan);
            pushVertex(data, br, normal, {1.0F, 0.0F}, tan);
            pushVertex(data, tr, normal, {1.0F, 1.0F}, tan);
            pushVertex(data, tl, normal, {0.0F, 1.0F}, tan);

            data.indices.push_back(base + 0);
            data.indices.push_back(base + 1);
            data.indices.push_back(base + 2);
            data.indices.push_back(base + 0);
            data.indices.push_back(base + 2);
            data.indices.push_back(base + 3);
        };

        buildFace({0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F}, {1.0F, 0.0F, 0.0F});
        buildFace({0.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F}, {-1.0F, 0.0F, 0.0F});
        buildFace({1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, -1.0F});
        buildFace({-1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
        buildFace({0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, -1.0F}, {1.0F, 0.0F, 0.0F});
        buildFace({0.0F, -1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 0.0F});

        return data;
    }

    MeshData GeometryUtils::getPlane(float width, float depth, uint32_t subdivisions)
    {
        MeshData data;
        subdivisions = clampMin(subdivisions, 1U);

        const float halfW = width * 0.5F;
        const float halfD = depth * 0.5F;

        for (uint32_t z = 0; z <= subdivisions; ++z) {
            for (uint32_t x = 0; x <= subdivisions; ++x) {
                const float u = static_cast<float>(x) / subdivisions;
                const float v = static_cast<float>(z) / subdivisions;

                const glm::vec3 pos((u * width) - halfW, 0.0F,
                                    (v * depth) - halfD);

                pushVertex(data, pos, {0.0F, 1.0F, 0.0F}, {u, v},
                           {1.0F, 0.0F, 0.0F, 1.0F});
            }
        }

        const uint32_t stride = subdivisions + 1;
        for (uint32_t z = 0; z < subdivisions; ++z) {
            for (uint32_t x = 0; x < subdivisions; ++x) {
              const uint32_t tl = (z * stride) + x;
              const uint32_t tr = tl + 1;
              const uint32_t bl = ((z + 1) * stride) + x;
              const uint32_t br = bl + 1;

              data.indices.push_back(tl);
              data.indices.push_back(bl);
              data.indices.push_back(tr);

              data.indices.push_back(tr);
              data.indices.push_back(bl);
              data.indices.push_back(br);
            }
        }

        return data;
    }

    MeshData GeometryUtils::getSphere(float radius, uint32_t segments, uint32_t rings)
    {
        MeshData data;
        segments = clampMin(segments, 3U);
        rings = clampMin(rings, 2U);

        const auto pi = glm::pi<float>();
        for (uint32_t y = 0; y <= rings; ++y) {
            for (uint32_t x = 0; x <= segments; ++x) {
                const float xSegment = static_cast<float>(x) / segments;
                const float ySegment = static_cast<float>(y) / rings;

                const float theta = xSegment * 2.0F * pi;
                const float phi = ySegment * pi;

                const float xPos = std::cos(theta) * std::sin(phi);
                const float yPos = std::cos(phi);
                const float zPos = std::sin(theta) * std::sin(phi);

                const glm::vec3 normal(xPos, yPos, zPos);
                const glm::vec3 pos = normal * radius;

                glm::vec3 tangent(-std::sin(theta), 0.0F, std::cos(theta));
                tangent = glm::normalize(tangent);

                pushVertex(data, pos, normal, {xSegment, ySegment},
                           glm::vec4(tangent, 1.0F));
            }
        }

        const uint32_t stride = segments + 1;
        for (uint32_t y = 0; y < rings; ++y) {
            for (uint32_t x = 0; x < segments; ++x) {
              const uint32_t i0 = (y * stride) + x;
              const uint32_t i1 = i0 + 1;
              const uint32_t i2 = ((y + 1) * stride) + x;
              const uint32_t i3 = i2 + 1;

              data.indices.push_back(i0);
              data.indices.push_back(i2);
              data.indices.push_back(i1);

              data.indices.push_back(i1);
              data.indices.push_back(i2);
              data.indices.push_back(i3);
            }
        }

        return data;
    }

    MeshData GeometryUtils::getCylinder(float radius, float height, uint32_t slices)
    {
        MeshData data;
        slices = clampMin(slices, 3U);

        const float halfH = height * 0.5F;
        const auto pi = glm::pi<float>();

        const auto bodyStart = static_cast<uint32_t>(data.vertices.size());
        for (uint32_t i = 0; i <= slices; ++i) {
            const float u = static_cast<float>(i) / slices;
            const float theta = u * 2.0F * pi;
            const float x = std::cos(theta);
            const float z = std::sin(theta);

            const glm::vec3 normal(x, 0.0F, z);
            const glm::vec3 tangent(-z, 0.0F, x);

            pushVertex(data, {x * radius, halfH, z * radius}, normal, {u, 0.0F},
                       glm::vec4(tangent, 1.0F));
            pushVertex(data, {x * radius, -halfH, z * radius}, normal,
                       {u, 1.0F}, glm::vec4(tangent, 1.0F));
        }

        for (uint32_t i = 0; i < slices; ++i) {
          const uint32_t tl = bodyStart + (i * 2);
          const uint32_t bl = tl + 1;
          const uint32_t tr = tl + 2;
          const uint32_t br = bl + 2;

          data.indices.push_back(tl);
          data.indices.push_back(bl);
          data.indices.push_back(tr);
          data.indices.push_back(tr);
          data.indices.push_back(bl);
          data.indices.push_back(br);
        }

        auto generateCap = [&](bool top) {
            const float y = top ? halfH : -halfH;
            const glm::vec3 normal(0.0F, top ? 1.0F : -1.0F, 0.0F);
            const glm::vec4 tan(1.0F, 0.0F, 0.0F, 1.0F);

            const auto centerIdx = static_cast<uint32_t>(data.vertices.size());
            pushVertex(data, {0.0F, y, 0.0F}, normal, {0.5F, 0.5F}, tan);

            const auto rimStart = static_cast<uint32_t>(data.vertices.size());
            for (uint32_t i = 0; i <= slices; ++i) {
              const float theta = static_cast<float>(i) / slices * 2.0F * pi;
              const float x = std::cos(theta);
              const float z = std::sin(theta);
              const glm::vec2 uv = glm::vec2(x, z) * 0.5F + 0.5F;
              pushVertex(data, {x * radius, y, z * radius}, normal, uv, tan);
            }

            for (uint32_t i = 0; i < slices; ++i) {
                if (top) {
                    data.indices.push_back(centerIdx);
                    data.indices.push_back(rimStart + i + 1);
                    data.indices.push_back(rimStart + i);
                } else {
                    data.indices.push_back(centerIdx);
                    data.indices.push_back(rimStart + i);
                    data.indices.push_back(rimStart + i + 1);
                }
            }
        };

        generateCap(true);
        generateCap(false);

        return data;
    }

    MeshData GeometryUtils::getCapsule(float radius, float height, uint32_t slices, uint32_t stacks)
    {
        MeshData data;
        slices = clampMin(slices, 3U);
        stacks = clampMin(stacks, 2U);

        const auto pi = glm::pi<float>();
        const float halfPi = pi * 0.5F;
        const float cylinderHeight = std::max(0.0F, height - (2.0F * radius));
        const float halfH = cylinderHeight * 0.5F;
        const float totalHeight = cylinderHeight + (2.0F * radius);
        const float topY = halfH + radius;

        std::vector<uint32_t> ringStarts;
        ringStarts.reserve((stacks * 2) + 2);

        auto pushRing = [&](float y, float ringRadius,
                            const glm::vec3 &normalBase, float v) {
          const auto start = static_cast<uint32_t>(data.vertices.size());
          for (uint32_t i = 0; i <= slices; ++i) {
            const float u = static_cast<float>(i) / slices;
            const float theta = u * 2.0F * pi;
            const float x = std::cos(theta);
            const float z = std::sin(theta);

            glm::vec3 normal = normalBase;
            if (normalBase.y == 0.0F) {
              normal = glm::vec3(x, 0.0F, z);
            } else {
              normal = glm::normalize(glm::vec3(
                  x * ringRadius, normalBase.y * radius, z * ringRadius));
            }

            const glm::vec3 pos(x * ringRadius, y, z * ringRadius);
            glm::vec3 tangent(-std::sin(theta), 0.0F, std::cos(theta));
            tangent = glm::normalize(tangent);

            pushVertex(data, pos, normal, {u, v}, glm::vec4(tangent, 1.0F));
          }
          ringStarts.push_back(start);
        };

        for (uint32_t i = 0; i <= stacks; ++i) {
            const float t = static_cast<float>(i) / stacks;
            const float phi = t * halfPi;
            const float ringRadius = std::sin(phi) * radius;
            const float y = (std::cos(phi) * radius) + halfH;
            const float v = (topY - y) / totalHeight;
            pushRing(y, ringRadius, {0.0F, std::cos(phi), 0.0F}, v);
        }

        if (cylinderHeight > 0.0F) {
          const float y = -halfH;
          const float v = (topY - y) / totalHeight;
          pushRing(y, radius, {0.0F, 0.0F, 0.0F}, v);
        }

        for (uint32_t i = 0; i < stacks; ++i) {
            const float t = static_cast<float>(i) / stacks;
            const float phi = t * halfPi;
            const float ringRadius = std::sin(phi) * radius;
            const float y = (-std::cos(phi) * radius) - halfH;
            const float v = (topY - y) / totalHeight;
            pushRing(y, ringRadius, {0.0F, -std::cos(phi), 0.0F}, v);
        }

        for (size_t r = 0; r + 1 < ringStarts.size(); ++r) {
            const uint32_t ringA = ringStarts[r];
            const uint32_t ringB = ringStarts[r + 1];
            for (uint32_t i = 0; i < slices; ++i) {
                const uint32_t i0 = ringA + i;
                const uint32_t i1 = ringA + i + 1;
                const uint32_t i2 = ringB + i;
                const uint32_t i3 = ringB + i + 1;

                data.indices.push_back(i0);
                data.indices.push_back(i2);
                data.indices.push_back(i1);

                data.indices.push_back(i1);
                data.indices.push_back(i2);
                data.indices.push_back(i3);
            }
        }

        return data;
    }

    MeshData GeometryUtils::getTorus(float outerRadius, float innerRadius, uint32_t nsides, uint32_t nrings)
    {
        MeshData data;
        nsides = clampMin(nsides, 3U);
        nrings = clampMin(nrings, 3U);

        const auto pi = glm::pi<float>();

        for (uint32_t ring = 0; ring <= nrings; ++ring) {
            const float u = static_cast<float>(ring) / nrings;
            const float theta = u * 2.0F * pi;
            const float cosTheta = std::cos(theta);
            const float sinTheta = std::sin(theta);

            for (uint32_t side = 0; side <= nsides; ++side) {
                const float v = static_cast<float>(side) / nsides;
                const float phi = v * 2.0F * pi;

                const float cosPhi = std::cos(phi);
                const float sinPhi = std::sin(phi);

                const float ringRadius = outerRadius + (innerRadius * cosPhi);
                const glm::vec3 pos(
                    ringRadius * cosTheta,
                    innerRadius * sinPhi,
                    ringRadius * sinTheta
                );

                const glm::vec3 normal(
                    cosTheta * cosPhi,
                    sinPhi,
                    sinTheta * cosPhi
                );

                const glm::vec3 tangent(-sinTheta * ringRadius, 0.0F,
                                        cosTheta * ringRadius);

                pushVertex(data, pos, glm::normalize(normal), {u, v},
                           glm::vec4(glm::normalize(tangent), 1.0F));
            }
        }

        const uint32_t stride = nsides + 1;
        for (uint32_t ring = 0; ring < nrings; ++ring) {
            for (uint32_t side = 0; side < nsides; ++side) {
              const uint32_t i0 = (ring * stride) + side;
              const uint32_t i1 = i0 + 1;
              const uint32_t i2 = ((ring + 1) * stride) + side;
              const uint32_t i3 = i2 + 1;

              data.indices.push_back(i0);
              data.indices.push_back(i2);
              data.indices.push_back(i1);

              data.indices.push_back(i1);
              data.indices.push_back(i2);
              data.indices.push_back(i3);
            }
        }

        return data;
    }

}
