#include "pqc_poly/codegen.hpp"

#include <sstream>
#include <string_view>
#include <vector>

namespace pqc_poly
{
namespace
{

enum class wrap_sign
{
    add,
    subtract,
};

[[nodiscard]] wrap_sign ring_wrap_sign(operation op) noexcept
{
    if (op == operation::cyclic_mul)
    {
        return wrap_sign::add;
    }

    return wrap_sign::subtract;
}

void validate_trial(const request &req, const candidate_trial &trial)
{
    // emission trusts the independent checker, not the search path that built the trial
    const std::vector<std::string> errors = check_trial(req, trial);

    if (!errors.empty())
    {
        std::ostringstream message;
        message << "bad plan: ";

        for (std::size_t i = 0; i < errors.size(); ++i)
        {
            if (i != 0)
            {
                message << ", ";
            }
            message << errors[i];
        }

        throw codegen_error(message.str());
    }

    if (!trial.analysis.legal)
    {
        throw codegen_error("cannot emit an illegal plan");
    }
}

[[nodiscard]] std::string generated_arguments(aliasing alias)
{
    // only r is restricted because a and b are always allowed to overlap each other
    if (alias == aliasing::no)
    {
        return "std::int32_t *PQC_POLY_RESTRICT r, "
               "const std::int32_t *a, const std::int32_t *b";
    }

    return "std::int32_t *r, const std::int32_t *a, const std::int32_t *b";
}

[[nodiscard]] std::string full_body(operation op)
{
    const std::string_view fold_operator = ring_wrap_sign(op) == wrap_sign::add ? "+=" : "-=";
    std::ostringstream out;

    out << R"pqc(    // the full convolution keeps the multiply loop branch free
    // aligned scratch gives vector loads a stable boundary without changing the api
    alignas(64) acc_t t[2 * pqc_poly_n - 1]{};

    for (std::size_t i = 0; i < pqc_poly_n; ++i)
    {
        const acc_t ai = static_cast<acc_t>(a[i]);

        // each j writes a distinct lane, so this loop is safe to vectorize
        PQC_POLY_VECTOR_LOOP
        for (std::size_t j = 0; j < pqc_poly_n; ++j)
        {
            t[i + j] += ai * static_cast<acc_t>(b[j]);
        }
    }

    // the ring sign is applied once after the hot convolution loop
    for (std::size_t i = 0; i + 1 < pqc_poly_n; ++i)
    {
        t[i] )pqc"
        << fold_operator << R"pqc( t[i + pqc_poly_n];
    }

    PQC_POLY_VECTOR_LOOP
    for (std::size_t i = 0; i < pqc_poly_n; ++i)
    {
        r[i] = reduce_mod_q(t[i]);
    }
)pqc";

    return out.str();
}

[[nodiscard]] std::string fold_body(operation op, std::uint64_t block)
{
    const std::string_view fold_operator = ring_wrap_sign(op) == wrap_sign::add ? "+=" : "-=";
    std::ostringstream out;

    out << R"pqc(    // split wrap and non-wrap ranges so the inner loops stay predictable
    // one aligned ring-sized buffer is the complete explicit scratch allocation
    alignas(64) acc_t t[pqc_poly_n]{};

    for (std::size_t ii = 0; ii < pqc_poly_n;)
    {
        const std::size_t ie = pqc_poly_n - ii < )pqc"
        << block << " ? pqc_poly_n : ii + " << block << R"pqc(;

        for (std::size_t jj = 0; jj < pqc_poly_n;)
        {
            const std::size_t je = pqc_poly_n - jj < )pqc"
        << block << " ? pqc_poly_n : jj + " << block << R"pqc(;

            for (std::size_t i = ii; i < ie; ++i)
            {
                const acc_t ai = static_cast<acc_t>(a[i]);
                const std::size_t split = pqc_poly_n - i;
                const std::size_t direct_end = je < split ? je : split;
                const std::size_t wrap_start = jj > split ? jj : split;

                // direct and wrapped lanes are disjoint within each loop
                PQC_POLY_VECTOR_LOOP
                for (std::size_t j = jj; j < direct_end; ++j)
                {
                    t[i + j] += ai * static_cast<acc_t>(b[j]);
                }

                PQC_POLY_VECTOR_LOOP
                for (std::size_t j = wrap_start; j < je; ++j)
                {
                    t[j - split] )pqc"
        << fold_operator << R"pqc( ai * static_cast<acc_t>(b[j]);
                }
            }

            jj = je;
        }

        ii = ie;
    }

    PQC_POLY_VECTOR_LOOP
    for (std::size_t i = 0; i < pqc_poly_n; ++i)
    {
        r[i] = reduce_mod_q(t[i]);
    }
)pqc";

    return out.str();
}

