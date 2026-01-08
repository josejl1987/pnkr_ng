#include "rhi/vulkan/vulkan_utils.hpp"

#include "pnkr/core/common.hpp"
#include "pnkr/rhi/rhi_types.hpp"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>

using namespace pnkr::util;

namespace pnkr::renderer::rhi::vulkan
{
    namespace
    {
        template <typename K, typename V>
        struct Entry
        {
          K k;
          V v;
        };

        template <typename K, typename V>
        Entry(K, V) -> Entry<K, V>;

        template <typename E> struct IsFlagsEnum : std::false_type {};

        template <>
        struct IsFlagsEnum<pnkr::renderer::rhi::BufferUsage> : std::true_type
        {
        };

        template <>
        struct IsFlagsEnum<pnkr::renderer::rhi::TextureUsage> : std::true_type
        {
        };

        template <>
        struct IsFlagsEnum<pnkr::renderer::rhi::ShaderStage> : std::true_type
        {
        };

        template <typename E>
        inline constexpr bool IS_FLAGS_ENUM_V = IsFlagsEnum<E>::value;

        template <typename E, typename V, std::size_t N>
        struct EnumMap
        {
          std::array<Entry<E, V>, N> m_e{};

          constexpr explicit EnumMap(std::array<Entry<E, V>, N> entries)
              : m_e(entries) {}

          [[nodiscard]] constexpr V to(E x, V def) const noexcept {
            for (auto it : m_e) {
              if (it.k == x) {
                return it.v;
              }
            }
            return def;
          }

          [[nodiscard]] constexpr E from(V x, E def) const noexcept {
            for (auto it : m_e) {
              if (it.v == x) {
                return it.k;
              }
            }
            return def;
          }
        };

        template <typename E, typename VkBits, std::size_t N>
        struct FlagsMap
        {
          static_assert(IS_FLAGS_ENUM_V<E>,
                        "FlagsMap used with non-flags enum.");

          std::array<Entry<E, VkBits>, N> m_e{};

          constexpr explicit FlagsMap(std::array<Entry<E, VkBits>, N> entries)
              : m_e(entries) {}

          [[nodiscard]] constexpr vk::Flags<VkBits>
          to(core::Flags<E> flags) const noexcept {
            vk::Flags<VkBits> out{};
            for (auto it : m_e) {
              if (flags.has(it.k)) {
                out |= it.v;
              }
            }
            return out;
          }

          [[nodiscard]] constexpr core::Flags<E>
          from(vk::Flags<VkBits> flags) const noexcept {
            core::Flags<E> out{};
            for (auto it : m_e) {
              if ((flags & it.v) == it.v) {
                out |= it.k;
              }
            }
            return out;
          }
        };

        template <typename E, typename V, std::size_t N>
        constexpr auto makeEnumMap(const std::array<Entry<E, V>, N>& a)
        {
            return EnumMap<E, V, N>(a);
        }

        template <typename E, typename VkBits, std::size_t N>
        constexpr auto makeFlagsMap(const std::array<Entry<E, VkBits>, N>& a)
        {
            return FlagsMap<E, VkBits, N>(a);
        }

