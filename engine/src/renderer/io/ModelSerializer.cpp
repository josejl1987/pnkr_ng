#include "pnkr/renderer/io/ModelSerializer.hpp"
#include "pnkr/renderer/scene/ModelDOD.hpp"
#include "pnkr/renderer/AssetManager.hpp"
#include "pnkr/core/cache.hpp"
#include "pnkr/core/common.hpp"

#include <cstring>
#include <istream>
#include <limits>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

#include "pnkr/renderer/gpu_shared/SkinningShared.h"
#include "pnkr/renderer/scene/Bounds.hpp"

namespace pnkr::renderer::io
{
    using namespace pnkr::core;
    using namespace pnkr::renderer::scene;
    using namespace pnkr::assets;

    namespace {
    constexpr uint16_t PMESH_VERSION = 6;

    struct MeshRange {
      uint32_t m_primCount;
    };

        struct TextureMetaCPU {
          uint8_t m_isSrgb = 1;
          uint8_t m_pad[3] = {};
        };

        void serializeLightSource(std::ostream &os, const LightSource &ls) {
          os.write(reinterpret_cast<const char *>(&ls.type), sizeof(ls.type));
          os.write(reinterpret_cast<const char *>(&ls.color), sizeof(ls.color));
          os.write(reinterpret_cast<const char *>(&ls.direction),
                   sizeof(ls.direction));
          os.write(reinterpret_cast<const char *>(&ls.intensity),
                   sizeof(ls.intensity));
          os.write(reinterpret_cast<const char *>(&ls.range), sizeof(ls.range));
          os.write(reinterpret_cast<const char *>(&ls.innerConeAngle),
                   sizeof(ls.innerConeAngle));
          os.write(reinterpret_cast<const char *>(&ls.outerConeAngle),
                   sizeof(ls.outerConeAngle));
          os.write(reinterpret_cast<const char *>(&ls.debugDraw),
                   sizeof(ls.debugDraw));
        }

        void deserializeLightSource(std::istream &is, LightSource &ls) {
          is.read(reinterpret_cast<char *>(&ls.type), sizeof(ls.type));
          is.read(reinterpret_cast<char *>(&ls.color), sizeof(ls.color));
          is.read(reinterpret_cast<char *>(&ls.direction),
                  sizeof(ls.direction));
          is.read(reinterpret_cast<char *>(&ls.intensity),
                  sizeof(ls.intensity));
          is.read(reinterpret_cast<char *>(&ls.range), sizeof(ls.range));
          is.read(reinterpret_cast<char *>(&ls.innerConeAngle),
                  sizeof(ls.innerConeAngle));
          is.read(reinterpret_cast<char *>(&ls.outerConeAngle),
                  sizeof(ls.outerConeAngle));
          is.read(reinterpret_cast<char *>(&ls.debugDraw),
                  sizeof(ls.debugDraw));
        }

        void deserializeLightSourceV1(std::istream &is, LightSource &ls) {
          uint64_t nameLen = 0;
          is.read(reinterpret_cast<char *>(&nameLen), sizeof(nameLen));
          if (nameLen > 0) {
            std::string ignored;
            ignored.resize(nameLen);
            is.read(ignored.data(), nameLen);
          }
          deserializeLightSource(is, ls);
        }

        template <typename T> void writeVal(std::ostream &os, const T &val) {
          os.write(reinterpret_cast<const char *>(&val), sizeof(T));
        }

        template <typename T> void readVal(std::istream &is, T &val) {
          is.read(reinterpret_cast<char *>(&val), sizeof(T));
        }

        void writeStr(std::ostream &os, const std::string &s) {
          uint64_t len = s.size();
          writeVal(os, len);
          if (len > 0) {
            os.write(s.data(), len);
          }
        }

        void readStr(std::istream &is, std::string &s) {
          uint64_t len = 0;
          readVal(is, len);
          s.resize(len);
          if (len > 0) {
            is.read(s.data(), len);
          }
        }

        template <typename T>
        void writeVec(std::ostream &os, const std::vector<T> &v) {
          static_assert(std::is_trivially_copyable_v<T>,
                        "T must be trivially copyable for writeVec");
          uint64_t count = v.size();
          writeVal(os, count);
          if (count > 0) {
            os.write(reinterpret_cast<const char *>(v.data()),
                     count * sizeof(T));
          }
        }

