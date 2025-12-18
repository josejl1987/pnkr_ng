// Model.cpp

#include "pnkr/renderer/scene/Model.hpp"
#include "pnkr/renderer/renderer.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/core/logger.hpp"

// fastgltf includes
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
#include <string>
#include <vector>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/vec4.hpp>

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
        std::ifstream f(p, std::ios::binary);
        if (!f) return {};

        f.seekg(0, std::ios::end);
        const auto sz = static_cast<size_t>(f.tellg());
        f.seekg(0, std::ios::beg);

        std::vector<std::uint8_t> out(sz);
        if (sz) f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(sz));
        return out;
    }

    static std::optional<std::size_t> pickImageIndex(const fastgltf::Texture& tex)
    {
        // Prefer standard images first, then extension-backed images.
        if (tex.imageIndex.has_value()) return tex.imageIndex.value();
        if (tex.webpImageIndex.has_value()) return tex.webpImageIndex.value();
        if (tex.ddsImageIndex.has_value()) return tex.ddsImageIndex.value();
        if (tex.basisuImageIndex.has_value()) return tex.basisuImageIndex.value();
        return std::nullopt;
    }

    static fastgltf::URIView getUriView(const fastgltf::sources::URI& u)
    {
        // fastgltf has had a few API variations here; keep this resilient.
        if constexpr (requires { u.uri; }) {
            // In some versions, u.uri is a fastgltf::URI / URIView-like.
            if constexpr (requires { u.uri.string(); }) {
                return fastgltf::URIView{u.uri.string()};
            } else if constexpr (requires { u.uri.c_str(); }) {
                return fastgltf::URIView{std::string_view{u.uri.c_str()}};
            } else {
                return fastgltf::URIView{};
            }
        } else if constexpr (requires { u.uri; }) {
            return u.uri;
        } else {
            return fastgltf::URIView{};
        }
    }

    template <class T>
    static void assignFromBytesLike(std::vector<std::uint8_t>& dst, const T& src)
    {
        if constexpr (requires { src.data(); src.size(); }) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(src.data());
            dst.assign(p, p + src.size());
        }
    }

static std::vector<std::uint8_t> base64Decode(std::string_view in)
{
    static constexpr std::array<std::uint8_t, 256> kDec = [] {
        std::array<std::uint8_t, 256> t{};
        t.fill(0xFF);
        for (int i = 'A'; i <= 'Z'; ++i) t[static_cast<std::uint8_t>(i)] = static_cast<std::uint8_t>(i - 'A');
        for (int i = 'a'; i <= 'z'; ++i) t[static_cast<std::uint8_t>(i)] = static_cast<std::uint8_t>(26 + i - 'a');
        for (int i = '0'; i <= '9'; ++i) t[static_cast<std::uint8_t>(i)] = static_cast<std::uint8_t>(52 + i - '0');
        t[static_cast<std::uint8_t>('+')] = 62;
        t[static_cast<std::uint8_t>('/')] = 63;
        return t;
    }();

    std::vector<std::uint8_t> out;
    out.reserve((in.size() * 3) / 4);

    std::uint32_t buf = 0;
    int bits = 0;
    int pad = 0;

    for (unsigned char c : in) {
        if (std::isspace(c)) continue;
        if (c == '=') { pad++; continue; }

        const std::uint8_t v = kDec[c];
        if (v == 0xFF) continue; // ignore non-base64 chars defensively

        buf = (buf << 6) | v;
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buf >> bits) & 0xFFu));
        }
    }

    // Trim padding bytes if present
    if (pad > 0 && out.size() >= static_cast<size_t>(pad)) {
        out.resize(out.size() - static_cast<size_t>(pad));
    }

    return out;
}