        constexpr auto K_FORMAT_MAP = makeEnumMap<Format,
                                                  vk::Format>(std::array{
            Entry{.k = Format::Undefined, .v = vk::Format::eUndefined},

            Entry{.k = Format::R8_UNORM, .v = vk::Format::eR8Unorm},
            Entry{.k = Format::R8G8_UNORM, .v = vk::Format::eR8G8Unorm},
            Entry{.k = Format::R8G8B8_UNORM, .v = vk::Format::eR8G8B8Unorm},
            Entry{.k = Format::R8G8B8A8_UNORM, .v = vk::Format::eR8G8B8A8Unorm},
            Entry{.k = Format::R8G8B8A8_SRGB, .v = vk::Format::eR8G8B8A8Srgb},
            Entry{.k = Format::B8G8R8A8_UNORM, .v = vk::Format::eB8G8R8A8Unorm},
            Entry{.k = Format::B8G8R8A8_SRGB, .v = vk::Format::eB8G8R8A8Srgb},

            Entry{.k = Format::R8_SNORM, .v = vk::Format::eR8Snorm},
            Entry{.k = Format::R8G8_SNORM, .v = vk::Format::eR8G8Snorm},
            Entry{.k = Format::R8G8B8_SNORM, .v = vk::Format::eR8G8B8Snorm},
            Entry{.k = Format::R8G8B8A8_SNORM, .v = vk::Format::eR8G8B8A8Snorm},

            Entry{.k = Format::R8_UINT, .v = vk::Format::eR8Uint},
            Entry{.k = Format::R8G8_UINT, .v = vk::Format::eR8G8Uint},
            Entry{.k = Format::R8G8B8A8_UINT, .v = vk::Format::eR8G8B8A8Uint},

            Entry{.k = Format::R8_SINT, .v = vk::Format::eR8Sint},
            Entry{.k = Format::R8G8_SINT, .v = vk::Format::eR8G8Sint},
            Entry{.k = Format::R8G8B8A8_SINT, .v = vk::Format::eR8G8B8A8Sint},

            Entry{.k = Format::R16_UNORM, .v = vk::Format::eR16Unorm},
            Entry{.k = Format::R16G16_UNORM, .v = vk::Format::eR16G16Unorm},
            Entry{.k = Format::R16G16B16A16_UNORM,
                  .v = vk::Format::eR16G16B16A16Unorm},

            Entry{.k = Format::R16_SNORM, .v = vk::Format::eR16Snorm},
            Entry{.k = Format::R16G16_SNORM, .v = vk::Format::eR16G16Snorm},
            Entry{.k = Format::R16G16B16A16_SNORM,
                  .v = vk::Format::eR16G16B16A16Snorm},

            Entry{.k = Format::R16_SFLOAT, .v = vk::Format::eR16Sfloat},
            Entry{.k = Format::R16G16_SFLOAT, .v = vk::Format::eR16G16Sfloat},
            Entry{.k = Format::R16G16B16_SFLOAT,
                  .v = vk::Format::eR16G16B16Sfloat},
            Entry{.k = Format::R16G16B16A16_SFLOAT,
                  .v = vk::Format::eR16G16B16A16Sfloat},

            Entry{.k = Format::R16_UINT, .v = vk::Format::eR16Uint},
            Entry{.k = Format::R16G16_UINT, .v = vk::Format::eR16G16Uint},
            Entry{.k = Format::R16G16B16A16_UINT,
                  .v = vk::Format::eR16G16B16A16Uint},

            Entry{.k = Format::R16_SINT, .v = vk::Format::eR16Sint},
            Entry{.k = Format::R16G16_SINT, .v = vk::Format::eR16G16Sint},
            Entry{.k = Format::R16G16B16A16_SINT,
                  .v = vk::Format::eR16G16B16A16Sint},

            Entry{.k = Format::R32_SFLOAT, .v = vk::Format::eR32Sfloat},
            Entry{.k = Format::R32G32_SFLOAT, .v = vk::Format::eR32G32Sfloat},
            Entry{.k = Format::R32G32B32_SFLOAT,
                  .v = vk::Format::eR32G32B32Sfloat},
            Entry{.k = Format::R32G32B32A32_SFLOAT,
                  .v = vk::Format::eR32G32B32A32Sfloat},

            Entry{.k = Format::R32_UINT, .v = vk::Format::eR32Uint},
            Entry{.k = Format::R32G32_UINT, .v = vk::Format::eR32G32Uint},
            Entry{.k = Format::R32G32B32_UINT, .v = vk::Format::eR32G32B32Uint},
            Entry{.k = Format::R32G32B32A32_UINT,
                  .v = vk::Format::eR32G32B32A32Uint},

            Entry{.k = Format::R32_SINT, .v = vk::Format::eR32Sint},
            Entry{.k = Format::R32G32_SINT, .v = vk::Format::eR32G32Sint},
            Entry{.k = Format::R32G32B32_SINT, .v = vk::Format::eR32G32B32Sint},
            Entry{.k = Format::R32G32B32A32_SINT,
                  .v = vk::Format::eR32G32B32A32Sint},

            Entry{.k = Format::B10G11R11_UFLOAT_PACK32,
                  .v = vk::Format::eB10G11R11UfloatPack32},
            Entry{.k = Format::A2B10G10R10_UNORM_PACK32,
                  .v = vk::Format::eA2B10G10R10UnormPack32},
            Entry{.k = Format::A2R10G10B10_UNORM_PACK32,
                  .v = vk::Format::eA2R10G10B10UnormPack32},
            Entry{.k = Format::E5B9G9R9_UFLOAT_PACK32,
                  .v = vk::Format::eE5B9G9R9UfloatPack32},

            Entry{.k = Format::D16_UNORM, .v = vk::Format::eD16Unorm},
            Entry{.k = Format::D32_SFLOAT, .v = vk::Format::eD32Sfloat},
            Entry{.k = Format::D24_UNORM_S8_UINT,
                  .v = vk::Format::eD24UnormS8Uint},
            Entry{.k = Format::D32_SFLOAT_S8_UINT,
                  .v = vk::Format::eD32SfloatS8Uint},
            Entry{.k = Format::S8_UINT, .v = vk::Format::eS8Uint},

            Entry{.k = Format::BC1_RGB_UNORM,
                  .v = vk::Format::eBc1RgbUnormBlock},
            Entry{.k = Format::BC1_RGB_SRGB, .v = vk::Format::eBc1RgbSrgbBlock},
            Entry{.k = Format::BC1_RGBA_UNORM,
                  .v = vk::Format::eBc1RgbaUnormBlock},
            Entry{.k = Format::BC1_RGBA_SRGB,
                  .v = vk::Format::eBc1RgbaSrgbBlock},
            Entry{.k = Format::BC2_UNORM, .v = vk::Format::eBc2UnormBlock},
            Entry{.k = Format::BC2_SRGB, .v = vk::Format::eBc2SrgbBlock},
            Entry{.k = Format::BC3_UNORM, .v = vk::Format::eBc3UnormBlock},
            Entry{.k = Format::BC3_SRGB, .v = vk::Format::eBc3SrgbBlock},
            Entry{.k = Format::BC4_UNORM, .v = vk::Format::eBc4UnormBlock},
            Entry{.k = Format::BC4_SNORM, .v = vk::Format::eBc4SnormBlock},
            Entry{.k = Format::BC5_UNORM, .v = vk::Format::eBc5UnormBlock},
            Entry{.k = Format::BC5_SNORM, .v = vk::Format::eBc5SnormBlock},
            Entry{.k = Format::BC6H_UFLOAT, .v = vk::Format::eBc6HUfloatBlock},
            Entry{.k = Format::BC6H_SFLOAT, .v = vk::Format::eBc6HSfloatBlock},
            Entry{.k = Format::BC7_UNORM, .v = vk::Format::eBc7UnormBlock},
            Entry{.k = Format::BC7_SRGB, .v = vk::Format::eBc7SrgbBlock},

            Entry{.k = Format::ASTC_4x4_UNORM,
                  .v = vk::Format::eAstc4x4UnormBlock},
            Entry{.k = Format::ASTC_4x4_SRGB,
                  .v = vk::Format::eAstc4x4SrgbBlock},
            Entry{.k = Format::ASTC_6x6_UNORM,
                  .v = vk::Format::eAstc6x6UnormBlock},
            Entry{.k = Format::ASTC_6x6_SRGB,
                  .v = vk::Format::eAstc6x6SrgbBlock},
            Entry{.k = Format::ASTC_8x8_UNORM,
                  .v = vk::Format::eAstc8x8UnormBlock},
            Entry{.k = Format::ASTC_8x8_SRGB,
                  .v = vk::Format::eAstc8x8SrgbBlock},

            Entry{.k = Format::ETC2_R8G8B8_UNORM,
                  .v = vk::Format::eEtc2R8G8B8UnormBlock},
            Entry{.k = Format::ETC2_R8G8B8_SRGB,
                  .v = vk::Format::eEtc2R8G8B8SrgbBlock},
            Entry{.k = Format::ETC2_R8G8B8A8_UNORM,
                  .v = vk::Format::eEtc2R8G8B8A8UnormBlock},
            Entry{.k = Format::ETC2_R8G8B8A8_SRGB,
                  .v = vk::Format::eEtc2R8G8B8A8SrgbBlock},
        });

