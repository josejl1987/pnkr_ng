#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace ShaderGen
{
    // Byte padding that is safe for N==0.
    template <size_t N>
    struct Pad
    {
        std::array<std::byte, N> bytes{};
    };

    // 32-bit bool in SPIR-V interface blocks (std140/std430 rules).
    using Bool = uint32_t;

    // Represents a SPIR-V runtime array member (e.g., `T data[];`).
    // This has no fixed size and therefore cannot participate in sizeof-based assertions.
    // It exists purely to preserve the member name/type in generated headers.
    template <typename T>
    struct RuntimeArray
    {
        using value_type = T;
    };

    static_assert(std::is_standard_layout_v<RuntimeArray<uint32_t>>);


    // Buffer-reference / device-address representation on CPU side.
    // (You push this as an address or store it in a UBO/SSBO CPU mirror.)
    struct DeviceAddress
    {
        uint64_t value = 0;
        DeviceAddress() = default;

        DeviceAddress(uint64_t v) : value(v)
        {
        }

        DeviceAddress& operator=(uint64_t v)
        {
            value = v;
            return *this;
        }

        operator uint64_t() const { return value; }
    };

    // Vec4-like POD with glm assignment support.
    template <typename T>
    struct Vec4
    {
        T x{}, y{}, z{}, w{};

        Vec4() = default;

        Vec4(T _x, T _y, T _z, T _w) : x(_x), y(_y), z(_z), w(_w)
        {
        }

        // glm convenience for float specialization
        Vec4(const glm::vec4& v) requires (std::is_same_v<T, float>)
        {
            x = v.x;
            y = v.y;
            z = v.z;
            w = v.w;
        }

        Vec4& operator=(const glm::vec4& v) requires (std::is_same_v<T, float>)
        {
            x = v.x;
            y = v.y;
            z = v.z;
            w = v.w;
            return *this;
        }

        operator glm::vec4() const requires (std::is_same_v<T, float>)
        {
            return glm::vec4(x, y, z, w);
        }
    };

    // Mat4 POD with glm assignment support (column-major float32).
    struct Mat4
    {
        std::array<float, 16> m{};
        Mat4() = default;
        Mat4(const glm::mat4& v) { store(v); }

        Mat4& operator=(const glm::mat4& v)
        {
            store(v);
            return *this;
        }

        operator glm::mat4() const { return load(); }

        void store(const glm::mat4& v)
        {
            std::memcpy(m.data(), glm::value_ptr(v), sizeof(float) * 16);
        }

        glm::mat4 load() const
        {
            glm::mat4 v{};
            std::memcpy(glm::value_ptr(v), m.data(), sizeof(float) * 16);
            return v;
        }
    };

    // Fixed-size scalar vectors with std140-friendly padding for vec3 (32-bit scalar).
    template <typename T>
    struct Vec2
    {
        T x{}, y{};


        Vec2() = default;

        Vec2(T _x, T _y) : x(_x), y(_y)
        {
        }

        // glm convenience for float specialization
        Vec2(const glm::vec3& v) requires (std::is_same_v<T, float>)
        {
            x = v.x;
            y = v.y;
        }

        Vec2& operator=(const glm::vec2& v) requires (std::is_same_v<T, float>)
        {
            x = v.x;
            y = v.y;
            return *this;
        }

        operator glm::vec2() const requires (std::is_same_v<T, float>)
        {
            return glm::vec2(x, y);
        }
    };

    template <typename T>
    struct Vec3
    {
        T x{}, y{}, z{}, _pad{};


        Vec3() = default;

        Vec3(T _x, T _y, T _z) : x(_x), y(_y), z(_z)
        {
        }


        // glm convenience for float specialization
        Vec3(const glm::vec3& v) requires (std::is_same_v<T, float>)
        {
            x = v.x;
            y = v.y;
            z = v.z;
        }

        Vec3& operator=(const glm::vec3& v) requires (std::is_same_v<T, float>)
        {
            x = v.x;
            y = v.y;
            z = v.z;
            return *this;
        }

        operator glm::vec3() const requires (std::is_same_v<T, float>)
        {
            return glm::vec3(x, y, z);
        }
    };

    using Float2 = Vec2<float>;
    using Float3 = Vec3<float>;
    using Float4 = Vec4<float>;
    using Int2 = Vec2<int32_t>;
    using Int3 = Vec3<int32_t>;
    using Int4 = Vec4<int32_t>;
    using UInt2 = Vec2<uint32_t>;
    using UInt3 = Vec3<uint32_t>;
    using UInt4 = Vec4<uint32_t>;
} // namespace ShaderGen
