#include "pnkr/renderer/scene/GLTFUtils.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <stb_image.h>
#include <fstream>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

#include "pnkr/core/logger.hpp"

namespace pnkr::renderer::scene
{
    std::vector<std::uint8_t> base64Decode(std::string_view in)
    {
        static const auto lookup = []()
        {
            std::vector<int> t(256, -1);
            for (int i = 0; i < 62; i++) {
              t[static_cast<std::uint8_t>("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghij"
                                          "klmnopqrstuvwxyz0123456789"[i])] = i;
            }
            t[static_cast<std::uint8_t>('+')] = 62;
            t[static_cast<std::uint8_t>('/')] = 63;
            return t;
        }();

        std::vector<std::uint8_t> out;
        out.reserve((in.size() * 3) / 4);

        std::uint32_t buf = 0;
        int bits = 0;

        for (unsigned char c : in)
        {
          if (std::isspace(c) != 0) {
            continue;
          }
          if (c == '=') {
            break;
          }
          if (lookup[c] == -1) {
            continue;
          }

            buf = (buf << 6) | static_cast<std::uint32_t>(lookup[c]);
            bits += 6;

            if (bits >= 8)
            {
                bits -= 8;
                out.push_back(static_cast<std::uint8_t>((buf >> bits) & 0xFF));
            }
        }
        return out;
    }

    static bool readFileBytes(const std::filesystem::path& p, std::vector<std::uint8_t>& out)
    {
        std::ifstream file(p, std::ios::binary);
        if (!file) {
          return false;
        }

        file.seekg(0, std::ios::end);
        const std::streamsize sz = file.tellg();
        if (sz <= 0) {
          return false;
        }
        file.seekg(0, std::ios::beg);

        out.resize(static_cast<size_t>(sz));
        file.read(reinterpret_cast<char*>(out.data()), sz);
        return file.good();
    }

    std::vector<std::uint8_t> extractImageBytes(
        const fastgltf::Asset& gltf,
        const fastgltf::Image& image,
        const std::filesystem::path& baseDir)
    {
        std::vector<std::uint8_t> bytes;

        std::visit(fastgltf::visitor{
                       [&](const fastgltf::sources::Array& a)
                       {
                           bytes.resize(a.bytes.size());
                           if (!bytes.empty()) {
                             std::memcpy(bytes.data(), a.bytes.data(),
                                         bytes.size());
                           }
                       },

                       [&](const fastgltf::sources::Vector& v)
                       {
                           bytes.resize(v.bytes.size());
                           if (!bytes.empty()) {
                             std::memcpy(bytes.data(), v.bytes.data(),
                                         bytes.size());
                           }
                       },

                       [&](const fastgltf::sources::ByteView& b)
                       {
                           bytes.resize(b.bytes.size());
                           if (!bytes.empty()) {
                             std::memcpy(bytes.data(), b.bytes.data(),
                                         bytes.size());
                           }
                       },

                       [&](const fastgltf::sources::BufferView& viewSrc)
                       {
                           const auto& bv = gltf.bufferViews[viewSrc.bufferViewIndex];
                           const auto& buf = gltf.buffers[bv.bufferIndex];

                           auto copySlice = [&](const std::uint8_t* src, size_t srcSize)
                           {
                               const size_t off = bv.byteOffset;
                               const size_t len = bv.byteLength;
                               if (off + len > srcSize) {
                                 return;
                               }
                               bytes.resize(len);
                               std::memcpy(bytes.data(), src + off, len);
                           };

                           std::visit(fastgltf::visitor{
                                          [&](const fastgltf::sources::Array& a)
                                          {
                                              copySlice(reinterpret_cast<const std::uint8_t*>(a.bytes.data()),
                                                        a.bytes.size());
                                          },
                                          [&](const fastgltf::sources::Vector& v)
                                          {
                                              copySlice(reinterpret_cast<const std::uint8_t*>(v.bytes.data()),
                                                        v.bytes.size());
                                          },
                                          [&](const fastgltf::sources::ByteView& b)
                                          {
                                              copySlice(reinterpret_cast<const std::uint8_t*>(b.bytes.data()),
                                                        b.bytes.size());
                                          },

                                          [&](const fastgltf::sources::URI& u)
                                          {

                                              std::filesystem::path p;

                                              if constexpr (requires { u.uri.fspath(); })
                                              {
                                                  p = (baseDir / u.uri.fspath()).lexically_normal();
                                              }
                                              else if constexpr (requires { u.uri.path(); })
                                              {
                                                  p = (baseDir / std::filesystem::path(u.uri.path())).
                                                      lexically_normal();
                                              }

                                              std::vector<std::uint8_t> whole;
                                              if (!p.empty() && readFileBytes(p, whole))
                                              {
                                                  copySlice(whole.data(), whole.size());
                                              }
                                          },

                                          [&](auto&)
                                          {
                                          }
                                      }, buf.data);
                       },

                       [&](const fastgltf::sources::URI& uriSrc)
                       {

                           if constexpr (requires { uriSrc.uri.scheme(); })
                           {
                               if (uriSrc.uri.scheme() == "data")
                               {
                                   const auto s = uriSrc.uri.path();
                                   const auto comma = s.find(',');
                                   if (comma != std::string_view::npos)
                                   {
                                       bytes = base64Decode(s.substr(comma + 1));
                                   }
                                   return;
                               }
                           }

                           std::filesystem::path p;

                           if constexpr (requires { uriSrc.uri.fspath(); })
                           {
                               p = (baseDir / uriSrc.uri.fspath()).lexically_normal();
                           } else if constexpr (requires {
                                                  uriSrc.uri.path();
                                                }) {
                             p = (baseDir /
                                  std::filesystem::path(uriSrc.uri.path()))
                                     .lexically_normal();
                           }

                           if (!p.empty())
                           {
                               (void)readFileBytes(p, bytes);
                           }
                       },

                       [&](const fastgltf::sources::Fallback&)
                       {
                       },

                       [&](auto&)
                       {
                       }

                   }, image.data);
        if (bytes.empty()) {
            core::Logger::Scene.warn("extractImageBytes: returned 0 bytes. mime='{}'", image.name);
            std::visit(
                fastgltf::visitor{
                    [&](const fastgltf::sources::BufferView &v) {
                      core::Logger::Scene.warn("  source=BufferView idx={}",
                                               v.bufferViewIndex);
                    },
                    [&](const fastgltf::sources::URI &u) {
                      if constexpr (requires {
                                      u.uri.scheme();
                                      u.uri.path();
                                    }) {
                        core::Logger::Scene.warn("  source=URI scheme='{}' path='{}'", u.uri.scheme(), std::string(u.uri.path()));
                      } else {
                        core::Logger::Scene.warn(
                            "  source=URI (scheme/path unavailable in this "
                            "fastgltf build)");
                      }
                    },
                    [&](const fastgltf::sources::Fallback &) {
                      core::Logger::Scene.warn("  source=Fallback");
                    },
                    [&](const fastgltf::sources::CustomBuffer &cb) {
                      core::Logger::Scene.warn("  source=CustomBuffer id={}",
                                               util::u32(cb.id));
                    },
                    [&](auto &) {
                      core::Logger::Scene.warn("  source=other");
                    }},
                image.data);
        }
        return bytes;
    }

