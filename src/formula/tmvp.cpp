#include "pqc_poly/formula.hpp"

#include "common.hpp"

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

inline constexpr std::size_t tmvp_leaf_n = 16;

template <std::size_t size>
[[nodiscard]] consteval std::size_t workspace_size() noexcept
{
    if constexpr (size <= tmvp_leaf_n)
    {
        return 0;
    }
    else
    {
        return 4 * size - 2 + workspace_size<size / 2>();
    }
}

template <std::size_t size>
inline void multiply_direct(std::uint16_t *PQC_POLY_RESTRICT r,
                            const std::uint16_t *PQC_POLY_RESTRICT diagonals,
                            const std::uint16_t *PQC_POLY_RESTRICT vector) noexcept
{
    // a leaf has at most sixteen products, so one uint32_t sum cannot overflow
    for (std::size_t row = 0; row < size; ++row)
    {
        std::uint32_t sum = 0;

        for (std::size_t column = 0; column < size; ++column)
        {
            sum += static_cast<std::uint32_t>(diagonals[size - 1 + row - column]) * vector[column];
        }

        r[row] = detail::reduce_q(sum);
    }
}

template <std::size_t size>
inline void multiply_recursive(std::uint16_t *PQC_POLY_RESTRICT r,
                               const std::uint16_t *PQC_POLY_RESTRICT diagonals,
                               const std::uint16_t *PQC_POLY_RESTRICT vector,
                               std::uint16_t *PQC_POLY_RESTRICT workspace) noexcept
{
    if constexpr (size <= tmvp_leaf_n)
    {
        multiply_direct<size>(r, diagonals, vector);
    }
    else
    {
        constexpr std::size_t half = size / 2;
        constexpr std::size_t child_diagonal_n = size - 1;

        // each frame packs its sums, diagonal differences, and three child results together
        std::uint16_t *const vector_sum = workspace;
        std::uint16_t *const upper_difference = vector_sum + half;
        std::uint16_t *const lower_difference = upper_difference + child_diagonal_n;
        std::uint16_t *const shared_product = lower_difference + child_diagonal_n;
        std::uint16_t *const upper_product = shared_product + half;
        std::uint16_t *const lower_product = upper_product + half;
        std::uint16_t *const child_workspace = lower_product + half;
        const std::uint16_t *const middle_diagonals = diagonals + half;

        for (std::size_t i = 0; i < half; ++i)
        {
            vector_sum[i] =
                detail::reduce_q(static_cast<std::uint32_t>(vector[i]) + vector[half + i]);
        }

        for (std::size_t i = 0; i < child_diagonal_n; ++i)
        {
            // reducing differences here bounds every deeper recursion to eleven bits
            upper_difference[i] =
                detail::reduce_q(static_cast<std::int64_t>(diagonals[i]) - middle_diagonals[i]);
            lower_difference[i] = detail::reduce_q(static_cast<std::int64_t>(middle_diagonals[i]) -
                                                   diagonals[size + i]);
        }

        // the three child calls share scratch because each result is complete before the next
        multiply_recursive<half>(shared_product, middle_diagonals, vector_sum, child_workspace);
        multiply_recursive<half>(upper_product, upper_difference, vector + half, child_workspace);
        multiply_recursive<half>(lower_product, lower_difference, vector, child_workspace);

        for (std::size_t i = 0; i < half; ++i)
        {
            r[i] =
                detail::reduce_q(static_cast<std::uint32_t>(shared_product[i]) + upper_product[i]);
            r[half + i] =
                detail::reduce_q(static_cast<std::int64_t>(shared_product[i]) - lower_product[i]);
        }
    }
}

}

void tmvp(poly &r, const signed_poly &a, const signed_poly &b) noexcept
{
    // all recursion storage is fixed and aligned so the hot path never reaches the heap
    alignas(64) std::array<std::uint16_t, 2 * detail::padded_n - 1> diagonals{};
    alignas(64) std::array<std::uint16_t, detail::padded_n> vector{};
    alignas(64) std::array<std::uint16_t, detail::padded_n> product{};
    alignas(64) std::array<std::uint16_t, workspace_size<detail::padded_n>()> workspace{};

    // the centered diagonal window encodes the x^509 equals one wrap directly
    for (std::ptrdiff_t diagonal = -static_cast<std::ptrdiff_t>(poly_n - 1);
         diagonal <= static_cast<std::ptrdiff_t>(poly_n - 1); ++diagonal)
    {
        const std::size_t source =
            diagonal < 0 ? static_cast<std::size_t>(static_cast<std::ptrdiff_t>(poly_n) + diagonal)
                         : static_cast<std::size_t>(diagonal);
        const std::size_t index =
            static_cast<std::size_t>(static_cast<std::ptrdiff_t>(detail::padded_n - 1) + diagonal);

        diagonals[index] = detail::normalize(a[source]);
    }

    detail::normalize_pad(vector, b);

    // the padded toeplitz form turns cyclic multiplication into one matrix product
    multiply_recursive<detail::padded_n>(product.data(), diagonals.data(), vector.data(),
                                         workspace.data());

    for (std::size_t i = 0; i < poly_n; ++i)
    {
        r[i] = product[i];
    }
}

}

#undef PQC_POLY_RESTRICT
