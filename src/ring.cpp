#include "pqc_poly/ring.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pqc_poly
{

void reference_multiply(poly &r, const signed_poly &a, const signed_poly &b) noexcept
{
    // this oracle keeps the full signed sum so optimized kernels never validate themselves
    alignas(64) std::array<std::int64_t, poly_n> t{};

    for (std::size_t i = 0; i < poly_n; ++i)
    {
        for (std::size_t j = 0; j < poly_n; ++j)
        {
            std::size_t k = i + j;

            if (k >= poly_n)
            {
                k -= poly_n;
            }

            t[k] += static_cast<std::int64_t>(a[i]) * static_cast<std::int64_t>(b[j]);
        }
    }

    for (std::size_t i = 0; i < poly_n; ++i)
    {
        std::int64_t v = t[i] % static_cast<std::int64_t>(poly_q);

        v += static_cast<std::int64_t>(v < 0) * poly_q;
        r[i] = static_cast<std::uint16_t>(v);
    }
}

}