        template <typename T>
        void readVec(std::istream &is, std::vector<T> &v) {
          static_assert(std::is_trivially_copyable_v<T>,
                        "T must be trivially copyable for readVec");
          uint64_t count = 0;
          readVal(is, count);
          v.resize(count);
          if (count > 0) {
            is.read(reinterpret_cast<char *>(v.data()), count * sizeof(T));
          }
        }

        void serializeImportedTexture(std::ostream &os,
                                      const ImportedTexture &t) {
          writeStr(os, t.sourcePath);
          writeVal(os, t.isSrgb);
          writeVal(os, t.isKtx);
          writeVal(os, t.priority);
        }

        void deserializeImportedTexture(std::istream &is, ImportedTexture &t) {
          readStr(is, t.sourcePath);
          readVal(is, t.isSrgb);
          readVal(is, t.isKtx);
          readVal(is, t.priority);
        }

        void serializeImportedMesh(std::ostream &os, const ImportedMesh &m) {
          writeStr(os, m.name);
          uint64_t primCount = m.primitives.size();
          writeVal(os, primCount);
          for (const auto &p : m.primitives) {
            writeVec(os, p.vertices);
            writeVec(os, p.indices);
            writeVal(os, p.materialIndex);
            writeVal(os, p.minPos);
            writeVal(os, p.maxPos);
            uint64_t targetCount = p.targets.size();
            writeVal(os, targetCount);
            for (const auto &t : p.targets) {
              writeVec(os, t.positionDeltas);
              writeVec(os, t.normalDeltas);
            }
          }
        }

        void deserializeImportedMesh(std::istream &is, ImportedMesh &m) {
          readStr(is, m.name);
          uint64_t primCount = 0;
          readVal(is, primCount);
          m.primitives.resize(primCount);
          for (auto &p : m.primitives) {
            readVec(is, p.vertices);
            readVec(is, p.indices);
            readVal(is, p.materialIndex);
            readVal(is, p.minPos);
            readVal(is, p.maxPos);
            uint64_t targetCount = 0;
            readVal(is, targetCount);
            p.targets.resize(targetCount);
            for (auto &t : p.targets) {
              readVec(is, t.positionDeltas);
              readVec(is, t.normalDeltas);
            }
          }
        }

        void serializeImportedNode(std::ostream &os, const ImportedNode &n) {
          writeStr(os, n.name);
          writeVal(os, n.localTransform);
          writeVal(os, n.parentIndex);
          writeVec(os, n.children);
          writeVal(os, n.meshIndex);
          writeVal(os, n.lightIndex);
          writeVal(os, n.cameraIndex);
          writeVal(os, n.skinIndex);
        }

        void deserializeImportedNode(std::istream &is, ImportedNode &n) {
          readStr(is, n.name);
          readVal(is, n.localTransform);
          readVal(is, n.parentIndex);
          readVec(is, n.children);
          readVal(is, n.meshIndex);
          readVal(is, n.lightIndex);
          readVal(is, n.cameraIndex);
          readVal(is, n.skinIndex);
        }

        void serializeAnimation(std::ostream &os, const Animation &a) {
          writeStr(os, a.name);
          writeVal(os, a.duration);
          uint64_t samplerCount = a.samplers.size();
          writeVal(os, samplerCount);
          for (const auto &s : a.samplers) {
            writeVal(os, s.interpolation);
            writeVec(os, s.inputs);
            writeVec(os, s.outputs);
          }
          writeVec(os, a.channels);
        }

        void deserializeAnimation(std::istream &is, Animation &a) {
          readStr(is, a.name);
          readVal(is, a.duration);
          uint64_t samplerCount = 0;
          readVal(is, samplerCount);
          a.samplers.resize(samplerCount);
          for (auto &s : a.samplers) {
            readVal(is, s.interpolation);
            readVec(is, s.inputs);
            readVec(is, s.outputs);
          }
          readVec(is, a.channels);
        }

        void serializeSkin(std::ostream &os, const Skin &s) {
          writeStr(os, s.name);
          writeVec(os, s.inverseBindMatrices);
          writeVec(os, s.joints);
          writeVal(os, s.skeletonRootNode);
        }

        void deserializeSkin(std::istream &is, Skin &s) {
          readStr(is, s.name);
          readVec(is, s.inverseBindMatrices);
          readVec(is, s.joints);
          readVal(is, s.skeletonRootNode);
        }

