#include "pnkr/renderer/scene/SceneUploader.hpp"
#include "pnkr/renderer/scene/ModelDOD.hpp"
#include "pnkr/renderer/scene/Components.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/renderer/scene/MaterialType.hpp"

namespace pnkr::renderer::scene {

    namespace {
        inline void copyTextureSlot(
            const TextureSlot& slot,
            RHIRenderer& renderer,
            uint32_t& outTexture,
            uint32_t& outSampler,
            uint32_t& outUV,
            glm::vec4& outTransform)
        {
            outTexture = util::u32(renderer.getTextureBindlessIndex(slot.texture));
            outSampler = util::u32(renderer.getBindlessSamplerIndex(slot.sampler));
            outUV = slot.uvChannel;
            outTransform = slot.transform;
        }
    }

    std::vector<gpu::MaterialDataGPU> SceneUploader::packMaterials(std::span<const MaterialData> materials, RHIRenderer& renderer) {
        std::vector<gpu::MaterialDataGPU> result;
        result.reserve(materials.size());

        for (const auto& mat : materials) {
            gpu::MaterialDataGPU gpuMat{};
            gpuMat.baseColorFactor = mat.baseColorFactor;

            gpuMat.metallicFactor = mat.metallicFactor;
            gpuMat.roughnessFactor = mat.roughnessFactor;
            gpuMat.normalScale = mat.normalScale;
            gpuMat.occlusionStrength = mat.occlusionStrength;

            gpuMat.emissiveFactor = mat.emissiveFactor * mat.emissiveStrength;
            gpuMat.alphaCutoff = mat.alphaCutoff;

            gpuMat.specularGlossiness = glm::vec4(mat.specularFactor, mat.glossinessFactor);
            gpuMat.specularFactors = glm::vec4(mat.specularColorFactor, mat.specularFactorScalar);

            gpuMat.clearcoatFactor = mat.clearcoatFactor;
            gpuMat.clearcoatRoughnessFactor = mat.clearcoatRoughnessFactor;
            gpuMat.clearcoatNormalScale = mat.clearcoatNormalScale;
            gpuMat.transmissionFactor = mat.transmissionFactor;
            gpuMat.thicknessFactor = mat.volumeThicknessFactor;

            gpuMat.anisotropyFactor = mat.anisotropyFactor;
            gpuMat.anisotropyRotation = mat.anisotropyRotation;

            copyTextureSlot(mat.anisotropy, renderer,
                gpuMat.anisotropyTexture, gpuMat.anisotropySampler,
                gpuMat.anisotropyTextureUV, gpuMat.anisotropyTransform);

            gpuMat.iridescenceFactor = mat.iridescenceFactor;
            gpuMat.iridescenceIor = mat.iridescenceIor;
            gpuMat.iridescenceThicknessMinimum = mat.iridescenceThicknessMinimum;
            gpuMat.iridescenceThicknessMaximum = mat.iridescenceThicknessMaximum;

            copyTextureSlot(mat.iridescence, renderer,
                gpuMat.iridescenceTexture, gpuMat.iridescenceSampler,
                gpuMat.iridescenceTextureUV, gpuMat.iridescenceTransform);

            copyTextureSlot(mat.iridescenceThickness, renderer,
                gpuMat.iridescenceThicknessTexture, gpuMat.iridescenceThicknessSampler,
                gpuMat.iridescenceThicknessUV, gpuMat.iridescenceThicknessTransform);

            gpuMat.attenuation = glm::vec4(mat.volumeAttenuationColor, mat.volumeAttenuationDistance);
            gpuMat.sheenFactors = glm::vec4(mat.sheenColorFactor, mat.sheenRoughnessFactor);

            gpuMat.materialType = materialMask(MaterialType::metallic_roughness);
            if (mat.isSpecularGlossiness) {
              gpuMat.materialType |= MaterialType::specular_glossiness;
            }
            if (mat.sheenColorFactor != glm::vec3(0.0F)) {
              gpuMat.materialType |= MaterialType::sheen;
            }
            if (mat.clearcoatFactor > 0.0F) {
              gpuMat.materialType |= MaterialType::clear_coat;
            }
            if (mat.hasSpecular) {
              gpuMat.materialType |= MaterialType::specular;
            }
            if (mat.transmissionFactor > 0.0F) {
              gpuMat.materialType |= MaterialType::transmission;
            }
            if (mat.volumeThicknessFactor > 0.0F) {
              gpuMat.materialType |= MaterialType::volume;
            }
            if (mat.isUnlit) {
              gpuMat.materialType |= MaterialType::unlit;
            }
            if (mat.doubleSided) {
              gpuMat.materialType |= MaterialType::double_sided;
            }
            if (mat.anisotropyFactor != 0.0F) {
              gpuMat.materialType |= MaterialType::anisotropy;
            }
            if (mat.iridescenceFactor > 0.0F) {
              gpuMat.materialType |= MaterialType::iridescence;
            }

            gpuMat.alphaMode = mat.alphaMode;
            gpuMat.ior = mat.ior;
            gpuMat.doubleSided = mat.doubleSided ? 1U : 0U;

            copyTextureSlot(mat.baseColor, renderer,
                gpuMat.baseColorTexture, gpuMat.baseColorSampler,
                gpuMat.baseColorTextureUV, gpuMat.baseColorTransform);

            copyTextureSlot(mat.metallicRoughness, renderer,
                gpuMat.metallicRoughnessTexture, gpuMat.metallicRoughnessTextureSampler,
                gpuMat.metallicRoughnessTextureUV, gpuMat.metallicRoughnessTransform);

            copyTextureSlot(mat.normal, renderer,
                gpuMat.normalTexture, gpuMat.normalSampler,
                gpuMat.normalTextureUV, gpuMat.normalTransform);

            copyTextureSlot(mat.emissive, renderer,
                gpuMat.emissiveTexture, gpuMat.emissiveTextureSampler,
                gpuMat.emissiveTextureUV, gpuMat.emissiveTransform);

            copyTextureSlot(mat.occlusion, renderer,
                gpuMat.occlusionTexture, gpuMat.occlusionTextureSampler,
                gpuMat.occlusionTextureUV, gpuMat.occlusionTransform);

            copyTextureSlot(mat.transmission, renderer,
                gpuMat.transmissionTexture, gpuMat.transmissionTextureSampler,
                gpuMat.transmissionTextureUV, gpuMat.transmissionTransform);

            copyTextureSlot(mat.thickness, renderer,
                gpuMat.thicknessTexture, gpuMat.thicknessTextureSampler,
                gpuMat.thicknessTextureUV, gpuMat.thicknessTransform);

            copyTextureSlot(mat.clearcoat, renderer,
                gpuMat.clearcoatTexture, gpuMat.clearcoatTextureSampler,
                gpuMat.clearcoatTextureUV, gpuMat.clearcoatTransform);

            copyTextureSlot(mat.clearcoatRoughness, renderer,
                gpuMat.clearcoatRoughnessTexture, gpuMat.clearcoatRoughnessTextureSampler,
                gpuMat.clearcoatRoughnessTextureUV, gpuMat.clearcoatRoughnessTransform);

            copyTextureSlot(mat.clearcoatNormal, renderer,
                gpuMat.clearcoatNormalTexture, gpuMat.clearcoatNormalTextureSampler,
                gpuMat.clearcoatNormalTextureUV, gpuMat.clearcoatNormalTransform);

            copyTextureSlot(mat.specular, renderer,
                gpuMat.specularTexture, gpuMat.specularTextureSampler,
                gpuMat.specularTextureUV, gpuMat.specularTransform);

            copyTextureSlot(mat.specularColor, renderer,
                gpuMat.specularColorTexture, gpuMat.specularColorTextureSampler,
                gpuMat.specularColorTextureUV, gpuMat.specularColorTransform);

            copyTextureSlot(mat.sheenColor, renderer,
                gpuMat.sheenColorTexture, gpuMat.sheenColorTextureSampler,
                gpuMat.sheenColorTextureUV, gpuMat.sheenColorTransform);

            copyTextureSlot(mat.sheenRoughness, renderer,
                gpuMat.sheenRoughnessTexture, gpuMat.sheenRoughnessTextureSampler,
                gpuMat.sheenRoughnessTextureUV, gpuMat.sheenRoughnessTransform);

            result.push_back(gpuMat);
        }

        return result;
    }

