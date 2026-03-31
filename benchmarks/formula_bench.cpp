#include "pqc_poly/formula.hpp"
#include "pqc_poly/ring.hpp"

#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{

std::uint64_t next_random(std::uint64_t &state) noexcept
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

void make_input(pqc_poly::signed_poly &a, pqc_poly::signed_poly &b) noexcept
{
    std::uint64_t state = 0x6a09e667f3bcc909ULL;

    for (std::size_t i = 0; i < pqc_poly::poly_n; ++i)
    {
        a[i] = static_cast<std::int16_t>(next_random(state) & 0x7ffU) - 1024;
        b[i] = static_cast<std::int16_t>(next_random(state) & 0x7ffU) - 1024;
    }
}

std::size_t parse_iterations(int argc, char **argv)
{
    if (argc == 1)
    {
        return 250;
    }
    if (argc != 2)
    {
        std::cerr << "usage: pqc-poly-formula-bench [iterations]\n";
        std::exit(2);
    }

    std::size_t iterations = 0;
    const std::string_view text(argv[1]);
    const auto result = std::from_chars(text.data(), text.data() + text.size(), iterations);

    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || iterations == 0)
    {
        std::cerr << "iterations must be a positive integer\n";
        std::exit(2);
    }

    return iterations;
}

void keep_result(const pqc_poly::poly &r) noexcept
{
    std::atomic_signal_fence(std::memory_order_seq_cst);
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "r,m"(r.data()) : "memory");
#endif
}

void run_one(pqc_poly::formula_kind kind, const pqc_poly::signed_poly &a,
             const pqc_poly::signed_poly &b, const pqc_poly::poly &expected, std::size_t iterations)
{
    pqc_poly::poly r{};

    pqc_poly::multiply(kind, r, a, b);
    if (r != expected)
    {
        std::cerr << pqc_poly::formula_name(kind) << " failed validation\n";
        std::exit(1);
    }

    for (std::size_t i = 0; i < 8; ++i)
    {
        pqc_poly::multiply(kind, r, a, b);
        keep_result(r);
    }

    const auto begin = std::chrono::steady_clock::now();
    std::uint64_t checksum = 0;

    for (std::size_t i = 0; i < iterations; ++i)
    {
        pqc_poly::multiply(kind, r, a, b);
        keep_result(r);
        checksum += r[i % pqc_poly::poly_n];
    }

    const auto end = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
    const double nanoseconds = static_cast<double>(elapsed) / static_cast<double>(iterations);

    std::cout << pqc_poly::formula_name(kind) << ' ' << nanoseconds
              << " ns/op checksum=" << checksum << '\n';
}

}

int main(int argc, char **argv)
{
    const std::size_t iterations = parse_iterations(argc, argv);
    pqc_poly::signed_poly a{};
    pqc_poly::signed_poly b{};
    pqc_poly::poly expected{};

    make_input(a, b);
    pqc_poly::reference_multiply(expected, a, b);

    for (pqc_poly::formula_kind kind : pqc_poly::formula_kinds)
    {
        run_one(kind, a, b, expected, iterations);
    }

    return 0;
}
