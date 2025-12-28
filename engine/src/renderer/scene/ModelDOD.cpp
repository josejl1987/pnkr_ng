#include "pnkr/renderer/scene/ModelDOD.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/core/cache.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>

#include <algorithm>
#include <cmath>
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
#include <cstring>
#include <unordered_map>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>

#include <ktx.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

namespace pnkr::renderer::scene
{
    using namespace pnkr::core;

    ModelDOD::ModelDOD() : m_scene(std::make_unique<SceneGraphDOD>()) {}
    ModelDOD::~ModelDOD() = default;

    namespace {
        struct MeshRange { uint32_t primCount; };

        struct TextureMetaCPU {
            uint8_t isSrgb = 1;   // 1 = sRGB, 0 = linear
            uint8_t _pad[3] = {};
        };

        static uint64_t fnv1a64(std::string_view s)
        {
            uint64_t h = 14695981039346656037ull;
            for (unsigned char c : s) { h ^= uint64_t(c); h *= 1099511628211ull; }
            return h;
        }

        static std::filesystem::path makeTextureCacheDir(const std::filesystem::path& assetPath)
        {
            return assetPath.parent_path() / ".cache" / "textures";
        }

        static std::filesystem::path makeCachedKtx2Path(
            const std::filesystem::path& cacheDir,
            std::string_view sourceKey,
            uint32_t maxSize,
            bool srgb)
        {
            // Appended |v2 to invalidate old caches generated with incorrect linear resizing for sRGB data.
            const std::string key = std::string(sourceKey) + "|" + std::to_string(maxSize) + "|" + (srgb ? "srgb" : "lin") + "|v2";
            const uint64_t h = fnv1a64(key);

            char buf[64];
            std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
            return cacheDir / (std::string(buf) + ".ktx2");
        }

        static uint32_t calcMipLevels(int w, int h)
        {
            uint32_t levels = 1;
            while (w > 1 || h > 1) {
                w = std::max(1, w / 2);
                h = std::max(1, h / 2);
                ++levels;
            }
            return levels;
        }



        static bool writeKtx2RGBA8Mipmapped(
            const std::filesystem::path& outFile,
            const uint8_t* rgba,
            int origW, int origH,
            uint32_t maxSize,
            bool srgb)
        {
            if (!rgba || origW <= 0 || origH <= 0) return false;

            // Compute original coverage at standard alpha cutoff (0.5)
            //const float kAlphaCutoff = 0.5f;
            //const float coverage = GetCoverage(rgba, origW, origH, kAlphaCutoff);

            const int newW = std::min(origW, int(maxSize));
            const int newH = std::min(origH, int(maxSize));
            const uint32_t mips = calcMipLevels(newW, newH);

            ktxTextureCreateInfo ci{};
            ci.vkFormat      = srgb ? 43 : 37; // VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM
            ci.baseWidth     = (uint32_t)newW;
            ci.baseHeight    = (uint32_t)newH;
            ci.baseDepth     = 1;
            ci.numDimensions = 2;
            ci.numLevels     = mips;
            ci.numLayers     = 1;
            ci.numFaces      = 1;
            ci.generateMipmaps = KTX_FALSE;

            ktxTexture2* tex = nullptr;
            if (ktxTexture2_Create(&ci, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &tex) != KTX_SUCCESS)
                return false;

            int w = newW;
            int h = newH;

            for (uint32_t level = 0; level < mips; ++level) {
                size_t offset = 0;
                ktxTexture_GetImageOffset(ktxTexture(tex), level, 0, 0, &offset);
                uint8_t* dst = ktxTexture_GetData(ktxTexture(tex)) + offset;

                if (srgb) {
                    stbir_resize_uint8_srgb(
                        rgba, origW, origH, 0,
                        dst, w, h, 0,
                        STBIR_RGBA);
                } else {
                    stbir_resize_uint8_linear(
                        rgba, origW, origH, 0,
                        dst, w, h, 0,
                        STBIR_RGBA);
                }


                w = std::max(1, w / 2);
                h = std::max(1, h / 2);
            }

            std::filesystem::create_directories(outFile.parent_path());
            const auto outStr = outFile.string();
            const KTX_error_code wr = ktxTexture_WriteToNamedFile(ktxTexture(tex), outStr.c_str());
            ktxTexture_Destroy(ktxTexture(tex));
            return wr == KTX_SUCCESS;
        }

        static void markTextureSrgb(std::vector<uint8_t>& isSrgb, size_t texIdx, bool srgb)
        {
            if (texIdx < isSrgb.size()) {
                isSrgb[texIdx] = srgb ? 1 : isSrgb[texIdx];
            }
        }

        static std::vector<uint8_t> computeTextureSrgbUsage(const fastgltf::Asset& gltf)
        {
            std::vector<uint8_t> isSrgb(gltf.textures.size(), 0);

            for (const auto& mat : gltf.materials) {
                const auto& pbr = mat.pbrData;
                if (pbr.baseColorTexture) markTextureSrgb(isSrgb, pbr.baseColorTexture->textureIndex, true);
                if (mat.emissiveTexture)  markTextureSrgb(isSrgb, mat.emissiveTexture->textureIndex, true);
                if (mat.specular) {
                    if (mat.specular->specularColorTexture) markTextureSrgb(isSrgb, mat.specular->specularColorTexture->textureIndex, true);
                }
                if (mat.sheen) {
                    if (mat.sheen->sheenColorTexture) markTextureSrgb(isSrgb, mat.sheen->sheenColorTexture->textureIndex, true);
                }
            }
            return isSrgb;
        }

