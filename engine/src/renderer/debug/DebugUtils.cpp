#include "pnkr/renderer/debug/DebugUtils.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <fstream>
#include <stdexcept>

namespace pnkr::renderer::debug::utils {

    void box(IDebugSink& sink, const glm::vec3& min, const glm::vec3& max, const glm::vec3& color) {
        glm::vec3 pts[8] = {
            glm::vec3(max.x, max.y, max.z), glm::vec3(max.x, max.y, min.z),
            glm::vec3(max.x, min.y, max.z), glm::vec3(max.x, min.y, min.z),
            glm::vec3(min.x, max.y, max.z), glm::vec3(min.x, max.y, min.z),
            glm::vec3(min.x, min.y, max.z), glm::vec3(min.x, min.y, min.z),
        };

        sink.line(pts[0], pts[1], color);
        sink.line(pts[2], pts[3], color);
        sink.line(pts[4], pts[5], color);
        sink.line(pts[6], pts[7], color);

        sink.line(pts[0], pts[2], color);
        sink.line(pts[1], pts[3], color);
        sink.line(pts[4], pts[6], color);
        sink.line(pts[5], pts[7], color);

        sink.line(pts[0], pts[4], color);
        sink.line(pts[1], pts[5], color);
        sink.line(pts[2], pts[6], color);
        sink.line(pts[3], pts[7], color);
    }

    void box(IDebugSink& sink, const glm::mat4& transform, const glm::vec3& size, const glm::vec3& color) {
      glm::vec3 halfSize = size * 0.5F;
      glm::vec3 pts[8] = {
          glm::vec3(+halfSize.x, +halfSize.y, +halfSize.z),
          glm::vec3(+halfSize.x, +halfSize.y, -halfSize.z),
          glm::vec3(+halfSize.x, -halfSize.y, +halfSize.z),
          glm::vec3(+halfSize.x, -halfSize.y, -halfSize.z),
          glm::vec3(-halfSize.x, +halfSize.y, +halfSize.z),
          glm::vec3(-halfSize.x, +halfSize.y, -halfSize.z),
          glm::vec3(-halfSize.x, -halfSize.y, +halfSize.z),
          glm::vec3(-halfSize.x, -halfSize.y, -halfSize.z),
      };

      for (auto &p : pts) {
        p = glm::vec3(transform * glm::vec4(p, 1.F));
      }

        sink.line(pts[0], pts[1], color);
        sink.line(pts[2], pts[3], color);
        sink.line(pts[4], pts[5], color);
        sink.line(pts[6], pts[7], color);

        sink.line(pts[0], pts[2], color);
        sink.line(pts[1], pts[3], color);
        sink.line(pts[4], pts[6], color);
        sink.line(pts[5], pts[7], color);

        sink.line(pts[0], pts[4], color);
        sink.line(pts[1], pts[5], color);
        sink.line(pts[2], pts[6], color);
        sink.line(pts[3], pts[7], color);
    }

    void plane(IDebugSink& sink, const glm::vec3& origin, const glm::vec3& v1, const glm::vec3& v2,
               int segments1, int segments2, const glm::vec3& color) {
        for (int i = 0; i <= segments1; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(segments1);
            sink.line(origin + v1 * t, origin + v1 * t + v2, color);
        }
        for (int i = 0; i <= segments2; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(segments2);
            sink.line(origin + v2 * t, origin + v2 * t + v1, color);
        }
    }

