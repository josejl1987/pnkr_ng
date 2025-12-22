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
#include <variant>

#include <filesystem>
#include <fstream>
#include <optional>
#include <stb_image.h>
#include <string>
#include <vector>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/vec4.hpp>

#include "pnkr/core/common.hpp"

namespace pnkr::renderer::scene
{
    static Transform toTransform(const fastgltf::Node& node)
    {
        Transform t{};
        std::visit(fastgltf::visitor{
                       [&](const fastgltf::TRS& trs)
                       {
                           t.m_translation = glm::make_vec3(trs.translation.data());
                           t.m_rotation = glm::make_quat(trs.rotation.data());
                           t.m_scale = glm::make_vec3(trs.scale.data());
                       },
                       [&](const fastgltf::math::fmat4x4& mat)
                       {
                           glm::mat4 m = glm::make_mat4(mat.data());
                           glm::vec3 skew{};
                           glm::vec4 perspective{};
                           glm::decompose(m, t.m_scale, t.m_rotation, t.m_translation, skew, perspective);
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
            fastgltf::Extensions::KHR_materials_pbrSpecularGlossiness);

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

    void Model::updateTransforms()
    {
        std::function<void(int, const glm::mat4&)> updateNode =
            [&](int nodeIdx, const glm::mat4& parentMat)
        {
            auto& node = m_nodes[nodeIdx];

            const glm::mat4 localMat = node.m_localTransform.mat4();
            const glm::mat4 worldMat = parentMat * localMat;

            glm::vec3 s{};
            glm::vec3 t{};
            glm::vec3 skew{};
            glm::vec4 p{};
            glm::quat r{};

            glm::decompose(worldMat, s, r, t, skew, p);

            node.m_worldTransform.m_translation = t;
            node.m_worldTransform.m_rotation = r;
            node.m_worldTransform.m_scale = s;

            for (int child : node.m_children)
            {
                updateNode(child, worldMat);
            }
        };

        for (int root : m_rootNodes)
        {
            updateNode(root, glm::mat4(1.0F));
        }
    }
} // namespace pnkr::renderer::scene