        static MaterialCPU toMaterialCPU(const MaterialData& md, const std::vector<TextureHandle>& textures) {
            MaterialCPU mc{};
            
            std::unordered_map<TextureHandle, int32_t> texToIndex;
            texToIndex.reserve(textures.size());
            for (size_t i = 0; i < textures.size(); ++i) texToIndex[textures[i]] = (int32_t)i;

            auto getTexIndex = [&](TextureHandle h) -> int32_t {
                if (h == INVALID_TEXTURE_HANDLE) return -1;
                auto it = texToIndex.find(h);
                return (it != texToIndex.end()) ? it->second : -1;
            };

            std::memcpy(mc.baseColorFactor, glm::value_ptr(md.m_baseColorFactor), 4 * sizeof(float));
            std::memcpy(mc.emissiveFactor, glm::value_ptr(md.m_emissiveFactor), 3 * sizeof(float));
            mc.emissiveFactor[3] = md.m_emissiveStrength;

            mc.metallic = md.m_metallicFactor;
            mc.roughness = md.m_roughnessFactor;
            mc.alphaCutoff = md.m_alphaCutoff;
            mc.ior = md.m_ior;

            mc.transmissionFactor = md.m_transmissionFactor;
            mc.clearcoatFactor = md.m_clearcoatFactor;
            mc.clearcoatRoughness = md.m_clearcoatRoughnessFactor;
            mc.clearcoatNormalScale = md.m_clearcoatNormalScale;

            mc.specularFactorScalar = md.m_specularFactorScalar;
            std::memcpy(mc.specularColorFactor, glm::value_ptr(md.m_specularColorFactor), 3 * sizeof(float));

            std::memcpy(mc.sheenColorFactor, glm::value_ptr(md.m_sheenColorFactor), 3 * sizeof(float));
            mc.sheenRoughnessFactor = md.m_sheenRoughnessFactor;

            mc.volumeThicknessFactor = md.m_volumeThicknessFactor;
            mc.volumeAttenuationDistance = md.m_volumeAttenuationDistance;
            std::memcpy(mc.volumeAttenuationColor, glm::value_ptr(md.m_volumeAttenuationColor), 3 * sizeof(float));

            mc.baseColorTex = getTexIndex(md.m_baseColorTexture);
            mc.normalTex = getTexIndex(md.m_normalTexture);
            mc.metallicRoughnessTex = getTexIndex(md.m_metallicRoughnessTexture);
            mc.occlusionTex = getTexIndex(md.m_occlusionTexture);
            mc.emissiveTex = getTexIndex(md.m_emissiveTexture);
            mc.clearcoatTex = getTexIndex(md.m_clearcoatTexture);
            mc.clearcoatRoughnessTex = getTexIndex(md.m_clearcoatRoughnessTexture);
            mc.clearcoatNormalTex = getTexIndex(md.m_clearcoatNormalTexture);
            mc.specularTex = getTexIndex(md.m_specularTexture);
            mc.specularColorTex = getTexIndex(md.m_specularColorTexture);
            mc.transmissionTex = getTexIndex(md.m_transmissionTexture);
            mc.sheenColorTex = getTexIndex(md.m_sheenColorTexture);
            mc.sheenRoughnessTex = getTexIndex(md.m_sheenRoughnessTexture);
            mc.volumeThicknessTex = getTexIndex(md.m_volumeThicknessTexture);

            mc.flags = 0;
            if (md.m_isUnlit) mc.flags |= Material_Unlit;
            if (md.m_isSpecularGlossiness) mc.flags |= Material_SpecularGlossiness;
            if (md.m_doubleSided) mc.flags |= Material_DoubleSided;
            if (md.m_hasSpecular) mc.flags |= Material_CastShadow | Material_ReceiveShadow; // placeholder logic
            mc.flags |= Material_CastShadow | Material_ReceiveShadow;
            if (md.m_alphaMode == 2) mc.flags |= Material_Transparent;

            return mc;
        }

        static MaterialData fromMaterialCPU(const MaterialCPU& mc, const std::vector<TextureHandle>& textures) {
            MaterialData md{};
            
            auto getTexHandle = [&](int32_t idx) -> TextureHandle {
                if (idx < 0 || static_cast<size_t>(idx) >= textures.size()) return INVALID_TEXTURE_HANDLE;
                return textures[idx];
            };

            // Initialize Spec-Compliant Defaults
            md.m_volumeAttenuationDistance = std::numeric_limits<float>::max(); // Default: Infinity
            md.m_volumeAttenuationColor = glm::vec3(1.0f);                      // Default: White
            md.m_emissiveStrength = 1.0f;                                       // Default: 1.0

            md.m_baseColorFactor = glm::make_vec4(mc.baseColorFactor);
            md.m_emissiveFactor = glm::make_vec3(mc.emissiveFactor);
            md.m_emissiveStrength = mc.emissiveFactor[3];

            md.m_metallicFactor = mc.metallic;
            md.m_roughnessFactor = mc.roughness;
            md.m_alphaCutoff = mc.alphaCutoff;
            md.m_ior = mc.ior;

            md.m_transmissionFactor = mc.transmissionFactor;
            md.m_clearcoatFactor = mc.clearcoatFactor;
            md.m_clearcoatRoughnessFactor = mc.clearcoatRoughness;
            md.m_clearcoatNormalScale = mc.clearcoatNormalScale;

            md.m_specularFactorScalar = mc.specularFactorScalar;
            md.m_specularColorFactor = glm::make_vec3(mc.specularColorFactor);

            md.m_sheenColorFactor = glm::make_vec3(mc.sheenColorFactor);
            md.m_sheenRoughnessFactor = mc.sheenRoughnessFactor;

            md.m_volumeThicknessFactor = mc.volumeThicknessFactor;
            md.m_volumeAttenuationDistance = mc.volumeAttenuationDistance;
            md.m_volumeAttenuationColor = glm::make_vec3(mc.volumeAttenuationColor);

            md.m_baseColorTexture = getTexHandle(mc.baseColorTex);
            md.m_normalTexture = getTexHandle(mc.normalTex);
            md.m_metallicRoughnessTexture = getTexHandle(mc.metallicRoughnessTex);
            md.m_occlusionTexture = getTexHandle(mc.occlusionTex);
            md.m_emissiveTexture = getTexHandle(mc.emissiveTex);
            md.m_clearcoatTexture = getTexHandle(mc.clearcoatTex);
            md.m_clearcoatRoughnessTexture = getTexHandle(mc.clearcoatRoughnessTex);
            md.m_clearcoatNormalTexture = getTexHandle(mc.clearcoatNormalTex);
            md.m_specularTexture = getTexHandle(mc.specularTex);
            md.m_specularColorTexture = getTexHandle(mc.specularColorTex);
            md.m_transmissionTexture = getTexHandle(mc.transmissionTex);
            md.m_sheenColorTexture = getTexHandle(mc.sheenColorTex);
            md.m_sheenRoughnessTexture = getTexHandle(mc.sheenRoughnessTex);
            md.m_volumeThicknessTexture = getTexHandle(mc.volumeThicknessTex);

            md.m_isUnlit = (mc.flags & Material_Unlit) != 0;
            md.m_isSpecularGlossiness = (mc.flags & Material_SpecularGlossiness) != 0;
            md.m_doubleSided = (mc.flags & Material_DoubleSided) != 0;
            md.m_alphaMode = (mc.flags & Material_Transparent) ? 2 : 0;

            return md;
        }