        constexpr auto K_BUFFER_USAGE_MAP =
            makeFlagsMap<BufferUsage, vk::BufferUsageFlagBits>(std::array{
                Entry{.k = BufferUsage::TransferSrc,
                      .v = vk::BufferUsageFlagBits::eTransferSrc},
                Entry{.k = BufferUsage::TransferDst,
                      .v = vk::BufferUsageFlagBits::eTransferDst},
                Entry{.k = BufferUsage::UniformBuffer,
                      .v = vk::BufferUsageFlagBits::eUniformBuffer},
                Entry{.k = BufferUsage::StorageBuffer,
                      .v = vk::BufferUsageFlagBits::eStorageBuffer},
                Entry{.k = BufferUsage::IndexBuffer,
                      .v = vk::BufferUsageFlagBits::eIndexBuffer},
                Entry{.k = BufferUsage::VertexBuffer,
                      .v = vk::BufferUsageFlagBits::eVertexBuffer},
                Entry{.k = BufferUsage::IndirectBuffer,
                      .v = vk::BufferUsageFlagBits::eIndirectBuffer},
                Entry{.k = BufferUsage::ShaderDeviceAddress,
                      .v = vk::BufferUsageFlagBits::eShaderDeviceAddress},
            });

        constexpr auto K_IMAGE_USAGE_MAP =
            makeFlagsMap<TextureUsage, vk::ImageUsageFlagBits>(std::array{
                Entry{.k = TextureUsage::TransferSrc,
                      .v = vk::ImageUsageFlagBits::eTransferSrc},
                Entry{.k = TextureUsage::TransferDst,
                      .v = vk::ImageUsageFlagBits::eTransferDst},
                Entry{.k = TextureUsage::Sampled,
                      .v = vk::ImageUsageFlagBits::eSampled},
                Entry{.k = TextureUsage::Storage,
                      .v = vk::ImageUsageFlagBits::eStorage},
                Entry{.k = TextureUsage::ColorAttachment,
                      .v = vk::ImageUsageFlagBits::eColorAttachment},
                Entry{.k = TextureUsage::DepthStencilAttachment,
                      .v = vk::ImageUsageFlagBits::eDepthStencilAttachment},
                Entry{.k = TextureUsage::InputAttachment,
                      .v = vk::ImageUsageFlagBits::eInputAttachment},
                Entry{.k = TextureUsage::TransientAttachment,
                      .v = vk::ImageUsageFlagBits::eTransientAttachment},
            });

        constexpr auto K_MEMORY_USAGE_MAP = makeEnumMap<
            MemoryUsage, VmaMemoryUsage>(std::array{
            Entry{.k = MemoryUsage::GPUOnly, .v = VMA_MEMORY_USAGE_GPU_ONLY},
            Entry{.k = MemoryUsage::CPUToGPU, .v = VMA_MEMORY_USAGE_CPU_TO_GPU},
            Entry{.k = MemoryUsage::GPUToCPU, .v = VMA_MEMORY_USAGE_GPU_TO_CPU},
            Entry{.k = MemoryUsage::CPUOnly, .v = VMA_MEMORY_USAGE_CPU_ONLY},
            Entry{.k = MemoryUsage::GPULazy,
                  .v = VMA_MEMORY_USAGE_GPU_LAZILY_ALLOCATED},
        });

        constexpr auto K_SHADER_STAGE_MAP =
            makeFlagsMap<ShaderStage, vk::ShaderStageFlagBits>(std::array{
                Entry{.k = ShaderStage::Vertex,
                      .v = vk::ShaderStageFlagBits::eVertex},
                Entry{.k = ShaderStage::Fragment,
                      .v = vk::ShaderStageFlagBits::eFragment},
                Entry{.k = ShaderStage::Geometry,
                      .v = vk::ShaderStageFlagBits::eGeometry},
                Entry{.k = ShaderStage::Compute,
                      .v = vk::ShaderStageFlagBits::eCompute},
                Entry{.k = ShaderStage::TessControl,
                      .v = vk::ShaderStageFlagBits::eTessellationControl},
                Entry{.k = ShaderStage::TessEval,
                      .v = vk::ShaderStageFlagBits::eTessellationEvaluation},
            });

