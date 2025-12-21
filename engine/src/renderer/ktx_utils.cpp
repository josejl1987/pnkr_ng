#include "pnkr/renderer/ktx_utils.hpp"

#include <ktx.h>
#include <algorithm>
#include <cstring>

namespace pnkr::renderer
{
    namespace
    {
        rhi::Format mapKtx1ToRhiFormat(ktx_uint32_t glInternalformat)
        {
            // Common GL internal format constants (no GL headers required).
            constexpr ktx_uint32_t GL_R8 = 0x8229;
            constexpr ktx_uint32_t GL_RG8 = 0x822B;
            constexpr ktx_uint32_t GL_RGB8 = 0x8051;
            constexpr ktx_uint32_t GL_RGBA8 = 0x8058;
            constexpr ktx_uint32_t GL_SRGB8_ALPHA8 = 0x8C43;
            constexpr ktx_uint32_t GL_R16F = 0x822D;
            constexpr ktx_uint32_t GL_RG16F = 0x822F;
            constexpr ktx_uint32_t GL_RGBA16F = 0x881A;
            constexpr ktx_uint32_t GL_R32F = 0x822E;
            constexpr ktx_uint32_t GL_RG32F = 0x8230;
            constexpr ktx_uint32_t GL_RGB32F = 0x8815;
            constexpr ktx_uint32_t GL_RGBA32F = 0x8814;

            switch (glInternalformat)
            {
            case GL_R8: return rhi::Format::R8_UNORM;
            case GL_RG8: return rhi::Format::R8G8_UNORM;
            case GL_RGB8: return rhi::Format::R8G8B8_UNORM;
            case GL_RGBA8: return rhi::Format::R8G8B8A8_UNORM;
            case GL_SRGB8_ALPHA8: return rhi::Format::R8G8B8A8_SRGB;
            case GL_R16F: return rhi::Format::R16_SFLOAT;
            case GL_RG16F: return rhi::Format::R16G16_SFLOAT;
            case GL_RGBA16F: return rhi::Format::R16G16B16A16_SFLOAT;
            case GL_R32F: return rhi::Format::R32_SFLOAT;
            case GL_RG32F: return rhi::Format::R32G32_SFLOAT;
            case GL_RGB32F: return rhi::Format::R32G32B32_SFLOAT;
            case GL_RGBA32F: return rhi::Format::R32G32B32A32_SFLOAT;
            default: return rhi::Format::Undefined;
            }
        }

        rhi::Format mapKtx2VkFormatToRhi(ktx_uint32_t vkFormat)
        {
            // Vulkan VkFormat numeric values (from vulkan_core.h).
            constexpr ktx_uint32_t VK_FORMAT_R8_UNORM = 9;
            constexpr ktx_uint32_t VK_FORMAT_R8G8_UNORM = 16;
            constexpr ktx_uint32_t VK_FORMAT_R8G8B8_UNORM = 23;
            constexpr ktx_uint32_t VK_FORMAT_R8G8B8A8_UNORM = 37;
            constexpr ktx_uint32_t VK_FORMAT_R8G8B8A8_SRGB = 43;
            constexpr ktx_uint32_t VK_FORMAT_B8G8R8A8_UNORM = 44;
            constexpr ktx_uint32_t VK_FORMAT_B8G8R8A8_SRGB = 50;
            constexpr ktx_uint32_t VK_FORMAT_R16_SFLOAT = 76;
            constexpr ktx_uint32_t VK_FORMAT_R16G16_SFLOAT = 83;
            constexpr ktx_uint32_t VK_FORMAT_R16G16B16A16_SFLOAT = 97;
            constexpr ktx_uint32_t VK_FORMAT_R32_SFLOAT = 100;
            constexpr ktx_uint32_t VK_FORMAT_R32G32_SFLOAT = 103;
            constexpr ktx_uint32_t VK_FORMAT_R32G32B32_SFLOAT = 106;
            constexpr ktx_uint32_t VK_FORMAT_R32G32B32A32_SFLOAT = 109;
            constexpr ktx_uint32_t VK_FORMAT_BC1_RGB_UNORM_BLOCK = 131;
            constexpr ktx_uint32_t VK_FORMAT_BC1_RGB_SRGB_BLOCK = 132;
            constexpr ktx_uint32_t VK_FORMAT_BC3_UNORM_BLOCK = 137;
            constexpr ktx_uint32_t VK_FORMAT_BC3_SRGB_BLOCK = 138;
            constexpr ktx_uint32_t VK_FORMAT_BC7_UNORM_BLOCK = 145;
            constexpr ktx_uint32_t VK_FORMAT_BC7_SRGB_BLOCK = 146;

            switch (vkFormat)
            {
            case VK_FORMAT_R8_UNORM: return rhi::Format::R8_UNORM;
            case VK_FORMAT_R8G8_UNORM: return rhi::Format::R8G8_UNORM;
            case VK_FORMAT_R8G8B8_UNORM: return rhi::Format::R8G8B8_UNORM;
            case VK_FORMAT_R8G8B8A8_UNORM: return rhi::Format::R8G8B8A8_UNORM;
            case VK_FORMAT_R8G8B8A8_SRGB: return rhi::Format::R8G8B8A8_SRGB;
            case VK_FORMAT_B8G8R8A8_UNORM: return rhi::Format::B8G8R8A8_UNORM;
            case VK_FORMAT_B8G8R8A8_SRGB: return rhi::Format::B8G8R8A8_SRGB;
            case VK_FORMAT_R16_SFLOAT: return rhi::Format::R16_SFLOAT;
            case VK_FORMAT_R16G16_SFLOAT: return rhi::Format::R16G16_SFLOAT;
            case VK_FORMAT_R16G16B16A16_SFLOAT: return rhi::Format::R16G16B16A16_SFLOAT;
            case VK_FORMAT_R32_SFLOAT: return rhi::Format::R32_SFLOAT;
            case VK_FORMAT_R32G32_SFLOAT: return rhi::Format::R32G32_SFLOAT;
            case VK_FORMAT_R32G32B32_SFLOAT: return rhi::Format::R32G32B32_SFLOAT;
            case VK_FORMAT_R32G32B32A32_SFLOAT: return rhi::Format::R32G32B32A32_SFLOAT;
            case VK_FORMAT_BC1_RGB_UNORM_BLOCK: return rhi::Format::BC1_RGB_UNORM;
            case VK_FORMAT_BC1_RGB_SRGB_BLOCK: return rhi::Format::BC1_RGB_SRGB;
            case VK_FORMAT_BC3_UNORM_BLOCK: return rhi::Format::BC3_UNORM;
            case VK_FORMAT_BC3_SRGB_BLOCK: return rhi::Format::BC3_SRGB;
            case VK_FORMAT_BC7_UNORM_BLOCK: return rhi::Format::BC7_UNORM;
            case VK_FORMAT_BC7_SRGB_BLOCK: return rhi::Format::BC7_SRGB;
            default: return rhi::Format::Undefined;
            }
        }

