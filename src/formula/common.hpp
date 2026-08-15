#ifndef PQC_POLY_FORMULA_COMMON_HPP
#define PQC_POLY_FORMULA_COMMON_HPP

#include "pqc_poly/ring.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pqc_poly::detail
{

inline constexpr std::size_t padded_n = 512;
inline constexpr std::size_t linear_n = 2 * poly_n - 1;
inline constexpr std::uint16_t q_mask = poly_q - 1;

[[nodiscard]] constexpr std::uint16_t reduce_q(std::uint32_t v) noexcept
{
    return static_cast<std::uint16_t>(v & q_mask);
}

[[nodiscard]] constexpr std::uint16_t reduce_q(std::int64_t v) noexcept
{
    return static_cast<std::uint16_t>(static_cast<std::uint64_t>(v) & q_mask);
}

[[nodiscard]] constexpr std::uint16_t normalize(std::int16_t v) noexcept
{
    return static_cast<std::uint16_t>(v) & q_mask;
}

template <std::size_t size>
inline void normalize_pad(std::array<std::uint16_t, size> &r, const signed_poly &a) noexcept
{
    static_assert(size >= poly_n);

    r.fill(0);
    for (std::size_t i = 0; i < poly_n; ++i)
    {
        r[i] = normalize(a[i]);
    }
}

template <typename value_type, std::size_t size>
inline void fold_linear(poly &r, const std::array<value_type, size> &a) noexcept
{
    static_assert(size >= linear_n);

    // only one wrapped coefficient exists at each index because the product has degree below 2n
    for (std::size_t i = 0; i + poly_n < linear_n; ++i)
    {
        r[i] = reduce_q(static_cast<std::int64_t>(a[i]) + a[i + poly_n]);
    }

    r[poly_n - 1] = reduce_q(static_cast<std::int64_t>(a[poly_n - 1]));
}

}

#endif