        static void serializeLightSource(std::ostream& os, const LightSource& ls) {
            os.write(reinterpret_cast<const char*>(&ls.type), sizeof(ls.type));
            os.write(reinterpret_cast<const char*>(&ls.color), sizeof(ls.color));
            os.write(reinterpret_cast<const char*>(&ls.direction), sizeof(ls.direction));
            os.write(reinterpret_cast<const char*>(&ls.intensity), sizeof(ls.intensity));
            os.write(reinterpret_cast<const char*>(&ls.range), sizeof(ls.range));
            os.write(reinterpret_cast<const char*>(&ls.innerConeAngle), sizeof(ls.innerConeAngle));
            os.write(reinterpret_cast<const char*>(&ls.outerConeAngle), sizeof(ls.outerConeAngle));
            os.write(reinterpret_cast<const char*>(&ls.debugDraw), sizeof(ls.debugDraw));
        }

        static void deserializeLightSource(std::istream& is, LightSource& ls) {
            is.read(reinterpret_cast<char*>(&ls.type), sizeof(ls.type));
            is.read(reinterpret_cast<char*>(&ls.color), sizeof(ls.color));
            is.read(reinterpret_cast<char*>(&ls.direction), sizeof(ls.direction));
            is.read(reinterpret_cast<char*>(&ls.intensity), sizeof(ls.intensity));
            is.read(reinterpret_cast<char*>(&ls.range), sizeof(ls.range));
            is.read(reinterpret_cast<char*>(&ls.innerConeAngle), sizeof(ls.innerConeAngle));
            is.read(reinterpret_cast<char*>(&ls.outerConeAngle), sizeof(ls.outerConeAngle));
            is.read(reinterpret_cast<char*>(&ls.debugDraw), sizeof(ls.debugDraw));
        }

        static void deserializeLightSourceV1(std::istream& is, LightSource& ls) {
            uint64_t nameLen = 0;
            is.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
            if (nameLen > 0) {
                std::string ignored;
                ignored.resize(nameLen);
                is.read(ignored.data(), nameLen);
            }
            deserializeLightSource(is, ls);
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
    } // anonymous namespace

    bool ModelDOD::saveCache(const std::filesystem::path& path)
    {
        CacheWriter writer(path.string());
        if (!writer.isOpen()) return false;

        // 1. Materials
        std::vector<MaterialCPU> matsCPU;
        matsCPU.reserve(m_materials.size());
        for (const auto& m : m_materials) matsCPU.push_back(toMaterialCPU(m, m_textures));
        writer.writeChunk(makeFourCC("MATL"), 1, matsCPU);
        writer.writeStringListChunk(makeFourCC("TXFN"), 1, m_textureFiles);

        std::vector<TextureMetaCPU> texMeta;
        texMeta.resize(m_textureFiles.size());
        for (size_t i = 0; i < texMeta.size(); ++i) {
            texMeta[i].isSrgb = (i < m_textureIsSrgb.size()) ? m_textureIsSrgb[i] : 1;
        }
        writer.writeChunk(makeFourCC("TXMD"), 1, texMeta);

        // 2. Scene Graph
        auto& reg = m_scene->registry;
        writer.writeSparseSet(makeFourCC("SLOC"), 1, reg.getPool<LocalTransform>());
        writer.writeSparseSet(makeFourCC("SGLO"), 1, reg.getPool<WorldTransform>());
        writer.writeSparseSet(makeFourCC("SHIE"), 1, reg.getPool<Relationship>());
        writer.writeSparseSet(makeFourCC("SMES"), 1, reg.getPool<MeshRenderer>());
        writer.writeCustomSparseSet(makeFourCC("SLIT"), 2, reg.getPool<LightSource>(), serializeLightSource);
        writer.writeSparseSet(makeFourCC("SCAM"), 1, reg.getPool<CameraComponent>());
        writer.writeSparseSet(makeFourCC("SSKN"), 1, reg.getPool<SkinComponent>());

        std::vector<std::string> names;
        for (auto entity : reg.view<Name>()) names.push_back(reg.get<Name>(entity).str);
        writer.writeStringListChunk(makeFourCC("SNMS"), 1, names);

        writer.writeChunk(makeFourCC("SROT"), 1, m_scene->roots);
        writer.writeChunk(makeFourCC("STOP"), 1, m_scene->topoOrder);

        // 3. Meshes
        std::vector<MeshRange> meshRanges;
        std::vector<PrimitiveDOD> allPrims;
        std::vector<std::string> meshNames;
        for (const auto& m : m_meshes) {
            meshRanges.push_back({ (uint32_t)m.primitives.size() });
            meshNames.push_back(m.name);
            for (const auto& p : m.primitives) allPrims.push_back(p);
        }
        writer.writeChunk(makeFourCC("MRNG"), 1, meshRanges);
        writer.writeChunk(makeFourCC("MPRI"), 1, allPrims);
        writer.writeStringListChunk(makeFourCC("MNAM"), 1, meshNames);

        return true;
    }