        void serializeLight(std::ostream &os, const Light &l) {
          writeStr(os, l.m_name);
          writeVal(os, l.m_color);
          writeVal(os, l.m_direction);
          writeVal(os, l.m_intensity);
          writeVal(os, l.m_range);
          writeVal(os, l.m_innerConeAngle);
          writeVal(os, l.m_outerConeAngle);
          writeVal(os, l.m_type);
        }

        void deserializeLight(std::istream &is, Light &l) {
          readStr(is, l.m_name);
          readVal(is, l.m_color);
          readVal(is, l.m_direction);
          readVal(is, l.m_intensity);
          readVal(is, l.m_range);
          readVal(is, l.m_innerConeAngle);
          readVal(is, l.m_outerConeAngle);
          readVal(is, l.m_type);
        }

        void serializeCamera(std::ostream &os, const GltfCamera &c) {
          writeStr(os, c.name);
          writeVal(os, c.type);
          writeVal(os, c.yfovRad);
          writeVal(os, c.aspectRatio);
          writeVal(os, c.znear);
          writeVal(os, c.zfar);
          writeVal(os, c.xmag);
          writeVal(os, c.ymag);
        }

        void deserializeCamera(std::istream &is, GltfCamera &c) {
          readStr(is, c.name);
          readVal(is, c.type);
          readVal(is, c.yfovRad);
          readVal(is, c.aspectRatio);
          readVal(is, c.znear);
          readVal(is, c.zfar);
          readVal(is, c.xmag);
          readVal(is, c.ymag);
        }
    }

    bool ModelSerializer::savePMESH(const assets::ImportedModel& model, const std::filesystem::path& path)
    {
        CacheWriter writer(path.string());
        if (!writer.isOpen()) {
          return false;
        }

        writer.setHeaderVersion(PMESH_VERSION);

        auto serializeList = [&](uint32_t fcc, auto serializer, const auto& list) {
            uint64_t count = list.size();
            size_t headerPos = 0;
            writer.beginChunk(fcc, 1, headerPos);

            writeVal(writer.getStream(), count);
            for (const auto &item : list) {
              serializer(writer.getStream(), item);
            }

            writer.endChunk(headerPos);
        };

        serializeList(makeFourCC("TEXS"), serializeImportedTexture, model.textures);
        writer.writeChunk(makeFourCC("MATS"), 1, model.materials);
        serializeList(makeFourCC("MESH"), serializeImportedMesh, model.meshes);
        serializeList(makeFourCC("NODE"), serializeImportedNode, model.nodes);
        serializeList(makeFourCC("ANIM"), serializeAnimation, model.animations);
        serializeList(makeFourCC("SKIN"), serializeSkin, model.skins);
        serializeList(makeFourCC("LIGT"), serializeLight, model.lights);
        serializeList(makeFourCC("CAMS"), serializeCamera, model.cameras);
        writer.writeChunk(makeFourCC("ROOT"), 1, model.rootNodes);

        return true;
    }

