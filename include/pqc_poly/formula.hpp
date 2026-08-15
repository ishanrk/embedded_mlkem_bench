#ifndef PQC_POLY_FORMULA_HPP
#define PQC_POLY_FORMULA_HPP

#include "pqc_poly/ring.hpp"

#include <array>
#include <cstddef>
#include <string_view>

namespace pqc_poly
{

enum class formula_kind
{
    schoolbook,
    karatsuba,
    ntt,
    toom_cook,
    tmvp,
};

inline constexpr std::array<formula_kind, 5> formula_kinds = {
    formula_kind::schoolbook, formula_kind::karatsuba, formula_kind::ntt,
    formula_kind::toom_cook,  formula_kind::tmvp,
};

[[nodiscard]] std::string_view formula_name(formula_kind kind) noexcept;

void schoolbook(poly &r, const signed_poly &a, const signed_poly &b) noexcept;
void karatsuba(poly &r, const signed_poly &a, const signed_poly &b) noexcept;
void ntt(poly &r, const signed_poly &a, const signed_poly &b) noexcept;
void toom_cook(poly &r, const signed_poly &a, const signed_poly &b) noexcept;
void tmvp(poly &r, const signed_poly &a, const signed_poly &b) noexcept;

void multiply(formula_kind kind, poly &r, const signed_poly &a, const signed_poly &b) noexcept;

}

#endif