[[nodiscard]] std::string output_body(operation op)
{
    const std::string_view fold_operator = ring_wrap_sign(op) == wrap_sign::add ? "+=" : "-=";
    std::ostringstream out;

    out << R"pqc(    // four independent lanes hide integer multiply latency without scratch
    // r is disjoint here because later outputs still read every input coefficient
    for (std::size_t k = 0; k < pqc_poly_n; ++k)
    {
        acc_t s0 = 0;
        acc_t s1 = 0;
        acc_t s2 = 0;
        acc_t s3 = 0;
        std::size_t i = 0;

        for (; i + 3 <= k; i += 4)
        {
            s0 += static_cast<acc_t>(a[i]) * static_cast<acc_t>(b[k - i]);
            s1 += static_cast<acc_t>(a[i + 1]) * static_cast<acc_t>(b[k - i - 1]);
            s2 += static_cast<acc_t>(a[i + 2]) * static_cast<acc_t>(b[k - i - 2]);
            s3 += static_cast<acc_t>(a[i + 3]) * static_cast<acc_t>(b[k - i - 3]);
        }
        for (; i <= k; ++i)
        {
            s0 += static_cast<acc_t>(a[i]) * static_cast<acc_t>(b[k - i]);
        }

        i = k + 1;
        for (; i + 3 < pqc_poly_n; i += 4)
        {
            s0 )pqc"
        << fold_operator
        << R"pqc( static_cast<acc_t>(a[i]) * static_cast<acc_t>(b[pqc_poly_n - (i - k)]);
            s1 )pqc"
        << fold_operator
        << R"pqc( static_cast<acc_t>(a[i + 1]) * static_cast<acc_t>(b[pqc_poly_n - (i + 1 - k)]);
            s2 )pqc"
        << fold_operator
        << R"pqc( static_cast<acc_t>(a[i + 2]) * static_cast<acc_t>(b[pqc_poly_n - (i + 2 - k)]);
            s3 )pqc"
        << fold_operator
        << R"pqc( static_cast<acc_t>(a[i + 3]) * static_cast<acc_t>(b[pqc_poly_n - (i + 3 - k)]);
        }
        for (; i < pqc_poly_n; ++i)
        {
            s0 )pqc"
        << fold_operator
        << R"pqc( static_cast<acc_t>(a[i]) * static_cast<acc_t>(b[pqc_poly_n - (i - k)]);
        }

        r[k] = reduce_mod_q((s0 + s1) + (s2 + s3));
    }
)pqc";

    return out.str();
}

[[nodiscard]] std::string reduction_body(std::uint32_t q)
{
    // a generated mask avoids even a constant division for binary moduli
    const bool power_of_two = q != 0 && (q & (q - 1)) == 0;

    if (power_of_two)
    {
        std::ostringstream out;

        out << R"pqc(PQC_POLY_FORCE_INLINE std::int32_t reduce_mod_q(acc_t x) noexcept
{
    // a power-of-two modulus reduces to one mask, including negative inputs
    using unsigned_acc_t = std::make_unsigned_t<acc_t>;
    constexpr unsigned_acc_t mask = static_cast<unsigned_acc_t>()pqc"
            << q - 1 << R"pqc();
    return static_cast<std::int32_t>(static_cast<unsigned_acc_t>(x) & mask);
}
)pqc";

        return out.str();
    }

    return R"pqc(PQC_POLY_FORCE_INLINE std::int32_t reduce_mod_q(acc_t x) noexcept
{
    // the compile-time modulus lets the compiler replace division when useful
    acc_t y = x % static_cast<acc_t>(pqc_poly_q);
    y += static_cast<acc_t>(y < 0) * static_cast<acc_t>(pqc_poly_q);
    return static_cast<std::int32_t>(y);
}
)pqc";
}

[[nodiscard]] std::string generated_body(const request &req, const analysis_verdict &verdict)
{
    // the checked schedule controls code shape; no runtime schedule branch is emitted
    switch (verdict.plan.sched)
    {
        case schedule::full:
            return full_body(req.op);
        case schedule::fold:
            return fold_body(req.op, verdict.plan.block);
        case schedule::output:
            return output_body(req.op);
    }

    throw codegen_error("unknown schedule");
}

}

