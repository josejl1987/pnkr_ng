#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include "pnkr/renderer/shadergen_common.hpp"

namespace ShaderGen {
namespace gltf_vert {

#pragma pack(push, 1)
struct PerDrawData
{
    ShaderGen::Mat4 view;
    ShaderGen::Mat4 proj;
    ShaderGen::Mat4 model;
    ShaderGen::Float4 cameraPos;
    uint64_t transformBufferPtr;
    uint64_t materialBufferPtr;
    uint64_t environmentBufferPtr;
    uint64_t lightBufferPtr;
    uint32_t lightCount;
    uint32_t envId;
    uint32_t transmissionFramebuffer;
    uint32_t transmissionFramebufferSampler;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<PerDrawData>);
static_assert(sizeof(PerDrawData) == 256);
static_assert(offsetof(PerDrawData, view) == 0);
static_assert(offsetof(PerDrawData, proj) == 64);
static_assert(offsetof(PerDrawData, model) == 128);
static_assert(offsetof(PerDrawData, cameraPos) == 192);
static_assert(offsetof(PerDrawData, transformBufferPtr) == 208);
static_assert(offsetof(PerDrawData, materialBufferPtr) == 216);
static_assert(offsetof(PerDrawData, environmentBufferPtr) == 224);
static_assert(offsetof(PerDrawData, lightBufferPtr) == 232);
static_assert(offsetof(PerDrawData, lightCount) == 240);
static_assert(offsetof(PerDrawData, envId) == 244);
static_assert(offsetof(PerDrawData, transmissionFramebuffer) == 248);
static_assert(offsetof(PerDrawData, transmissionFramebufferSampler) == 252);

#pragma pack(push, 1)
struct PerFrameData
{
    PerDrawData drawable;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<PerFrameData>);
static_assert(sizeof(PerFrameData) == 256);
static_assert(offsetof(PerFrameData, drawable) == 0);

#pragma pack(push, 1)
struct BindlessStorageBuffer
{
    ShaderGen::RuntimeArray<uint32_t> data;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<BindlessStorageBuffer>);

#pragma pack(push, 1)
struct outWorldPos
{
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<outWorldPos>);

#pragma pack(push, 1)
struct outUV0
{
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<outUV0>);

#pragma pack(push, 1)
struct outColor
{
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<outColor>);

#pragma pack(push, 1)
struct outInstanceIndex
{
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<outInstanceIndex>);

#pragma pack(push, 1)
struct GLTFTransform
{
    ShaderGen::Mat4 model;
    ShaderGen::Mat4 normalMatrix;
    uint32_t nodeIndex;
    uint32_t primIndex;
    uint32_t materialIndex;
    uint32_t sortingType;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<GLTFTransform>);

#pragma pack(push, 1)
struct AnonStruct
{
    PerDrawData m0;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct>);

#pragma pack(push, 1)
struct GLTFTransform_31
{
    ShaderGen::Mat4 model;
    ShaderGen::Mat4 normalMatrix;
    uint32_t nodeIndex;
    uint32_t primIndex;
    uint32_t materialIndex;
    uint32_t sortingType;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<GLTFTransform_31>);
static_assert(sizeof(GLTFTransform_31) == 144);
static_assert(offsetof(GLTFTransform_31, model) == 0);
static_assert(offsetof(GLTFTransform_31, normalMatrix) == 64);
static_assert(offsetof(GLTFTransform_31, nodeIndex) == 128);
static_assert(offsetof(GLTFTransform_31, primIndex) == 132);
static_assert(offsetof(GLTFTransform_31, materialIndex) == 136);
static_assert(offsetof(GLTFTransform_31, sortingType) == 140);

#pragma pack(push, 1)
struct AnonStruct_30
{
    ShaderGen::RuntimeArray<GLTFTransform_31> m0;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_30>);

#pragma pack(push, 1)
struct AnonStruct_32
{
    ShaderGen::Mat4 m0;
    ShaderGen::Mat4 m1;
    uint32_t m2;
    uint32_t m3;
    uint32_t m4;
    uint32_t m5;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_32>);

#pragma pack(push, 1)
struct TransformBuffer
{
    ShaderGen::RuntimeArray<GLTFTransform_31> transforms;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<TransformBuffer>);

#pragma pack(push, 1)
struct AnonStruct_36
{
    ShaderGen::Mat4 m0;
    ShaderGen::Mat4 m1;
    uint32_t m2;
    uint32_t m3;
    uint32_t m4;
    uint32_t m5;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_36>);

#pragma pack(push, 1)
struct AnonStruct_39
{
    ShaderGen::Mat4 m0;
    ShaderGen::Mat4 m1;
    uint32_t m2;
    uint32_t m3;
    uint32_t m4;
    uint32_t m5;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_39>);

#pragma pack(push, 1)
struct AnonStruct_125
{
    ShaderGen::Float4 m0;
    float m1;
    std::array<float, 1> m2;
    std::array<float, 1> m3;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_125>);

#pragma pack(push, 1)
struct AnonStruct_150
{
    ShaderGen::RuntimeArray<uint32_t> m0;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_150>);

#pragma pack(push, 1)
struct AnonStruct_151
{
    ShaderGen::RuntimeArray<uint32_t> m0;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_151>);

} // namespace gltf_vert
} // namespace ShaderGen