    void frustum(IDebugSink& sink, const glm::mat4& viewProj, const glm::vec3& color, std::vector<glm::vec3>* outCorners) {
        glm::mat4 inv = glm::inverse(viewProj);
        glm::vec4 frustum[8] = {
            {-1, -1, 0, 1}, {1, -1, 0, 1}, {1, 1, 0, 1}, {-1, 1, 0, 1},
            {-1, -1, 1, 1}, {1, -1, 1, 1}, {1, 1, 1, 1}, {-1, 1, 1, 1}
        };

        glm::vec3 pp[8];
        for (int i = 0; i < 8; ++i) {
            glm::vec4 p = inv * frustum[i];
            pp[i] = glm::vec3(p) / p.w;
            if (outCorners != nullptr) {
              outCorners->push_back(pp[i]);
            }
        }

        sink.line(pp[0], pp[4], color);
        sink.line(pp[1], pp[5], color);
        sink.line(pp[2], pp[6], color);
        sink.line(pp[3], pp[7], color);
        sink.line(pp[0], pp[1], color);
        sink.line(pp[1], pp[2], color);
        sink.line(pp[2], pp[3], color);
        sink.line(pp[3], pp[0], color);
        sink.line(pp[4], pp[5], color);
        sink.line(pp[5], pp[6], color);
        sink.line(pp[6], pp[7], color);
        sink.line(pp[7], pp[4], color);
    }

    void frustum(IDebugSink& sink, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& color, std::vector<glm::vec3>* outCorners) {
        frustum(sink, proj * view, color, outCorners);
    }

    void circle(IDebugSink& sink, const glm::vec3& center, float radius, const glm::vec3& normal, const glm::vec3& color, int segments) {
      glm::vec3 up = (std::abs(normal.z) < 0.999F) ? glm::vec3(0, 0, 1)
                                                   : glm::vec3(1, 0, 0);
      glm::vec3 tangent = glm::normalize(glm::cross(up, normal));
      glm::vec3 bitangent = glm::cross(normal, tangent);

      glm::vec3 prev = center + tangent * radius;
      for (int i = 1; i <= segments; ++i) {
        float angle = 2.0F * glm::pi<float>() * static_cast<float>(i) /
                      static_cast<float>(segments);
        glm::vec3 curr =
            center +
            (tangent * glm::cos(angle) + bitangent * glm::sin(angle)) * radius;
        sink.line(prev, curr, color);
        prev = curr;
      }
    }

    void sphere(IDebugSink& sink, const glm::vec3& center, float radius, const glm::vec3& color, int segments) {
      float angleStep = 2.0F * glm::pi<float>() / static_cast<float>(segments);

      auto drawCircle = [&](int axis) {
        glm::vec3 prev(0.F);
        if (axis == 0) {
          prev = center + glm::vec3(radius, 0, 0);
        }
        if (axis == 1) {
          prev = center + glm::vec3(0, radius, 0);
        }
        if (axis == 2) {
          prev = center + glm::vec3(radius, 0, 0);
        }

        for (int i = 1; i <= segments; ++i) {
          float a = static_cast<float>(i) * angleStep;
          float c = glm::cos(a) * radius;
          float s = glm::sin(a) * radius;
          glm::vec3 curr(0.F);

          if (axis == 0) {
            curr = center + glm::vec3(c, s, 0);
          }
          if (axis == 1) {
            curr = center + glm::vec3(0, c, s);
          }
          if (axis == 2) {
            curr = center + glm::vec3(c, 0, s);
          }

          sink.line(prev, curr, color);
          prev = curr;
        }
      };

      drawCircle(0);
      drawCircle(1);
      drawCircle(2);
    }

    rhi::ShaderModuleDescriptor loadSpirvShader(const std::filesystem::path& shaderPath, rhi::ShaderStage stage) {
        std::ifstream file(shaderPath, std::ios::binary);
        if (!file.is_open()) {
            std::filesystem::path alt = std::filesystem::path("bin") / shaderPath;
            file.open(alt, std::ios::binary);
            if (!file.is_open()) {
              throw std::runtime_error("Failed to open SPIR-V shader file: " +
                                       shaderPath.string());
            }
        }

        std::vector<uint32_t> spirv;
        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        spirv.resize(fileSize / sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(spirv.data()), fileSize);

        return rhi::ShaderModuleDescriptor{.stage = stage, .spirvCode = spirv, .entryPoint = "main"};
    }

}
