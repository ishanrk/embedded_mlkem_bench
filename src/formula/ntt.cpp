#include "pqc_poly/formula.hpp"

#include "common.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace pqc_poly
{

namespace
{

inline constexpr std::size_t ntt_n = 1024;
inline constexpr std::uint32_t prime_0 = 40961;
inline constexpr std::uint32_t prime_1 = 65537;
inline constexpr std::uint32_t crt_inverse = 43694;
inline constexpr std::uint32_t primitive_root = 3;
inline constexpr std::size_t twiddle_n = ntt_n - 1;

// the prime product covers the largest canonical coefficient before reduction by q

[[nodiscard]] constexpr std::uint32_t reverse_bits(std::uint32_t v) noexcept
{
    std::uint32_t r = 0;

    for (std::size_t i = 0; i < 10; ++i)
    {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }

    return r;
}

[[nodiscard]] consteval auto make_bit_reverse() noexcept
{
    std::array<std::uint16_t, ntt_n> table{};

    for (std::size_t i = 0; i < ntt_n; ++i)
    {
        table[i] = static_cast<std::uint16_t>(reverse_bits(static_cast<std::uint32_t>(i)));
    }

    return table;
}

template <std::uint32_t modulus>
[[nodiscard]] constexpr std::uint32_t power_mod(std::uint32_t base, std::uint32_t exponent) noexcept
{
    std::uint64_t result = 1;
    std::uint64_t factor = base;

    while (exponent != 0)
    {
        if ((exponent & 1u) != 0)
        {
            result = result * factor % modulus;
        }

        factor = factor * factor % modulus;
        exponent >>= 1;
    }

    return static_cast<std::uint32_t>(result);
}

template <std::uint32_t modulus, bool inverse>
[[nodiscard]] consteval auto make_twiddles() noexcept
{
    std::array<std::uint32_t, twiddle_n> table{};

    for (std::size_t length = 2; length <= ntt_n; length *= 2)
    {
        std::uint32_t root =
            power_mod<modulus>(primitive_root, static_cast<std::uint32_t>((modulus - 1) / length));

        if constexpr (inverse)
        {
            root = power_mod<modulus>(root, modulus - 2);
        }

        const std::size_t half = length / 2;
        const std::size_t offset = half - 1;
        std::uint64_t factor = 1;

        // a stage stores only its unique half because every group reuses the same factors
        for (std::size_t i = 0; i < half; ++i)
        {
            table[offset + i] = static_cast<std::uint32_t>(factor);
            factor = factor * root % modulus;
        }
    }

    return table;
}

inline constexpr auto bit_reverse = make_bit_reverse();
inline constexpr auto prime_0_forward = make_twiddles<prime_0, false>();
inline constexpr auto prime_0_inverse = make_twiddles<prime_0, true>();
inline constexpr auto prime_1_forward = make_twiddles<prime_1, false>();
inline constexpr auto prime_1_inverse = make_twiddles<prime_1, true>();
inline constexpr std::uint32_t prime_0_scale = power_mod<prime_0>(ntt_n, prime_0 - 2);
inline constexpr std::uint32_t prime_1_scale = power_mod<prime_1>(ntt_n, prime_1 - 2);

template <std::uint32_t modulus>
[[nodiscard]] inline std::uint32_t multiply_mod(std::uint32_t a, std::uint32_t b) noexcept
{
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(a) * b % modulus);
}

template <std::uint32_t modulus>
inline void butterflies(std::array<std::uint32_t, ntt_n> &v,
                        const std::array<std::uint32_t, twiddle_n> &twiddles) noexcept
{
    // all values remain below the modulus between butterflies
    for (std::size_t length = 2; length <= ntt_n; length *= 2)
    {
        const std::size_t half = length / 2;
        const std::size_t offset = half - 1;

        for (std::size_t start = 0; start < ntt_n; start += length)
        {
            for (std::size_t i = 0; i < half; ++i)
            {
                const std::uint32_t low = v[start + i];
                const std::uint32_t high =
                    multiply_mod<modulus>(v[start + half + i], twiddles[offset + i]);
                const std::uint32_t sum = low + high;

                v[start + i] = sum >= modulus ? sum - modulus : sum;
                v[start + half + i] = low >= high ? low - high : low + modulus - high;
            }
        }
    }
}

inline void bit_reverse_permute(std::array<std::uint32_t, ntt_n> &v) noexcept
{
    for (std::size_t i = 1; i < ntt_n; ++i)
    {
        const std::size_t j = bit_reverse[i];

        if (i < j)
        {
            std::swap(v[i], v[j]);
        }
    }
}

template <std::uint32_t modulus>
inline void load_forward(std::array<std::uint32_t, ntt_n> &v, const signed_poly &a,
                         const std::array<std::uint32_t, twiddle_n> &twiddles) noexcept
{
    v.fill(0);

    // loading in bit-reversed order removes a permutation from each input
    for (std::size_t i = 0; i < poly_n; ++i)
    {
        v[bit_reverse[i]] = detail::normalize(a[i]);
    }

    butterflies<modulus>(v, twiddles);
}

template <std::uint32_t modulus>
inline void convolve(std::array<std::uint32_t, ntt_n> &a_values,
                     std::array<std::uint32_t, ntt_n> &b_values, const signed_poly &a,
                     const signed_poly &b, const std::array<std::uint32_t, twiddle_n> &forward,
                     const std::array<std::uint32_t, twiddle_n> &inverse,
                     std::uint32_t scale) noexcept
{
    load_forward<modulus>(a_values, a, forward);
    load_forward<modulus>(b_values, b, forward);

    for (std::size_t i = 0; i < ntt_n; ++i)
    {
        a_values[i] = multiply_mod<modulus>(a_values[i], b_values[i]);
    }

    // the inverse uses the same iterative kernel after one fixed permutation
    bit_reverse_permute(a_values);
    butterflies<modulus>(a_values, inverse);

    for (std::size_t i = 0; i < detail::linear_n; ++i)
    {
        a_values[i] = multiply_mod<modulus>(a_values[i], scale);
    }
}

[[nodiscard]] inline std::uint16_t combine_residues(std::uint32_t first,
                                                    std::uint32_t second) noexcept
{
    const std::uint32_t difference = (second + prime_1 - first % prime_1) % prime_1;
    const std::uint32_t lift = multiply_mod<prime_1>(difference, crt_inverse);
    const std::uint64_t value = first + static_cast<std::uint64_t>(prime_0) * lift;

    return static_cast<std::uint16_t>(value & detail::q_mask);
}

}