    rhi::SamplerAddressMode toAddressMode(fastgltf::Wrap wrap)
    {
        switch (wrap)
        {
        case fastgltf::Wrap::Repeat: return rhi::SamplerAddressMode::Repeat;
        case fastgltf::Wrap::MirroredRepeat: return rhi::SamplerAddressMode::MirroredRepeat;
        case fastgltf::Wrap::ClampToEdge: return rhi::SamplerAddressMode::ClampToEdge;
        default: return rhi::SamplerAddressMode::Repeat;
        }
    }

    std::optional<std::size_t> pickImageIndex(const fastgltf::Texture& texture)
    {
      if (texture.imageIndex.has_value()) {
        return texture.imageIndex.value();
      }
      if (texture.webpImageIndex.has_value()) {
        return texture.webpImageIndex.value();
      }
      if (texture.ddsImageIndex.has_value()) {
        return texture.ddsImageIndex.value();
      }
      if (texture.basisuImageIndex.has_value()) {
        return texture.basisuImageIndex.value();
      }
        return std::nullopt;
    }

    fastgltf::URIView getUriView(const fastgltf::sources::URI& uri)
    {
        if constexpr (requires { uri.uri; })
        {
          if constexpr (requires { uri.uri.string(); }) {
            return fastgltf::URIView{uri.uri.string()};
          } else if constexpr (requires { uri.uri.c_str(); }) {
            return fastgltf::URIView{std::string_view{uri.uri.c_str()}};
          } else {
            return fastgltf::URIView{};
          }
        } else {
          return fastgltf::URIView{};
        }
    }

    float getNormalScale(const fastgltf::NormalTextureInfo& info)
    {
        if constexpr (requires { info.scale; })
        {
            return info.scale;
        }
        return 1.0F;
    }

    float getOcclusionStrength(const fastgltf::OcclusionTextureInfo& info)
    {
        if constexpr (requires { info.strength; })
        {
            return info.strength;
        }
        return 1.0F;
    }

    uint32_t toAlphaMode(fastgltf::AlphaMode mode)
    {
        switch (mode)
        {
        case fastgltf::AlphaMode::Mask:
          return 1U;
        case fastgltf::AlphaMode::Blend:
          return 2U;
        case fastgltf::AlphaMode::Opaque: default:
          return 0U;
        }
    }