    bool ModelDOD::loadCache(const std::filesystem::path& path, RHIRenderer& renderer)
    {
        CacheReader reader(path.string());
        if (!reader.isOpen()) return false;

        auto chunks = reader.listChunks();

        std::vector<MaterialCPU> matsCPU;
        std::vector<MeshRange> meshRanges;
        std::vector<PrimitiveDOD> allPrims;
        std::vector<std::string> meshNames;
        std::vector<TextureMetaCPU> texMeta;

        for (const auto& c : chunks) {
            uint32_t fcc = c.header.fourcc;
            auto& reg = m_scene->registry;
            if (fcc == makeFourCC("MATL")) reader.readChunk(c, matsCPU);
            else if (fcc == makeFourCC("TXFN")) reader.readStringListChunk(c, m_textureFiles);
            else if (fcc == makeFourCC("TXMD")) reader.readChunk(c, texMeta);
            else if (fcc == makeFourCC("SLOC")) reader.readSparseSet(c, reg.getPool<LocalTransform>());
            else if (fcc == makeFourCC("SGLO")) reader.readSparseSet(c, reg.getPool<WorldTransform>());
            else if (fcc == makeFourCC("SHIE")) reader.readSparseSet(c, reg.getPool<Relationship>());
            else if (fcc == makeFourCC("SMES")) reader.readSparseSet(c, reg.getPool<MeshRenderer>());
            else if (fcc == makeFourCC("SLIT")) {
                if (c.header.version == 1) {
                    reader.readCustomSparseSet(c, reg.getPool<LightSource>(), deserializeLightSourceV1);
                } else {
                    reader.readCustomSparseSet(c, reg.getPool<LightSource>(), deserializeLightSource);
                }
            }
            else if (fcc == makeFourCC("SCAM")) reader.readSparseSet(c, reg.getPool<CameraComponent>());
            else if (fcc == makeFourCC("SSKN")) reader.readSparseSet(c, reg.getPool<SkinComponent>());
            else if (fcc == makeFourCC("SNMS")) {
                std::vector<std::string> names;
                reader.readStringListChunk(c, names);
                // Note: This logic assumes a 1:1 mapping of packed entities to names which might be fragile
                // For a robust system, Name component should store Entity ID too.
            }
            else if (fcc == makeFourCC("SROT")) reader.readChunk(c, m_scene->roots);
            else if (fcc == makeFourCC("STOP")) reader.readChunk(c, m_scene->topoOrder);
            else if (fcc == makeFourCC("MRNG")) reader.readChunk(c, meshRanges);
            else if (fcc == makeFourCC("MPRI")) reader.readChunk(c, allPrims);
            else if (fcc == makeFourCC("MNAM")) reader.readStringListChunk(c, meshNames);
        }

        // Reconstruct Meshes
        m_meshes.clear();
        size_t primOffset = 0;
        for (size_t i = 0; i < meshRanges.size(); ++i) {
            MeshDOD m;
            m.name = (i < meshNames.size()) ? meshNames[i] : "";
            for (size_t p = 0; p < meshRanges[i].primCount; ++p) {
                if (primOffset < allPrims.size()) {
                    m.primitives.push_back(allPrims[primOffset++]);
                }
            }
            m_meshes.push_back(std::move(m));
        }

        // Textures must be loaded from m_textureFiles
        m_textures.clear();
        m_textureIsSrgb.clear();
        m_textures.reserve(m_textureFiles.size());
        m_textureIsSrgb.reserve(m_textureFiles.size());
        for (size_t i = 0; i < m_textureFiles.size(); ++i) {
            const auto& texPath = m_textureFiles[i];
            const bool srgb = (i < texMeta.size()) ? (texMeta[i].isSrgb != 0) : true;
            m_textureIsSrgb.push_back(srgb ? 1 : 0);

            if (texPath.empty()) {
                m_textures.push_back(INVALID_TEXTURE_HANDLE);
            } else {
                auto handle = renderer.loadTextureKTX(texPath, srgb);
                m_textures.push_back(handle);
            }
        }

        // Reconstruct MaterialData
        m_materials.clear();
        for (const auto& mc : matsCPU) {
            m_materials.push_back(fromMaterialCPU(mc, m_textures));
        }

        m_scene->onHierarchyChanged();

        return true;
    }

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

    static float getNormalScale(const fastgltf::NormalTextureInfo& info) {
        return info.scale;
    }

    static float getOcclusionStrength(const fastgltf::OcclusionTextureInfo& info) {
        return info.strength;
    }

    static std::vector<uint8_t> base64Decode(std::string_view in)
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

        const uint32_t nodeCount = static_cast<uint32_t>(gltf.nodes.size());
        std::vector<ecs::Entity> entityMap(nodeCount);
        for (uint32_t i = 0; i < nodeCount; ++i) {
            entityMap[i] = model->scene().registry.create();
        }

        // --- Lights ---
        std::vector<Light> gltfLights;
        gltfLights.reserve(gltf.lights.size());
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
            gltfLights.push_back(l);
        }

        // --- Cameras ---
        model->m_cameras.clear();
        model->m_cameras.reserve(gltf.cameras.size());
        for (const auto& c : gltf.cameras)
        {
            GltfCamera out{};
            out.name = c.name;

            std::visit(fastgltf::visitor{
                [&](const fastgltf::Camera::Perspective& p)
                {
                    out.type = GltfCamera::Type::Perspective;
                    out.yfovRad = p.yfov;
                    out.aspectRatio = p.aspectRatio.value_or(0.0f);
                    out.znear = p.znear;
                    out.zfar  = p.zfar.value_or(0.0f);
                },
                [&](const fastgltf::Camera::Orthographic& o)
                {
                    out.type = GltfCamera::Type::Orthographic;
                    out.xmag = o.xmag;
                    out.ymag = o.ymag;
                    out.znear = o.znear;
                    out.zfar  = o.zfar;
                }
            }, c.camera);

            model->m_cameras.push_back(std::move(out));
        }

        // --- Textures (precached to KTX2) ---
        const uint32_t kMaxTextureSize = 2048;
        const auto cacheDir = makeTextureCacheDir(path);

        model->m_textureIsSrgb = computeTextureSrgbUsage(gltf);
        model->m_textures.resize(gltf.textures.size(), INVALID_TEXTURE_HANDLE);
        model->m_textureFiles.resize(gltf.textures.size());

        for (size_t texIdx = 0; texIdx < gltf.textures.size(); ++texIdx) {
            const auto imgIndexOpt = pickImageIndex(gltf.textures[texIdx]);
            if (!imgIndexOpt) {
                model->m_textures[texIdx] = INVALID_TEXTURE_HANDLE;
                model->m_textureFiles[texIdx].clear();
                continue;
            }

            const auto& img = gltf.images[*imgIndexOpt];
            std::string sourceKey;
            std::string uriPath;

            std::visit(fastgltf::visitor{
                [&](const fastgltf::sources::URI& uriSrc) {
                    const auto uri = getUriView(uriSrc);
                    if (uri.valid() && uri.isLocalPath()) {
                        uriPath = (path.parent_path() / uri.fspath()).lexically_normal().string();
                    }
                },
                [](auto&) {}
            }, img.data);

            const auto encoded = extractImageBytes(gltf, img, path.parent_path());
            if (encoded.empty()) {
                model->m_textureFiles[texIdx].clear();
                model->m_textures[texIdx] = INVALID_TEXTURE_HANDLE;
                continue;
            }

            if (!uriPath.empty()) {
                sourceKey = uriPath;
            } else {
                const uint64_t contentHash = fnv1a64(std::string_view((const char*)encoded.data(), encoded.size()));
                sourceKey = path.string() + "#img" + std::to_string(*imgIndexOpt) + "#tex" + std::to_string(texIdx) +
                            "#h" + std::to_string((unsigned long long)contentHash);
            }

            const bool srgb = (texIdx < model->m_textureIsSrgb.size()) ? (model->m_textureIsSrgb[texIdx] != 0) : true;
            const auto ktxPath = makeCachedKtx2Path(cacheDir, sourceKey, kMaxTextureSize, srgb);

            if (!std::filesystem::exists(ktxPath)) {
                int w = 0, h = 0, comp = 0;
                stbi_uc* rgba = stbi_load_from_memory(encoded.data(), util::u32(encoded.size()), &w, &h, &comp, 4);

                if (rgba) {
                    (void)writeKtx2RGBA8Mipmapped(ktxPath, rgba, w, h, kMaxTextureSize, srgb);
                    stbi_image_free(rgba);
                } else {
                    const uint8_t white[4] = { 255, 255, 255, 255 };
                    (void)writeKtx2RGBA8Mipmapped(ktxPath, white, 1, 1, 1, srgb);
                }
            }

            model->m_textureFiles[texIdx] = ktxPath.lexically_normal().string();
            model->m_textures[texIdx] = renderer.loadTextureKTX(model->m_textureFiles[texIdx], srgb);
        }