void ntt(poly &r, const signed_poly &a, const signed_poly &b) noexcept
{
    // the second prime reuses both transform buffers while the first result stays compact
    alignas(64) std::array<std::uint32_t, ntt_n> a_values{};
    alignas(64) std::array<std::uint32_t, ntt_n> b_values{};
    alignas(64) std::array<std::uint32_t, detail::linear_n> first_product{};

    // static twiddles keep root setup outside the measured path
    convolve<prime_0>(a_values, b_values, a, b, prime_0_forward, prime_0_inverse, prime_0_scale);

    for (std::size_t i = 0; i < detail::linear_n; ++i)
    {
        first_product[i] = a_values[i];
    }

    convolve<prime_1>(a_values, b_values, a, b, prime_1_forward, prime_1_inverse, prime_1_scale);

    // crt reconstruction and cyclic folding share one output pass
    for (std::size_t i = 0; i + poly_n < detail::linear_n; ++i)
    {
        const std::uint32_t low = combine_residues(first_product[i], a_values[i]);
        const std::uint32_t high =
            combine_residues(first_product[i + poly_n], a_values[i + poly_n]);

        r[i] = detail::reduce_q(low + high);
    }

    r[poly_n - 1] = combine_residues(first_product[poly_n - 1], a_values[poly_n - 1]);
}

}