        constexpr auto K_RESOURCE_LAYOUT_MAP =
            makeEnumMap<ResourceLayout, vk::ImageLayout>(std::array{
                Entry{.k = ResourceLayout::Undefined,
                      .v = vk::ImageLayout::eUndefined},
                Entry{.k = ResourceLayout::General,
                      .v = vk::ImageLayout::eGeneral},
                Entry{.k = ResourceLayout::ColorAttachment,
                      .v = vk::ImageLayout::eColorAttachmentOptimal},
                Entry{.k = ResourceLayout::DepthStencilAttachment,
                      .v = vk::ImageLayout::eDepthStencilAttachmentOptimal},
                Entry{.k = ResourceLayout::DepthStencilReadOnly,
                      .v = vk::ImageLayout::eDepthStencilReadOnlyOptimal},
                Entry{.k = ResourceLayout::ShaderReadOnly,
                      .v = vk::ImageLayout::eShaderReadOnlyOptimal},
                Entry{.k = ResourceLayout::TransferSrc,
                      .v = vk::ImageLayout::eTransferSrcOptimal},
                Entry{.k = ResourceLayout::TransferDst,
                      .v = vk::ImageLayout::eTransferDstOptimal},
                Entry{.k = ResourceLayout::Present,
                      .v = vk::ImageLayout::ePresentSrcKHR},
            });

        constexpr auto K_PRIMITIVE_TOPOLOGY_MAP =
            makeEnumMap<PrimitiveTopology, vk::PrimitiveTopology>(std::array{
                Entry{.k = PrimitiveTopology::PointList,
                      .v = vk::PrimitiveTopology::ePointList},
                Entry{.k = PrimitiveTopology::LineList,
                      .v = vk::PrimitiveTopology::eLineList},
                Entry{.k = PrimitiveTopology::LineStrip,
                      .v = vk::PrimitiveTopology::eLineStrip},
                Entry{.k = PrimitiveTopology::TriangleList,
                      .v = vk::PrimitiveTopology::eTriangleList},
                Entry{.k = PrimitiveTopology::TriangleStrip,
                      .v = vk::PrimitiveTopology::eTriangleStrip},
                Entry{.k = PrimitiveTopology::TriangleFan,
                      .v = vk::PrimitiveTopology::eTriangleFan},
                Entry{.k = PrimitiveTopology::PatchList,
                      .v = vk::PrimitiveTopology::ePatchList},
            });

        constexpr auto K_POLYGON_MODE_MAP =
            makeEnumMap<PolygonMode, vk::PolygonMode>(std::array{
                Entry{.k = PolygonMode::Fill, .v = vk::PolygonMode::eFill},
                Entry{.k = PolygonMode::Line, .v = vk::PolygonMode::eLine},
                Entry{.k = PolygonMode::Point, .v = vk::PolygonMode::ePoint},
            });

        constexpr auto K_CULL_MODE_MAP =
            makeEnumMap<CullMode, vk::CullModeFlagBits>(std::array{
                Entry{.k = CullMode::None, .v = vk::CullModeFlagBits::eNone},
                Entry{.k = CullMode::Front, .v = vk::CullModeFlagBits::eFront},
                Entry{.k = CullMode::Back, .v = vk::CullModeFlagBits::eBack},
                Entry{.k = CullMode::FrontAndBack,
                      .v = vk::CullModeFlagBits::eFrontAndBack},
            });

        constexpr auto K_COMPARE_OP_MAP =
            makeEnumMap<CompareOp, vk::CompareOp>(std::array{
                Entry{.k = CompareOp::Never, .v = vk::CompareOp::eNever},
                Entry{.k = CompareOp::Less, .v = vk::CompareOp::eLess},
                Entry{.k = CompareOp::Equal, .v = vk::CompareOp::eEqual},
                Entry{.k = CompareOp::LessOrEqual,
                      .v = vk::CompareOp::eLessOrEqual},
                Entry{.k = CompareOp::Greater, .v = vk::CompareOp::eGreater},
                Entry{.k = CompareOp::NotEqual, .v = vk::CompareOp::eNotEqual},
                Entry{.k = CompareOp::GreaterOrEqual,
                      .v = vk::CompareOp::eGreaterOrEqual},
                Entry{.k = CompareOp::Always, .v = vk::CompareOp::eAlways},
            });

        constexpr auto K_BLEND_FACTOR_MAP = makeEnumMap<
            BlendFactor, vk::BlendFactor>(std::array{
            Entry{.k = BlendFactor::Zero, .v = vk::BlendFactor::eZero},
            Entry{.k = BlendFactor::One, .v = vk::BlendFactor::eOne},
            Entry{.k = BlendFactor::SrcColor, .v = vk::BlendFactor::eSrcColor},
            Entry{.k = BlendFactor::OneMinusSrcColor,
                  .v = vk::BlendFactor::eOneMinusSrcColor},
            Entry{.k = BlendFactor::DstColor, .v = vk::BlendFactor::eDstColor},
            Entry{.k = BlendFactor::OneMinusDstColor,
                  .v = vk::BlendFactor::eOneMinusDstColor},
            Entry{.k = BlendFactor::SrcAlpha, .v = vk::BlendFactor::eSrcAlpha},
            Entry{.k = BlendFactor::OneMinusSrcAlpha,
                  .v = vk::BlendFactor::eOneMinusSrcAlpha},
            Entry{.k = BlendFactor::DstAlpha, .v = vk::BlendFactor::eDstAlpha},
            Entry{.k = BlendFactor::OneMinusDstAlpha,
                  .v = vk::BlendFactor::eOneMinusDstAlpha},
        });