    SceneUploader::LightResult SceneUploader::packLights(const SceneGraphDOD& scene) {
        LightResult result;
        auto lightView = scene.registry().view<LightSource, WorldTransform>();
        int currentLightIndex = 0;

        lightView.each([&](ecs::Entity entity, LightSource& light, WorldTransform& world) {
            const glm::mat4& worldM = world.matrix;
            glm::vec3 dir = glm::normalize(
                glm::vec3(worldM * glm::vec4(light.direction, 0.0F)));

            gpu::LightDataGPU l{};
            l.directionAndRange = glm::vec4(dir, (light.range <= 0.0F) ? 10000.0F : light.range);
            l.colorAndIntensity = glm::vec4(light.color, light.intensity);
            l.positionAndInnerCone = glm::vec4(glm::vec3(worldM[3]), std::cos(light.innerConeAngle));

            l.params = glm::vec4(
                std::cos(light.outerConeAngle),
                glm::uintBitsToFloat(static_cast<uint32_t>(light.type)),
                glm::intBitsToFloat(static_cast<int32_t>(entity)),
                0.0F
            );

            if (result.shadowCasterIndex == -1 && (light.type == LightType::Directional || light.type == LightType::Spot)) {
                result.shadowCasterIndex = currentLightIndex;
            }

            result.lights.push_back(l);
            currentLightIndex++;
        });

        return result;
    }