    bool ModelSerializer::loadPMESH(assets::ImportedModel& model, const std::filesystem::path& path)
    {
        CacheReader reader(path.string());
        if (!reader.isOpen()) {
          return false;
        }

        if (reader.header().version != PMESH_VERSION) {
            core::Logger::Asset.warn("PMESH version mismatch for {}: expected {}, got {}",
                               path.string(), PMESH_VERSION, reader.header().version);
            return false;
        }

        auto chunks = reader.listChunks();
        if (!reader.isValid() || chunks.empty()) {
            core::Logger::Asset.error("PMESH cache corrupted or empty, deleting: {}", path.string());
            std::error_code ec;
            reader.getStream().close();
            std::filesystem::remove(path, ec);
            return false;
        }

        auto deserializeList = [&](const auto& chunk, auto deserializer, auto& list) -> bool {
            auto& stream = reader.getStream();
            stream.seekg(chunk.offset + sizeof(ChunkHeader));
            uint64_t count = 0;
            readVal(stream, count);
            if (!stream) {
              return false;
            }
            list.resize(count);
            for (auto& item : list) {
                deserializer(stream, item);
                if (!stream) {
                  return false;
                }
            }
            return true;
        };

        bool success = true;
        for (const auto& c : chunks) {
            uint32_t fcc = c.header.fourcc;
            bool chunkOk = true;
            if (fcc == makeFourCC("TEXS")) {
              chunkOk = deserializeList(c, deserializeImportedTexture,
                                        model.textures);
            } else if (fcc == makeFourCC("MATS")) {
              chunkOk = reader.readChunk(c, model.materials);
            } else if (fcc == makeFourCC("MESH")) {
              chunkOk =
                  deserializeList(c, deserializeImportedMesh, model.meshes);
            } else if (fcc == makeFourCC("NODE")) {
              chunkOk =
                  deserializeList(c, deserializeImportedNode, model.nodes);
            } else if (fcc == makeFourCC("ANIM")) {
              chunkOk =
                  deserializeList(c, deserializeAnimation, model.animations);
            } else if (fcc == makeFourCC("SKIN")) {
              chunkOk = deserializeList(c, deserializeSkin, model.skins);
            } else if (fcc == makeFourCC("LIGT")) {
              chunkOk = deserializeList(c, deserializeLight, model.lights);
            } else if (fcc == makeFourCC("CAMS")) {
              chunkOk = deserializeList(c, deserializeCamera, model.cameras);
            } else if (fcc == makeFourCC("ROOT")) {
              chunkOk = reader.readChunk(c, model.rootNodes);
            }
            if (!chunkOk) {
                success = false;
                break;
            }
        }

        if (!success) {
            core::Logger::Asset.error("PMESH cache '{}' is obsolete or corrupted. Deleting...", path.string());
            std::error_code ec;
            reader.getStream().close();
            std::filesystem::remove(path, ec);
            return false;
        }

        return true;
    }

    MaterialCPU ModelSerializer::toMaterialCPU(const MaterialData& md, const std::vector<TextureHandle>& textures)
    {
        MaterialCPU mc{};

        std::unordered_map<TextureHandle, int32_t> texToIndex;
        texToIndex.reserve(textures.size());
        for (size_t i = 0; i < textures.size(); ++i) {
          texToIndex[textures[i]] = (int32_t)i;
        }

        auto getTexIndex = [&](TextureHandle h) -> int32_t {
          if (h == INVALID_TEXTURE_HANDLE) {
            return -1;
          }
          auto it = texToIndex.find(h);
          return (it != texToIndex.end()) ? it->second : -1;
        };

        auto copySlot = [&](const TextureSlot& src, ImportedTextureSlot& dst) {
            dst.textureIndex = getTexIndex(src.texture);
            dst.sampler = src.sampler;
            dst.uvChannel = src.uvChannel;
            dst.transform = src.transform;
        };

        mc.baseColorFactor = md.baseColorFactor;
        mc.emissiveFactor = md.emissiveFactor;
        mc.emissiveStrength = md.emissiveStrength;

        mc.metallicFactor = md.metallicFactor;
        mc.roughnessFactor = md.roughnessFactor;
        mc.alphaCutoff = md.alphaCutoff;
        mc.ior = md.ior;

        mc.transmissionFactor = md.transmissionFactor;
        mc.clearcoatFactor = md.clearcoatFactor;
        mc.clearcoatRoughnessFactor = md.clearcoatRoughnessFactor;
        mc.clearcoatNormalScale = md.clearcoatNormalScale;

        mc.specularFactorScalar = md.specularFactorScalar;
        mc.specularColorFactor = md.specularColorFactor;

        mc.sheenColorFactor = md.sheenColorFactor;
        mc.sheenRoughnessFactor = md.sheenRoughnessFactor;

        mc.volumeThicknessFactor = md.volumeThicknessFactor;
        mc.volumeAttenuationDistance = md.volumeAttenuationDistance;
        mc.volumeAttenuationColor = md.volumeAttenuationColor;

        copySlot(md.baseColor, mc.baseColor);
        copySlot(md.normal, mc.normal);
        copySlot(md.metallicRoughness, mc.metallicRoughness);
        copySlot(md.occlusion, mc.occlusion);
        copySlot(md.emissive, mc.emissive);
        copySlot(md.clearcoat, mc.clearcoat);
        copySlot(md.clearcoatRoughness, mc.clearcoatRoughness);
        copySlot(md.clearcoatNormal, mc.clearcoatNormal);
        copySlot(md.specular, mc.specular);
        copySlot(md.specularColor, mc.specularColor);
        copySlot(md.transmission, mc.transmission);
        copySlot(md.sheenColor, mc.sheenColor);
        copySlot(md.sheenRoughness, mc.sheenRoughness);
        copySlot(md.thickness, mc.volumeThickness);
        copySlot(md.anisotropy, mc.anisotropy);
        copySlot(md.iridescence, mc.iridescence);
        copySlot(md.iridescenceThickness, mc.iridescenceThickness);

        mc.isUnlit = md.isUnlit;
        mc.isSpecularGlossiness = md.isSpecularGlossiness;
        mc.doubleSided = md.doubleSided;
        mc.hasSpecular = md.hasSpecular;
        mc.alphaMode = md.alphaMode;

        return mc;
    }

