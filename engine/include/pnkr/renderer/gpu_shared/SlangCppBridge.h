#pragma once

#ifdef __cplusplus
    #include <glm/glm.hpp>
    #include <cstdint>

    using uint = uint32_t;
    using uint2 = glm::uvec2;
    using uint3 = glm::uvec3;
    using uint4 = glm::uvec4;

    using int2 = glm::ivec2;
    using int3 = glm::ivec3;
    using int4 = glm::ivec4;

    using float2 = glm::vec2;
    using float3 = glm::vec3;
    using float4 = glm::vec4;
    using float4x4 = glm::mat4;
    using float3x3 = glm::mat3;

    #define BDA_PTR(Type) uint64_t

    #define VK_PUSH_CONSTANT
    #define VK_BINDING(x, y)
    #define ALIGN_16 alignas(16)

#else

    #define BDA_PTR(Type) Type*

    #define VK_PUSH_CONSTANT [[vk::push_constant]]
    #define VK_BINDING(x, y) [[vk::binding(x, y)]]
    #define ALIGN_16 [spirv(aligned, 16)]
#endif

#define BINDLESS_INVALID_ID 0xFFFFFFFFu
#define BINDLESS_INVALID_TEXTURE 0xFFFFFFFFu