    gpu::EnvironmentMapDataGPU SceneUploader::packEnvironment(
        RHIRenderer& renderer,
        TextureHandle env,
        TextureHandle irr,
        TextureHandle brdf,
        float iblStrength
    ) {
        gpu::EnvironmentMapDataGPU gpuEnv{};

        gpuEnv.envMapTexture = util::u32(rhi::TextureBindlessHandle::Invalid);
        gpuEnv.irradianceTexture = util::u32(rhi::TextureBindlessHandle::Invalid);
        gpuEnv.brdfLutTexture = util::u32(rhi::TextureBindlessHandle::Invalid);
        gpuEnv.envMapTextureCharlie = util::u32(rhi::TextureBindlessHandle::Invalid);

        uint32_t clampSampler = util::u32(renderer.getBindlessSamplerIndex(rhi::SamplerAddressMode::ClampToEdge));

        gpuEnv.envMapSampler = clampSampler;
        gpuEnv.irradianceSampler = clampSampler;
        gpuEnv.brdfLutSampler = clampSampler;
        gpuEnv.envMapTextureCharlieSampler = clampSampler;
        gpuEnv.iblStrength = iblStrength;

        auto getCubeIndex = [&](TextureHandle h) -> rhi::TextureBindlessHandle {
          if (h == INVALID_TEXTURE_HANDLE) {
            return rhi::TextureBindlessHandle::Invalid;
          }
          auto *tex = renderer.getTexture(h);
          if (tex && tex->type() == rhi::TextureType::TextureCube) {
            return renderer.getTextureBindlessIndex(h);
          }
          return rhi::TextureBindlessHandle::Invalid;
        };

        auto get2DIndex = [&](TextureHandle h) -> rhi::TextureBindlessHandle {
          if (h == INVALID_TEXTURE_HANDLE) {
            return rhi::TextureBindlessHandle::Invalid;
          }
          auto *tex = renderer.getTexture(h);
          if (tex && tex->type() != rhi::TextureType::TextureCube) {
            return renderer.getTextureBindlessIndex(h);
          }
          return rhi::TextureBindlessHandle::Invalid;
        };

        gpuEnv.envMapTexture = util::u32(getCubeIndex(env));
        gpuEnv.irradianceTexture = util::u32(getCubeIndex(irr));
        gpuEnv.brdfLutTexture = util::u32(get2DIndex(brdf));

        return gpuEnv;
    }

    SortingType SceneUploader::classifyMaterial(const MaterialData& mat) {

      if (mat.alphaMode == 2U) {
        return SortingType::Transparent;
      }

      if (mat.alphaMode == 1U) {
        return SortingType::Opaque;
      }

      if (mat.transmissionFactor > 0.0F) {
        return SortingType::Transmission;
      }

        return SortingType::Opaque;
    }

    gpu::MaterialDataGPU SceneUploader::toGPUFormat(const MaterialData& material, RHIRenderer& renderer) {

        std::vector<MaterialData> mats = { material };
        auto result = packMaterials(mats, renderer);
        return result[0];
    }

}
