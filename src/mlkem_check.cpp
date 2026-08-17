#include "pqc_poly/mlkem_plan.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace pqc_poly
{
namespace
{

void add_once(std::vector<std::string> &out, std::string_view value)
{
    if (std::find(out.begin(), out.end(), value) == out.end())
    {
        out.emplace_back(value);
    }
}

[[nodiscard]] std::string checked_id(const mlkem_plan &plan)
{
    std::string out = "mlk";
    switch (plan.level)
    {
        case mlkem_level::mlkem512:
            out += "512";
            break;
        case mlkem_level::mlkem768:
            out += "768";
            break;
        case mlkem_level::mlkem1024:
            out += "1024";
            break;
        default:
            return {};
    }
    switch (plan.forward)
    {
        case ntt_traversal::stage_major:
            out += "_fstage";
            break;
        case ntt_traversal::fuse_two_layers:
            out += "_ffuse2";
            break;
        default:
            return {};
    }
    switch (plan.inverse)
    {
        case intt_traversal::stage_major:
            out += "_istage";
            break;
        case intt_traversal::fuse_two_layers:
            out += "_ifuse2";
            break;
        default:
            return {};
    }
    switch (plan.inverse_reduction)
    {
        case intt_sum_reduction::every_layer:
            out += "_reach";
            break;
        case intt_sum_reduction::after_layer_pair:
            out += "_rpair";
            break;
        default:
            return {};
    }
    switch (plan.basemul)
    {
        case basemul_schedule::cached_late32:
            out += "_bcachelate";
            break;
        case basemul_schedule::cached_eager32:
            out += "_bcacheeager";
            break;
        case basemul_schedule::direct_eager32:
            out += "_bdirecteager";
            break;
        default:
            return {};
    }
    switch (plan.instruction)
    {
        case mlkem_instruction::none:
            out += "_xnone";
            break;
        case mlkem_instruction::fqmul:
            out += "_xfqmul";
            break;
        default:
            return {};
    }
    return out;
}

[[nodiscard]] unsigned checked_k(mlkem_level level) noexcept
{
    switch (level)
    {
        case mlkem_level::mlkem512:
            return 2;
        case mlkem_level::mlkem768:
            return 3;
        case mlkem_level::mlkem1024:
            return 4;
        default:
            return 0;
    }
}

[[nodiscard]] std::vector<mlkem_record> checked_forward()
{
    std::vector<mlkem_record> out;
    for (unsigned layer = 1; layer < 8; ++layer)
    {
        const unsigned length = 256U / (1U << layer);
        const unsigned count = 256U / (2U * length);
        for (unsigned block = 0; block < count; ++block)
        {
            const unsigned left = block * 2U * length;
            out.push_back(
                {static_cast<std::uint16_t>(layer), static_cast<std::uint16_t>(block),
                 static_cast<std::uint16_t>(count + block), static_cast<std::uint16_t>(left),
                 static_cast<std::uint16_t>(left + length), static_cast<std::uint16_t>(length)});
        }
    }
    return out;
}

[[nodiscard]] std::vector<mlkem_record> checked_inverse()
{
    std::vector<mlkem_record> out;
    for (unsigned length = 2; length <= 128; length *= 2)
    {
        const unsigned layer = 8U;
        unsigned shift = 0;
        for (unsigned value = length; value > 1; value >>= 1U)
        {
            ++shift;
        }
        const unsigned logical_layer = layer - shift;
        const unsigned count = 256U / (2U * length);
        for (unsigned block = 0; block < count; ++block)
        {
            const unsigned left = block * 2U * length;
            out.push_back(
                {static_cast<std::uint16_t>(logical_layer), static_cast<std::uint16_t>(block),
                 static_cast<std::uint16_t>(2U * count - 1U - block),
                 static_cast<std::uint16_t>(left), static_cast<std::uint16_t>(left + length),
                 static_cast<std::uint16_t>(length)});
        }
    }
    return out;
}

void check_records(std::vector<std::string> &out, std::span<const mlkem_record> actual,
                   std::span<const mlkem_record> expected)
{
    if (actual.size() < expected.size())
    {
        add_once(out, "missing_butterfly");
    }
    if (actual.size() > expected.size())
    {
        add_once(out, "duplicate_butterfly");
    }
    const std::size_t count = std::min(actual.size(), expected.size());
    for (std::size_t i = 0; i < count; ++i)
    {
        if (actual[i].zeta_index != expected[i].zeta_index ||
            actual[i].layer != expected[i].layer || actual[i].block != expected[i].block)
        {
            add_once(out, "bad_twiddle_schedule");
        }
        if (actual[i].left_base != expected[i].left_base ||
            actual[i].right_base != expected[i].right_base ||
            actual[i].length != expected[i].length || actual[i].right_base >= 256 ||
            actual[i].left_base >= 256)
        {
            add_once(out, "array_index");
        }
    }
}

}

std::vector<std::string> check_mlkem_plan(const mlkem_request &request,
                                          const mlkem_candidate &candidate)
{
    std::vector<std::string> out;
    if (candidate.schema != "pqc-poly-bench/mlkem-plan-v1")
    {
        add_once(out, "bad_schema");
    }

    const std::string id = checked_id(candidate.plan);
    if (id.empty() || candidate.id != id)
    {
        add_once(out, "bad_plan_id");
    }

    const std::vector<mlkem_record> forward = checked_forward();
    const std::vector<mlkem_record> inverse = checked_inverse();
    check_records(out, candidate.forward_records, forward);
    check_records(out, candidate.inverse_records, inverse);

    constexpr std::uint32_t q = 3329;
    constexpr std::uint32_t forward_bound = 8U * q;
    constexpr std::uint32_t inverse_bound = 4U * q;
    if (candidate.forward_bound != forward_bound || forward_bound > 32768U)
    {
        add_once(out, "coefficient_storage_overflow");
    }
    if (candidate.inverse_lazy_bound != inverse_bound || inverse_bound > 32768U)
    {
        add_once(out, "barrett_input_range");
    }

    const unsigned k = checked_k(candidate.plan.level);
    const unsigned __int128 accumulator = static_cast<unsigned __int128>(k) * 2U * 4096U * 32768U;
    if (k == 0 || accumulator >= static_cast<unsigned __int128>(INT32_MAX) ||
        candidate.accumulator_bound != static_cast<std::uint64_t>(accumulator))
    {
        add_once(out, "accumulator_overflow");
    }
    constexpr std::uint64_t eager_bound = 2U * 4U * (q - 1U);
    if (eager_bound > static_cast<std::uint64_t>(INT16_MAX))
    {
        add_once(out, "montgomery_input_range");
    }

    const bool direct = candidate.plan.basemul == basemul_schedule::direct_eager32;
    const std::uint32_t cache = direct ? 0U : k * 128U;
    const std::uint32_t scratch = cache * 2U;
    if (candidate.mulcache_coefficients != cache)
    {
        add_once(out, "array_index");
    }
    if (candidate.scratch_bytes != scratch || scratch > request.scratch_limit)
    {
        add_once(out, "scratch_limit");
    }
    const std::uint32_t caller = static_cast<std::uint32_t>((2U * k + 1U) * 512U);
    if (candidate.caller_workspace_bytes != caller || caller > request.caller_workspace_limit)
    {
        add_once(out, "caller_workspace_limit");
    }
    if (!candidate.ntt_in_place || !candidate.intt_in_place)
    {
        add_once(out, "alias");
    }
    if (!candidate.fixed_loop_structure)
    {
        add_once(out, "constant_time_structure");
    }

    std::vector<std::string> rejection;
    if (scratch > request.scratch_limit)
    {
        rejection.emplace_back("scratch_limit");
    }
    if (caller > request.caller_workspace_limit)
    {
        rejection.emplace_back("caller_workspace_limit");
    }
    if (candidate.plan.instruction == mlkem_instruction::fqmul)
    {
        rejection.emplace_back("instruction_unavailable");
        add_once(out, "instruction_unavailable");
    }
    if (candidate.rejections != rejection || candidate.legal != rejection.empty())
    {
        for (const std::string &reason : rejection)
        {
            add_once(out, reason);
        }
        if (rejection.empty())
        {
            add_once(out, "analysis_overflow");
        }
    }
    return out;
}

}
