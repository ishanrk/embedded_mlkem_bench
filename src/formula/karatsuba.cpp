#include "pqc_poly/formula.hpp"

#include "common.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#if defined(_MSC_VER)
#define PQC_POLY_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#define PQC_POLY_RESTRICT __restrict__
#else
#define PQC_POLY_RESTRICT
#endif

namespace pqc_poly
{

namespace
{

inline constexpr std::size_t karatsuba_leaf_n = 32;

template <std::size_t size>
[[nodiscard]] consteval std::size_t workspace_size() noexcept
{
    if constexpr (size <= karatsuba_leaf_n)
    {
        return 0;
    }
    else
    {
        return 2 * size - 1 + workspace_size<size / 2>();
    }
}

template <std::size_t size>
inline void multiply_leaf(std::uint16_t *PQC_POLY_RESTRICT r,
                          const std::uint16_t *PQC_POLY_RESTRICT a,
                          const std::uint16_t *PQC_POLY_RESTRICT b) noexcept
{
    std::fill_n(r, 2 * size - 1, std::uint16_t{0});

    // q divides 2^16, so lane wrap preserves the residue without an inner reduction
    for (std::size_t i = 0; i < size; ++i)
    {
        const std::uint32_t av = a[i];

        for (std::size_t j = 0; j < size; ++j)
        {
            const std::uint32_t v = r[i + j] + av * b[j];

            r[i + j] = static_cast<std::uint16_t>(v);
        }
    }
}

template <std::size_t size>
inline void multiply_recursive(std::uint16_t *PQC_POLY_RESTRICT r,
                               const std::uint16_t *PQC_POLY_RESTRICT a,
                               const std::uint16_t *PQC_POLY_RESTRICT b,
                               std::uint16_t *PQC_POLY_RESTRICT workspace) noexcept
{
    if constexpr (size <= karatsuba_leaf_n)
    {
        multiply_leaf<size>(r, a, b);
    }
    else
    {
        constexpr std::size_t half = size / 2;
        constexpr std::size_t half_product_n = size - 1;

        std::fill_n(r, 2 * size - 1, std::uint16_t{0});

        // low and high products land directly in their final nonoverlapping ranges
        multiply_recursive<half>(r, a, b, workspace);
        multiply_recursive<half>(r + size, a + half, b + half, workspace);

        // sums and the middle product occupy one compact frame ahead of child scratch
        std::uint16_t *const a_sum = workspace;
        std::uint16_t *const b_sum = a_sum + half;
        std::uint16_t *const middle = b_sum + half;
        std::uint16_t *const child_workspace = middle + half_product_n;

        for (std::size_t i = 0; i < half; ++i)
        {
            a_sum[i] = detail::reduce_q(static_cast<std::uint32_t>(a[i]) + a[i + half]);
            b_sum[i] = detail::reduce_q(static_cast<std::uint32_t>(b[i]) + b[i + half]);
        }

        multiply_recursive<half>(middle, a_sum, b_sum, child_workspace);

        // subtracting both endpoint products leaves the two cross terms
        for (std::size_t i = 0; i < half_product_n; ++i)
        {
            middle[i] = detail::reduce_q(static_cast<std::int64_t>(middle[i]) - r[i] - r[size + i]);
        }

        for (std::size_t i = 0; i < half_product_n; ++i)
        {
            r[half + i] = detail::reduce_q(static_cast<std::uint32_t>(r[half + i]) + middle[i]);
        }
    }
}

}

void karatsuba(poly &r, const signed_poly &a, const signed_poly &b) noexcept
{
    // fixed aligned storage keeps allocation and allocator state out of the hot path
    alignas(64) std::array<std::uint16_t, detail::padded_n> a_padded{};
    alignas(64) std::array<std::uint16_t, detail::padded_n> b_padded{};
    alignas(64) std::array<std::uint16_t, 2 * detail::padded_n - 1> product{};
    alignas(64) std::array<std::uint16_t, workspace_size<detail::padded_n>()> workspace{};

    detail::normalize_pad(a_padded, a);
    detail::normalize_pad(b_padded, b);

    // one workspace is reused by every completed branch of the recursion
    multiply_recursive<detail::padded_n>(product.data(), a_padded.data(), b_padded.data(),
                                         workspace.data());

    detail::fold_linear(r, product);
}

}

#undef PQC_POLY_RESTRICT
