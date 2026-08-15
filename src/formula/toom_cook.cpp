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

inline constexpr std::size_t block_n = 170;
inline constexpr std::size_t block_storage_n = 176;
inline constexpr std::size_t block_product_n = 2 * block_n - 1;
inline constexpr std::size_t block_product_storage_n = 344;
inline constexpr std::size_t toom_linear_n = 1024;

// padded lane sizes keep every large workspace member aligned to a cache line
using block = std::array<std::int64_t, block_storage_n>;
using block_product = std::array<std::int64_t, block_product_storage_n>;

struct alignas(64) toom_workspace
{
    alignas(64) std::array<block, 3> a_parts{};
    alignas(64) std::array<block, 3> b_parts{};
    alignas(64) block a_evaluation{};
    alignas(64) block b_evaluation{};
    alignas(64) std::array<block_product, 5> products{};
    alignas(64) std::array<std::int64_t, toom_linear_n> linear{};
};

inline void split_polynomial(std::array<block, 3> &parts, const signed_poly &a) noexcept
{
    for (std::size_t i = 0; i < poly_n; ++i)
    {
        parts[i / block_n][i % block_n] = a[i];
    }
}

inline void evaluate(block &r, const std::array<block, 3> &parts, std::int64_t point) noexcept
{
    const std::int64_t point_squared = point * point;

    for (std::size_t i = 0; i < block_n; ++i)
    {
        r[i] = parts[0][i] + point * parts[1][i] + point_squared * parts[2][i];
    }
}

inline void multiply_blocks(block_product &r, const block &PQC_POLY_RESTRICT a,
                            const block &PQC_POLY_RESTRICT b) noexcept
{
    r.fill(0);

    // the inner loop walks contiguous output and input lanes for straightforward vectorization
    for (std::size_t i = 0; i < block_n; ++i)
    {
        const std::int64_t av = a[i];

        for (std::size_t j = 0; j < block_n; ++j)
        {
            r[i + j] += av * b[j];
        }
    }
}

inline void evaluated_product(block_product &r, toom_workspace &workspace,
                              std::int64_t point) noexcept
{
    evaluate(workspace.a_evaluation, workspace.a_parts, point);
    evaluate(workspace.b_evaluation, workspace.b_parts, point);
    multiply_blocks(r, workspace.a_evaluation, workspace.b_evaluation);
}

}

void toom_cook(poly &r, const signed_poly &a, const signed_poly &b) noexcept
{
    // one fixed workspace replaces every temporary allocation from evaluation through folding
    toom_workspace workspace{};

    split_polynomial(workspace.a_parts, a);
    split_polynomial(workspace.b_parts, b);

    multiply_blocks(workspace.products[0], workspace.a_parts[0], workspace.b_parts[0]);
    evaluated_product(workspace.products[1], workspace, 1);
    evaluated_product(workspace.products[2], workspace, -1);
    evaluated_product(workspace.products[3], workspace, 2);
    multiply_blocks(workspace.products[4], workspace.a_parts[2], workspace.b_parts[2]);

    // five point products are enough to recover the five coefficient blocks
    // interpolation stays exact in int64 before the final power-of-two reduction
    for (std::size_t i = 0; i < block_product_n; ++i)
    {
        const std::int64_t c0 = workspace.products[0][i];
        const std::int64_t c4 = workspace.products[4][i];
        const std::int64_t even_sum = workspace.products[1][i] + workspace.products[2][i];
        const std::int64_t odd_sum = (workspace.products[1][i] - workspace.products[2][i]) / 2;
        const std::int64_t c2 = even_sum / 2 - c0 - c4;
        const std::int64_t weighted_odd = (workspace.products[3][i] - c0 - 4 * c2 - 16 * c4) / 2;
        const std::int64_t c3 = (weighted_odd - odd_sum) / 3;
        const std::int64_t c1 = odd_sum - c3;

        workspace.linear[i] += c0;
        workspace.linear[block_n + i] += c1;
        workspace.linear[2 * block_n + i] += c2;
        workspace.linear[3 * block_n + i] += c3;
        workspace.linear[4 * block_n + i] += c4;
    }

    detail::fold_linear(r, workspace.linear);
}

}

#undef PQC_POLY_RESTRICT