    MaterialData ModelSerializer::fromMaterialCPU(const MaterialCPU& mc, const std::vector<TextureHandle>& textures)
    {
        MaterialData md{};

        auto getTexHandle = [&](int32_t idx) -> TextureHandle {
          if (idx < 0 || static_cast<size_t>(idx) >= textures.size()) {
            return INVALID_TEXTURE_HANDLE;
          }
          return textures[idx];
        };

        auto copySlot = [&](const ImportedTextureSlot& src, TextureSlot& dst) {
            dst.texture = getTexHandle(src.textureIndex);
            dst.sampler = src.sampler;
            dst.uvChannel = src.uvChannel;
            dst.transform = src.transform;
        };

        md.volumeAttenuationDistance = mc.volumeAttenuationDistance;
        md.volumeAttenuationColor = mc.volumeAttenuationColor;
        md.emissiveStrength = mc.emissiveStrength;

        md.baseColorFactor = mc.baseColorFactor;
        md.emissiveFactor = mc.emissiveFactor;

        md.metallicFactor = mc.metallicFactor;
        md.roughnessFactor = mc.roughnessFactor;
        md.alphaCutoff = mc.alphaCutoff;
        md.ior = mc.ior;

        md.transmissionFactor = mc.transmissionFactor;
        md.clearcoatFactor = mc.clearcoatFactor;
        md.clearcoatRoughnessFactor = mc.clearcoatRoughnessFactor;
        md.clearcoatNormalScale = mc.clearcoatNormalScale;

        md.specularFactorScalar = mc.specularFactorScalar;
        md.specularColorFactor = mc.specularColorFactor;

        md.sheenColorFactor = mc.sheenColorFactor;
        md.sheenRoughnessFactor = mc.sheenRoughnessFactor;

        md.volumeThicknessFactor = mc.volumeThicknessFactor;
        md.volumeAttenuationDistance = mc.volumeAttenuationDistance;
        md.volumeAttenuationColor = mc.volumeAttenuationColor;

        copySlot(mc.baseColor, md.baseColor);
        copySlot(mc.normal, md.normal);
        copySlot(mc.metallicRoughness, md.metallicRoughness);
        copySlot(mc.occlusion, md.occlusion);
        copySlot(mc.emissive, md.emissive);
        copySlot(mc.clearcoat, md.clearcoat);
        copySlot(mc.clearcoatRoughness, md.clearcoatRoughness);
        copySlot(mc.clearcoatNormal, md.clearcoatNormal);
        copySlot(mc.specular, md.specular);
        copySlot(mc.specularColor, md.specularColor);
        copySlot(mc.transmission, md.transmission);
        copySlot(mc.sheenColor, md.sheenColor);
        copySlot(mc.sheenRoughness, md.sheenRoughness);
        copySlot(mc.volumeThickness, md.thickness);
        copySlot(mc.anisotropy, md.anisotropy);
        copySlot(mc.iridescence, md.iridescence);
        copySlot(mc.iridescenceThickness, md.iridescenceThickness);

        md.isUnlit = mc.isUnlit;
        md.isSpecularGlossiness = mc.isSpecularGlossiness;
        md.doubleSided = mc.doubleSided;
        md.hasSpecular = mc.hasSpecular;
        md.alphaMode = mc.alphaMode;

        return md;
    }

