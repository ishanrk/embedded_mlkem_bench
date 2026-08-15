#include "pqc_poly/formula.hpp"

#include <cstdlib>
#include <string_view>

namespace pqc_poly
{

std::string_view formula_name(formula_kind kind) noexcept
{
    switch (kind)
    {
        case formula_kind::schoolbook:
            return "schoolbook";
        case formula_kind::karatsuba:
            return "karatsuba";
        case formula_kind::ntt:
            return "ntt";
        case formula_kind::toom_cook:
            return "toom_cook";
        case formula_kind::tmvp:
            return "tmvp";
    }

    std::abort();
}

void multiply(formula_kind kind, poly &r, const signed_poly &a, const signed_poly &b) noexcept
{
    switch (kind)
    {
        case formula_kind::schoolbook:
            schoolbook(r, a, b);
            return;
        case formula_kind::karatsuba:
            karatsuba(r, a, b);
            return;
        case formula_kind::ntt:
            ntt(r, a, b);
            return;
        case formula_kind::toom_cook:
            toom_cook(r, a, b);
            return;
        case formula_kind::tmvp:
            tmvp(r, a, b);
            return;
    }

    std::abort();
}

}
