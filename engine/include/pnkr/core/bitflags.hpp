#pragma once

#include <type_traits>
#include <compare>

namespace pnkr::core
{
    template <typename BitType>
    class Flags
    {
    public:
        using MaskType = std::underlying_type_t<BitType>;

        constexpr Flags() noexcept : m_mask(0) {}
        constexpr Flags(BitType bit) noexcept : m_mask(static_cast<MaskType>(bit)) {}
        constexpr Flags(Flags<BitType> const& rhs) noexcept = default;
        constexpr explicit Flags(MaskType flags) noexcept : m_mask(flags) {}

        auto operator<=>(Flags<BitType> const&) const = default;

        constexpr bool operator!() const noexcept
        {
            return !m_mask;
        }

        constexpr Flags<BitType> operator&(Flags<BitType> const& rhs) const noexcept
        {
            return Flags<BitType>(m_mask & rhs.m_mask);
        }

        constexpr Flags<BitType> operator|(Flags<BitType> const& rhs) const noexcept
        {
            return Flags<BitType>(m_mask | rhs.m_mask);
        }

        constexpr Flags<BitType> operator^(Flags<BitType> const& rhs) const noexcept
        {
            return Flags<BitType>(m_mask ^ rhs.m_mask);
        }

        constexpr Flags<BitType> operator~() const noexcept
        {
            return Flags<BitType>(static_cast<MaskType>(~m_mask));
        }

        constexpr Flags<BitType>& operator=(Flags<BitType> const& rhs) noexcept = default;

        constexpr Flags<BitType>& operator|=(Flags<BitType> const& rhs) noexcept
        {
            m_mask |= rhs.m_mask;
            return *this;
        }

        constexpr Flags<BitType>& operator&=(Flags<BitType> const& rhs) noexcept
        {
            m_mask &= rhs.m_mask;
            return *this;
        }

        constexpr Flags<BitType>& operator^=(Flags<BitType> const& rhs) noexcept
        {
            m_mask ^= rhs.m_mask;
            return *this;
        }

        explicit constexpr operator bool() const noexcept
        {
            return !!m_mask;
        }

        explicit constexpr operator MaskType() const noexcept
        {
            return m_mask;
        }

        constexpr bool has(BitType bit) const noexcept
        {
            return (m_mask & static_cast<MaskType>(bit)) == static_cast<MaskType>(bit);
        }

        constexpr bool hasAny(Flags<BitType> flags) const noexcept
        {
            return (m_mask & flags.m_mask) != 0;
        }

    private:
        MaskType m_mask;
    };

    template <typename BitType>
    constexpr Flags<BitType> operator|(BitType bit, Flags<BitType> const& flags) noexcept
    {
        return flags | bit;
    }

    template <typename BitType>
    constexpr Flags<BitType> operator&(BitType bit, Flags<BitType> const& flags) noexcept
    {
        return flags & bit;
    }

    template <typename BitType>
    constexpr Flags<BitType> operator^(BitType bit, Flags<BitType> const& flags) noexcept
    {
        return flags ^ bit;
    }
}

#define PNKR_ENABLE_BITMASK_OPERATORS(BitType) \
    inline constexpr pnkr::core::Flags<BitType> operator|(BitType lhs, BitType rhs) noexcept \
    { \
        return pnkr::core::Flags<BitType>(lhs) | rhs; \
    } \
    inline constexpr pnkr::core::Flags<BitType> operator&(BitType lhs, BitType rhs) noexcept \
    { \
        return pnkr::core::Flags<BitType>(lhs) & rhs; \
    } \
    inline constexpr pnkr::core::Flags<BitType> operator^(BitType lhs, BitType rhs) noexcept \
    { \
        return pnkr::core::Flags<BitType>(lhs) ^ rhs; \
    } \
    inline constexpr pnkr::core::Flags<BitType> operator~(BitType bit) noexcept \
    { \
        return ~(pnkr::core::Flags<BitType>(bit)); \
    }