        // --- Materials ---
        model->m_materials.reserve(gltf.materials.size());
        for (const auto& mat : gltf.materials) {
            MaterialData md{};

            // 1. Initialize Defaults according to glTF Spec
            md.m_emissiveStrength = 1.0f;
            md.m_volumeAttenuationColor = glm::vec3(1.0f);
            md.m_volumeAttenuationDistance = std::numeric_limits<float>::max();

            md.m_doubleSided = mat.doubleSided;
            md.m_isUnlit = mat.unlit;

            // 2. Handle Workflow: Specular-Glossiness OR Metallic-Roughness
            if (mat.specularGlossiness) {
                md.m_isSpecularGlossiness = true;
                // Map PBR SpecGloss -> Internal Storage
                md.m_baseColorFactor = glm::make_vec4(mat.specularGlossiness->diffuseFactor.data());
                md.m_specularColorFactor = glm::make_vec3(mat.specularGlossiness->specularFactor.data());
                md.m_glossinessFactor = mat.specularGlossiness->glossinessFactor; // Use specific field
                md.m_metallicFactor = 0.0f; // Not used in this workflow
                md.m_roughnessFactor = 1.0f - md.m_glossinessFactor; // Approximation for systems needing roughness

                if (mat.specularGlossiness->diffuseTexture.has_value()) {
                    size_t texIdx = mat.specularGlossiness->diffuseTexture->textureIndex;
                    if (texIdx < model->m_textures.size()) md.m_baseColorTexture = model->m_textures[texIdx];
                    md.m_baseColorUV = getTexCoordIndex(*mat.specularGlossiness->diffuseTexture);
                    md.m_baseColorSampler = getSamplerAddressMode(gltf, texIdx);
                }

                if (mat.specularGlossiness->specularGlossinessTexture.has_value()) {
                    // This texture packs Specular (RGB) + Glossiness (A)
                    size_t texIdx = mat.specularGlossiness->specularGlossinessTexture->textureIndex;
                    if (texIdx < model->m_textures.size()) md.m_specularTexture = model->m_textures[texIdx];
                    md.m_specularUV = getTexCoordIndex(*mat.specularGlossiness->specularGlossinessTexture);
                    md.m_specularSampler = getSamplerAddressMode(gltf, texIdx);
                }
            }
            else {
                // Standard Metallic-Roughness
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

                if (pbr.metallicRoughnessTexture.has_value()) {
                    size_t texIdx = pbr.metallicRoughnessTexture->textureIndex;
                    if (texIdx < model->m_textures.size()) md.m_metallicRoughnessTexture = model->m_textures[texIdx];
                    md.m_metallicRoughnessUV = getTexCoordIndex(*pbr.metallicRoughnessTexture);
                    md.m_metallicRoughnessSampler = getSamplerAddressMode(gltf, texIdx);
                }
            }

            md.m_alphaMode = toAlphaMode(mat.alphaMode);
            md.m_alphaCutoff = mat.alphaCutoff;
            md.m_emissiveFactor = glm::make_vec3(mat.emissiveFactor.data());

            // 3. Fix: KHR_materials_emissive_strength
            // fastgltf places this directly on material if extension is enabled
            if (mat.emissiveStrength != 1.0f) {
                md.m_emissiveStrength = mat.emissiveStrength;
            }

            if (mat.normalTexture.has_value()) {
                size_t texIdx = mat.normalTexture->textureIndex;
                if (texIdx < model->m_textures.size()) md.m_normalTexture = model->m_textures[texIdx];
                md.m_normalUV = getTexCoordIndex(*mat.normalTexture);
                md.m_normalSampler = getSamplerAddressMode(gltf, texIdx);
                md.m_normalScale = getNormalScale(*mat.normalTexture);
            }

            if (mat.occlusionTexture.has_value()) {
                size_t texIdx = mat.occlusionTexture->textureIndex;
                if (texIdx < model->m_textures.size()) md.m_occlusionTexture = model->m_textures[texIdx];
                md.m_occlusionUV = getTexCoordIndex(*mat.occlusionTexture);
                md.m_occlusionSampler = getSamplerAddressMode(gltf, texIdx);
                md.m_occlusionStrength = getOcclusionStrength(*mat.occlusionTexture);
            }

            if (mat.emissiveTexture.has_value()) {
                size_t texIdx = mat.emissiveTexture->textureIndex;
                if (texIdx < model->m_textures.size()) md.m_emissiveTexture = model->m_textures[texIdx];
                md.m_emissiveUV = getTexCoordIndex(*mat.emissiveTexture);
                md.m_emissiveSampler = getSamplerAddressMode(gltf, texIdx);
            }

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
                md.m_volumeThicknessFactor = mat.volume->thicknessFactor;
                md.m_volumeAttenuationDistance = mat.volume->attenuationDistance;

                // FIX: Infinite distance logic
                if (md.m_volumeAttenuationDistance == 0.0f) {
                    md.m_volumeAttenuationDistance = std::numeric_limits<float>::max();
                }

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

        model->m_materialsCPU.clear();
        model->m_materialsCPU.reserve(model->m_materials.size());
        for (const auto& mat : model->m_materials)
        {
            model->m_materialsCPU.push_back(toMaterialCPU(mat, model->m_textures));
        }

        // --- Meshes (Unified Geometry Loading) ---
        model->m_cpuVertices.clear();
        model->m_cpuIndices.clear();
        std::vector<Vertex>& globalVertices = model->m_cpuVertices;
        std::vector<uint32_t>& globalIndices = model->m_cpuIndices;
        std::vector<MorphVertex> allMorphVertices;

        // --- Add System Primitives ---
        uint32_t primitiveCount = 0;
        {
            std::vector<std::pair<geometry::MeshData, std::string>> defaults;
            defaults.push_back({geometry::GeometryUtils::getCube(1.0f), "PrimitiveCube"});
            defaults.push_back({geometry::GeometryUtils::getPlane(1.0f, 1.0f, 1), "PrimitivePlane"});
            defaults.push_back({geometry::GeometryUtils::getSphere(1.0f, 32, 16), "PrimitiveSphere"});
            defaults.push_back({geometry::GeometryUtils::getCylinder(0.5f, 1.0f, 32), "PrimitiveCylinder"});
            defaults.push_back({geometry::GeometryUtils::getCapsule(0.5f, 1.0f, 32, 8), "PrimitiveCapsule"});
            defaults.push_back({geometry::GeometryUtils::getTorus(1.0f, 0.3f, 16, 32), "PrimitiveTorus"});

            primitiveCount = (uint32_t)defaults.size();

            // Ensure material 0 exists for primitives
            if (model->m_materials.empty()) model->m_materials.push_back({});

            for (const auto& p : defaults) {
                model->appendPrimitiveMeshData(p.first, 0, p.second);
            }
        }

        model->m_meshes.reserve(gltf.meshes.size() + primitiveCount);
        model->m_meshBounds.reserve(gltf.meshes.size() + primitiveCount);
        model->m_morphTargetInfos.resize(gltf.meshes.size() + primitiveCount);
        model->m_morphStates.resize(gltf.meshes.size() + primitiveCount);

        for (size_t meshIdx = 0; meshIdx < gltf.meshes.size(); ++meshIdx) {
            const auto& gMesh = gltf.meshes[meshIdx];
            MeshDOD meshDOD;
            meshDOD.name = gMesh.name;
            bool hasBounds = false;
            glm::vec3 meshMin(std::numeric_limits<float>::max());
            glm::vec3 meshMax(std::numeric_limits<float>::lowest());

            uint32_t meshVertexStart = (uint32_t)globalVertices.size();

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
                    globalVertices[vStart + idx].m_meshIndex = (uint32_t)meshIdx + primitiveCount;
                    globalVertices[vStart + idx].m_localIndex = (uint32_t)(vStart + idx - meshVertexStart);

                    meshMin = glm::min(meshMin, pos);
                    meshMax = glm::max(meshMax, pos);
                    hasBounds = true;
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
                if (const auto* it = gPrim.findAttribute("JOINTS_0"); it != gPrim.attributes.end()) {
                    fastgltf::iterateAccessorWithIndex<glm::uvec4>(gltf, gltf.accessors[it->accessorIndex], [&](glm::uvec4 joints, size_t idx) {
                        globalVertices[vStart + idx].m_joints = joints;
                    });
                }
                if (const auto* it = gPrim.findAttribute("WEIGHTS_0"); it != gPrim.attributes.end()) {
                    fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[it->accessorIndex], [&](glm::vec4 weights, size_t idx) {
                        globalVertices[vStart + idx].m_weights = weights;
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

            // Process Morph Targets for this mesh
            auto& morphInfo = model->m_morphTargetInfos[meshIdx + primitiveCount];
            morphInfo.meshIndex = (uint32_t)meshIdx + primitiveCount;
            if (!gMesh.primitives.empty() && !gMesh.primitives[0].targets.empty()) {
                size_t numTargets = gMesh.primitives[0].targets.size();
                uint32_t meshVertexCount = (uint32_t)globalVertices.size() - meshVertexStart;

                for (size_t targetIdx = 0; targetIdx < numTargets; ++targetIdx) {
                    uint32_t targetOffset = (uint32_t)allMorphVertices.size();
                    morphInfo.targetOffsets.push_back(targetOffset);
                    allMorphVertices.resize(targetOffset + meshVertexCount);

                    uint32_t currentPrimVOffset = 0;
                    for (size_t primIdx = 0; primIdx < gMesh.primitives.size(); ++primIdx) {
                        const auto& gPrim = gMesh.primitives[primIdx];
                        const auto* itPos = gPrim.findAttribute("POSITION");
                        if (itPos == gPrim.attributes.end()) continue;
                        size_t vCount = gltf.accessors[itPos->accessorIndex].count;

                        if (targetIdx < gPrim.targets.size()) {
                            const auto& target = gPrim.targets[targetIdx];
                            // NOTE: gPrim.targets[targetIdx] is an attribute list, not a Primitive, so it has no findAttribute().
                            const auto* posIt = std::find_if(target.begin(), target.end(),
                                [](const fastgltf::Attribute& a) {
                                    return a.name == "POSITION";
                                });
                            if (posIt != target.end()) {
                                fastgltf::iterateAccessorWithIndex<glm::vec3>(
                                    gltf, gltf.accessors[posIt->accessorIndex],
                                    [&](glm::vec3 v, size_t idx) {
                                        allMorphVertices[targetOffset + currentPrimVOffset + idx].positionDelta = v;
                                    });
                            }

                            const auto* normIt = std::find_if(target.begin(), target.end(),
                                [](const fastgltf::Attribute& a) {
                                    return a.name == "NORMAL";
                                });
                            if (normIt != target.end()) {
                                fastgltf::iterateAccessorWithIndex<glm::vec3>(
                                    gltf, gltf.accessors[normIt->accessorIndex],
                                    [&](glm::vec3 v, size_t idx) {
                                        allMorphVertices[targetOffset + currentPrimVOffset + idx].normalDelta = v;
                                    });
                            }
                        }
                        currentPrimVOffset += (uint32_t)vCount;
                    }
                }
            }

            BoundingBox box{};
            if (hasBounds) {
                box.m_min = meshMin;
                box.m_max = meshMax;
            } else {
                box.m_min = glm::vec3(0.0f);
                box.m_max = glm::vec3(0.0f);
            }
            model->m_meshBounds.push_back(box);
        }

        // --- Skins ---
        model->m_skins.reserve(gltf.skins.size());
        for (const auto& gSkin : gltf.skins) {
            Skin skin;
            skin.name = gSkin.name;
            skin.skeletonRootNode = gSkin.skeleton.has_value() ? (int)entityMap[gSkin.skeleton.value()] : -1;

            skin.joints.reserve(gSkin.joints.size());
            for (auto jointIdx : gSkin.joints) {
                skin.joints.push_back(static_cast<uint32_t>(entityMap[jointIdx]));
            }

            if (gSkin.inverseBindMatrices.has_value()) {
                const auto& acc = gltf.accessors[gSkin.inverseBindMatrices.value()];
                skin.inverseBindMatrices.resize(acc.count);
                fastgltf::iterateAccessorWithIndex<glm::mat4>(gltf, acc, [&](glm::mat4 m, size_t idx) {
                    skin.inverseBindMatrices[idx] = m;
                });
            } else {
                skin.inverseBindMatrices.assign(skin.joints.size(), glm::mat4(1.0f));
            }
            model->m_skins.push_back(std::move(skin));
        }

        // --- Animations ---
        model->m_animations.reserve(gltf.animations.size());
        for (const auto& gAnim : gltf.animations) {
            Animation anim;
            anim.name = gAnim.name;

            for (const auto& gSampler : gAnim.samplers) {
                AnimationSampler sampler;
                if (gSampler.interpolation == fastgltf::AnimationInterpolation::Linear) sampler.interpolation = InterpolationType::Linear;
                else if (gSampler.interpolation == fastgltf::AnimationInterpolation::Step) sampler.interpolation = InterpolationType::Step;
                else sampler.interpolation = InterpolationType::CubicSpline;

                const auto& inputAcc = gltf.accessors[gSampler.inputAccessor];
                sampler.inputs.resize(inputAcc.count);
                fastgltf::iterateAccessorWithIndex<float>(gltf, inputAcc, [&](float t, size_t idx) {
                    sampler.inputs[idx] = t;
                    anim.duration = std::max(anim.duration, t);
                });

                const auto& outputAcc = gltf.accessors[gSampler.outputAccessor];
                sampler.outputs.resize(outputAcc.count);
                if (outputAcc.type == fastgltf::AccessorType::Scalar) {
                     fastgltf::iterateAccessorWithIndex<float>(gltf, outputAcc, [&](float v, size_t idx) {
                        sampler.outputs[idx] = glm::vec4(v, 0, 0, 0);
                    });
                } else if (outputAcc.type == fastgltf::AccessorType::Vec3) {
                     fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, outputAcc, [&](glm::vec3 v, size_t idx) {
                        sampler.outputs[idx] = glm::vec4(v, 0.0f);
                    });
                } else if (outputAcc.type == fastgltf::AccessorType::Vec4) {
                     fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, outputAcc, [&](glm::vec4 v, size_t idx) {
                        sampler.outputs[idx] = v;
                    });
                }
                anim.samplers.push_back(std::move(sampler));
            }

            for (const auto& gChannel : gAnim.channels) {
                if (!gChannel.nodeIndex.has_value()) continue;
                AnimationChannel channel;
                channel.samplerIndex = (int)gChannel.samplerIndex;
                channel.targetNode = static_cast<uint32_t>(entityMap[gChannel.nodeIndex.value()]);
                if (gChannel.path == fastgltf::AnimationPath::Translation) channel.path = AnimationPath::Translation;
                else if (gChannel.path == fastgltf::AnimationPath::Rotation) channel.path = AnimationPath::Rotation;
                else if (gChannel.path == fastgltf::AnimationPath::Scale) channel.path = AnimationPath::Scale;
                else if (gChannel.path == fastgltf::AnimationPath::Weights) channel.path = AnimationPath::Weights;
                anim.channels.push_back(channel);
            }
            model->m_animations.push_back(std::move(anim));
        }

        model->uploadUnifiedBuffers(renderer);

        if (!allMorphVertices.empty()) {
            model->morphVertexBuffer = renderer.createBuffer({
                .size = allMorphVertices.size() * sizeof(MorphVertex),
                .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                .debugName = "ModelDOD Morph Delta Buffer"
            });
            renderer.getBuffer(model->morphVertexBuffer)->uploadData(allMorphVertices.data(), allMorphVertices.size() * sizeof(MorphVertex));
        }

        // Load Nodes via ECS
        const size_t sceneIndex = gltf.defaultScene.value_or(0);

        for (uint32_t i = 0; i < nodeCount; ++i) {
            const auto& node = gltf.nodes[i];
            ecs::Entity e = entityMap[i];

            auto& local = model->scene().registry.emplace<LocalTransform>(e);
            local.matrix = getLocalMatrix(node);
            model->scene().registry.emplace<WorldTransform>(e);

            if (node.meshIndex.has_value()) {
                model->scene().registry.emplace<MeshRenderer>(e, (int32_t)node.meshIndex.value() + (int32_t)primitiveCount);
            }
            if (node.lightIndex.has_value()) {
                const size_t lightIndex = static_cast<size_t>(node.lightIndex.value());
                if (lightIndex < gltfLights.size()) {
                    const auto& glight = gltfLights[lightIndex];
                    auto& ls = model->scene().registry.emplace<LightSource>(e);
                    ls.type = glight.m_type;
                    ls.color = glight.m_color;
                    ls.direction = glight.m_direction;
                    ls.intensity = glight.m_intensity;
                    ls.range = glight.m_range;
                    ls.innerConeAngle = glight.m_innerConeAngle;
                    ls.outerConeAngle = glight.m_outerConeAngle;
                    ls.debugDraw = glight.m_debugDraw;

                    if (!gltfLights[lightIndex].m_name.empty() && !model->scene().registry.has<Name>(e)) {
                        model->scene().registry.emplace<Name>(e, gltfLights[lightIndex].m_name);
                    }
                }
            }
            if (node.cameraIndex.has_value()) {
                model->scene().registry.emplace<CameraComponent>(e, (int32_t)node.cameraIndex.value());
            }
            if (node.skinIndex.has_value()) {
                model->scene().registry.emplace<SkinComponent>(e, (int32_t)node.skinIndex.value());
            }
            if (!node.name.empty()) {
                model->scene().registry.emplace<Name>(e, std::string(node.name));
            }
        }

        // Hierarchy
        for (uint32_t i = 0; i < nodeCount; ++i) {
            const auto& node = gltf.nodes[i];
            for (auto childIdx : node.children) {
                model->scene().setParent(entityMap[childIdx], entityMap[i]);
            }
        }

        // Scene Roots
        if (sceneIndex < gltf.scenes.size()) {
            for (auto nodeIdx : gltf.scenes[sceneIndex].nodeIndices) {
                // If it doesn't have a parent, it's a root.
                // setParent(e, NULL_ENTITY) will add it to roots.
                Relationship& rel = model->scene().registry.get<Relationship>(entityMap[nodeIdx]);
                if (rel.parent == ecs::NULL_ENTITY) {
                    model->scene().setParent(entityMap[nodeIdx], ecs::NULL_ENTITY);
                }
            }
        }

        model->scene().recalculateGlobalTransformsFull();

        return model;
    }