    core::Result<void> ModelSerializer::saveCache(ModelDOD& model, const std::filesystem::path& path)
    {
        CacheWriter writer(path.string());
        if (!writer.isOpen()) {
          return core::Unexpected<std::string>(
              "Failed to open ModelDOD cache for writing: " + path.string());
        }

        std::vector<MaterialCPU> matsCPU;
        const auto& materials = model.materials();
        const auto& textures = model.textures();
        const auto& textureFiles = model.textureFiles();
        const auto& textureIsSrgb = model.textureIsSrgb();

        matsCPU.reserve(materials.size());
        for (const auto &m : materials) {
          matsCPU.push_back(toMaterialCPU(m, textures));
        }
        writer.writeChunk(makeFourCC("MATL"), 1, matsCPU);
        writer.writeStringListChunk(makeFourCC("TXFN"), 1, textureFiles);

        std::vector<TextureMetaCPU> texMeta;
        texMeta.resize(textureFiles.size());
        for (size_t i = 0; i < texMeta.size(); ++i) {
          texMeta[i].m_isSrgb =
              (i < textureIsSrgb.size()) ? textureIsSrgb[i] : 1;
        }
        writer.writeChunk(makeFourCC("TXMD"), 1, texMeta);

        auto& reg = model.scene().registry();
        writer.writeSparseSet(makeFourCC("SLOC"), 1, reg.getPool<LocalTransform>());
        writer.writeSparseSet(makeFourCC("SGLO"), 1, reg.getPool<WorldTransform>());
        writer.writeSparseSet(makeFourCC("SHIE"), 1, reg.getPool<Relationship>());
        writer.writeSparseSet(makeFourCC("SMES"), 1, reg.getPool<MeshRenderer>());
        writer.writeCustomSparseSet(makeFourCC("SLIT"), 2, reg.getPool<LightSource>(), serializeLightSource);
        writer.writeSparseSet(makeFourCC("SCAM"), 1, reg.getPool<CameraComponent>());
        writer.writeSparseSet(makeFourCC("SSKN"), 1, reg.getPool<SkinComponent>());

        std::vector<std::string> names;
        for (auto entity : reg.view<Name>()) {
          names.push_back(reg.get<Name>(entity).str);
        }
        writer.writeStringListChunk(makeFourCC("SNMS"), 1, names);

        writer.writeChunk(makeFourCC("SROT"), 1, model.scene().roots());
        writer.writeChunk(makeFourCC("STOP"), 1, model.scene().topoOrder());

        std::vector<MeshRange> meshRanges;
        std::vector<PrimitiveDOD> allPrims;
        std::vector<std::string> meshNames;
        for (const auto& m : model.meshes()) {
            meshRanges.push_back({ (uint32_t)m.primitives.size() });
            meshNames.push_back(m.name);
            for (const auto &p : m.primitives) {
              allPrims.push_back(p);
            }
        }
        writer.writeChunk(makeFourCC("MRNG"), 1, meshRanges);
        writer.writeChunk(makeFourCC("MPRI"), 1, allPrims);
        writer.writeStringListChunk(makeFourCC("MNAM"), 1, meshNames);

        auto serializeList = [&](uint32_t fcc, auto serializer, const auto& list) {
            uint64_t count = list.size();
            size_t headerPos = 0;
            writer.beginChunk(fcc, 1, headerPos);
            writeVal(writer.getStream(), count);
            for (const auto &item : list) {
              serializer(writer.getStream(), item);
            }
            writer.endChunk(headerPos);
        };

        serializeList(makeFourCC("CSKN"), serializeSkin, model.skins());
        serializeList(makeFourCC("CANM"), serializeAnimation, model.animations());
        serializeList(makeFourCC("CCAM"), serializeCamera, model.cameras());

        static_assert(std::is_trivially_copyable_v<Vertex>, "Vertex must be trivially copyable for serialization");
        writer.writeChunk(makeFourCC("VERTS"), 1, model.cpuVerticesMutable());

        static_assert(std::is_trivially_copyable_v<uint32_t>, "uint32_t must be trivially copyable for serialization");
        writer.writeChunk(makeFourCC("INDXS"), 1, model.cpuIndicesMutable());

        writer.writeChunk(makeFourCC("MBND"), 1, model.meshBoundsMutable());

        const auto& morphInfos = model.morphTargetInfos();
        {
          size_t headerPos = 0;
          writer.beginChunk(makeFourCC("MORI"), 1, headerPos);
          auto count = static_cast<uint32_t>(morphInfos.size());
          writeVal(writer.getStream(), count);
          for (const auto &minfo : morphInfos) {
            writeVal(writer.getStream(), minfo.meshIndex);
            writeVec(writer.getStream(), minfo.targetOffsets);
          }
            writer.endChunk(headerPos);
        }

        return {};
    }