        constexpr auto K_BLEND_OP_MAP =
            makeEnumMap<BlendOp, vk::BlendOp>(std::array{
                Entry{.k = BlendOp::Add, .v = vk::BlendOp::eAdd},
                Entry{.k = BlendOp::Subtract, .v = vk::BlendOp::eSubtract},
                Entry{.k = BlendOp::ReverseSubtract,
                      .v = vk::BlendOp::eReverseSubtract},
                Entry{.k = BlendOp::Min, .v = vk::BlendOp::eMin},
                Entry{.k = BlendOp::Max, .v = vk::BlendOp::eMax},
            });

        constexpr auto K_FILTER_MAP =
            makeEnumMap<Filter, vk::Filter>(std::array{
                Entry{.k = Filter::Nearest, .v = vk::Filter::eNearest},
                Entry{.k = Filter::Linear, .v = vk::Filter::eLinear},
            });

        constexpr auto K_SAMPLER_ADDRESS_MODE_MAP =
            makeEnumMap<SamplerAddressMode, vk::SamplerAddressMode>(std::array{
                Entry{.k = SamplerAddressMode::Repeat,
                      .v = vk::SamplerAddressMode::eRepeat},
                Entry{.k = SamplerAddressMode::MirroredRepeat,
                      .v = vk::SamplerAddressMode::eMirroredRepeat},
                Entry{.k = SamplerAddressMode::ClampToEdge,
                      .v = vk::SamplerAddressMode::eClampToEdge},
                Entry{.k = SamplerAddressMode::ClampToBorder,
                      .v = vk::SamplerAddressMode::eClampToBorder},
            });

        constexpr auto K_LOAD_OP_MAP =
            makeEnumMap<LoadOp, vk::AttachmentLoadOp>(std::array{
                Entry{.k = LoadOp::Load, .v = vk::AttachmentLoadOp::eLoad},
                Entry{.k = LoadOp::Clear, .v = vk::AttachmentLoadOp::eClear},
                Entry{.k = LoadOp::DontCare,
                      .v = vk::AttachmentLoadOp::eDontCare},
            });

        constexpr auto K_STORE_OP_MAP =
            makeEnumMap<StoreOp, vk::AttachmentStoreOp>(std::array{
                Entry{.k = StoreOp::Store, .v = vk::AttachmentStoreOp::eStore},
                Entry{.k = StoreOp::DontCare,
                      .v = vk::AttachmentStoreOp::eDontCare},
            });

        constexpr auto K_DESCRIPTOR_TYPE_MAP =
            makeEnumMap<DescriptorType, vk::DescriptorType>(std::array{
                Entry{.k = DescriptorType::Sampler,
                      .v = vk::DescriptorType::eSampler},
                Entry{.k = DescriptorType::CombinedImageSampler,
                      .v = vk::DescriptorType::eCombinedImageSampler},
                Entry{.k = DescriptorType::SampledImage,
                      .v = vk::DescriptorType::eSampledImage},
                Entry{.k = DescriptorType::StorageImage,
                      .v = vk::DescriptorType::eStorageImage},
                Entry{.k = DescriptorType::UniformBuffer,
                      .v = vk::DescriptorType::eUniformBuffer},
                Entry{.k = DescriptorType::StorageBuffer,
                      .v = vk::DescriptorType::eStorageBuffer},
                Entry{.k = DescriptorType::UniformBufferDynamic,
                      .v = vk::DescriptorType::eUniformBufferDynamic},
                Entry{.k = DescriptorType::StorageBufferDynamic,
                      .v = vk::DescriptorType::eStorageBufferDynamic},

            });