    uint32_t ModelDOD::appendPrimitiveMeshData(const geometry::MeshData& primitiveData,
                                               uint32_t materialIndex,
                                               const std::string& name)
    {
        const uint32_t meshId = static_cast<uint32_t>(m_meshes.size());
        const uint32_t firstIndex = static_cast<uint32_t>(m_cpuIndices.size());
        const int32_t vertexOffset = static_cast<int32_t>(m_cpuVertices.size());

        m_cpuVertices.reserve(m_cpuVertices.size() + primitiveData.vertices.size());
        for (size_t i = 0; i < primitiveData.vertices.size(); ++i) {
            Vertex v = primitiveData.vertices[i];
            v.m_meshIndex = meshId;
            v.m_localIndex = static_cast<uint32_t>(i);
            m_cpuVertices.push_back(v);
        }

        m_cpuIndices.reserve(m_cpuIndices.size() + primitiveData.indices.size());
        for (uint32_t idx : primitiveData.indices) {
            m_cpuIndices.push_back(idx);
        }

        MeshDOD mesh;
        mesh.name = name;
        PrimitiveDOD prim;
        prim.firstIndex = firstIndex;
        prim.indexCount = static_cast<uint32_t>(primitiveData.indices.size());
        prim.vertexOffset = vertexOffset;
        prim.materialIndex = (materialIndex < m_materials.size()) ? materialIndex : 0;
        mesh.primitives.push_back(prim);
        m_meshes.push_back(mesh);

        BoundingBox bounds{};
        if (!primitiveData.vertices.empty()) {
            glm::vec3 minPos(std::numeric_limits<float>::max());
            glm::vec3 maxPos(std::numeric_limits<float>::lowest());
            for (const auto& v : primitiveData.vertices) {
                minPos = glm::min(minPos, v.m_position);
                maxPos = glm::max(maxPos, v.m_position);
            }
            bounds.m_min = minPos;
            bounds.m_max = maxPos;
        }
        m_meshBounds.push_back(bounds);

        MorphTargetInfo morphInfo{};
        morphInfo.meshIndex = meshId;
        m_morphTargetInfos.push_back(morphInfo);
        m_morphStates.push_back({});

        return meshId;
    }

