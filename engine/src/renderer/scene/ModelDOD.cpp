#include "pnkr/renderer/scene/ModelDOD.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/common.hpp"

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

namespace pnkr::renderer::scene
{

    static std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& p)
    {
        std::ifstream file(p, std::ios::binary);
        if (!file) return {};
        file.seekg(0, std::ios::end);
        const auto size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> out(size);
        if (size != 0u) file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
        return out;
    }

    static std::optional<std::size_t> pickImageIndex(const fastgltf::Texture& texture)
    {
        if (texture.imageIndex.has_value()) return texture.imageIndex.value();
        if (texture.webpImageIndex.has_value()) return texture.webpImageIndex.value();
        if (texture.ddsImageIndex.has_value()) return texture.ddsImageIndex.value();
        if (texture.basisuImageIndex.has_value()) return texture.basisuImageIndex.value();
        return std::nullopt;
    }

    static fastgltf::URIView getUriView(const fastgltf::sources::URI& uri)
    {
        if constexpr (requires { uri.uri; }) {
            if constexpr (requires { uri.uri.string(); }) return fastgltf::URIView{uri.uri.string()};
            else if constexpr (requires { uri.uri.c_str(); }) return fastgltf::URIView{std::string_view{uri.uri.c_str()}};
            else return fastgltf::URIView{};
        } else return fastgltf::URIView{};
    }

    static rhi::SamplerAddressMode toAddressMode(fastgltf::Wrap wrap)
    {
        switch (wrap) {
            case fastgltf::Wrap::Repeat: return rhi::SamplerAddressMode::Repeat;
            case fastgltf::Wrap::MirroredRepeat: return rhi::SamplerAddressMode::MirroredRepeat;
            case fastgltf::Wrap::ClampToEdge: return rhi::SamplerAddressMode::ClampToEdge;
            default: return rhi::SamplerAddressMode::Repeat;
        }
    }

    template <typename T>
    static uint32_t getTexCoordIndex(const T& info)
    {
        if constexpr (requires { info.texCoord; }) return util::u32(info.texCoord);
        else if constexpr (requires { info.texCoordIndex; }) return util::u32(info.texCoordIndex);
        else return 0;
    }

    static rhi::SamplerAddressMode getSamplerAddressMode(const fastgltf::Asset& gltf, size_t textureIndex)
    {
        if (textureIndex >= gltf.textures.size()) return rhi::SamplerAddressMode::Repeat;
        const auto& tex = gltf.textures[textureIndex];
        if (!tex.samplerIndex.has_value() || tex.samplerIndex.value() >= gltf.samplers.size()) return rhi::SamplerAddressMode::Repeat;
        const auto& sampler = gltf.samplers[tex.samplerIndex.value()];
        return toAddressMode(sampler.wrapS);
    }

    static uint32_t toAlphaMode(fastgltf::AlphaMode mode)
    {
        switch (mode) {
            case fastgltf::AlphaMode::Mask: return 1u;
            case fastgltf::AlphaMode::Blend: return 2u;
            case fastgltf::AlphaMode::Opaque: default: return 0u;
        }
    }

    static std::vector<std::uint8_t> base64Decode(std::string_view in)
    {
        static constexpr std::array<std::uint8_t, 256> kDec = [] {
            std::array<std::uint8_t, 256> t{}; t.fill(0xFF);
            for (int i = 'A'; i <= 'Z'; ++i) t[static_cast<std::uint8_t>(i)] = static_cast<std::uint8_t>(i - 'A');
            for (int i = 'a'; i <= 'z'; ++i) t[static_cast<std::uint8_t>(i)] = static_cast<std::uint8_t>(26 + i - 'a');
            for (int i = '0'; i <= '9'; ++i) t[static_cast<std::uint8_t>(i)] = static_cast<std::uint8_t>(52 + i - '0');
            t[static_cast<std::uint8_t>('+')] = 62; t[static_cast<std::uint8_t>('/')] = 63;
            return t;
        }();
        std::vector<std::uint8_t> out; out.reserve((in.size() * 3) / 4);
        std::uint32_t buf = 0; int bits = 0; int pad = 0;
        for (unsigned char c : in) {
            if (std::isspace(c) != 0) continue;
            if (c == '=') { pad++; continue; }
            const std::uint8_t v = kDec[c];
            if (v == 0xFF) continue;
            buf = (buf << 6) | v; bits += 6;
            if (bits >= 8) { bits -= 8; out.push_back(static_cast<std::uint8_t>((buf >> bits) & 0xFFU)); }
        }
        if (pad > 0 && out.size() >= static_cast<size_t>(pad)) out.resize(out.size() - static_cast<size_t>(pad));
        return out;
    }

    static std::vector<std::uint8_t> decodeDataUriBytes(std::string_view uri)
    {
        if (!uri.starts_with("data:")) return {};
        const auto comma = uri.find(',');
        if (comma == std::string_view::npos) return {};
        const std::string_view meta = uri.substr(5, comma - 5);
        const std::string_view payload = uri.substr(comma + 1);
        if (meta.find(";base64") == std::string_view::npos) return {};
        return base64Decode(payload);
    }

    static std::vector<std::uint8_t> extractImageBytes(const fastgltf::Asset& gltf, const fastgltf::Image& image, const std::filesystem::path& baseDir)
    {
        std::vector<std::uint8_t> bytes;
        std::visit(fastgltf::visitor{
            [&](const fastgltf::sources::BufferView& view) {
                const auto& bv = gltf.bufferViews[view.bufferViewIndex];
                const auto& buf = gltf.buffers[bv.bufferIndex];
                std::visit(fastgltf::visitor{
                    [&](const fastgltf::sources::Array& a) { bytes.assign(reinterpret_cast<const std::uint8_t*>(a.bytes.data() + bv.byteOffset), reinterpret_cast<const std::uint8_t*>(a.bytes.data() + bv.byteOffset) + bv.byteLength); },
                    [&](const fastgltf::sources::Vector& v) { bytes.assign(reinterpret_cast<const std::uint8_t*>(v.bytes.data() + bv.byteOffset), reinterpret_cast<const std::uint8_t*>(v.bytes.data() + bv.byteOffset) + bv.byteLength); },
                    [&](const fastgltf::sources::ByteView& bvSrc) { bytes.assign(reinterpret_cast<const std::uint8_t*>(bvSrc.bytes.data() + bv.byteOffset), reinterpret_cast<const std::uint8_t*>(bvSrc.bytes.data() + bv.byteOffset) + bv.byteLength); },
                    [](auto&) {}
                }, buf.data);
            },
            [&](const fastgltf::sources::Array& a) { bytes.assign(reinterpret_cast<const std::uint8_t*>(a.bytes.data()), reinterpret_cast<const std::uint8_t*>(a.bytes.data()) + a.bytes.size()); },
            [&](const fastgltf::sources::Vector& v) { bytes.assign(reinterpret_cast<const std::uint8_t*>(v.bytes.data()), reinterpret_cast<const std::uint8_t*>(v.bytes.data()) + v.bytes.size()); },
            [&](const fastgltf::sources::ByteView& v) { bytes.assign(reinterpret_cast<const std::uint8_t*>(v.bytes.data()), reinterpret_cast<const std::uint8_t*>(v.bytes.data()) + v.bytes.size()); },
            [&](const fastgltf::sources::URI& uriSrc) {
                const auto uri = getUriView(uriSrc);
                if (!uri.valid()) return;
                if (uri.isDataUri()) { bytes = decodeDataUriBytes(uri.string()); return; }
                if (!uri.isLocalPath()) return;
                bytes = readFileBytes(baseDir / uri.fspath());
            },
            [](auto&) {}
        }, image.data);
        return bytes;
    }





    std::unique_ptr<ModelDOD> ModelDOD::load(RHIRenderer& renderer, const std::filesystem::path& path, bool vertexPulling)
    {
        (void)vertexPulling; // Fix: Suppress unused parameter warning

        fastgltf::Parser parser(
            fastgltf::Extensions::KHR_texture_basisu | fastgltf::Extensions::MSFT_texture_dds | fastgltf::Extensions::EXT_texture_webp |
            fastgltf::Extensions::KHR_materials_pbrSpecularGlossiness | fastgltf::Extensions::KHR_materials_clearcoat |
            fastgltf::Extensions::KHR_materials_sheen | fastgltf::Extensions::KHR_materials_specular |
            fastgltf::Extensions::KHR_materials_ior | fastgltf::Extensions::KHR_materials_unlit |
            fastgltf::Extensions::KHR_materials_transmission | fastgltf::Extensions::KHR_materials_volume |
            fastgltf::Extensions::KHR_materials_emissive_strength | fastgltf::Extensions::KHR_lights_punctual);

        auto data = fastgltf::GltfDataBuffer::FromPath(path);
        if (data.error() != fastgltf::Error::None) return nullptr;

        auto asset = parser.loadGltf(data.get(), path.parent_path(), fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages);
        if (asset.error() != fastgltf::Error::None) return nullptr;

        auto& gltf = asset.get();
        auto model = std::make_unique<ModelDOD>();

        // --- Lights ---
        model->m_lights.reserve(gltf.lights.size());
        for (const auto& gLight : gltf.lights) {
            Light l{}; l.m_name = gLight.name; l.m_color = glm::make_vec3(gLight.color.data());
            l.m_intensity = gLight.intensity; l.m_range = gLight.range.value_or(0.0f);
            if (gLight.type == fastgltf::LightType::Directional) l.m_type = LightType::Directional;
            else if (gLight.type == fastgltf::LightType::Point) l.m_type = LightType::Point;
            else if (gLight.type == fastgltf::LightType::Spot) {
                l.m_type = LightType::Spot;
                l.m_innerConeAngle = gLight.innerConeAngle.value_or(0.0f);
                l.m_outerConeAngle = gLight.outerConeAngle.value_or(0.785398f);
            }
            model->m_lights.push_back(l);
        }
        if (model->m_lights.empty()) model->m_lights.push_back({});

        // --- Textures ---
        model->m_textures.reserve(gltf.textures.size());
        for (size_t texIdx = 0; texIdx < gltf.textures.size(); ++texIdx) {
            const auto imgIndexOpt = pickImageIndex(gltf.textures[texIdx]);
            if (!imgIndexOpt) { model->m_textures.push_back(INVALID_TEXTURE_HANDLE); continue; }
            const auto encoded = extractImageBytes(gltf, gltf.images[*imgIndexOpt], path.parent_path());
            if (encoded.empty()) { model->m_textures.push_back(INVALID_TEXTURE_HANDLE); continue; }
            int w, h, comp;
            stbi_uc* pixels = stbi_load_from_memory(encoded.data(), util::u32(encoded.size()), &w, &h, &comp, 4);
            if (!pixels) { model->m_textures.push_back(INVALID_TEXTURE_HANDLE); continue; }
            model->m_textures.push_back(renderer.createTexture(pixels, w, h, 4, true)); 
            stbi_image_free(pixels);
        }

        // --- Materials ---
        model->m_materials.reserve(gltf.materials.size());
        for (const auto& mat : gltf.materials) {
            MaterialData md{};
            md.m_isUnlit = mat.unlit;
            const auto& pbr = mat.pbrData;
            md.m_baseColorFactor = glm::make_vec4(pbr.baseColorFactor.data());
            md.m_metallicFactor = pbr.metallicFactor;
            md.m_roughnessFactor = pbr.roughnessFactor;
            if (pbr.baseColorTexture.has_value()) {
                size_t texIdx = pbr.baseColorTexture->textureIndex;
                if (texIdx < model->m_textures.size()) md.m_baseColorTexture = model->m_textures[texIdx];
                md.m_baseColorUV = getTexCoordIndex(*pbr.baseColorTexture);
                md.m_baseColorSampler = getSamplerAddressMode(gltf, texIdx);
            }
            md.m_alphaMode = toAlphaMode(mat.alphaMode);
            md.m_alphaCutoff = mat.alphaCutoff;
            
            if (mat.transmission) {
                md.m_transmissionFactor = mat.transmission->transmissionFactor;
                if (mat.transmission->transmissionTexture.has_value()) {
                     size_t texIdx = mat.transmission->transmissionTexture->textureIndex;
                     if (texIdx < model->m_textures.size()) md.m_transmissionTexture = model->m_textures[texIdx];
                     md.m_transmissionUV = getTexCoordIndex(*mat.transmission->transmissionTexture);
                     md.m_transmissionSampler = getSamplerAddressMode(gltf, texIdx);
                }
            }
            if (mat.volume) {
                // Fix: Access direct members (removed .value_or)
                md.m_volumeThicknessFactor = mat.volume->thicknessFactor;
                md.m_volumeAttenuationDistance = mat.volume->attenuationDistance;
                md.m_volumeAttenuationColor = glm::make_vec3(mat.volume->attenuationColor.data());
                if (mat.volume->thicknessTexture.has_value()) {
                     size_t texIdx = mat.volume->thicknessTexture->textureIndex;
                     if (texIdx < model->m_textures.size()) md.m_volumeThicknessTexture = model->m_textures[texIdx];
                     md.m_volumeThicknessUV = getTexCoordIndex(*mat.volume->thicknessTexture);
                     md.m_volumeThicknessSampler = getSamplerAddressMode(gltf, texIdx);
                }
            }

                md.m_ior = mat.ior;


            model->m_materials.push_back(md);
        }
        if (model->m_materials.empty()) model->m_materials.push_back({});

        // --- Meshes (Unified Geometry Loading) ---
        std::vector<Vertex> globalVertices;
        std::vector<uint32_t> globalIndices;
        
        model->m_meshes.reserve(gltf.meshes.size());

        for (const auto& gMesh : gltf.meshes) {
            MeshDOD meshDOD;
            meshDOD.name = gMesh.name;
            
            for (const auto& gPrim : gMesh.primitives) {
                PrimitiveDOD primDOD;
                primDOD.firstIndex = static_cast<uint32_t>(globalIndices.size());
                primDOD.vertexOffset = static_cast<int32_t>(globalVertices.size());
                primDOD.materialIndex = gPrim.materialIndex.has_value() ? static_cast<uint32_t>(gPrim.materialIndex.value()) : 0;

                // Load Vertices
                const auto* itPos = gPrim.findAttribute("POSITION");
                if (itPos == gPrim.attributes.end()) continue; 
                
                const auto& posAccessor = gltf.accessors[itPos->accessorIndex];
                size_t vCount = posAccessor.count;
                size_t vStart = globalVertices.size();
                globalVertices.resize(vStart + vCount);

                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor, [&](glm::vec3 pos, size_t idx) {
                    globalVertices[vStart + idx].m_position = pos;
                    globalVertices[vStart + idx].m_color = glm::vec3(1.0f);
                    globalVertices[vStart + idx].m_normal = glm::vec3(0.0f, 1.0f, 0.0f);
                    globalVertices[vStart + idx].m_texCoord0 = glm::vec2(0.0f);
                    globalVertices[vStart + idx].m_texCoord1 = glm::vec2(0.0f);
                    globalVertices[vStart + idx].m_tangent = glm::vec4(0.0f);
                });

                if (const auto* it = gPrim.findAttribute("NORMAL"); it != gPrim.attributes.end()) {
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[it->accessorIndex], [&](glm::vec3 norm, size_t idx) {
                        globalVertices[vStart + idx].m_normal = norm;
                    });
                }
                
                if (const auto* it = gPrim.findAttribute("TEXCOORD_0"); it != gPrim.attributes.end()) {
                     fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[it->accessorIndex], [&](glm::vec2 uv, size_t idx) {
                        globalVertices[vStart + idx].m_texCoord0 = uv;
                    });
                }
                if (const auto* it = gPrim.findAttribute("TEXCOORD_1"); it != gPrim.attributes.end()) {
                     fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[it->accessorIndex], [&](glm::vec2 uv, size_t idx) {
                        globalVertices[vStart + idx].m_texCoord1 = uv;
                    });
                }
                if (const auto* it = gPrim.findAttribute("COLOR_0"); it != gPrim.attributes.end()) {
                     fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[it->accessorIndex], [&](glm::vec3 col, size_t idx) {
                        globalVertices[vStart + idx].m_color = col;
                    });
                }
                if (const auto* it = gPrim.findAttribute("TANGENT"); it != gPrim.attributes.end()) {
                     fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[it->accessorIndex], [&](glm::vec4 tan, size_t idx) {
                        globalVertices[vStart + idx].m_tangent = tan;
                    });
                }

                // Indices
                if (gPrim.indicesAccessor.has_value()) {
                    const auto& acc = gltf.accessors[gPrim.indicesAccessor.value()];
                    if (acc.componentType == fastgltf::ComponentType::UnsignedByte) {
                        fastgltf::iterateAccessor<std::uint8_t>(gltf, acc, [&](std::uint8_t v) { globalIndices.push_back(v); });
                    } else if (acc.componentType == fastgltf::ComponentType::UnsignedShort) {
                        fastgltf::iterateAccessor<std::uint16_t>(gltf, acc, [&](std::uint16_t v) { globalIndices.push_back(v); });
                    } else {
                        fastgltf::iterateAccessor<std::uint32_t>(gltf, acc, [&](std::uint32_t v) { globalIndices.push_back(v); });
                    }
                } else {
                    for(size_t i=0; i<vCount; ++i) globalIndices.push_back(static_cast<uint32_t>(i));
                }
                
                primDOD.indexCount = static_cast<uint32_t>(globalIndices.size()) - primDOD.firstIndex;
                meshDOD.primitives.push_back(primDOD);
            }
            model->m_meshes.push_back(meshDOD);
        }

        // Create GPU Buffers
        if (!globalVertices.empty()) {
            model->vertexBuffer = renderer.createBuffer({
                .size = globalVertices.size() * sizeof(Vertex),
                // Add TransferDst just in case, change MemoryUsage to CPUToGPU
                .usage = rhi::BufferUsage::VertexBuffer | rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                .memoryUsage = rhi::MemoryUsage::CPUToGPU, // [FIXED] Was GPUOnly
                .debugName = "ModelDOD Unified VBO"
            });
            renderer.getBuffer(model->vertexBuffer)->uploadData(globalVertices.data(), globalVertices.size() * sizeof(Vertex));
        }

        if (!globalIndices.empty()) {
            model->indexBuffer = renderer.createBuffer({
                .size = globalIndices.size() * sizeof(uint32_t),
                // Add TransferDst just in case, change MemoryUsage to CPUToGPU
                .usage = rhi::BufferUsage::IndexBuffer | rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                .memoryUsage = rhi::MemoryUsage::CPUToGPU, // [FIXED] Was GPUOnly
                .debugName = "ModelDOD Unified IBO"
            });
            renderer.getBuffer(model->indexBuffer)->uploadData(globalIndices.data(), globalIndices.size() * sizeof(uint32_t));
        }

        // Load Nodes via SceneGraph
        // Fix: Use defaultScene instead of scene
        const size_t sceneIndex = gltf.defaultScene.value_or(0);
        model->scene().buildFromFastgltf(gltf, sceneIndex);
        return model;
    }
}