        constexpr auto K_DYNAMIC_STATE_MAP =
            makeEnumMap<DynamicState, vk::DynamicState>(std::array{
                Entry{.k = DynamicState::Viewport,
                      .v = vk::DynamicState::eViewport},
                Entry{.k = DynamicState::Scissor,
                      .v = vk::DynamicState::eScissor},
                Entry{.k = DynamicState::LineWidth,
                      .v = vk::DynamicState::eLineWidth},
                Entry{.k = DynamicState::DepthBias,
                      .v = vk::DynamicState::eDepthBias},
                Entry{.k = DynamicState::BlendConstants,
                      .v = vk::DynamicState::eBlendConstants},
                Entry{.k = DynamicState::DepthBounds,
                      .v = vk::DynamicState::eDepthBounds},
                Entry{.k = DynamicState::StencilCompareMask,
                      .v = vk::DynamicState::eStencilCompareMask},
                Entry{.k = DynamicState::StencilWriteMask,
                      .v = vk::DynamicState::eStencilWriteMask},
                Entry{.k = DynamicState::StencilReference,
                      .v = vk::DynamicState::eStencilReference},
                Entry{.k = DynamicState::CullMode,
                      .v = vk::DynamicState::eCullMode},
                Entry{.k = DynamicState::FrontFace,
                      .v = vk::DynamicState::eFrontFace},
                Entry{.k = DynamicState::PrimitiveTopology,
                      .v = vk::DynamicState::ePrimitiveTopology},
                Entry{.k = DynamicState::ViewportWithCount,
                      .v = vk::DynamicState::eViewportWithCount},
                Entry{.k = DynamicState::ScissorWithCount,
                      .v = vk::DynamicState::eScissorWithCount},
                Entry{.k = DynamicState::VertexInputBindingStride,
                      .v = vk::DynamicState::eVertexInputBindingStride},
                Entry{.k = DynamicState::DepthTestEnable,
                      .v = vk::DynamicState::eDepthTestEnable},
                Entry{.k = DynamicState::DepthWriteEnable,
                      .v = vk::DynamicState::eDepthWriteEnable},
                Entry{.k = DynamicState::DepthCompareOp,
                      .v = vk::DynamicState::eDepthCompareOp},
                Entry{.k = DynamicState::DepthBoundsTestEnable,
                      .v = vk::DynamicState::eDepthBoundsTestEnable},
                Entry{.k = DynamicState::StencilTestEnable,
                      .v = vk::DynamicState::eStencilTestEnable},
                Entry{.k = DynamicState::StencilOp,
                      .v = vk::DynamicState::eStencilOp},
                Entry{.k = DynamicState::RasterizerDiscardEnable,
                      .v = vk::DynamicState::eRasterizerDiscardEnable},
                Entry{.k = DynamicState::DepthBiasEnable,
                      .v = vk::DynamicState::eDepthBiasEnable},
                Entry{.k = DynamicState::PrimitiveRestartEnable,
                      .v = vk::DynamicState::ePrimitiveRestartEnable},
            });
    }

    vk::Format VulkanUtils::toVkFormat(Format format)
    {
      return K_FORMAT_MAP.to(format, vk::Format::eUndefined);
    }

    Format VulkanUtils::fromVkFormat(vk::Format format)
    {
      return K_FORMAT_MAP.from(format, Format::Undefined);
    }

    vk::BufferUsageFlags VulkanUtils::toVkBufferUsage(BufferUsageFlags usage)
    {
      return K_BUFFER_USAGE_MAP.to(usage);
    }

    BufferUsageFlags VulkanUtils::fromVkBufferUsage(vk::BufferUsageFlags flags)
    {
      return K_BUFFER_USAGE_MAP.from(flags);
    }

    vk::ImageUsageFlags VulkanUtils::toVkImageUsage(TextureUsageFlags usage)
    {
      return K_IMAGE_USAGE_MAP.to(usage);
    }

    TextureUsageFlags VulkanUtils::fromVkImageUsage(vk::ImageUsageFlags flags)
    {
      return K_IMAGE_USAGE_MAP.from(flags);
    }

    vk::SampleCountFlagBits VulkanUtils::toVkSampleCount(uint32_t count)
    {
        switch (count)
        {
        case 1: return vk::SampleCountFlagBits::e1;
        case 2: return vk::SampleCountFlagBits::e2;
        case 4: return vk::SampleCountFlagBits::e4;
        case 8: return vk::SampleCountFlagBits::e8;
        case 16: return vk::SampleCountFlagBits::e16;
        case 32: return vk::SampleCountFlagBits::e32;
        case 64: return vk::SampleCountFlagBits::e64;
        default: return vk::SampleCountFlagBits::e1;
        }
    }

    VmaMemoryUsage VulkanUtils::toVmaMemoryUsage(MemoryUsage usage)
    {
      return K_MEMORY_USAGE_MAP.to(usage, VMA_MEMORY_USAGE_AUTO);
    }

    vk::ShaderStageFlags VulkanUtils::toVkShaderStage(ShaderStageFlags stage)
    {
      return K_SHADER_STAGE_MAP.to(stage);
    }

    ShaderStageFlags VulkanUtils::fromVkShaderStage(vk::ShaderStageFlags flags)
    {
      return K_SHADER_STAGE_MAP.from(flags);
    }

    vk::PipelineStageFlags2 VulkanUtils::toVkPipelineStage(ShaderStageFlags stage)
    {
      if (!stage) {
        return vk::PipelineStageFlagBits2::eNone;
      }
      if (stage.has(ShaderStage::Host)) {
        return vk::PipelineStageFlagBits2::eHost;
      }

        if (stage.has(ShaderStage::All))
        {
            return vk::PipelineStageFlagBits2::eAllCommands | vk::PipelineStageFlagBits2::eHost;
        }

        vk::PipelineStageFlags2 flags{};

        if (stage.has(ShaderStage::Vertex)) {
          flags |= vk::PipelineStageFlagBits2::eVertexShader;
        }
        if (stage.has(ShaderStage::Fragment)) {
          flags |= vk::PipelineStageFlagBits2::eFragmentShader;
        }
        if (stage.has(ShaderStage::Geometry)) {
          flags |= vk::PipelineStageFlagBits2::eGeometryShader;
        }
        if (stage.has(ShaderStage::Compute)) {
          flags |= vk::PipelineStageFlagBits2::eComputeShader;
        }

        if (stage.has(ShaderStage::TessEval)) {
          flags |= vk::PipelineStageFlagBits2::eTessellationEvaluationShader;
        }
        if (stage.has(ShaderStage::TessControl)) {
          flags |= vk::PipelineStageFlagBits2::eTessellationControlShader;
        }

        if (stage.has(ShaderStage::DepthStencilAttachment))
        {
            flags |= vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                vk::PipelineStageFlagBits2::eLateFragmentTests;
        }

        if (stage.has(ShaderStage::RenderTarget))
        {
            flags |= vk::PipelineStageFlagBits2::eColorAttachmentOutput |
                vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                vk::PipelineStageFlagBits2::eLateFragmentTests;
        }

        if (stage.has(ShaderStage::Transfer)) {
          flags |= vk::PipelineStageFlagBits2::eTransfer;
        }
        if (stage.has(ShaderStage::DrawIndirect)) {
          flags |= vk::PipelineStageFlagBits2::eDrawIndirect;
        }
        if (stage.has(ShaderStage::Host)) {
          flags |= vk::PipelineStageFlagBits2::eHost;
        }

        return flags;
    }