    void ModelDOD::uploadUnifiedBuffers(RHIRenderer& renderer)
    {
        if (!m_cpuVertices.empty()) {
            vertexBuffer = renderer.createBuffer({
                .size = m_cpuVertices.size() * sizeof(Vertex),
                .usage = rhi::BufferUsage::VertexBuffer | rhi::BufferUsage::StorageBuffer |
                         rhi::BufferUsage::ShaderDeviceAddress | rhi::BufferUsage::TransferDst,
                .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                .debugName = "ModelDOD Unified VBO"
            });
            renderer.getBuffer(vertexBuffer)->uploadData(m_cpuVertices.data(), m_cpuVertices.size() * sizeof(Vertex));
        }

        if (!m_cpuIndices.empty()) {
            indexBuffer = renderer.createBuffer({
                .size = m_cpuIndices.size() * sizeof(uint32_t),
                .usage = rhi::BufferUsage::IndexBuffer | rhi::BufferUsage::StorageBuffer |
                         rhi::BufferUsage::ShaderDeviceAddress | rhi::BufferUsage::TransferDst,
                .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                .debugName = "ModelDOD Unified IBO"
            });
            renderer.getBuffer(indexBuffer)->uploadData(m_cpuIndices.data(), m_cpuIndices.size() * sizeof(uint32_t));
        }
    }

