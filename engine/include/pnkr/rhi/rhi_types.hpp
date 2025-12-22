#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <type_traits>

namespace pnkr::renderer::rhi
{
    // Backend selection
    enum class RHIBackend
    {
        Vulkan,
        DirectX12,
        Metal,
        Auto // Auto-detect best available
    };

    // Helper for enum class flag operations
    template<typename T>
    requires std::is_enum_v<T>
    constexpr bool hasFlag(T value, T flag) {
        return (static_cast<std::underlying_type_t<T>>(value) &
                static_cast<std::underlying_type_t<T>>(flag)) != 0;
    }

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
        Present
    };

    // Resource formats (map to VkFormat, DXGI_FORMAT, MTLPixelFormat)
    enum class Format
    {
        Undefined,

        // Color formats
        R8_UNORM,
        R8G8_UNORM,
        R8G8B8_UNORM,
        R8G8B8A8_UNORM,
        R8G8B8A8_SRGB,
        B8G8R8A8_UNORM,
        B8G8R8A8_SRGB,

        R16_SFLOAT,
        R16G16_SFLOAT,
        R16G16B16A16_SFLOAT,

        R32_SFLOAT,
        R32G32_SFLOAT,
        R32G32B32_SFLOAT,
        R32G32B32A32_SFLOAT,

        // Depth/stencil formats
        D16_UNORM,
        D32_SFLOAT,
        D24_UNORM_S8_UINT,
        D32_SFLOAT_S8_UINT,

        // Compressed formats
        BC1_RGB_UNORM,
        BC1_RGB_SRGB,
        BC3_UNORM,
        BC3_SRGB,
        BC7_UNORM,
        BC7_SRGB
    };

    // Buffer usage flags
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

    // Texture usage flags
    enum class TextureUsage : uint32_t
    {
        None = 0,
        TransferSrc = 1 << 0,
        TransferDst = 1 << 1,
        Sampled = 1 << 2,
        Storage = 1 << 3,
        ColorAttachment = 1 << 4,
        DepthStencilAttachment = 1 << 5,
        InputAttachment = 1 << 6
    };

    // Memory properties
    enum class MemoryUsage
    {
        GPUOnly, // Device local (VRAM)
        CPUToGPU, // Upload heap
        GPUToCPU, // Readback heap
        CPUOnly // Staging
    };

    // Shader stages
    enum class ShaderStage : uint32_t
    {
        None = 0,
        Vertex = 1 << 0,
        Fragment = 1 << 1,
        Geometry = 1 << 2,
        Compute = 1 << 3,
        TessControl = 1 << 4,
        TessEval = 1 << 5,
        RenderTarget = 1 << 6,
        Transfer = 1 << 7,
        Host = 1 << 8,
        All = Vertex | Fragment | Geometry | Compute | TessControl | TessEval | RenderTarget | Transfer
    };

    // Primitive topology
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

    // Polygon mode
    enum class PolygonMode
    {
        Fill,
        Line,
        Point
    };

    // Cull mode
    enum class CullMode
    {
        None,
        Front,
        Back,
        FrontAndBack
    };

    // Blend factors
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

    // Blend operations
    enum class BlendOp
    {
        Add,
        Subtract,
        ReverseSubtract,
        Min,
        Max
    };

    // Compare operations
    enum class CompareOp
    {
        Never,
        Less,
        Equal,
        LessOrEqual,
        Greater,
        NotEqual,
        GreaterOrEqual,
        Always
    };

    // Texture filters
    enum class Filter
    {
        Nearest,
        Linear
    };

    // Sampler address modes
    enum class SamplerAddressMode
    {
        Repeat,
        MirroredRepeat,
        ClampToEdge,
        ClampToBorder
    };

    // Load operations
    enum class LoadOp
    {
        Load,
        Clear,
        DontCare
    };

    // Store operations
    enum class StoreOp
    {
        Store,
        DontCare
    };

    // Pipeline bind point
    enum class PipelineBindPoint
    {
        Graphics,
        Compute
    };

    // Descriptor types
    enum class DescriptorType
    {
        Sampler,
        CombinedImageSampler, // Texture + Sampler
        SampledImage,         // Texture without sampler (Separate)
        StorageImage,         // RWTexture / image2D
        UniformBuffer,        // UBO / cbuffer
        StorageBuffer,        // SSBO / StructuredBuffer
        UniformBufferDynamic,
        StorageBufferDynamic,
        InputAttachment       // Subpass input
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

    // Viewport and scissor
    struct Viewport
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
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

    // Clear values
    struct ClearColorValue
    {
        union
        {
            float float32[4];
            int32_t int32[4];
            uint32_t uint32[4];
        };
    };

    struct ClearDepthStencilValue
    {
        float depth = 1.0f;
        uint32_t stencil = 0;
    };

    struct ClearValue
    {
        bool isDepthStencil = false;

        union
        {
            ClearColorValue color;
            ClearDepthStencilValue depthStencil;
        };
    };

    // Vertex input
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

    // Descriptor set layout binding
    struct DescriptorBinding
    {
        uint32_t binding;
        DescriptorType type;
        uint32_t count = 1;
        ShaderStage stages;
    };

    struct DescriptorSetLayout
    {
        std::vector<DescriptorBinding> bindings;
    };


    // A simple wrapper for an index into the global bindless arrays
    struct BindlessHandle {
        uint32_t index = 0xFFFFFFFF;
        bool isValid() const { return index != 0xFFFFFFFF; }
    };

    // Operator overloads for flags
    inline BufferUsage operator|(BufferUsage a, BufferUsage b)
    {
        return static_cast<BufferUsage>(
            static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline BufferUsage operator&(BufferUsage a, BufferUsage b)
    {
        return static_cast<BufferUsage>(
            static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    inline BufferUsage& operator|=(BufferUsage& a, BufferUsage b)
    {
        a = a | b;
        return a;
    }

    inline BufferUsage& operator&=(BufferUsage& a, BufferUsage b)
    {
        a = a & b;
        return a;
    }

    inline TextureUsage operator|(TextureUsage a, TextureUsage b)
    {
        return static_cast<TextureUsage>(
            static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline TextureUsage operator&(TextureUsage a, TextureUsage b)
    {
        return static_cast<TextureUsage>(
            static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    inline TextureUsage& operator|=(TextureUsage& a, TextureUsage b)
    {
        a = a | b;
        return a;
    }

    inline TextureUsage& operator&=(TextureUsage& a, TextureUsage b)
    {
        a = a & b;
        return a;
    }

    inline ShaderStage operator|(ShaderStage a, ShaderStage b)
    {
        return static_cast<ShaderStage>(
            static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline ShaderStage operator&(ShaderStage a, ShaderStage b)
    {
        return static_cast<ShaderStage>(
            static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    inline ShaderStage& operator|=(ShaderStage& a, ShaderStage b)
    {
        a = a | b;
        return a;
    }

    inline ShaderStage& operator&=(ShaderStage& a, ShaderStage b)
    {
        a = a & b;
        return a;
    }
} // namespace pnkr::renderer::rhi