    vk::ImageLayout VulkanUtils::toVkImageLayout(ResourceLayout layout)
    {
        switch (layout)
        {
        case ResourceLayout::VertexBufferRead:
        case ResourceLayout::IndexBufferRead:
        case ResourceLayout::IndirectBufferRead:
        case ResourceLayout::UniformBufferRead:
            PNKR_ASSERT(false, "toVkImageLayout called with buffer-only ResourceLayout");
            return vk::ImageLayout::eUndefined;
        default:
            break;
        }

        return K_RESOURCE_LAYOUT_MAP.to(layout, vk::ImageLayout::eUndefined);
    }

    vk::PrimitiveTopology VulkanUtils::toVkTopology(PrimitiveTopology topology)
    {
      return K_PRIMITIVE_TOPOLOGY_MAP.to(topology,
                                         vk::PrimitiveTopology::eTriangleList);
    }

    PrimitiveTopology VulkanUtils::fromVkTopology(vk::PrimitiveTopology topology)
    {
      return K_PRIMITIVE_TOPOLOGY_MAP.from(topology,
                                           PrimitiveTopology::TriangleList);
    }

    vk::PolygonMode VulkanUtils::toVkPolygonMode(PolygonMode mode)
    {
      return K_POLYGON_MODE_MAP.to(mode, vk::PolygonMode::eFill);
    }

    PolygonMode VulkanUtils::fromVkPolygonMode(vk::PolygonMode mode)
    {
      return K_POLYGON_MODE_MAP.from(mode, PolygonMode::Fill);
    }

    vk::CullModeFlags VulkanUtils::toVkCullMode(CullMode mode)
    {
      const vk::CullModeFlagBits bits =
          K_CULL_MODE_MAP.to(mode, vk::CullModeFlagBits::eNone);
      return {bits};
    }

    CullMode VulkanUtils::fromVkCullMode(vk::CullModeFlags flags)
    {
      if ((flags & vk::CullModeFlagBits::eFront) &&
          (flags & vk::CullModeFlagBits::eBack)) {
        return CullMode::FrontAndBack;
      }
      if (flags & vk::CullModeFlagBits::eFront) {
        return CullMode::Front;
      }
      if (flags & vk::CullModeFlagBits::eBack) {
        return CullMode::Back;
      }
        return CullMode::None;
    }

    vk::CompareOp VulkanUtils::toVkCompareOp(CompareOp op)
    {
      if (op == CompareOp::None) {
        return vk::CompareOp::eAlways;
      }
      return K_COMPARE_OP_MAP.to(op, vk::CompareOp::eLess);
    }

    CompareOp VulkanUtils::fromVkCompareOp(vk::CompareOp op)
    {
      return K_COMPARE_OP_MAP.from(op, CompareOp::Less);
    }

    vk::BlendFactor VulkanUtils::toVkBlendFactor(BlendFactor factor)
    {
      return K_BLEND_FACTOR_MAP.to(factor, vk::BlendFactor::eOne);
    }

    BlendFactor VulkanUtils::fromVkBlendFactor(vk::BlendFactor factor)
    {
      return K_BLEND_FACTOR_MAP.from(factor, BlendFactor::One);
    }

    vk::BlendOp VulkanUtils::toVkBlendOp(BlendOp op)
    {
      return K_BLEND_OP_MAP.to(op, vk::BlendOp::eAdd);
    }

    BlendOp VulkanUtils::fromVkBlendOp(vk::BlendOp op)
    {
      return K_BLEND_OP_MAP.from(op, BlendOp::Add);
    }

    vk::Filter VulkanUtils::toVkFilter(Filter filter)
    {
      return K_FILTER_MAP.to(filter, vk::Filter::eLinear);
    }

    Filter VulkanUtils::fromVkFilter(vk::Filter filter)
    {
      return K_FILTER_MAP.from(filter, Filter::Linear);
    }

    vk::SamplerAddressMode VulkanUtils::toVkAddressMode(SamplerAddressMode mode)
    {
      return K_SAMPLER_ADDRESS_MODE_MAP.to(mode,
                                           vk::SamplerAddressMode::eRepeat);
    }

    SamplerAddressMode VulkanUtils::fromVkAddressMode(vk::SamplerAddressMode mode)
    {
      return K_SAMPLER_ADDRESS_MODE_MAP.from(mode, SamplerAddressMode::Repeat);
    }

    vk::AttachmentLoadOp VulkanUtils::toVkLoadOp(LoadOp op)
    {
      return K_LOAD_OP_MAP.to(op, vk::AttachmentLoadOp::eDontCare);
    }

