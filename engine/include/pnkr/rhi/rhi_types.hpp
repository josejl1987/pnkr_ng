#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <type_traits>
#include "pnkr/core/bitflags.hpp"

namespace pnkr::renderer::rhi
{

    enum class RHIBackend
    {
        Vulkan,
        DirectX12,
        Metal,
        Auto
    };

    enum class ResourceLayout
    {
        Undefined,
        General,
        ColorAttachment,
        DepthStencilAttachment,
        DepthStencilReadOnly,
        ShaderReadOnly,
        TransferSrc,
        TransferDst,
        Present, VertexBufferRead, IndexBufferRead, IndirectBufferRead, UniformBufferRead
    };

    enum class Format
    {
        Undefined,

        R8_UNORM,
        R8G8_UNORM,
        R8G8B8_UNORM,
        R8G8B8A8_UNORM,
        R8G8B8A8_SRGB,
        B8G8R8A8_UNORM,
        B8G8R8A8_SRGB,

        R8_SNORM,
        R8G8_SNORM,
        R8G8B8_SNORM,
        R8G8B8A8_SNORM,

        R8_UINT,
        R8G8_UINT,
        R8G8B8A8_UINT,

        R8_SINT,
        R8G8_SINT,
        R8G8B8A8_SINT,

        R16_UNORM,
        R16G16_UNORM,
        R16G16B16A16_UNORM,

        R16_SNORM,
        R16G16_SNORM,
        R16G16B16A16_SNORM,

        R16_SFLOAT,
        R16G16_SFLOAT,
        R16G16B16_SFLOAT,
        R16G16B16A16_SFLOAT,

        R16_UINT,
        R16G16_UINT,
        R16G16B16A16_UINT,

        R16_SINT,
        R16G16_SINT,
        R16G16B16A16_SINT,

        R32_SFLOAT,
        R32G32_SFLOAT,
        R32G32B32_SFLOAT,
        R32G32B32A32_SFLOAT,

        R32_UINT,
        R32G32_UINT,
        R32G32B32_UINT,
        R32G32B32A32_UINT,

        R32_SINT,
        R32G32_SINT,
        R32G32B32_SINT,
        R32G32B32A32_SINT,

        B10G11R11_UFLOAT_PACK32,
        A2B10G10R10_UNORM_PACK32,
        A2R10G10B10_UNORM_PACK32,
        E5B9G9R9_UFLOAT_PACK32,

        D16_UNORM,
        D32_SFLOAT,
        D24_UNORM_S8_UINT,
        D32_SFLOAT_S8_UINT,
        S8_UINT,

        BC1_RGB_UNORM,
        BC1_RGB_SRGB,
        BC1_RGBA_UNORM,
        BC1_RGBA_SRGB,
        BC2_UNORM,
        BC2_SRGB,
        BC3_UNORM,
        BC3_SRGB,
        BC4_UNORM,
        BC4_SNORM,
        BC5_UNORM,
        BC5_SNORM,
        BC6H_UFLOAT,
        BC6H_SFLOAT,
        BC7_UNORM,
        BC7_SRGB,

        ASTC_4x4_UNORM,
        ASTC_4x4_SRGB,
        ASTC_6x6_UNORM,
        ASTC_6x6_SRGB,
        ASTC_8x8_UNORM,
        ASTC_8x8_SRGB,

        ETC2_R8G8B8_UNORM,
        ETC2_R8G8B8_SRGB,
        ETC2_R8G8B8A8_UNORM,
        ETC2_R8G8B8A8_SRGB,
    };

    enum class BufferUsage : uint32_t
    {
        None = 0,
        TransferSrc = 1 << 0,
        TransferDst = 1 << 1,
        UniformBuffer = 1 << 2,
        StorageBuffer = 1 << 3,
        IndexBuffer = 1 << 4,
        VertexBuffer = 1 << 5,
        IndirectBuffer = 1 << 6,
        ShaderDeviceAddress  = 1 << 7

    };
    PNKR_ENABLE_BITMASK_OPERATORS(BufferUsage);
    using BufferUsageFlags = core::Flags<BufferUsage>;

    enum class TextureUsage : uint32_t
    {
        None = 0,
        TransferSrc = 1 << 0,
        TransferDst = 1 << 1,
        Sampled = 1 << 2,
        Storage = 1 << 3,
        ColorAttachment = 1 << 4,
        DepthStencilAttachment = 1 << 5,
        InputAttachment = 1 << 6,
        TransientAttachment = 1 << 7
    };
    PNKR_ENABLE_BITMASK_OPERATORS(TextureUsage);
    using TextureUsageFlags = core::Flags<TextureUsage>;

    // Define Offset3D for consistent use
    struct Offset3D {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
    };

    struct TextureSubresource
    {
        uint32_t mipLevel = 0;
        uint32_t arrayLayer = 0;
    };