std::string generate_header(const request &req, const candidate_trial &trial)
{
    validate_trial(req, trial);

    const std::string_view alias_contract = req.alias == aliasing::may
                                                ? "r, a, and b may overlap"
                                                : "r is disjoint from a and b; a and b may overlap";
    std::ostringstream out;

    out << R"pqc(#ifndef PQC_POLY_KERNEL_HPP
#define PQC_POLY_KERNEL_HPP

#include <cstddef>
#include <cstdint>

inline constexpr std::size_t pqc_poly_n = static_cast<std::size_t>()pqc"
        << req.n << R"pqc();
inline constexpr std::int32_t pqc_poly_q = static_cast<std::int32_t>()pqc"
        << req.q << R"pqc();

/*
 * input coefficients: [)pqc"
        << input_lower_bound(req) << ", " << input_upper_bound(req) << R"pqc(]
 * output coefficients: [0, )pqc"
        << req.q - 1 << R"pqc(]
 * alias contract: )pqc"
        << alias_contract << R"pqc(
 */

extern "C" void pqc_poly_mul(
    std::int32_t *r,
    const std::int32_t *a,
    const std::int32_t *b) noexcept;

// this compatibility name has no separate code or call overhead
inline void polysel_mul(
    std::int32_t *r,
    const std::int32_t *a,
    const std::int32_t *b) noexcept
{
    pqc_poly_mul(r, a, b);
}

#endif
)pqc";

    return out.str();
}

std::string generate_source(const request &req, const candidate_trial &trial)
{
    validate_trial(req, trial);

    const analysis_verdict &verdict = trial.analysis;
    const std::string_view accumulator_type =
        verdict.plan.acc_bits == 32 ? "std::int32_t" : "std::int64_t";
    const std::string_view alias_note =
        req.alias == aliasing::no
            ? "r is restricted because the checked contract keeps it disjoint"
            : "all input reads finish before writes, so exact overlap stays safe";
    std::ostringstream out;

    out << R"pqc(#include "kernel.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

// these hints preserve a portable scalar fallback on unknown compilers
#if defined(_MSC_VER)
#define PQC_POLY_FORCE_INLINE __forceinline
#define PQC_POLY_HOT
#define PQC_POLY_RESTRICT __restrict
#define PQC_POLY_VECTOR_LOOP __pragma(loop(ivdep))
#elif defined(__clang__)
#define PQC_POLY_FORCE_INLINE inline __attribute__((always_inline))
#define PQC_POLY_HOT __attribute__((hot))
#define PQC_POLY_RESTRICT __restrict__
#define PQC_POLY_VECTOR_LOOP
#elif defined(__GNUC__)
#define PQC_POLY_FORCE_INLINE inline __attribute__((always_inline))
#define PQC_POLY_HOT __attribute__((hot))
#define PQC_POLY_RESTRICT __restrict__
#define PQC_POLY_VECTOR_LOOP _Pragma("GCC ivdep")
#else
#define PQC_POLY_FORCE_INLINE inline
#define PQC_POLY_HOT
#define PQC_POLY_RESTRICT
#define PQC_POLY_VECTOR_LOOP
#endif

namespace
{

using acc_t = )pqc"
        << accumulator_type << R"pqc(;

inline constexpr std::uint64_t accumulator_bound = )pqc"
        << wide_to_string(verdict.accumulator_bound) << R"pqc(ULL;

static_assert(
    accumulator_bound <= static_cast<std::uint64_t>(std::numeric_limits<acc_t>::max()),
    "accumulator too small");

)pqc" << reduction_body(req.q)
        << R"pqc(
}

/*
 * plan: )pqc"
        << plan_id(verdict.plan) << R"pqc(
 * raw accumulator bound: )pqc"
        << wide_to_string(verdict.accumulator_bound) << R"pqc(
 * explicit scratch bytes: )pqc"
        << wide_to_string(verdict.temporary_bytes) << R"pqc(
 */
// )pqc" << alias_note
        << R"pqc(
extern "C" PQC_POLY_HOT void pqc_poly_mul(
    )pqc"
        << generated_arguments(req.alias) << R"pqc() noexcept
{
)pqc" << generated_body(req, verdict)
        << R"pqc(}

#undef PQC_POLY_VECTOR_LOOP
#undef PQC_POLY_RESTRICT
#undef PQC_POLY_HOT
#undef PQC_POLY_FORCE_INLINE
)pqc";

    return out.str();
}

}