    vk::AttachmentStoreOp VulkanUtils::toVkStoreOp(StoreOp op)
    {
      return K_STORE_OP_MAP.to(op, vk::AttachmentStoreOp::eStore);
    }

    vk::DescriptorType VulkanUtils::toVkDescriptorType(DescriptorType type)
    {
      return K_DESCRIPTOR_TYPE_MAP.to(type, vk::DescriptorType::eUniformBuffer);
    }

    DescriptorType VulkanUtils::fromVkDescriptorType(vk::DescriptorType type)
    {
      return K_DESCRIPTOR_TYPE_MAP.from(type, DescriptorType::UniformBuffer);
    }

    vk::Viewport VulkanUtils::toVkViewport(const Viewport& viewport)
    {
        return vk::Viewport{
            viewport.x,
            viewport.y + viewport.height,
            viewport.width,
            -viewport.height,
            viewport.minDepth,
            viewport.maxDepth
        };
    }

    vk::Rect2D VulkanUtils::toVkRect2D(const Rect2D& rect)
    {
        return vk::Rect2D{
            vk::Offset2D{rect.x, rect.y},
            vk::Extent2D{rect.width, rect.height}
        };
    }

    vk::Extent2D VulkanUtils::toVkExtent2D(const Extent2D& extent)
    {
        return vk::Extent2D{extent.width, extent.height};
    }

    vk::Extent3D VulkanUtils::toVkExtent3D(const Extent3D& extent)
    {
        return vk::Extent3D{extent.width, extent.height, extent.depth};
    }

    vk::ClearValue VulkanUtils::toVkClearValue(const ClearValue& clearValue)
    {
        vk::ClearValue vkClear{};

        if (clearValue.isDepthStencil)
        {
            vkClear.depthStencil.depth = clearValue.depthStencil.depth;
            vkClear.depthStencil.stencil = clearValue.depthStencil.stencil;
        }
        else
        {
            std::memcpy(vkClear.color.float32, clearValue.color.float32, sizeof(float) * 4);
        }

        return vkClear;
    }

    vk::DynamicState VulkanUtils::toVkDynamicState(DynamicState state)
    {
      return K_DYNAMIC_STATE_MAP.to(state, vk::DynamicState::eViewport);
    }

    vk::ImageAspectFlags VulkanUtils::getImageAspectMask(vk::Format format)
    {
        if (format == vk::Format::eD16Unorm ||
            format == vk::Format::eD32Sfloat ||
            format == vk::Format::eD24UnormS8Uint ||
            format == vk::Format::eD32SfloatS8Uint)
        {
            vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eDepth;

            if (format == vk::Format::eD24UnormS8Uint ||
                format == vk::Format::eD32SfloatS8Uint)
            {
                aspectMask |= vk::ImageAspectFlagBits::eStencil;
            }

            return aspectMask;
        }

        return vk::ImageAspectFlagBits::eColor;
    }

    vk::ImageAspectFlags VulkanUtils::rhiToVkTextureAspectFlags(Format format) {
        switch (format) {
            case Format::D16_UNORM:
            case Format::D32_SFLOAT:
            case Format::D24_UNORM_S8_UINT: return vk::ImageAspectFlagBits::eDepth;
            case Format::S8_UINT: return vk::ImageAspectFlagBits::eStencil;
            default: return vk::ImageAspectFlagBits::eColor;
        }
    }

    void VulkanUtils::setDebugName(vk::Device device, vk::ObjectType type, uint64_t handle, const std::string& name)
    {
        if (!name.empty() && VULKAN_HPP_DEFAULT_DISPATCHER.vkSetDebugUtilsObjectNameEXT != nullptr)
        {
            vk::DebugUtilsObjectNameInfoEXT nameInfo{};
            nameInfo.objectType = type;
            nameInfo.objectHandle = handle;
            nameInfo.pObjectName = name.c_str();
            device.setDebugUtilsObjectNameEXT(nameInfo);
        }
    }

    std::pair<vk::PipelineStageFlags2, vk::AccessFlags2> VulkanUtils::getLayoutStageAccess(vk::ImageLayout layout)
    {
        switch (layout)
        {
        case vk::ImageLayout::eUndefined:
            return {vk::PipelineStageFlagBits2::eTopOfPipe, vk::AccessFlags2{}};

        case vk::ImageLayout::eTransferDstOptimal:
            return {vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite};

        case vk::ImageLayout::eTransferSrcOptimal:
            return {vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead};

        case vk::ImageLayout::eColorAttachmentOptimal:
            return {
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite
            };

        case vk::ImageLayout::eDepthStencilAttachmentOptimal:
            return {
                vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
                vk::AccessFlagBits2::eDepthStencilAttachmentWrite | vk::AccessFlagBits2::eDepthStencilAttachmentRead
            };

        case vk::ImageLayout::eShaderReadOnlyOptimal:
            return {
                vk::PipelineStageFlagBits2::eFragmentShader |
                vk::PipelineStageFlagBits2::eComputeShader |
                vk::PipelineStageFlagBits2::eVertexShader,
                vk::AccessFlagBits2::eShaderRead
            };

        case vk::ImageLayout::eGeneral:
            return {
                vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite
            };

        case vk::ImageLayout::ePresentSrcKHR:
            return {vk::PipelineStageFlagBits2::eBottomOfPipe, vk::AccessFlags2{}};

        default:
            return {
                vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite
            };
        }
    }
}