    struct Rect2D
    {
        int32_t x = 0;
        int32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct Extent2D
    {
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct Extent3D
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 1;
    };
    struct TextureCopyRegion
    {
        TextureSubresource srcSubresource;
        TextureSubresource dstSubresource;

        // For vkCmdBlitImage: These define the corners of the source and destination regions.
        // For vkCmdCopyImage/vkCmdResolveImage: These are typically {0,0,0} for full subresource operations.
        Offset3D srcOffsets[2];
        Offset3D dstOffsets[2];

        // Compatibility with existing code that expects srcOffset/dstOffset as single points (index 0)
        // We can add accessors or just use srcOffsets[0] in implementation. 
        // However, to match previous struct structure closer for code compatibility:
        Offset3D& srcOffset() { return srcOffsets[0]; }
        const Offset3D& srcOffset() const { return srcOffsets[0]; }
        Offset3D& dstOffset() { return dstOffsets[0]; }
        const Offset3D& dstOffset() const { return dstOffsets[0]; }

        Extent3D extent;
    };

    struct BufferTextureCopyRegion
    {
        uint64_t bufferOffset = 0;
        uint32_t bufferRowLength = 0;
        uint32_t bufferImageHeight = 0;
        TextureSubresource textureSubresource;
        Offset3D textureOffset;
        Extent3D textureExtent;
    };

    enum class TextureAspect : uint32_t
    {
        Color = 1 << 0,
        Depth = 1 << 1,
        Stencil = 1 << 2,
        Metadata = 1 << 3,
        Plane0 = 1 << 4,
        Plane1 = 1 << 5,
        Plane2 = 1 << 6,
        All = Color | Depth | Stencil
    };
    PNKR_ENABLE_BITMASK_OPERATORS(TextureAspect);
    using TextureAspectFlags = core::Flags<TextureAspect>;

    enum class MemoryUsage
    {
        GPUOnly,
        CPUToGPU,
        GPUToCPU,
        CPUOnly,
        GPULazy
    };

    enum class ShaderStage : uint32_t
    {
        None = 0,
        Vertex = 1 << 0,
        Fragment = 1 << 1,
        Geometry = 1 << 2,
        Compute = 1 << 3,
        TessControl = 1 << 4,
        TessEval = 1 << 5,
        DepthStencilAttachment = 1 << 6,
        RenderTarget = 1 << 7,
        Transfer = 1 << 8,
        Host = 1 << 9,
        DrawIndirect = 1 << 10,
        AllGraphics = Vertex | Fragment | Geometry | TessControl | TessEval | RenderTarget | DepthStencilAttachment,
        All = AllGraphics | Compute | Transfer | DrawIndirect
    };
    PNKR_ENABLE_BITMASK_OPERATORS(ShaderStage);
    using ShaderStageFlags = core::Flags<ShaderStage>;

    struct DrawIndexedIndirectCommand
    {
        uint32_t indexCount = 0;
        uint32_t instanceCount = 0;
        uint32_t firstIndex = 0;
        int32_t vertexOffset = 0;
        uint32_t firstInstance = 0;
    };

    enum class PrimitiveTopology
    {
        PointList,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip,
        TriangleFan,
        PatchList
    };

    enum class PolygonMode
    {
        Fill,
        Line,
        Point
    };

    enum class CullMode
    {
        None,
        Front,
        Back,
        FrontAndBack
    };

    enum class BlendFactor
    {
        Zero,
        One,
        SrcColor,
        OneMinusSrcColor,
        DstColor,
        OneMinusDstColor,
        SrcAlpha,
        OneMinusSrcAlpha,
        DstAlpha,
        OneMinusDstAlpha
    };

    enum class BlendOp
    {
        Add,
        Subtract,
        ReverseSubtract,
        Min,
        Max
    };

    enum class CompareOp
    {
        None,
        Never,
        Less,
        Equal,
        LessOrEqual,
        Greater,
        NotEqual,
        GreaterOrEqual,
        Always
    };

    enum class Filter
    {
        Nearest,
        Linear
    };

    enum class SamplerAddressMode
    {
        Repeat,
        MirroredRepeat,
        ClampToEdge,
        ClampToBorder
    };

    enum class LoadOp
    {
        Load,
        Clear,
        DontCare
    };

    enum class StoreOp
    {
        Store,
        DontCare
    };

    enum class PipelineBindPoint
    {
        Graphics,
        Compute
    };

    enum class DescriptorType
    {
        Sampler,
        CombinedImageSampler,
        SampledImage,
        StorageImage,
        UniformBuffer,
        StorageBuffer,
        UniformBufferDynamic,
        StorageBufferDynamic,
        InputAttachment
    };

    enum class VertexSemantic {
        Position,
        Color,
        Normal,
        TexCoord,
        TexCoord0,
        TexCoord1,
        Tangent,
        Bitangent,
        BoneIds,
        Weights,
        Unknown
    };

