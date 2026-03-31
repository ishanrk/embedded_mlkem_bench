#ifndef PQC_POLY_RING_HPP
#define PQC_POLY_RING_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace pqc_poly
{

inline constexpr std::size_t poly_n = 509;
inline constexpr std::uint16_t poly_q = 2048;

using signed_poly = std::array<std::int16_t, poly_n>;
using poly = std::array<std::uint16_t, poly_n>;

void reference_multiply(poly &r, const signed_poly &a, const signed_poly &b) noexcept;

}

#endif
