#include "pnkr/assets/GLTFParser.hpp"
#include "pnkr/renderer/geometry/GeometryUtils.hpp"
#include "pnkr/assets/GeometryProcessor.hpp"
#include "pnkr/renderer/scene/GLTFUtils.hpp"
#include "pnkr/renderer/scene/Light.hpp"
#include "pnkr/core/logger.hpp"
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace pnkr::assets {

    namespace {
        renderer::rhi::SamplerAddressMode getSamplerAddressMode(const fastgltf::Asset& gltf, size_t textureIndex) {
            if (textureIndex >= gltf.textures.size()) {
                return renderer::rhi::SamplerAddressMode::Repeat;
            }
            const auto& tex = gltf.textures[textureIndex];
            if (!tex.samplerIndex.has_value() ||
                tex.samplerIndex.value() >= gltf.samplers.size()) {
                return renderer::rhi::SamplerAddressMode::Repeat;
            }
            const auto& sampler = gltf.samplers[tex.samplerIndex.value()];
            return renderer::scene::toAddressMode(sampler.wrapS);
        }

        glm::vec4 getTextureTransform(const std::optional<fastgltf::TextureInfo>& info) {
            if (info.has_value() && info->transform) {
                const auto& t = *info->transform;
                return { t.uvOffset[0], t.uvOffset[1], t.uvScale[0], t.uvScale[1] };
            }
            return { 0.0F, 0.0F, 1.0F, 1.0F };
        }

        glm::vec4 getTextureTransform(const std::optional<fastgltf::NormalTextureInfo>& info) {
            if (info.has_value() && info->transform) {
                const auto& t = *info->transform;
                return { t.uvOffset[0], t.uvOffset[1], t.uvScale[0], t.uvScale[1] };
            }
            return { 0.0F, 0.0F, 1.0F, 1.0F };
        }

        glm::vec4 getTextureTransform(const std::optional<fastgltf::OcclusionTextureInfo>& info) {
            if (info.has_value() && info->transform) {
                const auto& t = *info->transform;
                return { t.uvOffset[0], t.uvOffset[1], t.uvScale[0], t.uvScale[1] };
            }
            return { 0.0F, 0.0F, 1.0F, 1.0F };
        }

        void fillImportedTextureSlot(const fastgltf::Asset& gltf,
            const std::optional<fastgltf::TextureInfo>& info,
            ImportedTextureSlot& slot)
        {
            if (info)
            {
                slot.textureIndex = (int32_t)info->textureIndex;
                slot.uvChannel = (uint32_t)info->texCoordIndex;
                slot.sampler = getSamplerAddressMode(gltf, info->textureIndex);
                slot.transform = getTextureTransform(info);
            }
        }

        void fillImportedTextureSlot(const fastgltf::Asset& gltf,
            const std::optional<fastgltf::NormalTextureInfo>& info,
            ImportedTextureSlot& slot)
        {
            if (info)
            {
                slot.textureIndex = (int32_t)info->textureIndex;
                slot.uvChannel = (uint32_t)info->texCoordIndex;
                slot.sampler = getSamplerAddressMode(gltf, info->textureIndex);
                slot.transform = getTextureTransform(info);
            }
        }

        void fillImportedTextureSlot(const fastgltf::Asset& gltf,
            const std::optional<fastgltf::OcclusionTextureInfo>& info,
            ImportedTextureSlot& slot)
        {
            if (info)
            {
                slot.textureIndex = (int32_t)info->textureIndex;
                slot.uvChannel = (uint32_t)info->texCoordIndex;
                slot.sampler = getSamplerAddressMode(gltf, info->textureIndex);
                slot.transform = getTextureTransform(info);
            }
        }

        uint32_t toAlphaMode(fastgltf::AlphaMode mode) {
            switch (mode) {
            case fastgltf::AlphaMode::Mask:
                return 1U;
            case fastgltf::AlphaMode::Blend:
                return 2U;
            case fastgltf::AlphaMode::Opaque:
            default:
                return 0U;
            }
        }

        glm::mat4 getLocalMatrix(const fastgltf::Node& node) {
            return std::visit(
                fastgltf::visitor{
                    [&](const fastgltf::TRS& trs) {
                      const glm::vec3 t = glm::make_vec3(trs.translation.data());
                      const auto& q = trs.rotation;
                      const glm::quat r(q[3], q[0], q[1], q[2]);
                      const glm::vec3 s = glm::make_vec3(trs.scale.data());
                      return glm::translate(glm::mat4(1.0F), t) * glm::toMat4(r) *
                             glm::scale(glm::mat4(1.0F), s);
                    },
                    [&](const fastgltf::math::fmat4x4& m) {
                      return glm::make_mat4(m.data());
                    } },
                node.transform);
        }

        template <typename T, size_t numComp>
        bool tryFastPath(const fastgltf::Asset& gltf,
            const fastgltf::Accessor& accessor,
            std::vector<renderer::Vertex>& vertices,
            size_t offsetInVertex) {
            if (!accessor.bufferViewIndex.has_value()) {
                return false;
            }
            const auto& bufferView = gltf.bufferViews[*accessor.bufferViewIndex];

            if (accessor.componentType != fastgltf::ComponentType::Float ||
                accessor.type !=
                (numComp == 3
                    ? fastgltf::AccessorType::Vec3
                    : (numComp == 2
                        ? fastgltf::AccessorType::Vec2
                        : (numComp == 4
                            ? fastgltf::AccessorType::Vec4
                            : fastgltf::AccessorType::Scalar)))) {
                return false;
            }

            const size_t expectedStride = numComp * sizeof(float);
            const bool tightlyPacked = !bufferView.byteStride ||
                *bufferView.byteStride == expectedStride;

            if (!tightlyPacked) {
                return false;
            }

            const auto& buffer = gltf.buffers[bufferView.bufferIndex];
            const uint8_t* bufferData = nullptr;

            std::visit(
                fastgltf::visitor{
                    [&](const fastgltf::sources::Vector& v) {
                      bufferData =
                          reinterpret_cast<const uint8_t*>(v.bytes.data());
                    },
                    [&](const fastgltf::sources::ByteView& v) {
                      bufferData =
                          reinterpret_cast<const uint8_t*>(v.bytes.data());
                    },
                    [&](const fastgltf::sources::Array& v) {
                      bufferData =
                          reinterpret_cast<const uint8_t*>(v.bytes.data());
                    },
                    [](const auto&) {}},
                buffer.data);

            if (!bufferData) {
                return false;
            }

            const uint8_t* src =
                bufferData + bufferView.byteOffset + accessor.byteOffset;
            uint8_t* dstBase =
                reinterpret_cast<uint8_t*>(vertices.data()) + offsetInVertex;

            for (size_t i = 0; i < accessor.count; ++i) {
                std::copy_n(src + (i * expectedStride), expectedStride,
                    dstBase + (i * sizeof(renderer::Vertex)));
            }

            return true;
        }

    } // namespace

    void GLTFParser::populateModel(ImportedModel& model, const fastgltf::Asset& gltf, LoadProgress* progress)
    {
        if (progress != nullptr) {
            uint32_t primTotal = 0;
            for (const auto& mesh : gltf.meshes) {
                primTotal += (uint32_t)mesh.primitives.size();
            }
            progress->meshesTotal.store(primTotal, std::memory_order_relaxed);
            // Note: LoadStage::LoadingTextures is set by AssetImporter usually
        }

        model.lights.reserve(gltf.lights.size());
        for (const auto& gLight : gltf.lights)
        {
            renderer::scene::Light l{};
            l.m_name = gLight.name;
            l.m_color = glm::make_vec3(gLight.color.data());
            l.m_intensity = gLight.intensity;
            l.m_range = gLight.range.value_or(0.0F);
            if (gLight.type == fastgltf::LightType::Directional) {
                l.m_type = renderer::scene::LightType::Directional;
            }
            else if (gLight.type == fastgltf::LightType::Point) {
                l.m_type = renderer::scene::LightType::Point;
            }
            else if (gLight.type == fastgltf::LightType::Spot) {
                l.m_type = renderer::scene::LightType::Spot;
                l.m_innerConeAngle = gLight.innerConeAngle.value_or(0.0F);
                l.m_outerConeAngle = gLight.outerConeAngle.value_or(0.785398F);
            }
            model.lights.push_back(l);
        }

        model.cameras.reserve(gltf.cameras.size());
        for (const auto& c : gltf.cameras)
        {
            renderer::scene::GltfCamera out{};
            out.name = c.name;

            std::visit(fastgltf::visitor{
                           [&](const fastgltf::Camera::Perspective& p)
                           {
                               out.type = renderer::scene::GltfCamera::Type::Perspective;
                               out.yfovRad = p.yfov;
                               out.aspectRatio = p.aspectRatio.value_or(0.0F);
                               out.znear = p.znear;
                               out.zfar = p.zfar.value_or(0.0F);
                           },
                           [&](const fastgltf::Camera::Orthographic& o)
                           {
                               out.type = renderer::scene::GltfCamera::Type::Orthographic;
                               out.xmag = o.xmag;
                               out.ymag = o.ymag;
                               out.znear = o.znear;
                               out.zfar = o.zfar;
                           }
                }, c.camera);

            model.cameras.push_back(std::move(out));
        }

        // Texture loading is handled by AssetImporter / TextureCacheSystem
        // Here we just skip to materials, assuming texture indices are valid.

        {
            model.materials.reserve(gltf.materials.size());
            for (const auto& mat : gltf.materials)
            {
                ImportedMaterial im{};
                im.doubleSided = mat.doubleSided;
                im.isUnlit = mat.unlit;
                im.alphaMode = toAlphaMode(mat.alphaMode);
                im.alphaCutoff = mat.alphaCutoff;
                im.emissiveFactor = glm::make_vec3(mat.emissiveFactor.data());
                im.emissiveStrength = mat.emissiveStrength;
                im.ior = mat.ior;
                im.normalScale =
                    mat.normalTexture ? mat.normalTexture->scale : 1.0F;
                im.occlusionStrength = mat.occlusionTexture
                    ? mat.occlusionTexture->strength
                    : 1.0F;

                if (mat.specularGlossiness)
                {
                    im.isSpecularGlossiness = true;
                    im.baseColorFactor = glm::make_vec4(mat.specularGlossiness->diffuseFactor.data());
                    im.specularFactor = glm::make_vec3(mat.specularGlossiness->specularFactor.data());
                    im.glossinessFactor = mat.specularGlossiness->glossinessFactor;
                    im.metallicFactor = 0.0F;
                    im.roughnessFactor = 1.0F - im.glossinessFactor;

                    fillImportedTextureSlot(gltf, mat.specularGlossiness->diffuseTexture, im.baseColor);
                    fillImportedTextureSlot(gltf, mat.specularGlossiness->specularGlossinessTexture, im.metallicRoughness);
                }
                else
                {
                    const auto& pbr = mat.pbrData;
                    im.baseColorFactor = glm::make_vec4(pbr.baseColorFactor.data());
                    im.metallicFactor = pbr.metallicFactor;
                    im.roughnessFactor = pbr.roughnessFactor;

                    fillImportedTextureSlot(gltf, pbr.baseColorTexture, im.baseColor);
                    fillImportedTextureSlot(gltf, pbr.metallicRoughnessTexture, im.metallicRoughness);
                }

                fillImportedTextureSlot(gltf, mat.normalTexture, im.normal);
                fillImportedTextureSlot(gltf, mat.occlusionTexture, im.occlusion);
                fillImportedTextureSlot(gltf, mat.emissiveTexture, im.emissive);

                if (mat.transmission)
                {
                    im.transmissionFactor = mat.transmission->transmissionFactor;
                    fillImportedTextureSlot(gltf, mat.transmission->transmissionTexture, im.transmission);
                }
                if (mat.clearcoat)
                {
                    im.clearcoatFactor = mat.clearcoat->clearcoatFactor;
                    im.clearcoatRoughnessFactor = mat.clearcoat->clearcoatRoughnessFactor;
                    im.clearcoatNormalScale =
                        mat.clearcoat->clearcoatNormalTexture
                        ? mat.clearcoat->clearcoatNormalTexture->scale
                        : 1.0F;
                    fillImportedTextureSlot(gltf, mat.clearcoat->clearcoatTexture, im.clearcoat);
                    fillImportedTextureSlot(gltf, mat.clearcoat->clearcoatRoughnessTexture, im.clearcoatRoughness);
                    fillImportedTextureSlot(gltf, mat.clearcoat->clearcoatNormalTexture, im.clearcoatNormal);
                }
                if (mat.specular)
                {
                    im.hasSpecular = true;
                    im.specularFactorScalar = mat.specular->specularFactor;
                    im.specularColorFactor = glm::make_vec3(mat.specular->specularColorFactor.data());
                    fillImportedTextureSlot(gltf, mat.specular->specularTexture, im.specular);
                    fillImportedTextureSlot(gltf, mat.specular->specularColorTexture, im.specularColor);
                }
                if (mat.sheen)
                {
                    im.sheenColorFactor = glm::make_vec3(mat.sheen->sheenColorFactor.data());
                    im.sheenRoughnessFactor = mat.sheen->sheenRoughnessFactor;
                    fillImportedTextureSlot(gltf, mat.sheen->sheenColorTexture, im.sheenColor);
                    fillImportedTextureSlot(gltf, mat.sheen->sheenRoughnessTexture, im.sheenRoughness);
                }
                if (mat.volume)
                {
                    im.volumeThicknessFactor = mat.volume->thicknessFactor;
                    im.volumeAttenuationDistance = mat.volume->attenuationDistance;
                    im.volumeAttenuationColor = glm::make_vec3(mat.volume->attenuationColor.data());
                    if (im.volumeAttenuationDistance == 0.0F) {
                        im.volumeAttenuationDistance =
                            std::numeric_limits<float>::infinity();
                    }
                    fillImportedTextureSlot(gltf, mat.volume->thicknessTexture, im.volumeThickness);
                }
                if (mat.anisotropy)
                {
                    im.anisotropyFactor = mat.anisotropy->anisotropyStrength;
                    im.anisotropyRotation = mat.anisotropy->anisotropyRotation;
                    fillImportedTextureSlot(gltf, mat.anisotropy->anisotropyTexture, im.anisotropy);
                }
                if (mat.iridescence)
                {
                    im.iridescenceFactor = mat.iridescence->iridescenceFactor;
                    im.iridescenceIor = mat.iridescence->iridescenceIor;
                    im.iridescenceThicknessMinimum = mat.iridescence->iridescenceThicknessMinimum;
                    im.iridescenceThicknessMaximum = mat.iridescence->iridescenceThicknessMaximum;
                    fillImportedTextureSlot(gltf, mat.iridescence->iridescenceTexture, im.iridescence);
                    fillImportedTextureSlot(gltf, mat.iridescence->iridescenceThicknessTexture, im.iridescenceThickness);
                }

                model.materials.push_back(im);
            }
        }
        if (model.materials.empty()) {
            model.materials.push_back({});
        }

        auto defaultMaterialIndex =
            static_cast<uint32_t>(model.materials.size());
        model.materials.push_back({});

        {
            if (progress != nullptr) {
                progress->currentStage.store(LoadStage::ProcessingMeshes,
                    std::memory_order_relaxed);
            }

            for (size_t meshIdx = 0; meshIdx < gltf.meshes.size(); ++meshIdx)
            {
                const auto& gMesh = gltf.meshes[meshIdx];
                ImportedMesh impMesh;
                impMesh.name = gMesh.name;
                impMesh.primitives.reserve(gMesh.primitives.size());

                for (const auto& gPrim : gMesh.primitives)
                {
                    if (progress != nullptr) {
                        progress->meshesProcessed.fetch_add(
                            1, std::memory_order_relaxed);
                    }
                    ImportedPrimitive impPrim;
                    impPrim.materialIndex = gPrim.materialIndex.has_value()
                        ? static_cast<uint32_t>(gPrim.materialIndex.value())
                        : defaultMaterialIndex;

                    const auto* itPos = gPrim.findAttribute("POSITION");
                    if (itPos == gPrim.attributes.end()) {
                        continue;
                    }

                    const auto& posAccessor = gltf.accessors[itPos->accessorIndex];
                    size_t vCount = posAccessor.count;
                    impPrim.vertices.resize(vCount);

                    for (size_t i = 0; i < vCount; ++i)
                    {
                        impPrim.vertices[i].position =
                            glm::vec4(0.0F, 0.0F, 0.0F, 1.0F);
                        impPrim.vertices[i].color = glm::vec4(1.0F);
                        impPrim.vertices[i].normal =
                            glm::vec4(0.0F, 1.0F, 0.0F, 0.0F);
                        impPrim.vertices[i].meshIndex = (uint32_t)meshIdx;
                        impPrim.vertices[i].localIndex = (uint32_t)i;
                    }

                    glm::vec3 pMin(std::numeric_limits<float>::max());
                    glm::vec3 pMax(std::numeric_limits<float>::lowest());

                    if (!tryFastPath<glm::vec3, 3>(gltf, posAccessor, impPrim.vertices,
                        offsetof(renderer::Vertex, position)))
                    {
                        fastgltf::iterateAccessorWithIndex<glm::vec3>(
                            gltf, posAccessor, [&](glm::vec3 pos, size_t idx) {
                                impPrim.vertices[idx].position =
                                    glm::vec4(pos, 1.0F);
                                pMin = glm::min(pMin, pos);
                                pMax = glm::max(pMax, pos);
                            });
                    }
                    else
                    {

                        for (const auto& v : impPrim.vertices)
                        {
                            pMin = glm::min(pMin, glm::vec3(v.position));
                            pMax = glm::max(pMax, glm::vec3(v.position));
                        }
                    }

                    impPrim.minPos = pMin;
                    impPrim.maxPos = pMax;

                    if (const auto* it = gPrim.findAttribute("NORMAL"); it != gPrim.attributes.end())
                    {
                        if (!tryFastPath<glm::vec3, 3>(gltf, gltf.accessors[it->accessorIndex], impPrim.vertices,
                            offsetof(renderer::Vertex, normal)))
                        {
                            fastgltf::iterateAccessorWithIndex<glm::vec3>(
                                gltf, gltf.accessors[it->accessorIndex],
                                [&](glm::vec3 norm, size_t idx) {
                                    impPrim.vertices[idx].normal =
                                        glm::vec4(norm, 0.0F);
                                });
                        }
                    }
                    if (const auto* it = gPrim.findAttribute("TEXCOORD_0"); it != gPrim.attributes.end())
                    {
                        if (!tryFastPath<glm::vec2, 2>(gltf, gltf.accessors[it->accessorIndex], impPrim.vertices,
                            offsetof(renderer::Vertex, uv0)))
                        {
                            fastgltf::iterateAccessorWithIndex<glm::vec2>(
                                gltf, gltf.accessors[it->accessorIndex], [&](glm::vec2 uv, size_t idx)
                                {
                                    impPrim.vertices[idx].uv0 = uv;
                                });
                        }
                    }
                    if (const auto* it = gPrim.findAttribute("TEXCOORD_1"); it != gPrim.attributes.end())
                    {
                        if (!tryFastPath<glm::vec2, 2>(gltf, gltf.accessors[it->accessorIndex], impPrim.vertices,
                            offsetof(renderer::Vertex, uv1)))
                        {
                            fastgltf::iterateAccessorWithIndex<glm::vec2>(
                                gltf, gltf.accessors[it->accessorIndex], [&](glm::vec2 uv, size_t idx)
                                {
                                    impPrim.vertices[idx].uv1 = uv;
                                });
                        }
                    }
                    if (const auto* it = gPrim.findAttribute("COLOR_0"); it != gPrim.attributes.end())
                    {
                        const auto& accessor = gltf.accessors[it->accessorIndex];
                        if (accessor.type == fastgltf::AccessorType::Vec4)
                        {
                            if (!tryFastPath<glm::vec4, 4>(gltf, accessor, impPrim.vertices,
                                offsetof(renderer::Vertex, color)))
                            {
                                fastgltf::iterateAccessorWithIndex<glm::vec4>(
                                    gltf, accessor,
                                    [&](glm::vec4 col, size_t idx) {
                                        if (idx < 5) {
                                            core::Logger::Scene.info(
                                                "Loaded Vec4 Color {}: {}, {}, {}, "
                                                "{}",
                                                idx, col.r, col.g, col.b, col.a);
                                        }
                                        impPrim.vertices[idx].color = col;
                                    });
                            }
                        }
                        else
                        {
                            if (!tryFastPath<glm::vec3, 3>(gltf, accessor, impPrim.vertices,
                                offsetof(renderer::Vertex, color)))
                            {
                                fastgltf::iterateAccessorWithIndex<glm::vec3>(
                                    gltf, accessor,
                                    [&](glm::vec3 col, size_t idx) {
                                        impPrim.vertices[idx].color =
                                            glm::vec4(col, 1.0F);
                                    });
                            }
                        }
                    }
                    bool hasTangents = false;
                    if (const auto* it = gPrim.findAttribute("TANGENT"); it != gPrim.attributes.end())
                    {
                        hasTangents = true;
                        if (!tryFastPath<glm::vec4, 4>(gltf, gltf.accessors[it->accessorIndex], impPrim.vertices,
                            offsetof(renderer::Vertex, tangent)))
                        {
                            fastgltf::iterateAccessorWithIndex<glm::vec4>(
                                gltf, gltf.accessors[it->accessorIndex], [&](glm::vec4 tan, size_t idx)
                                {
                                    impPrim.vertices[idx].tangent = tan;
                                });
                        }
                    }
                    if (const auto* it = gPrim.findAttribute("JOINTS_0"); it != gPrim.attributes.end())
                    {
                        fastgltf::iterateAccessorWithIndex<glm::uvec4>(gltf, gltf.accessors[it->accessorIndex],
                            [&](glm::uvec4 joints, size_t idx)
                            {
                                impPrim.vertices[idx].joints = joints;
                            });
                    }
                    if (const auto* it = gPrim.findAttribute("WEIGHTS_0"); it != gPrim.attributes.end())
                    {
                        fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[it->accessorIndex],
                            [&](glm::vec4 weights, size_t idx)
                            {
                                impPrim.vertices[idx].weights = weights;
                            });
                    }

                    if (gPrim.indicesAccessor.has_value())
                    {
                        const auto& acc = gltf.accessors[gPrim.indicesAccessor.value()];
                        impPrim.indices.resize(acc.count);
                        if (acc.componentType == fastgltf::ComponentType::UnsignedByte)
                        {
                            fastgltf::iterateAccessorWithIndex<std::uint8_t>(gltf, acc, [&](std::uint8_t v, size_t i)
                                {
                                    impPrim.indices[i] = v;
                                });
                        }
                        else if (acc.componentType == fastgltf::ComponentType::UnsignedShort)
                        {
                            fastgltf::iterateAccessorWithIndex<std::uint16_t>(gltf, acc, [&](std::uint16_t v, size_t i)
                                {
                                    impPrim.indices[i] = v;
                                });
                        }
                        else
                        {
                            fastgltf::iterateAccessorWithIndex<std::uint32_t>(gltf, acc, [&](std::uint32_t v, size_t i)
                                {
                                    impPrim.indices[i] = v;
                                });
                        }
                    }
                    else
                    {
                        impPrim.indices.resize(vCount);
                        for (size_t i = 0; i < vCount; ++i) {
                            impPrim.indices[i] = static_cast<uint32_t>(i);
                        }
                    }

                    if (!hasTangents &&
                        gPrim.findAttribute("NORMAL") != gPrim.attributes.end() &&
                        gPrim.findAttribute("TEXCOORD_0") != gPrim.attributes.end())
                    {
                        GeometryProcessor::generateTangents(impPrim);
                    }

                    if (!gPrim.targets.empty())
                    {
                        for (const auto& gTarget : gPrim.targets)
                        {
                            ImportedPrimitive::MorphTarget impTarget;
                            impTarget.positionDeltas.resize(vCount,
                                glm::vec3(0.0F));
                            impTarget.normalDeltas.resize(vCount,
                                glm::vec3(0.0F));
                            impTarget.tangentDeltas.resize(vCount,
                                glm::vec3(0.0F));

                            const auto* posIt = std::ranges::find_if(
                                gTarget, [](const fastgltf::Attribute& a) {
                                    return a.name == "POSITION";
                                });
                            if (posIt != gTarget.end())
                            {
                                fastgltf::iterateAccessorWithIndex<glm::vec3>(
                                    gltf, gltf.accessors[posIt->accessorIndex],
                                    [&](glm::vec3 v, size_t idx) { impTarget.positionDeltas[idx] = v; });
                            }

                            const auto* normIt = std::ranges::find_if(
                                gTarget, [](const fastgltf::Attribute& a) {
                                    return a.name == "NORMAL";
                                });
                            if (normIt != gTarget.end())
                            {
                                fastgltf::iterateAccessorWithIndex<glm::vec3>(
                                    gltf, gltf.accessors[normIt->accessorIndex],
                                    [&](glm::vec3 v, size_t idx) { impTarget.normalDeltas[idx] = v; });
                            }

                            const auto* tangIt = std::ranges::find_if(
                                gTarget, [](const fastgltf::Attribute& a) {
                                    return a.name == "TANGENT";
                                });
                            if (tangIt != gTarget.end())
                            {
                                fastgltf::iterateAccessorWithIndex<glm::vec3>(
                                    gltf, gltf.accessors[tangIt->accessorIndex],
                                    [&](glm::vec3 v, size_t idx) { impTarget.tangentDeltas[idx] = v; });
                            }
                            impPrim.targets.push_back(std::move(impTarget));
                        }
                    }

                    impMesh.primitives.push_back(std::move(impPrim));
                }
                model.meshes.push_back(std::move(impMesh));
            }
        }

        renderer::scene::loadSkins(gltf, model.skins, [](size_t i) { return (uint32_t)i; });
        renderer::scene::loadAnimations(gltf, model.animations, [](size_t i) { return (uint32_t)i; });

        model.nodes.resize(gltf.nodes.size());
        for (size_t i = 0; i < gltf.nodes.size(); ++i)
        {
            const auto& gNode = gltf.nodes[i];
            ImportedNode& imNode = model.nodes[i];
            imNode.name = gNode.name;
            imNode.meshIndex = gNode.meshIndex.has_value() ? (int)gNode.meshIndex.value() : -1;
            imNode.lightIndex = gNode.lightIndex.has_value() ? (int)gNode.lightIndex.value() : -1;
            imNode.cameraIndex = gNode.cameraIndex.has_value() ? (int)gNode.cameraIndex.value() : -1;
            imNode.skinIndex = gNode.skinIndex.has_value() ? (int)gNode.skinIndex.value() : -1;

            imNode.localTransform = getLocalMatrix(gNode);
            imNode.children.reserve(gNode.children.size());

            for (auto childIdx : gNode.children)
            {
                imNode.children.push_back((int)childIdx);
                model.nodes[childIdx].parentIndex = (int)i;
            }
        }

        const size_t sceneIdx = gltf.defaultScene.value_or(0);
        if (sceneIdx < gltf.scenes.size())
        {
            const auto& scene = gltf.scenes[sceneIdx];

            ImportedNode sceneRoot;
            sceneRoot.name = scene.name.empty() ? "SceneRoot" : scene.name;
            sceneRoot.localTransform = glm::mat4(1.0F);
            sceneRoot.parentIndex = -1;

            const auto sceneRootIdx = static_cast<int>(model.nodes.size());

            for (auto nodeIdx : scene.nodeIndices)
            {
                sceneRoot.children.push_back(static_cast<int>(nodeIdx));
                model.nodes[nodeIdx].parentIndex = sceneRootIdx;
            }

            model.nodes.push_back(std::move(sceneRoot));
            model.rootNodes.push_back(sceneRootIdx);
        }
    }
}