static std::vector<std::uint8_t> decodeDataUriBytes(std::string_view uri)
{
    // Expected form: data:[<mime>][;base64],<payload>
    if (!uri.starts_with("data:")) return {};

    const auto comma = uri.find(',');
    if (comma == std::string_view::npos) return {};

    const std::string_view meta = uri.substr(5, comma - 5);
    const std::string_view payload = uri.substr(comma + 1);

    const bool isBase64 = (meta.find(";base64") != std::string_view::npos);
    if (!isBase64) {
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
        [&](const fastgltf::sources::BufferView& view) {
            const auto& bv = gltf.bufferViews[view.bufferViewIndex];
            const auto& buf = gltf.buffers[bv.bufferIndex];

            std::visit(fastgltf::visitor{
                [&](const fastgltf::sources::Array& a) {
                    const auto* begin = reinterpret_cast<const std::uint8_t*>(a.bytes.data() + bv.byteOffset);
                    bytes.assign(begin, begin + bv.byteLength);
                },
                [&](const fastgltf::sources::Vector& v) {
                    const auto* begin = reinterpret_cast<const std::uint8_t*>(v.bytes.data() + bv.byteOffset);
                    bytes.assign(begin, begin + bv.byteLength);
                },
                [&](const fastgltf::sources::ByteView& bvSrc) {
                    const auto* begin = reinterpret_cast<const std::uint8_t*>(bvSrc.bytes.data() + bv.byteOffset);
                    bytes.assign(begin, begin + bv.byteLength);
                },
                [](auto&) {}
            }, buf.data);
        },

        [&](const fastgltf::sources::Array& a) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(a.bytes.data());
            bytes.assign(p, p + a.bytes.size());
        },

        [&](const fastgltf::sources::Vector& v) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(v.bytes.data());
            bytes.assign(p, p + v.bytes.size());
        },

        [&](const fastgltf::sources::ByteView& v) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(v.bytes.data());
            bytes.assign(p, p + v.bytes.size());
        },

        [&](const fastgltf::sources::URI& uriSrc) {
            const auto uri = getUriView(uriSrc);
            if (!uri.valid()) return;

            if (uri.isDataUri()) {
                bytes = decodeDataUriBytes(uri.string());
                return;
            }

            if (!uri.isLocalPath()) {
                core::Logger::warn("[Model] Non-local image URI not supported: {}", std::string(uri.string()));
                return;
            }

            const std::filesystem::path p = baseDir / uri.fspath();
            bytes = readFileBytes(p);
        },

        [](auto&) {}
    }, image.data);

    return bytes;
}


    std::unique_ptr<Model> Model::load(RHIRenderer& renderer, const std::filesystem::path& path)
    {
        // Enable common texture extensions so assets using them do not fail parsing.
        fastgltf::Parser parser(
            fastgltf::Extensions::KHR_texture_basisu |
            fastgltf::Extensions::MSFT_texture_dds |
            fastgltf::Extensions::EXT_texture_webp);

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

        // --- Textures ---
        model->m_textures.reserve(gltf.textures.size());
        for (const auto& tex : gltf.textures)
        {
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

            int w = 0, h = 0, comp = 0;
            stbi_uc* pixels = stbi_load_from_memory(
                reinterpret_cast<const stbi_uc*>(encoded.data()),
                static_cast<int>(encoded.size()),
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

            model->m_textures.push_back(renderer.createTexture(pixels, w, h, 4, true));
            stbi_image_free(pixels);
        }

        // --- Materials ---
        model->m_materials.reserve(gltf.materials.size());
        for (const auto& mat : gltf.materials)
        {
            MaterialData md{};
            const auto& pbr = mat.pbrData;

            md.baseColorFactor = glm::make_vec4(pbr.baseColorFactor.data());

            if (pbr.baseColorTexture.has_value())
            {
                const size_t texIdx = pbr.baseColorTexture.value().textureIndex;
                if (texIdx < model->m_textures.size())
                    md.baseColorTexture = model->m_textures[texIdx];
            }

            model->m_materials.push_back(md);
        }
        if (model->m_materials.empty())
            model->m_materials.push_back({});

        // --- Nodes & Meshes ---
        model->m_nodes.resize(gltf.nodes.size());
        for (auto& n : model->m_nodes) n.parentIndex = -1;

        // Pass 1: fill node basics + children lists
        for (size_t i = 0; i < gltf.nodes.size(); ++i)
        {
            const auto& gNode = gltf.nodes[i];
            auto& myNode = model->m_nodes[i];

            myNode.name = gNode.name;
            myNode.localTransform = toTransform(gNode);

            myNode.children.clear();
            myNode.children.reserve(gNode.children.size());
            for (const auto& childIdx : gNode.children)
                myNode.children.push_back(static_cast<int>(childIdx));
        }

        // Pass 2: set parents (ensures children nodes exist)
        for (size_t i = 0; i < model->m_nodes.size(); ++i)
        {
            for (int child : model->m_nodes[i].children)
            {
                if (child >= 0 && static_cast<size_t>(child) < model->m_nodes.size())
                    model->m_nodes[child].parentIndex = static_cast<int>(i);
            }
        }

        // Mesh primitives
        for (size_t i = 0; i < gltf.nodes.size(); ++i)
        {
            const auto& gNode = gltf.nodes[i];
            auto& myNode = model->m_nodes[i];

            if (!gNode.meshIndex.has_value())
                continue;

            const auto& gMesh = gltf.meshes[gNode.meshIndex.value()];
            for (const auto& gPrim : gMesh.primitives)
            {
                std::vector<Vertex> vertices;
                std::vector<uint32_t> indices;

                auto itPos = gPrim.findAttribute("POSITION");
                if (itPos == gPrim.attributes.end())
                    continue;

                const auto& posAccessor = gltf.accessors[itPos->accessorIndex];
                vertices.resize(posAccessor.count);

                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor,
                                                             [&](glm::vec3 pos, size_t idx)
                                                             {
                                                                 vertices[idx].m_position = pos;
                                                                 vertices[idx].m_color = glm::vec3(1.0f);
                                                                 vertices[idx].m_normal = glm::vec3(0.0f, 1.0f, 0.0f);
                                                                 vertices[idx].m_texCoord = glm::vec2(0.0f);
                                                                 vertices[idx].m_tangent = glm::vec4(0.0f); // Init tangent
                                                             });

                if (auto it = gPrim.findAttribute("NORMAL"); it != gPrim.attributes.end()) {
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[it->accessorIndex],
                                                                 [&](glm::vec3 norm, size_t idx)
                                                                 {
                                                                     vertices[idx].m_normal = norm;
                                                                 });
                }

                if (auto it = gPrim.findAttribute("TEXCOORD_0"); it != gPrim.attributes.end()) {
                    fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[it->accessorIndex],
                                                                 [&](glm::vec2 uv, size_t idx)
                                                                 {
                                                                     vertices[idx].m_texCoord = uv;
                                                                 });
                }

                // Load Tangents
                if (auto it = gPrim.findAttribute("TANGENT"); it != gPrim.attributes.end()) {
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
                            fastgltf::iterateAccessor<std::uint8_t>(gltf, acc, [&](std::uint8_t v) { indices.push_back(v); });
                            break;
                        case fastgltf::ComponentType::UnsignedShort:
                            fastgltf::iterateAccessor<std::uint16_t>(gltf, acc, [&](std::uint16_t v) { indices.push_back(v); });
                            break;
                        default:
                            fastgltf::iterateAccessor<std::uint32_t>(gltf, acc, [&](std::uint32_t v) { indices.push_back(v); });
                            break;
                    }
                }
                else
                {
                    indices.resize(vertices.size());
                    for (size_t k = 0; k < vertices.size(); ++k)
                        indices[k] = static_cast<std::uint32_t>(k);
                }

                MeshPrimitive prim{};
                prim.mesh = renderer.createMesh(vertices, indices);
                prim.materialIndex = gPrim.materialIndex.has_value()
                                         ? static_cast<std::uint32_t>(gPrim.materialIndex.value())
                                         : 0;

                myNode.meshPrimitives.push_back(prim);
            }
        }

        model->m_rootNodes.clear();
        for (size_t i = 0; i < model->m_nodes.size(); ++i)
        {
            if (model->m_nodes[i].parentIndex == -1)
                model->m_rootNodes.push_back(static_cast<int>(i));
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

            const glm::mat4 localMat = node.localTransform.mat4();
            const glm::mat4 worldMat = parentMat * localMat;

            glm::vec3 s{}, t{}, skew{};
            glm::vec4 p{};
            glm::quat r{};

            glm::decompose(worldMat, s, r, t, skew, p);

            node.worldTransform.m_translation = t;
            node.worldTransform.m_rotation = r;
            node.worldTransform.m_scale = s;

            for (int child : node.children)
                updateNode(child, worldMat);
        };

        for (int root : m_rootNodes)
            updateNode(root, glm::mat4(1.0f));
    }
} // namespace pnkr::renderer::scene
