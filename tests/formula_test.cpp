#include "pqc_poly/formula.hpp"
#include "pqc_poly/ring.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace
{

[[nodiscard]] std::uint64_t next_random(std::uint64_t &state) noexcept
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

[[nodiscard]] pqc_poly::signed_poly random_poly(std::uint64_t &state) noexcept
{
    pqc_poly::signed_poly r{};

    for (std::int16_t &v : r)
    {
        v = static_cast<std::int16_t>(next_random(state) & 0x7ffu) - 1024;
    }

    return r;
}

[[nodiscard]] bool check_formula(std::string_view label, pqc_poly::formula_kind kind,
                                 const pqc_poly::signed_poly &a, const pqc_poly::signed_poly &b,
                                 const pqc_poly::poly &expected)
{
    pqc_poly::poly actual{};

    pqc_poly::multiply(kind, actual, a, b);

    for (std::size_t i = 0; i < pqc_poly::poly_n; ++i)
    {
        if (actual[i] != expected[i])
        {
            std::cerr << label << ": " << pqc_poly::formula_name(kind) << " differs at coefficient "
                      << i << " (wanted " << expected[i] << ", got " << actual[i] << ")\n";
            return false;
        }

        if (actual[i] >= pqc_poly::poly_q)
        {
            std::cerr << label << ": " << pqc_poly::formula_name(kind)
                      << " returned a noncanonical coefficient\n";
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool check_case(std::string_view label, const pqc_poly::signed_poly &a,
                              const pqc_poly::signed_poly &b)
{
    pqc_poly::poly expected{};

    pqc_poly::reference_multiply(expected, a, b);

    for (pqc_poly::formula_kind kind : pqc_poly::formula_kinds)
    {
        if (!check_formula(label, kind, a, b, expected))
        {
            return false;
        }
    }

    return true;
}

}

int main()
{
    pqc_poly::signed_poly dense_a{};
    pqc_poly::signed_poly dense_b{};

    for (std::size_t i = 0; i < pqc_poly::poly_n; ++i)
    {
        dense_a[i] = static_cast<std::int16_t>((37 * i + 55) & 0x7ffu) - 1024;
        dense_b[i] = static_cast<std::int16_t>((73 * i + 95) & 0x7ffu) - 1024;
    }

    if (!check_case("dense", dense_a, dense_b))
    {
        return 1;
    }

    pqc_poly::signed_poly wrap_a{};
    pqc_poly::signed_poly wrap_b{};

    wrap_a[0] = -713;
    wrap_a[pqc_poly::poly_n - 1] = 1023;
    wrap_b[0] = 997;
    wrap_b[1] = -881;
    wrap_b[pqc_poly::poly_n - 1] = 619;

    if (!check_case("cyclic wrap", wrap_a, wrap_b))
    {
        return 1;
    }

    if (!check_case("shared input", dense_a, dense_a))
    {
        return 1;
    }

    std::uint64_t state = 0x1c69b3f74ac4ae35ull;

    for (std::size_t round = 0; round < 16; ++round)
    {
        const pqc_poly::signed_poly a = random_poly(state);
        const pqc_poly::signed_poly b = random_poly(state);

        if (!check_case("random", a, b))
        {
            return 1;
        }
    }

    return 0;
}