        rhi::TextureType pickTextureType(const ktxTexture* texture)
        {
            if (texture->isCubemap == KTX_TRUE)
            {
                return rhi::TextureType::TextureCube;
            }

            if (texture->numDimensions == 1 || texture->baseHeight == 0)
            {
                return rhi::TextureType::Texture1D;
            }

            if (texture->numDimensions == 3 || texture->baseDepth > 1)
            {
                return rhi::TextureType::Texture3D;
            }

            return rhi::TextureType::Texture2D;
        }

        bool setError(std::string* error, const std::string& message)
        {
            if (error != nullptr)
            {
                *error = message;
            }
            return false;
        }
    } // namespace

    bool KTXUtils::loadFromFile(const std::filesystem::path& path,
                                KTXTextureData& out,
                                std::string* error)
    {
        out = {};

        ktxTexture* texture = nullptr;
        const std::string pathString = path.string();
        const auto result = ktxTexture_CreateFromNamedFile(
            pathString.c_str(),
            KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
            &texture);

        if (result != KTX_SUCCESS || texture == nullptr)
        {
            return setError(error, "KTX load failed: " + pathString);
        }

        out.texture = texture;
        out.extent = rhi::Extent3D{
            .width = texture->baseWidth,
            .height = texture->baseHeight,
            .depth = texture->baseDepth == 0 ? 1u : texture->baseDepth
        };
        out.mipLevels = std::max(1u, texture->numLevels);
        out.numLayers = std::max(1u, texture->numLayers);
        out.numFaces = std::max(1u, texture->numFaces);
        out.arrayLayers = out.numLayers * out.numFaces;
        out.isCubemap = texture->isCubemap == KTX_TRUE;
        out.isArray = texture->isArray == KTX_TRUE;
        out.type = pickTextureType(texture);

        if (texture->classId == ktxTexture2_c)
        {
            auto* texture2 = reinterpret_cast<ktxTexture2*>(texture);
            if (ktxTexture2_NeedsTranscoding(texture2))
            {
                const auto transcodeResult = ktxTexture2_TranscodeBasis(
                    texture2,
                    KTX_TTF_BC7_RGBA,
                    0);

                if (transcodeResult != KTX_SUCCESS)
                {
                    ktxTexture_Destroy(texture);
                    return setError(error, "KTX transcode failed: " + pathString);
                }
            }
            out.format = mapKtx2VkFormatToRhi(texture2->vkFormat);
        }
        else
        {
            auto* texture1 = reinterpret_cast<ktxTexture1*>(texture);
            out.format = mapKtx1ToRhiFormat(texture1->glInternalformat);
        }

        if (out.format == rhi::Format::Undefined)
        {
            destroy(out);
            return setError(error, "Unsupported KTX format in: " + pathString);
        }

        out.data.resize(texture->dataSize);
        if (!out.data.empty() && texture->pData != nullptr)
        {
            std::memcpy(out.data.data(), texture->pData, texture->dataSize);
        }

        return true;
    }

    void KTXUtils::destroy(KTXTextureData& data)
    {
        if (data.texture != nullptr)
        {
            ktxTexture_Destroy(data.texture);
            data.texture = nullptr;
        }
        data.data.clear();
    }
} // namespace pnkr::renderer