    core::Result<void> ModelSerializer::loadCache(ModelDOD& model, const std::filesystem::path& path, RHIRenderer& renderer)
    {
        CacheReader reader(path.string());
        if (!reader.isOpen()) {
          return core::Unexpected<std::string>(
              "Failed to open ModelDOD cache for reading: " + path.string());
        }

        auto chunks = reader.listChunks();
        if (!reader.isValid() || chunks.empty()) {
            core::Logger::Asset.error("ModelDOD cache corrupted or empty, deleting: {}", path.string());
            std::error_code ec;
            std::filesystem::remove(path, ec);
            return core::Unexpected<std::string>(
                "ModelDOD cache corrupted or empty: " + path.string());
        }

        std::vector<MaterialCPU> matsCPU;
        std::vector<MeshRange> meshRanges;
        std::vector<PrimitiveDOD> allPrims;
        std::vector<std::string> meshNames;
        std::vector<TextureMetaCPU> texMeta;

        bool hasGeometry = false;
        bool success = true;
        for (const auto& c : chunks) {
            uint32_t fcc = c.header.fourcc;
            auto& reg = model.scene().registry();
            if (fcc == makeFourCC("MATL")) {
              success &= reader.readChunk(c, matsCPU);
            } else if (fcc == makeFourCC("TXFN")) {
              success &=
                  reader.readStringListChunk(c, model.textureFilesMutable());
            } else if (fcc == makeFourCC("TXMD")) {
              success &= reader.readChunk(c, texMeta);
            } else if (fcc == makeFourCC("SLOC")) {
              success &= reader.readSparseSet(c, reg.getPool<LocalTransform>());
            } else if (fcc == makeFourCC("SGLO")) {
              success &= reader.readSparseSet(c, reg.getPool<WorldTransform>());
            } else if (fcc == makeFourCC("SHIE")) {
              success &= reader.readSparseSet(c, reg.getPool<Relationship>());
            } else if (fcc == makeFourCC("SMES")) {
              success &= reader.readSparseSet(c, reg.getPool<MeshRenderer>());
            } else if (fcc == makeFourCC("SLIT")) {
              if (c.header.version == 1) {
                success &= reader.readCustomSparseSet(
                    c, reg.getPool<LightSource>(), deserializeLightSourceV1);
              } else {
                success &= reader.readCustomSparseSet(
                    c, reg.getPool<LightSource>(), deserializeLightSource);
              }
            } else if (fcc == makeFourCC("SCAM")) {
              success &=
                  reader.readSparseSet(c, reg.getPool<CameraComponent>());
            } else if (fcc == makeFourCC("SSKN")) {
              success &= reader.readSparseSet(c, reg.getPool<SkinComponent>());
            } else if (fcc == makeFourCC("SNMS")) {
              std::vector<std::string> names;
              success &= reader.readStringListChunk(c, names);

            } else if (fcc == makeFourCC("SROT")) {
              success &= reader.readChunk(c, model.scene().roots());
            } else if (fcc == makeFourCC("STOP")) {
              success &= reader.readChunk(c, model.scene().topoOrder());
            } else if (fcc == makeFourCC("CSKN")) {
              auto &stream = reader.getStream();
              stream.seekg(c.offset + sizeof(ChunkHeader));
              uint64_t count = 0;
              readVal(stream, count);
              model.skinsMutable().resize(count);
              for (auto &item : model.skinsMutable()) {
                deserializeSkin(stream, item);
              }
            } else if (fcc == makeFourCC("CANM")) {
              auto &stream = reader.getStream();
              stream.seekg(c.offset + sizeof(ChunkHeader));
              uint64_t count = 0;
              readVal(stream, count);
              model.animationsMutable().resize(count);
              for (auto &item : model.animationsMutable()) {
                deserializeAnimation(stream, item);
              }
            } else if (fcc == makeFourCC("CCAM")) {
              auto &stream = reader.getStream();
              stream.seekg(c.offset + sizeof(ChunkHeader));
              uint64_t count = 0;
              readVal(stream, count);
              model.camerasMutable().resize(count);
              for (auto &item : model.camerasMutable()) {
                deserializeCamera(stream, item);
              }
            } else if (fcc == makeFourCC("MRNG")) {
              success &= reader.readChunk(c, meshRanges);
            } else if (fcc == makeFourCC("MPRI")) {
              success &= reader.readChunk(c, allPrims);
            } else if (fcc == makeFourCC("MNAM")) {
              success &= reader.readStringListChunk(c, meshNames);

            } else if (fcc == makeFourCC("VERTS")) {
              success &= reader.readChunk(c, model.cpuVerticesMutable());
              hasGeometry = true;
            } else if (fcc == makeFourCC("INDXS")) {
              success &= reader.readChunk(c, model.cpuIndicesMutable());
            } else if (fcc == makeFourCC("MBND")) {
              success &= reader.readChunk(c, model.meshBoundsMutable());
            } else if (fcc == makeFourCC("MORI")) {

              auto &stream = reader.getStream();
              stream.seekg(c.offset + sizeof(ChunkHeader));
              uint32_t count = 0;
              readVal(stream, count);
              auto &morphInfos = model.morphTargetInfos();
              morphInfos.resize(count);
              for (uint32_t i = 0; i < count; ++i) {
                readVal(stream, morphInfos[i].meshIndex);
                readVec(stream, morphInfos[i].targetOffsets);
              }
            }
        }

        if (!success) {
            core::Logger::Asset.error("Failed to read one or more chunks in ModelDOD cache: {}", path.string());
            return core::Unexpected<std::string>(
                "Failed to read one or more ModelDOD cache chunks: " + path.string());
        }

        auto& meshes = model.meshesMutable();
        meshes.clear();
        size_t primOffset = 0;
        for (size_t i = 0; i < meshRanges.size(); ++i) {
            MeshDOD m;
            m.name = (i < meshNames.size()) ? meshNames[i] : "";
            for (size_t p = 0; p < meshRanges[i].m_primCount; ++p) {
              if (primOffset < allPrims.size()) {
                m.primitives.push_back(allPrims[primOffset++]);
              }
            }
            meshes.push_back(std::move(m));
        }

        auto& textures = model.texturesMutable();
        auto& textureFiles = model.textureFilesMutable();
        auto& textureIsSrgb = model.textureIsSrgbMutable();
        textures.clear();
        textureIsSrgb.clear();
        textures.reserve(textureFiles.size());
        textureIsSrgb.reserve(textureFiles.size());
        for (size_t i = 0; i < textureFiles.size(); ++i) {
            const auto& texPath = textureFiles[i];
            const bool srgb =
                (i < texMeta.size()) ? (texMeta[i].m_isSrgb != 0) : true;
            textureIsSrgb.push_back(srgb ? 1 : 0);

            if (texPath.empty()) {
                textures.push_back(INVALID_TEXTURE_HANDLE);
            } else {
                auto handle = renderer.assets()->loadTextureKTX(texPath, srgb);
                textures.push_back(handle);
            }
        }

        auto& materials = model.materialsMutable();
        materials.clear();
        for (const auto& mc : matsCPU) {
            materials.push_back(fromMaterialCPU(mc, textures));
        }

        auto& reg = model.scene().registry();
        auto meshView = reg.view<MeshRenderer>();
        meshView.each([&](ecs::Entity e, MeshRenderer& mr) {
            if (!reg.has<LocalBounds>(e)) {
              auto &lb = reg.emplace<LocalBounds>(e);
              const int32_t meshId = mr.meshID;
              const auto &meshBounds = model.meshBounds();
              if (meshId >= 0 &&
                  static_cast<size_t>(meshId) < meshBounds.size()) {
                lb.aabb = meshBounds[meshId];
              }
            }
            if (!reg.has<WorldBounds>(e)) {
              reg.emplace<WorldBounds>(e);
            }
            if (!reg.has<Visibility>(e)) {
              reg.emplace<Visibility>(e);
            }
            if (!reg.has<BoundsDirtyTag>(e)) {
              reg.emplace<BoundsDirtyTag>(e);
            }
        });

        model.scene().onHierarchyChanged();

        if (hasGeometry) {
            model.uploadUnifiedBuffers(renderer);
        } else {
            core::Logger::Asset.warn("ModelDOD cache for {} does not contain geometry data. Vertex/index buffers will not be created.", path.string());
        }

        return {};
    }
}