    Transform toTransform(const fastgltf::Node& node)
    {
        Transform out;
        std::visit(fastgltf::visitor{
            [&](const fastgltf::TRS& trs)
            {
                out.m_translation = glm::make_vec3(trs.translation.data());
                const auto& q = trs.rotation;
                out.m_rotation = glm::quat(q[3], q[0], q[1], q[2]);
                out.m_scale = glm::make_vec3(trs.scale.data());
                out.m_matrix = out.mat4();
            },
            [&](const fastgltf::math::fmat4x4& m)
            {
                out.m_matrix = glm::make_mat4(m.data());
                glm::vec3 skew;
                glm::vec4 perspective;
                glm::decompose(out.m_matrix, out.m_scale, out.m_rotation, out.m_translation, skew, perspective);
            }
        }, node.transform);
        return out;
    }

    void loadSkins(const fastgltf::Asset& gltf, std::vector<Skin>& outSkins,
                   const std::function<uint32_t(size_t)>& nodeIndexMapper)
    {
        outSkins.reserve(gltf.skins.size());
        for (const auto& gSkin : gltf.skins)
        {
            Skin skin;
            skin.name = gSkin.name;
            skin.skeletonRootNode = gSkin.skeleton.has_value()
                                        ? static_cast<int>(nodeIndexMapper(gSkin.skeleton.value()))
                                        : -1;

            skin.joints.reserve(gSkin.joints.size());
            for (auto jointIdx : gSkin.joints)
            {
                skin.joints.push_back(nodeIndexMapper(jointIdx));
            }

            if (gSkin.inverseBindMatrices.has_value())
            {
                const auto& acc = gltf.accessors[gSkin.inverseBindMatrices.value()];
                skin.inverseBindMatrices.resize(acc.count);
                fastgltf::iterateAccessorWithIndex<glm::mat4>(gltf, acc, [&](glm::mat4 m, size_t idx)
                {
                    skin.inverseBindMatrices[idx] = m;
                });
            }
            else
            {
              skin.inverseBindMatrices.assign(skin.joints.size(),
                                              glm::mat4(1.0F));
            }
            outSkins.push_back(std::move(skin));
        }
    }

    void loadAnimations(const fastgltf::Asset& gltf, std::vector<Animation>& outAnimations,
                        const std::function<uint32_t(size_t)>& nodeIndexMapper)
    {
        outAnimations.reserve(gltf.animations.size());
        for (const auto& gAnim : gltf.animations)
        {
            Animation anim;
            anim.name = gAnim.name;

            for (const auto& gSampler : gAnim.samplers)
            {
                AnimationSampler sampler;
                if (gSampler.interpolation ==
                    fastgltf::AnimationInterpolation::Linear) {
                  sampler.interpolation = InterpolationType::Linear;
                } else if (gSampler.interpolation ==
                           fastgltf::AnimationInterpolation::Step) {
                  sampler.interpolation = InterpolationType::Step;
                } else {
                  sampler.interpolation = InterpolationType::CubicSpline;
                }

                const auto& inputAcc = gltf.accessors[gSampler.inputAccessor];
                sampler.inputs.resize(inputAcc.count);
                fastgltf::iterateAccessorWithIndex<float>(gltf, inputAcc, [&](float t, size_t idx)
                {
                    sampler.inputs[idx] = t;
                    anim.duration = std::max(anim.duration, t);
                });

                const auto& outputAcc = gltf.accessors[gSampler.outputAccessor];
                sampler.outputs.resize(outputAcc.count);
                if (outputAcc.type == fastgltf::AccessorType::Scalar)
                {
                    fastgltf::iterateAccessorWithIndex<float>(gltf, outputAcc, [&](float v, size_t idx)
                    {
                        sampler.outputs[idx] = glm::vec4(v, 0, 0, 0);
                    });
                }
                else if (outputAcc.type == fastgltf::AccessorType::Vec3)
                {
                  fastgltf::iterateAccessorWithIndex<glm::vec3>(
                      gltf, outputAcc, [&](glm::vec3 v, size_t idx) {
                        sampler.outputs[idx] = glm::vec4(v, 0.0F);
                      });
                }
                else if (outputAcc.type == fastgltf::AccessorType::Vec4)
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, outputAcc, [&](glm::vec4 v, size_t idx)
                    {
                        sampler.outputs[idx] = v;
                    });
                }
                anim.samplers.push_back(std::move(sampler));
            }

            for (const auto& gChannel : gAnim.channels)
            {
              if (!gChannel.nodeIndex.has_value()) {
                continue;
              }
              AnimationChannel channel{};
              channel.samplerIndex = static_cast<int>(gChannel.samplerIndex);
              channel.targetNode = nodeIndexMapper(gChannel.nodeIndex.value());
              if (gChannel.path == fastgltf::AnimationPath::Translation) {
                channel.path = AnimationPath::Translation;
              } else if (gChannel.path == fastgltf::AnimationPath::Rotation) {
                channel.path = AnimationPath::Rotation;
              } else if (gChannel.path == fastgltf::AnimationPath::Scale) {
                channel.path = AnimationPath::Scale;
              } else if (gChannel.path == fastgltf::AnimationPath::Weights) {
                channel.path = AnimationPath::Weights;
              }
                anim.channels.push_back(channel);
            }
            outAnimations.push_back(std::move(anim));
        }
    }
}
