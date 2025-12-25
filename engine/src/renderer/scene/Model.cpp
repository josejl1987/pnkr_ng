// Model.cpp

#include "pnkr/renderer/scene/Model.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/core/logger.hpp"


#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>

#include <cstdint>
#include <functional>
#include <limits>
#include <variant>

#include <filesystem>
#include <fstream>
#include <optional>
#include <stb_image.h>
#include <string>
#include <vector>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>

#include "pnkr/core/common.hpp"

namespace pnkr::renderer::scene
{
    static float resolveOptionalFloat(float value, float)
    {
        return value;
    }

    template <typename T>
    static auto resolveOptionalFloat(const T& value, float fallback)
        -> decltype(value.value_or(fallback), float{})
    {
        return static_cast<float>(value.value_or(fallback));
    }

    static Transform toTransform(const fastgltf::Node& node)
    {
        Transform t{};
        std::visit(fastgltf::visitor{
                       [&](const fastgltf::TRS& trs)
                       {
                           t.m_translation = glm::make_vec3(trs.translation.data());
                           t.m_rotation = glm::make_quat(trs.rotation.data());
                           t.m_scale = glm::make_vec3(trs.scale.data());
                           t.m_matrix = t.mat4();
                       },
                       [&](const fastgltf::math::fmat4x4& mat)
                       {
                           t.m_matrix = glm::make_mat4(mat.data());
                           glm::vec3 skew{};
                           glm::vec4 perspective{};
                           glm::decompose(t.m_matrix, t.m_scale, t.m_rotation, t.m_translation, skew, perspective);
                       }
                   },
                   node.transform);
        return t;
    }