    struct VertexInputAttribute {
        uint32_t location;
        uint32_t binding;
        Format format;
        uint32_t offset;
        VertexSemantic semantic;
    };

    struct Viewport
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
    };


    struct ClearColorValue
    {
        float float32[4] = {};
        int32_t int32[4] = {};
        uint32_t uint32[4] = {};
    };

    struct ClearDepthStencilValue
    {
        float depth = 1.0f;
        uint32_t stencil = 0;
    };

    struct ClearValue
    {
        bool isDepthStencil = false;

        ClearColorValue color{};
        ClearDepthStencilValue depthStencil{};
    };

    enum class VertexInputRate
    {
        Vertex,
        Instance
    };

    enum class DynamicState
    {
        Viewport,
        Scissor,
        LineWidth,
        DepthBias,
        BlendConstants,
        DepthBounds,
        StencilCompareMask,
        StencilWriteMask,
        StencilReference,
        CullMode,
        FrontFace,
        PrimitiveTopology,
        ViewportWithCount,
        ScissorWithCount,
        VertexInputBindingStride,
        DepthTestEnable,
        DepthWriteEnable,
        DepthCompareOp,
        DepthBoundsTestEnable,
        StencilTestEnable,
        StencilOp,
        RasterizerDiscardEnable,
        DepthBiasEnable,
        PrimitiveRestartEnable
    };

    struct VertexInputBinding
    {
        uint32_t binding;
        uint32_t stride;
        VertexInputRate inputRate;
    };

    enum class DescriptorBindingFlags
    {
        None = 0,
        UpdateAfterBind = 1 << 0,
        PartiallyBound = 1 << 1,
        VariableDescriptorCount = 1 << 2
    };
    PNKR_ENABLE_BITMASK_OPERATORS(DescriptorBindingFlags);

    struct DescriptorBinding
    {
        uint32_t binding;
        DescriptorType type;
        uint32_t count = 1;
        ShaderStageFlags stages;
        std::string name;
        core::Flags<DescriptorBindingFlags> flags = DescriptorBindingFlags::None;
    };

    struct DescriptorSetLayout
    {
        std::vector<DescriptorBinding> bindings;
    };

    constexpr uint32_t kInvalidBindlessIndex = ~0u;
    constexpr uint32_t kQueueFamilyIgnored = 0xFFFFFFFFu;

    struct TextureTag {};
    struct BufferTag {};
    struct SamplerTag {};

    template <typename Tag>
    class BindlessHandle {
    public:
        constexpr BindlessHandle() = default;
        constexpr explicit BindlessHandle(uint32_t index) : m_index(index) {}

        [[nodiscard]] bool isValid() const { return m_index != kInvalidBindlessIndex; }
        [[nodiscard]] uint32_t index() const { return m_index; }

        static const BindlessHandle Invalid;

        bool operator==(const BindlessHandle& other) const = default;

        explicit operator uint32_t() const { return m_index; }

    private:
        uint32_t m_index = kInvalidBindlessIndex;
    };

    template <typename Tag>
    inline const BindlessHandle<Tag> BindlessHandle<Tag>::Invalid = BindlessHandle<Tag>(kInvalidBindlessIndex);

    using TextureBindlessHandle = BindlessHandle<TextureTag>;
    using BufferBindlessHandle  = BindlessHandle<BufferTag>;
    using SamplerBindlessHandle = BindlessHandle<SamplerTag>;

    enum class TextureType
    {
        Texture1D,
        Texture2D,
        Texture3D,
        TextureCube
    };

    class RHICommandBuffer;
    using RHICommandList = RHICommandBuffer;

    static_assert((uint32_t)ShaderStage::Vertex != 0);
    static_assert((uint32_t)ShaderStage::Fragment != 0);
    static_assert((uint32_t)ShaderStage::Compute != 0);
    static_assert((uint32_t)ShaderStage::TessControl != 0);
    static_assert((uint32_t)ShaderStage::TessEval != 0);
    static_assert((uint32_t)ShaderStage::Geometry != 0);
    static_assert((uint32_t)ShaderStage::RenderTarget != 0);
    static_assert((uint32_t)ShaderStage::DepthStencilAttachment != 0);
    static_assert((uint32_t)ShaderStage::Transfer != 0);
    static_assert((uint32_t)ShaderStage::DrawIndirect != 0);

    static_assert(((uint32_t)ShaderStage::Vertex & (uint32_t)ShaderStage::Compute) == 0);
    static_assert(((uint32_t)ShaderStage::Fragment & (uint32_t)ShaderStage::Compute) == 0);
    static_assert(((uint32_t)ShaderStage::Compute & (uint32_t)ShaderStage::TessControl) == 0);
    static_assert(((uint32_t)ShaderStage::Compute & (uint32_t)ShaderStage::TessEval) == 0);
    static_assert(((uint32_t)ShaderStage::Vertex & (uint32_t)ShaderStage::TessControl) == 0);
}