    void ModelDOD::dropCpuGeometry()
    {
        m_cpuVertices.clear();
        m_cpuIndices.clear();
        m_cpuVertices.shrink_to_fit();
        m_cpuIndices.shrink_to_fit();
    }

    uint32_t ModelDOD::addPrimitiveToScene(RHIRenderer& renderer,
                                           const geometry::MeshData& primitiveData,
                                           uint32_t materialIndex,
                                           const glm::mat4& transform,
                                           const std::string& name)
    {
        if (primitiveData.vertices.empty() || primitiveData.indices.empty())
            return 0;

        const uint32_t meshId = appendPrimitiveMeshData(primitiveData, materialIndex, name);

        renderer.device()->waitIdle();
        uploadUnifiedBuffers(renderer);

        auto& scene = *m_scene;
        ecs::Entity parent = scene.root;
        ecs::Entity nodeId = scene.createNode(parent);

        scene.registry.get<LocalTransform>(nodeId).matrix = transform;
        scene.registry.emplace<MeshRenderer>(nodeId, static_cast<int32_t>(meshId));

        if (!name.empty()) {
            scene.registry.emplace<Name>(nodeId, name);
        }

        scene.onHierarchyChanged();
        return static_cast<uint32_t>(nodeId);
    }

    void ModelDOD::addPrimitiveMeshes(RHIRenderer& renderer,
                                      const std::vector<geometry::MeshData>& primitives,
                                      const std::vector<std::string>& names,
                                      uint32_t materialIndex)
    {
        if (primitives.empty())
            return;

        size_t appended = 0;
        for (size_t i = 0; i < primitives.size(); ++i) {
            const auto& data = primitives[i];
            if (data.vertices.empty() || data.indices.empty())
                continue;
            const std::string name = (i < names.size()) ? names[i] : "Primitive";
            appendPrimitiveMeshData(data, materialIndex, name);
            ++appended;
        }

        if (appended == 0)
            return;

        renderer.device()->waitIdle();
        uploadUnifiedBuffers(renderer);
    }

    int32_t ModelDOD::addLight(const Light& light, const glm::mat4& transform, const std::string& name)
    {
        auto& scene = *m_scene;
        ecs::Entity parent = scene.root;
        // If scene is not initialized (no root), we cannot attach a node.
        if (scene.root == ecs::NULL_ENTITY) return -1;

        ecs::Entity nodeId = scene.createNode(parent);

        const int32_t lightIndex = static_cast<int32_t>(scene.registry.getPool<LightSource>().size());
        auto& ls = scene.registry.emplace<LightSource>(nodeId);
        ls.type = light.m_type;
        ls.color = light.m_color;
        ls.direction = light.m_direction;
        ls.intensity = light.m_intensity;
        ls.range = light.m_range;
        ls.innerConeAngle = light.m_innerConeAngle;
        ls.outerConeAngle = light.m_outerConeAngle;
        ls.debugDraw = light.m_debugDraw;

        scene.registry.get<LocalTransform>(nodeId).matrix = transform;

        if (!name.empty()) {
            scene.registry.emplace<Name>(nodeId, name);
        } else if (!light.m_name.empty()) {
            scene.registry.emplace<Name>(nodeId, light.m_name);
        }

        scene.onHierarchyChanged();

        return lightIndex;
    }

    void ModelDOD::removeLight(int32_t lightIndex)
    {
        const auto& lightPool = scene().registry.getPool<LightSource>();
        if (lightIndex < 0 || static_cast<size_t>(lightIndex) >= lightPool.size()) return;

        // 1. Find the entity with this light
        auto& scene = *m_scene;
        auto lightView = scene.registry.view<LightSource>();

        ecs::Entity toDestroy = ecs::NULL_ENTITY;
        uint32_t currentIdx = 0;

        lightView.each([&](ecs::Entity e, LightSource& /*ls*/) {
            if (currentIdx == static_cast<uint32_t>(lightIndex)) {
                toDestroy = e;
            }
            currentIdx++;
        });

        if (toDestroy != ecs::NULL_ENTITY) {
            scene.destroyNode(toDestroy);
        }
    }
}