    static std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& p)
    {
        std::ifstream file(p, std::ios::binary);
        if (!file)
        {
            return {};
        }

        file.seekg(0, std::ios::end);
        const auto size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        std::vector<std::uint8_t> out(size);
        if (size != 0u)
        {
            file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
        }
        return out;
    }

    static std::optional<std::size_t> pickImageIndex(const fastgltf::Texture& texture)
    {
        // Prefer standard images first, then extension-backed images.
        if (texture.imageIndex.has_value())
        {
            return texture.imageIndex.value();
        }
        if (texture.webpImageIndex.has_value())
        {
            return texture.webpImageIndex.value();
        }
        if (texture.ddsImageIndex.has_value())
        {
            return texture.ddsImageIndex.value();
        }
        if (texture.basisuImageIndex.has_value())
        {
            return texture.basisuImageIndex.value();
        }
        return std::nullopt;
    }

    static fastgltf::URIView getUriView(const fastgltf::sources::URI& uri)
    {
        // fastgltf has had a few API variations here; keep this resilient.
        if constexpr (requires { uri.uri; })
        {
            // In some versions, u.uri is a fastgltf::URI / URIView-like.
            if constexpr (requires { uri.uri.string(); })
            {
                return fastgltf::URIView{uri.uri.string()};
            }
            else if constexpr (requires { uri.uri.c_str(); })
            {
                return fastgltf::URIView{std::string_view{uri.uri.c_str()}};
            }
            else
            {
                return fastgltf::URIView{};
            }
        }
        else if constexpr (requires { uri.uri; })
        {
            return uri.uri;
        }
        else
        {
            return fastgltf::URIView{};
        }
    }

    static rhi::SamplerAddressMode toAddressMode(fastgltf::Wrap wrap)
    {
        switch (wrap)
        {
        case fastgltf::Wrap::Repeat:
            return rhi::SamplerAddressMode::Repeat;
        case fastgltf::Wrap::MirroredRepeat:
            return rhi::SamplerAddressMode::MirroredRepeat;
        case fastgltf::Wrap::ClampToEdge:
            return rhi::SamplerAddressMode::ClampToEdge;
        default:
            return rhi::SamplerAddressMode::Repeat;
        }
    }
    template <typename T>
    static uint32_t getTexCoordIndex(const T& info)
    {
        if constexpr (requires { info.texCoord; })
        {
            return util::u32(info.texCoord);
        }
        else if constexpr (requires { info.texCoordIndex; })
        {
            return util::u32(info.texCoordIndex);
        }
        else
        {
            return 0;
        }
    }

    static rhi::SamplerAddressMode getSamplerAddressMode(const fastgltf::Asset& gltf, size_t textureIndex)
    {
        if (textureIndex >= gltf.textures.size())
        {
            return rhi::SamplerAddressMode::Repeat;
        }

        const auto& tex = gltf.textures[textureIndex];
        if (!tex.samplerIndex.has_value() || tex.samplerIndex.value() >= gltf.samplers.size())
        {
            return rhi::SamplerAddressMode::Repeat;
        }

        const auto& sampler = gltf.samplers[tex.samplerIndex.value()];
        fastgltf::Wrap wrapS = sampler.wrapS;
        fastgltf::Wrap wrapT = sampler.wrapT;

        if (wrapS != wrapT)
        {
            core::Logger::warn("[Model] Sampler wrapS != wrapT, using wrapS for bindless sampler.");
        }

        return toAddressMode(wrapS);
    }

    static float getNormalScale(const fastgltf::NormalTextureInfo& info)
    {
        if constexpr (requires { info.scale; })
        {
            return static_cast<float>(info.scale);
        }
        return 1.0f;
    }

    static float getOcclusionStrength(const fastgltf::OcclusionTextureInfo& info)
    {
        if constexpr (requires { info.strength; })
        {
            return static_cast<float>(info.strength);
        }
        return 1.0f;
    }

    template <typename T>
    static float getOptionalFloat(const T& value, float fallback)
    {
        if constexpr (requires { value.has_value(); })
        {
            return value.has_value() ? static_cast<float>(value.value()) : fallback;
        }
        else
        {
            return static_cast<float>(value);
        }
    }

    static uint32_t toAlphaMode(fastgltf::AlphaMode mode)
    {
        switch (mode)
        {
        case fastgltf::AlphaMode::Mask:
            return 1u;
        case fastgltf::AlphaMode::Blend:
            return 2u;
        case fastgltf::AlphaMode::Opaque:
        default:
            return 0u;
        }
    }

    static std::vector<std::uint8_t> base64Decode(std::string_view in)
    {
        static constexpr std::array<std::uint8_t, 256> kDec = []
        {
            std::array<std::uint8_t, 256> t{};
            t.fill(0xFF);
            for (int i = 'A'; i <= 'Z'; ++i)
            {
                t[static_cast<std::uint8_t>(i)] = static_cast<std::uint8_t>(i - 'A');
            }
            for (int i = 'a'; i <= 'z'; ++i)
            {
                t[static_cast<std::uint8_t>(i)] = static_cast<std::uint8_t>(26 + i - 'a');
            }
            for (int i = '0'; i <= '9'; ++i)
            {
                t[static_cast<std::uint8_t>(i)] = static_cast<std::uint8_t>(52 + i - '0');
            }
            t[static_cast<std::uint8_t>('+')] = 62;
            t[static_cast<std::uint8_t>('/')] = 63;
            return t;
        }();

        std::vector<std::uint8_t> out;
        out.reserve((in.size() * 3) / 4);

        std::uint32_t buf = 0;
        int bits = 0;
        int pad = 0;

        for (unsigned char c : in)
        {
            if (std::isspace(c) != 0)
            {
                continue;
            }
            if (c == '=')
            {
                pad++;
                continue;
            }

            const std::uint8_t v = kDec[c];
            if (v == 0xFF)
            {
                continue; // ignore non-base64 chars defensively
            }

            buf = (buf << 6) | v;
            bits += 6;

            if (bits >= 8)
            {
                bits -= 8;
                out.push_back(static_cast<std::uint8_t>((buf >> bits) & 0xFFU));
            }
        }

        if (pad > 0 && out.size() >= static_cast<size_t>(pad))
        {
            out.resize(out.size() - static_cast<size_t>(pad));
        }

        return out;
    }

    static std::vector<std::uint8_t> decodeDataUriBytes(std::string_view uri)
    {
        // Expected form: data:[<mime>][;base64],<payload>
        if (!uri.starts_with("data:"))
        {
            return {};
        }

        const auto comma = uri.find(',');
        if (comma == std::string_view::npos)
        {
            return {};
        }

        const std::string_view meta = uri.substr(5, comma - 5);
        const std::string_view payload = uri.substr(comma + 1);

        const bool isBase64 = (meta.find(";base64") != std::string_view::npos);
        if (!isBase64)
        {
            // glTF embedded binary/image data is effectively always base64.
            // If you need SVG/text data URIs later, implement URL-percent decoding here.
            return {};
        }

        return base64Decode(payload);
    }

    static std::vector<std::uint8_t> extractImageBytes(
        const fastgltf::Asset& gltf,
        const fastgltf::Image& image,
        const std::filesystem::path& baseDir)
    {
        std::vector<std::uint8_t> bytes;

        std::visit(fastgltf::visitor{
                       [&](const fastgltf::sources::BufferView& view)
                       {
                           const auto& bv = gltf.bufferViews[view.bufferViewIndex];
                           const auto& buf = gltf.buffers[bv.bufferIndex];

                           std::visit(fastgltf::visitor{
                                          [&](const fastgltf::sources::Array& a)
                                          {
                                              const auto* begin = reinterpret_cast<const std::uint8_t*>(a.bytes.data() +
                                                  bv.byteOffset);
                                              bytes.assign(begin, begin + bv.byteLength);
                                          },
                                          [&](const fastgltf::sources::Vector& v)
                                          {
                                              const auto* begin = reinterpret_cast<const std::uint8_t*>(v.bytes.data() +
                                                  bv.byteOffset);
                                              bytes.assign(begin, begin + bv.byteLength);
                                          },
                                          [&](const fastgltf::sources::ByteView& bvSrc)
                                          {
                                              const auto* begin = reinterpret_cast<const std::uint8_t*>(bvSrc.bytes.
                                                  data() + bv.byteOffset);
                                              bytes.assign(begin, begin + bv.byteLength);
                                          },
                                          [](auto&)
                                          {
                                          }
                                      }, buf.data);
                       },

                       [&](const fastgltf::sources::Array& a)
                       {
                           const auto* p = reinterpret_cast<const std::uint8_t*>(a.bytes.data());
                           bytes.assign(p, p + a.bytes.size());
                       },

                       [&](const fastgltf::sources::Vector& v)
                       {
                           const auto* p = reinterpret_cast<const std::uint8_t*>(v.bytes.data());
                           bytes.assign(p, p + v.bytes.size());
                       },

                       [&](const fastgltf::sources::ByteView& v)
                       {
                           const auto* p = reinterpret_cast<const std::uint8_t*>(v.bytes.data());
                           bytes.assign(p, p + v.bytes.size());
                       },

                       [&](const fastgltf::sources::URI& uriSrc)
                       {
                           const auto uri = getUriView(uriSrc);
                           if (!uri.valid())
                           {
                               return;
                           }

                           if (uri.isDataUri())
                           {
                               bytes = decodeDataUriBytes(uri.string());
                               return;
                           }

                           if (!uri.isLocalPath())
                           {
                               core::Logger::warn("[Model] Non-local image URI not supported: {}",
                                                  std::string(uri.string()));
                               return;
                           }

                           const std::filesystem::path p = baseDir / uri.fspath();
                           bytes = readFileBytes(p);
                       },

                       [](auto&)
                       {
                       }
                   }, image.data);

        return bytes;
    }


    std::unique_ptr<Model> Model::load(RHIRenderer& renderer, const std::filesystem::path& path, bool vertexPulling)
    {
        // Enable common texture extensions so assets using them do not fail parsing.
        fastgltf::Parser parser(
            fastgltf::Extensions::KHR_texture_basisu |
            fastgltf::Extensions::MSFT_texture_dds |
            fastgltf::Extensions::EXT_texture_webp |
            fastgltf::Extensions::KHR_materials_variants|
            fastgltf::Extensions::KHR_texture_transform|
            fastgltf::Extensions::KHR_materials_pbrSpecularGlossiness |
            fastgltf::Extensions::KHR_materials_clearcoat |
            fastgltf::Extensions::KHR_materials_sheen |
            fastgltf::Extensions::KHR_materials_specular |
            fastgltf::Extensions::KHR_materials_ior |
            fastgltf::Extensions::KHR_materials_unlit |
            fastgltf::Extensions::KHR_materials_transmission |
            fastgltf::Extensions::KHR_materials_volume |
            fastgltf::Extensions::KHR_materials_emissive_strength |
            fastgltf::Extensions::KHR_lights_punctual);

        auto data = fastgltf::GltfDataBuffer::FromPath(path);
        if (data.error() != fastgltf::Error::None)
        {
            core::Logger::error("Failed to load glTF file: {}", path.string());
            return nullptr;
        }

        const auto options =
            fastgltf::Options::LoadExternalBuffers |
            fastgltf::Options::LoadExternalImages;

        auto asset = parser.loadGltf(data.get(), path.parent_path(), options);
        if (asset.error() != fastgltf::Error::None)
        {
            core::Logger::error("Failed to parse glTF: Error Code {}", static_cast<int>(asset.error()));
            return nullptr;
        }

        auto& gltf = asset.get();
        auto model = std::make_unique<Model>();

        core::Logger::info("Loading Model (fastgltf): {} (Nodes: {})", path.string(), gltf.nodes.size());

        // --- Lights ---
        model->m_lights.reserve(gltf.lights.size());
        for (const auto& gLight : gltf.lights)
        {
            Light l{};
            l.m_name = gLight.name;
            l.m_color = glm::make_vec3(gLight.color.data());
            l.m_intensity = gLight.intensity;
            l.m_range = gLight.range.value_or(0.0f);

            if (gLight.type == fastgltf::LightType::Directional)
            {
                l.m_type = LightType::Directional;
            }
            else if (gLight.type == fastgltf::LightType::Point)
            {
                l.m_type = LightType::Point;
            }
            else if (gLight.type == fastgltf::LightType::Spot)
            {
                l.m_type = LightType::Spot;
                l.m_innerConeAngle = gLight.innerConeAngle.value_or(0.0f);
                l.m_outerConeAngle = gLight.outerConeAngle.value_or(0.785398f);
            }
            model->m_lights.push_back(l);
        }
        if (model->m_lights.empty())
        {
            model->m_lights.push_back({});
        }

        std::vector<bool> textureUsesSrgb(gltf.textures.size(), false);
        std::vector<bool> textureUsesLinear(gltf.textures.size(), false);
        auto markTextureUsage = [&](const auto& opt, bool srgb)
        {
            if (!opt.has_value()) { return; }
            const size_t texIdx = opt.value().textureIndex;
            if (texIdx >= gltf.textures.size()) { return; }
            if (srgb)
            {
                textureUsesSrgb[texIdx] = true;
            }
            else
            {
                textureUsesLinear[texIdx] = true;
            }
        };
        for (const auto& mat : gltf.materials)
        {
            markTextureUsage(mat.pbrData.baseColorTexture, true);
            markTextureUsage(mat.emissiveTexture, true);
            markTextureUsage(mat.pbrData.metallicRoughnessTexture, false);
            markTextureUsage(mat.normalTexture, false);
            markTextureUsage(mat.occlusionTexture, false);

#ifdef FASTGLTF_ENABLE_DEPRECATED_EXT
            if (mat.specularGlossiness)
            {
                const auto& sg = *mat.specularGlossiness;
                markTextureUsage(sg.diffuseTexture, true);
                markTextureUsage(sg.specularGlossinessTexture, true);
            }
#endif

            if (mat.clearcoat)
            {
                const auto& cc = *mat.clearcoat;
                markTextureUsage(cc.clearcoatTexture, true);
                markTextureUsage(cc.clearcoatRoughnessTexture, false);
                markTextureUsage(cc.clearcoatNormalTexture, false);
            }

            if (mat.sheen)
            {
                const auto& sh = *mat.sheen;
                markTextureUsage(sh.sheenColorTexture, true);
                markTextureUsage(sh.sheenRoughnessTexture, false);
            }

            if (mat.specular)
            {
                const auto& sp = *mat.specular;
                markTextureUsage(sp.specularTexture, true);
                markTextureUsage(sp.specularColorTexture, true);
            }

            if (mat.transmission)
            {
                const auto& tr = *mat.transmission;
                markTextureUsage(tr.transmissionTexture, true);
            }

            if (mat.volume)
            {
                const auto& vol = *mat.volume;
                markTextureUsage(vol.thicknessTexture, true);
            }
        }

        // --- Textures ---
        model->m_textures.reserve(gltf.textures.size());
        for (size_t texIdx = 0; texIdx < gltf.textures.size(); ++texIdx)
        {
            const auto& tex = gltf.textures[texIdx];
            const auto imgIndexOpt = pickImageIndex(tex);
            if (!imgIndexOpt)
            {
                model->m_textures.push_back(INVALID_TEXTURE_HANDLE);
                continue;
            }

            const auto& image = gltf.images[*imgIndexOpt];

            // Extract encoded bytes (supports BufferView/Vector/Array/URI).
            const auto encoded = extractImageBytes(gltf, image, path.parent_path());
            if (encoded.empty())
            {
                core::Logger::warn("[Model] Texture image had no bytes (imageIndex={})", *imgIndexOpt);
                model->m_textures.push_back(INVALID_TEXTURE_HANDLE);
                continue;
            }

            int w = 0;
            int h = 0;
            int comp = 0;
            stbi_uc* pixels = stbi_load_from_memory(
                encoded.data(),
                util::u32(encoded.size()),
                &w, &h, &comp, 4);

            if (!pixels)
            {
                // If your asset is KTX2/DDS, stb_image won't decode it (expected).
                core::Logger::warn("[Model] stb_image failed for imageIndex={}: {}",
                                   *imgIndexOpt,
                                   (stbi_failure_reason() ? stbi_failure_reason() : "unknown"));
                model->m_textures.push_back(INVALID_TEXTURE_HANDLE);
                continue;
            }

            bool srgb = textureUsesSrgb[texIdx] && !textureUsesLinear[texIdx];
            if (textureUsesSrgb[texIdx] && textureUsesLinear[texIdx])
            {
                core::Logger::warn("[Model] Texture {} used as sRGB and linear; using linear.", texIdx);
                srgb = false;
            }
            model->m_textures.push_back(renderer.createTexture(pixels, w, h, 4, srgb));
            stbi_image_free(pixels);
        }

        // --- Materials ---
        model->m_materials.reserve(gltf.materials.size());
        for (const auto& mat : gltf.materials)
        {
            MaterialData md{};
            md.m_isUnlit = mat.unlit;

            if (mat.specularGlossiness)
            {
                const auto& sg = *mat.specularGlossiness;
                md.m_isSpecularGlossiness = true;
                md.m_baseColorFactor = glm::make_vec4(sg.diffuseFactor.data());
                md.m_specularFactor = glm::make_vec3(sg.specularFactor.data());
                md.m_glossinessFactor = sg.glossinessFactor;

                if (sg.diffuseTexture.has_value())
                {
                    const auto& info = sg.diffuseTexture.value();
                    const size_t texIdx = info.textureIndex;
                    if (texIdx < model->m_textures.size())
                    {
                        md.m_baseColorTexture = model->m_textures[texIdx];
                    }
                    md.m_baseColorUV = getTexCoordIndex(info);
                    md.m_baseColorSampler = getSamplerAddressMode(gltf, texIdx);
                }

                if (sg.specularGlossinessTexture.has_value())
                {
                    const auto& info = sg.specularGlossinessTexture.value();
                    const size_t texIdx = info.textureIndex;
                    if (texIdx < model->m_textures.size())
                    {
                        md.m_metallicRoughnessTexture = model->m_textures[texIdx];
                    }
                    md.m_metallicRoughnessUV = getTexCoordIndex(info);
                    md.m_metallicRoughnessSampler = getSamplerAddressMode(gltf, texIdx);
                }
            }
            else
            {
                md.m_isSpecularGlossiness = false;
                const auto& pbr = mat.pbrData;
                md.m_baseColorFactor = glm::make_vec4(pbr.baseColorFactor.data());
                md.m_metallicFactor = pbr.metallicFactor;
                md.m_roughnessFactor = pbr.roughnessFactor;

                if (pbr.baseColorTexture.has_value())
                {
                    const auto& info = pbr.baseColorTexture.value();
                    const size_t texIdx = info.textureIndex;
                    if (texIdx < model->m_textures.size())
                    {
                        md.m_baseColorTexture = model->m_textures[texIdx];
                    }
                    md.m_baseColorUV = getTexCoordIndex(info);
                    md.m_baseColorSampler = getSamplerAddressMode(gltf, texIdx);
                }

                if (pbr.metallicRoughnessTexture.has_value())
                {
                    const auto& info = pbr.metallicRoughnessTexture.value();
                    const size_t texIdx = info.textureIndex;
                    if (texIdx < model->m_textures.size())
                    {
                        md.m_metallicRoughnessTexture = model->m_textures[texIdx];
                    }
                    md.m_metallicRoughnessUV = getTexCoordIndex(info);
                    md.m_metallicRoughnessSampler = getSamplerAddressMode(gltf, texIdx);
                }
            }

            md.m_emissiveFactor = glm::make_vec3(mat.emissiveFactor.data());

            md.m_alphaCutoff = mat.alphaCutoff;

            md.m_alphaMode = toAlphaMode(mat.alphaMode);

            md.m_ior = getOptionalFloat(mat.ior, md.m_ior);
            md.m_emissiveStrength = getOptionalFloat(mat.emissiveStrength, md.m_emissiveStrength);


            if (mat.normalTexture.has_value())

            {
                const auto& info = mat.normalTexture.value();

                const size_t texIdx = info.textureIndex;

                if (texIdx < model->m_textures.size())
                {
                    md.m_normalTexture = model->m_textures[texIdx];
                }

                md.m_normalUV = getTexCoordIndex(info);

                md.m_normalSampler = getSamplerAddressMode(gltf, texIdx);

                md.m_normalScale = getNormalScale(info);
            }


            if (mat.occlusionTexture.has_value())

            {
                const auto& info = mat.occlusionTexture.value();

                const size_t texIdx = info.textureIndex;

                if (texIdx < model->m_textures.size())
                {
                    md.m_occlusionTexture = model->m_textures[texIdx];
                }

                md.m_occlusionUV = getTexCoordIndex(info);

                md.m_occlusionSampler = getSamplerAddressMode(gltf, texIdx);

                md.m_occlusionStrength = getOcclusionStrength(info);
            }


            if (mat.emissiveTexture.has_value())

            {
                const auto& info = mat.emissiveTexture.value();

                const size_t texIdx = info.textureIndex;

                if (texIdx < model->m_textures.size())
                {
                    md.m_emissiveTexture = model->m_textures[texIdx];
                }

                md.m_emissiveUV = getTexCoordIndex(info);

                md.m_emissiveSampler = getSamplerAddressMode(gltf, texIdx);
            }

            if (mat.clearcoat)
            {
                const auto& cc = *mat.clearcoat;
                md.m_clearcoatFactor = cc.clearcoatFactor;
                md.m_clearcoatRoughnessFactor = cc.clearcoatRoughnessFactor;

                if (cc.clearcoatTexture.has_value())
                {
                    const auto& info = cc.clearcoatTexture.value();
                    const size_t texIdx = info.textureIndex;
                    if (texIdx < model->m_textures.size())
                    {
                        md.m_clearcoatTexture = model->m_textures[texIdx];
                    }
                    md.m_clearcoatUV = getTexCoordIndex(info);
                    md.m_clearcoatSampler = getSamplerAddressMode(gltf, texIdx);
                }
                if (cc.clearcoatRoughnessTexture.has_value())
                {
                    const auto& info = cc.clearcoatRoughnessTexture.value();
                    const size_t texIdx = info.textureIndex;
                    if (texIdx < model->m_textures.size())
                    {
                        md.m_clearcoatRoughnessTexture = model->m_textures[texIdx];
                    }
                    md.m_clearcoatRoughnessUV = getTexCoordIndex(info);
                    md.m_clearcoatRoughnessSampler = getSamplerAddressMode(gltf, texIdx);
                }

                if (cc.clearcoatNormalTexture.has_value())
                {
                    const auto& info = cc.clearcoatNormalTexture.value();
                    const size_t texIdx = info.textureIndex;
                    if (texIdx < model->m_textures.size())
                    {
                        md.m_clearcoatNormalTexture = model->m_textures[texIdx];
                    }
                    md.m_clearcoatNormalUV = getTexCoordIndex(info);
                    md.m_clearcoatNormalSampler = getSamplerAddressMode(gltf, texIdx);
                    md.m_clearcoatNormalScale = getNormalScale(info);
                }
            }

            if (mat.sheen)
            {
                const auto& sh = *mat.sheen;
                md.m_sheenColorFactor = glm::make_vec3(sh.sheenColorFactor.data());
                md.m_sheenRoughnessFactor = sh.sheenRoughnessFactor;

                if (sh.sheenColorTexture.has_value())
                {
                    const auto& info = sh.sheenColorTexture.value();
                    const size_t texIdx = info.textureIndex;
                    if (texIdx < model->m_textures.size())
                    {
                        md.m_sheenColorTexture = model->m_textures[texIdx];
                    }
                    md.m_sheenColorUV = getTexCoordIndex(info);
                    md.m_sheenColorSampler = getSamplerAddressMode(gltf, texIdx);
                }

                if (sh.sheenRoughnessTexture.has_value())
                {
                    const auto& info = sh.sheenRoughnessTexture.value();
                    const size_t texIdx = info.textureIndex;
                    if (texIdx < model->m_textures.size())
                    {
                        md.m_sheenRoughnessTexture = model->m_textures[texIdx];
                    }
                    md.m_sheenRoughnessUV = getTexCoordIndex(info);
                    md.m_sheenRoughnessSampler = getSamplerAddressMode(gltf, texIdx);
                }
            }

            if (mat.specular)
            {
                const auto& sp = *mat.specular;
                md.m_hasSpecular = true;
                md.m_specularFactorScalar = sp.specularFactor;
                md.m_specularColorFactor = glm::make_vec3(sp.specularColorFactor.data());

                if (sp.specularTexture.has_value())
                {
                    const auto& info = sp.specularTexture.value();
                    const size_t texIdx = info.textureIndex;
                    if (texIdx < model->m_textures.size())
                    {
                        md.m_specularTexture = model->m_textures[texIdx];
                    }
                    md.m_specularUV = getTexCoordIndex(info);
                    md.m_specularSampler = getSamplerAddressMode(gltf, texIdx);
                }

                if (sp.specularColorTexture.has_value())
                {
                    const auto& info = sp.specularColorTexture.value();
                    const size_t texIdx = info.textureIndex;
                    if (texIdx < model->m_textures.size())
                    {
                        md.m_specularColorTexture = model->m_textures[texIdx];
                    }
                    md.m_specularColorUV = getTexCoordIndex(info);
                    md.m_specularColorSampler = getSamplerAddressMode(gltf, texIdx);
                }
            }

            if (mat.transmission)
            {
                const auto& tr = *mat.transmission;
                md.m_transmissionFactor = tr.transmissionFactor;

                if (tr.transmissionTexture.has_value())
                {
                    const auto& info = tr.transmissionTexture.value();
                    const size_t texIdx = info.textureIndex;
                    if (texIdx < model->m_textures.size())
                    {
                        md.m_transmissionTexture = model->m_textures[texIdx];
                    }
                    md.m_transmissionUV = getTexCoordIndex(info);
                    md.m_transmissionSampler = getSamplerAddressMode(gltf, texIdx);
                }
            }

            if (mat.volume)
            {
                const auto& vol = *mat.volume;
                md.m_volumeThicknessFactor = vol.thicknessFactor;
                md.m_volumeAttenuationDistance =
                    resolveOptionalFloat(vol.attenuationDistance, std::numeric_limits<float>::infinity());
                md.m_volumeAttenuationColor = glm::make_vec3(vol.attenuationColor.data());

                if (vol.thicknessTexture.has_value())
                {
                    const auto& info = vol.thicknessTexture.value();
                    const size_t texIdx = info.textureIndex;
                    if (texIdx < model->m_textures.size())
                    {
                        md.m_volumeThicknessTexture = model->m_textures[texIdx];
                    }
                    md.m_volumeThicknessUV = getTexCoordIndex(info);
                    md.m_volumeThicknessSampler = getSamplerAddressMode(gltf, texIdx);
                }
            }

            model->m_materials.push_back(md);
        }


        if (model->m_materials.empty())
        {
            model->m_materials.push_back({});
        }

        // --- Nodes & Meshes ---
        model->m_nodes.resize(gltf.nodes.size());
        for (auto& n : model->m_nodes)
        {
            n.m_parentIndex = -1;
        }

        // Pass 1: fill node basics + children lists
        for (size_t i = 0; i < gltf.nodes.size(); ++i)
        {
            const auto& gNode = gltf.nodes[i];
            auto& myNode = model->m_nodes[i];

            myNode.m_name = gNode.name;
            myNode.m_localTransform = toTransform(gNode);
            myNode.m_lightIndex = gNode.lightIndex.has_value() ? static_cast<int>(gNode.lightIndex.value()) : -1;

            myNode.m_children.clear();
            myNode.m_children.reserve(gNode.children.size());
            for (const auto& childIdx : gNode.children)
            {
                myNode.m_children.push_back(util::u32(childIdx));
            }
        }

        // Pass 2: set parents (ensures children nodes exist)
        for (size_t i = 0; i < model->m_nodes.size(); ++i)
        {
            for (int child : model->m_nodes[i].m_children)
            {
                if (child >= 0 && util::sz(child) < model->m_nodes.size())
                {
                    model->m_nodes[child].m_parentIndex = util::sz(i);
                }
            }
        }

        // Mesh primitives
        for (size_t i = 0; i < gltf.nodes.size(); ++i)
        {
            const auto& gNode = gltf.nodes[i];
            auto& myNode = model->m_nodes[i];

            if (!gNode.meshIndex.has_value())
            {
                continue;
            }

            const auto& gMesh = gltf.meshes[gNode.meshIndex.value()];
            for (const auto& gPrim : gMesh.primitives)
            {
                std::vector<Vertex> vertices;
                std::vector<uint32_t> indices;

                const auto* itPos = gPrim.findAttribute("POSITION");
                if (itPos == gPrim.attributes.end())
                {
                    continue;
                }

                const auto& posAccessor = gltf.accessors[itPos->accessorIndex];
                vertices.resize(posAccessor.count);

                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor,
                                                              [&](glm::vec3 pos, size_t idx)
                                                              {
                                                                  vertices[idx].m_position = pos;
                                                                  vertices[idx].m_color = glm::vec3(1.0F);
                                                                  vertices[idx].m_normal = glm::vec3(0.0F, 1.0F, 0.0F);
                                                                  vertices[idx].m_texCoord0 = glm::vec2(0.0F);
                                                                  vertices[idx].m_texCoord1 = glm::vec2(0.0F);
                                                                  vertices[idx].m_tangent = glm::vec4(0.0F);
                                                              });

                if (const auto* it = gPrim.findAttribute("COLOR_0"); it != gPrim.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[it->accessorIndex],
                                                                  [&](glm::vec3 col, size_t idx)
                                                                  {
                                                                      vertices[idx].m_color = col;
                                                                  });
                }

                if (const auto* it = gPrim.findAttribute("NORMAL"); it != gPrim.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[it->accessorIndex],
                                                                  [&](glm::vec3 norm, size_t idx)
                                                                  {
                                                                      vertices[idx].m_normal = norm;
                                                                  });
                }

                if (const auto* it = gPrim.findAttribute("TEXCOORD_0"); it != gPrim.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[it->accessorIndex],
                                                                  [&](glm::vec2 uv, size_t idx)
                                                                  {
                                                                      vertices[idx].m_texCoord0 = uv;
                                                                  });
                }

                if (const auto* it = gPrim.findAttribute("TEXCOORD_1"); it != gPrim.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[it->accessorIndex],
                                                                  [&](glm::vec2 uv, size_t idx)
                                                                  {
                                                                      vertices[idx].m_texCoord1 = uv;
                                                                  });
                }

                // Load Tangents
                if (const auto* it = gPrim.findAttribute("TANGENT"); it != gPrim.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[it->accessorIndex],
                                                                  [&](glm::vec4 tan, size_t idx)
                                                                  {
                                                                      vertices[idx].m_tangent = tan;
                                                                  });
                }

                if (const auto* it = gPrim.findAttribute("JOINTS_0"); it != gPrim.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::uvec4>(gltf, gltf.accessors[it->accessorIndex],
                                                                  [&](glm::uvec4 joints, size_t idx)
                                                                  {
                                                                      vertices[idx].m_joints = joints;
                                                                  });
                }

                if (const auto* it = gPrim.findAttribute("WEIGHTS_0"); it != gPrim.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[it->accessorIndex],
                                                                  [&](glm::vec4 weights, size_t idx)
                                                                  {
                                                                      vertices[idx].m_weights = weights;
                                                                  });
                }

                // Indices: widen to uint32_t regardless of source component type.
                if (gPrim.indicesAccessor.has_value())
                {
                    const auto& acc = gltf.accessors[gPrim.indicesAccessor.value()];
                    switch (acc.componentType)
                    {
                    case fastgltf::ComponentType::UnsignedByte:
                        fastgltf::iterateAccessor<std::uint8_t>(
                            gltf, acc, [&](std::uint8_t v) { indices.push_back(v); });
                        break;
                    case fastgltf::ComponentType::UnsignedShort:
                        fastgltf::iterateAccessor<std::uint16_t>(gltf, acc, [&](std::uint16_t v)
                        {
                            indices.push_back(v);
                        });
                        break;
                    default:
                        fastgltf::iterateAccessor<std::uint32_t>(gltf, acc, [&](std::uint32_t v)
                        {
                            indices.push_back(v);
                        });
                        break;
                    }
                }
                else
                {
                    indices.resize(vertices.size());
                    for (size_t k = 0; k < vertices.size(); ++k)
                    {
                        indices[k] = util::u32(k);
                    }
                }

                MeshPrimitive prim{};
                prim.m_mesh = renderer.createMesh(vertices, indices, vertexPulling);
                prim.m_materialIndex = gPrim.materialIndex.has_value()
                                           ? util::u32(gPrim.materialIndex.value())
                                           : 0;
                prim.m_vertexBufferAddress = renderer.getMeshVertexBufferAddress(prim.m_mesh);

                myNode.m_meshPrimitives.push_back(prim);
            }
        }

        // --- Skins ---
        model->m_skins.reserve(gltf.skins.size());
        for (const auto& gSkin : gltf.skins)
        {
            Skin skin;
            skin.name = gSkin.name;
            skin.skeletonRootNode = gSkin.skeleton.has_value() ? static_cast<int>(gSkin.skeleton.value()) : -1;

            skin.joints.reserve(gSkin.joints.size());
            for (auto jointIdx : gSkin.joints)
            {
                skin.joints.push_back(static_cast<uint32_t>(jointIdx));
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
                skin.inverseBindMatrices.assign(skin.joints.size(), glm::mat4(1.0f));
            }
            model->m_skins.push_back(std::move(skin));
        }

        // --- Animations ---
        model->m_animations.reserve(gltf.animations.size());
        for (const auto& gAnim : gltf.animations)
        {
            Animation anim;
            anim.name = gAnim.name;

            for (const auto& gSampler : gAnim.samplers)
            {
                AnimationSampler sampler;
                if (gSampler.interpolation == fastgltf::AnimationInterpolation::Linear)
                    sampler.interpolation = InterpolationType::Linear;
                else if (gSampler.interpolation == fastgltf::AnimationInterpolation::Step)
                    sampler.interpolation = InterpolationType::Step;
                else sampler.interpolation = InterpolationType::CubicSpline;

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
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, outputAcc, [&](glm::vec3 v, size_t idx)
                    {
                        sampler.outputs[idx] = glm::vec4(v, 0.0f);
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
                if (!gChannel.nodeIndex.has_value()) continue;
                AnimationChannel channel;
                channel.samplerIndex = static_cast<int>(gChannel.samplerIndex);
                channel.targetNode = static_cast<uint32_t>(gChannel.nodeIndex.value());
                if (gChannel.path == fastgltf::AnimationPath::Translation) channel.path = AnimationPath::Translation;
                else if (gChannel.path == fastgltf::AnimationPath::Rotation) channel.path = AnimationPath::Rotation;
                else if (gChannel.path == fastgltf::AnimationPath::Scale) channel.path = AnimationPath::Scale;
                else if (gChannel.path == fastgltf::AnimationPath::Weights) channel.path = AnimationPath::Weights;
                anim.channels.push_back(channel);
            }
            model->m_animations.push_back(std::move(anim));
        }

        model->m_rootNodes.clear();
        for (size_t i = 0; i < model->m_nodes.size(); ++i)
        {
            if (model->m_nodes[i].m_parentIndex == -1)
            {
                model->m_rootNodes.push_back(static_cast<int>(i));
            }
        }

        model->updateTransforms();
        return model;
    }

    // Helper to recursively update matrices
    static void updateNodeHierarchy(Model& model, int nodeIndex, const glm::mat4& parentMatrix)
    {
        auto& node = model.nodesMutable()[nodeIndex];

        // Reconstruct local matrix from TRS components
        glm::mat4 T = glm::translate(glm::mat4(1.0f), node.m_localTransform.m_translation);
        glm::mat4 R = glm::toMat4(node.m_localTransform.m_rotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), node.m_localTransform.m_scale);

        // Update local matrix cache
        node.m_localTransform.m_matrix = T * R * S;

        // Calculate World Matrix
        node.m_worldTransform.m_matrix = parentMatrix * node.m_localTransform.m_matrix;

        // Recurse to children
        for (int childIndex : node.m_children)
        {
            updateNodeHierarchy(model, childIndex, node.m_worldTransform.m_matrix);
        }
    }

    void Model::updateTransforms()
    {
        glm::mat4 identity(1.0f);
        for (int rootIndex : m_rootNodes)
        {
            updateNodeHierarchy(*this, rootIndex, identity);
        }
    }
} // namespace pnkr::renderer::scene
