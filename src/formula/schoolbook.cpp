#include "pqc_poly/formula.hpp"

#include "common.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace pqc_poly
{

namespace
{

#if !defined(__AVX2__)
inline void schoolbook_portable(std::array<std::uint16_t, detail::linear_n> &t,
                                const signed_poly &a, const signed_poly &b) noexcept
{
    // uint16_t wrap is exact here because q divides the lane modulus 2^16
    for (std::size_t i = 0; i < poly_n; ++i)
    {
        const std::uint32_t av = static_cast<std::uint16_t>(a[i]);

        for (std::size_t j = 0; j < poly_n; ++j)
        {
            const std::uint32_t v = t[i + j] + av * static_cast<std::uint16_t>(b[j]);

            t[i + j] = static_cast<std::uint16_t>(v);
        }
    }
}
#endif

#if defined(__AVX2__)
inline void schoolbook_avx2(std::array<std::uint16_t, detail::linear_n> &t, const signed_poly &a,
                            const signed_poly &b) noexcept
{
    for (std::size_t i = 0; i < poly_n; ++i)
    {
        const __m256i av = _mm256_set1_epi16(a[i]);
        std::size_t j = 0;

        for (; j + 16 <= poly_n; j += 16)
        {
            const __m256i bv = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(b.data() + j));
            const __m256i tv =
                _mm256_loadu_si256(reinterpret_cast<const __m256i *>(t.data() + i + j));
            const __m256i pv = _mm256_mullo_epi16(av, bv);

            _mm256_storeu_si256(reinterpret_cast<__m256i *>(t.data() + i + j),
                                _mm256_add_epi16(tv, pv));
        }

        for (; j < poly_n; ++j)
        {
            const std::int32_t v =
                static_cast<std::int32_t>(t[i + j]) +
                static_cast<std::int32_t>(a[i]) * static_cast<std::int32_t>(b[j]);

            t[i + j] = static_cast<std::uint16_t>(v);
        }
    }
}
#endif

}

void schoolbook(poly &r, const signed_poly &a, const signed_poly &b) noexcept
{
    alignas(64) std::array<std::uint16_t, detail::linear_n> t{};

#if defined(__AVX2__)
    // the modulus divides 2^16, so wrapped vector lanes retain every bit needed modulo q
    schoolbook_avx2(t, a, b);
#else
    schoolbook_portable(t, a, b);
#endif

    detail::fold_linear(r, t);
}

}
