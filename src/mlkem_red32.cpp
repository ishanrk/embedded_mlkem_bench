#include "pqc_poly/mlkem_red32.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <tuple>

namespace pqc_poly
{
namespace
{

[[nodiscard]] std::string_view level_name(mlkem_level value) noexcept
{
    switch (value)
    {
        case mlkem_level::mlkem512:
            return "512";
        case mlkem_level::mlkem768:
            return "768";
        case mlkem_level::mlkem1024:
            return "1024";
    }
    return "invalid";
}

[[nodiscard]] std::string_view forward_name(ntt_traversal value) noexcept
{
    switch (value)
    {
        case ntt_traversal::stage_major:
            return "stage";
        case ntt_traversal::fuse_two_layers:
            return "fuse2";
    }
    return "invalid";
}

[[nodiscard]] std::string_view inverse_name(intt_traversal value) noexcept
{
    switch (value)
    {
        case intt_traversal::stage_major:
            return "stage";
        case intt_traversal::fuse_two_layers:
            return "fuse2";
    }
    return "invalid";
}

[[nodiscard]] std::string_view reduction_name(intt_sum_reduction value) noexcept
{
    switch (value)
    {
        case intt_sum_reduction::every_layer:
            return "each";
        case intt_sum_reduction::after_layer_pair:
            return "pair";
    }
    return "invalid";
}

[[nodiscard]] std::string_view base_name(basemul_schedule value) noexcept
{
    switch (value)
    {
        case basemul_schedule::cached_late32:
            return "cachelate";
        case basemul_schedule::cached_eager32:
            return "cacheeager";
        case basemul_schedule::direct_eager32:
            return "directeager";
    }
    return "invalid";
}

[[nodiscard]] unsigned level_k(mlkem_level level) noexcept
{
    switch (level)
    {
        case mlkem_level::mlkem512:
            return 2U;
        case mlkem_level::mlkem768:
            return 3U;
        case mlkem_level::mlkem1024:
            return 4U;
    }
    return 0U;
}

[[nodiscard]] std::vector<mlkem_record> forward_records()
{
    std::vector<mlkem_record> out;
    out.reserve(127);
    for (unsigned layer = 1; layer <= 7; ++layer)
    {
        const unsigned length = 256U >> layer;
        const unsigned blocks = 1U << (layer - 1U);
        for (unsigned block = 0; block < blocks; ++block)
        {
            const unsigned left = block * 2U * length;
            out.push_back(
                {static_cast<std::uint16_t>(layer), static_cast<std::uint16_t>(block),
                 static_cast<std::uint16_t>(blocks + block), static_cast<std::uint16_t>(left),
                 static_cast<std::uint16_t>(left + length), static_cast<std::uint16_t>(length)});
        }
    }
    return out;
}

[[nodiscard]] std::vector<mlkem_record> inverse_records()
{
    std::vector<mlkem_record> out;
    out.reserve(127);
    for (unsigned layer = 7; layer != 0; --layer)
    {
        const unsigned length = 256U >> layer;
        const unsigned blocks = 1U << (layer - 1U);
        for (unsigned block = 0; block < blocks; ++block)
        {
            const unsigned left = block * 2U * length;
            out.push_back({static_cast<std::uint16_t>(layer), static_cast<std::uint16_t>(block),
                           static_cast<std::uint16_t>((1U << layer) - 1U - block),
                           static_cast<std::uint16_t>(left),
                           static_cast<std::uint16_t>(left + length),
                           static_cast<std::uint16_t>(length)});
        }
    }
    return out;
}

void append_json_string(std::string &out, std::string_view value)
{
    static constexpr char hex[] = "0123456789abcdef";
    out.push_back('"');
    for (const unsigned char byte : value)
    {
        switch (byte)
        {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (byte < 0x20U)
                {
                    out += "\\u00";
                    out.push_back(hex[byte >> 4U]);
                    out.push_back(hex[byte & 0x0fU]);
                }
                else
                {
                    out.push_back(static_cast<char>(byte));
                }
        }
    }
    out.push_back('"');
}

void append_strings(std::string &out, std::span<const std::string> values)
{
    out.push_back('[');
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (i != 0U)
        {
            out += ", ";
        }
        append_json_string(out, values[i]);
    }
    out.push_back(']');
}

[[nodiscard]] std::uint64_t complete_cycles(const mlkem_measurement &value)
{
    if (value.encapsulation_cycles >
            std::numeric_limits<std::uint64_t>::max() - value.keygen_cycles ||
        value.decapsulation_cycles > std::numeric_limits<std::uint64_t>::max() -
                                          value.keygen_cycles -
                                          value.encapsulation_cycles)
    {
        throw mlkem_error("cycle total overflow");
    }
    return value.keygen_cycles + value.encapsulation_cycles + value.decapsulation_cycles;
}

}

std::int32_t red32_reference(std::uint32_t value) noexcept
{
    const std::int64_t signed_value =
        value <= static_cast<std::uint32_t>(INT32_MAX)
            ? static_cast<std::int64_t>(value)
            : static_cast<std::int64_t>(value) - INT64_C(4294967296);
    const std::uint32_t inverse = ((value & UINT32_C(0xffff)) * UINT32_C(62209)) &
                                  UINT32_C(0xffff);
    const std::int32_t signed_inverse =
        static_cast<std::int32_t>(inverse ^ UINT32_C(0x8000)) - INT32_C(32768);
    const std::int64_t numerator =
        signed_value - static_cast<std::int64_t>(signed_inverse) * INT64_C(3329);
    return static_cast<std::int32_t>(numerator / INT64_C(65536));
}

mlkem_plan red32_schedule_plan(const red32_plan &plan) noexcept
{
    return {plan.level, plan.forward, plan.inverse, plan.inverse_reduction, plan.basemul,
            mlkem_instruction::none};
}

std::string red32_plan_id(const red32_plan &plan)
{
    std::string out = "mlk";
    out += level_name(plan.level);
    out += "_f";
    out += forward_name(plan.forward);
    out += "_i";
    out += inverse_name(plan.inverse);
    out += "_r";
    out += reduction_name(plan.inverse_reduction);
    out += "_b";
    out += base_name(plan.basemul);
    out += "_xred32";
    return out;
}

std::vector<red32_plan> enumerate_red32_comparison_plans()
{
    constexpr std::array levels{mlkem_level::mlkem512, mlkem_level::mlkem768,
                                mlkem_level::mlkem1024};
    constexpr std::array forwards{ntt_traversal::stage_major, ntt_traversal::fuse_two_layers};
    constexpr std::array inverses{intt_traversal::stage_major, intt_traversal::fuse_two_layers};
    constexpr std::array reductions{intt_sum_reduction::every_layer,
                                    intt_sum_reduction::after_layer_pair};
    constexpr std::array bases{basemul_schedule::cached_late32,
                               basemul_schedule::cached_eager32,
                               basemul_schedule::direct_eager32};
    std::vector<red32_plan> out;
    out.reserve(72);
    for (const mlkem_level level : levels)
    {
        for (const ntt_traversal forward : forwards)
        {
            for (const intt_traversal inverse : inverses)
            {
                for (const intt_sum_reduction reduction : reductions)
                {
                    for (const basemul_schedule basemul : bases)
                    {
                        out.push_back({level, forward, inverse, reduction, basemul});
                    }
                }
            }
        }
    }
    return out;
}

red32_candidate analyze_red32_plan(const mlkem_request &request, const red32_plan &plan)
{
    const unsigned k = level_k(plan.level);
    constexpr std::uint64_t montgomery_bound =
        (4096U * 32768U + 32768U * 3329U + 65535U) / 65536U;
    const bool late = plan.basemul == basemul_schedule::cached_late32;
    red32_candidate out{
        .plan = plan,
        .id = red32_plan_id(plan),
        .forward_records = forward_records(),
        .inverse_records = inverse_records(),
        .forward_bound = 8U * 3329U,
        .inverse_lazy_bound = 4U * 3329U,
        .accumulator_bound =
            static_cast<std::uint64_t>(k) * 2U * (late ? 4096U * 32768U : montgomery_bound),
        .mulcache_coefficients =
            plan.basemul == basemul_schedule::direct_eager32 ? 0U : k * 128U,
        .scratch_bytes = plan.basemul == basemul_schedule::direct_eager32 ? 0U : k * 256U,
        .caller_workspace_bytes = static_cast<std::uint32_t>((2U * k + 1U) * 512U),
        .ntt_in_place = true,
        .intt_in_place = true,
        .fixed_loop_structure = true,
        .full_domain_reduction = true,
        .canonical_rs2_zero = true,
        .standard_mul_before_reduction = true,
    };
    if (k == 0U)
    {
        out.rejections.emplace_back("level");
    }
    if (out.scratch_bytes > request.scratch_limit)
    {
        out.rejections.emplace_back("scratch_limit");
    }
    if (out.caller_workspace_bytes > request.caller_workspace_limit)
    {
        out.rejections.emplace_back("caller_workspace_limit");
    }
    out.legal = out.rejections.empty();
    return out;
}

std::string serialize_red32_candidate(const red32_candidate &candidate)
{
    std::string out;
    out.reserve(2048);
    out += "{\n  \"schema\": ";
    append_json_string(out, candidate.schema);
    out += ",\n  \"id\": ";
    append_json_string(out, candidate.id);
    out += ",\n  \"plan\": {\n    \"level\": ";
    append_json_string(out, level_name(candidate.plan.level));
    out += ",\n    \"forward\": ";
    append_json_string(out, forward_name(candidate.plan.forward));
    out += ",\n    \"inverse\": ";
    append_json_string(out, inverse_name(candidate.plan.inverse));
    out += ",\n    \"inverse_reduction\": ";
    append_json_string(out, reduction_name(candidate.plan.inverse_reduction));
    out += ",\n    \"basemul\": ";
    append_json_string(out, base_name(candidate.plan.basemul));
    out += "\n  },\n  \"instruction\": {\n    \"name\": \"red32\",\n"
           "    \"encoding_mask\": \"0xfe00707f\",\n"
           "    \"encoding_match\": \"0x0000100b\",\n"
           "    \"canonical_rs2_zero\": ";
    out += candidate.canonical_rs2_zero ? "true" : "false";
    out += "\n  },\n  \"forward_record_count\": " +
           std::to_string(candidate.forward_records.size());
    out += ",\n  \"inverse_record_count\": " +
           std::to_string(candidate.inverse_records.size());
    out += ",\n  \"forward_bound\": " + std::to_string(candidate.forward_bound);
    out += ",\n  \"inverse_lazy_bound\": " + std::to_string(candidate.inverse_lazy_bound);
    out += ",\n  \"accumulator_bound\": " + std::to_string(candidate.accumulator_bound);
    out += ",\n  \"mulcache_coefficients\": " +
           std::to_string(candidate.mulcache_coefficients);
    out += ",\n  \"scratch_bytes\": " + std::to_string(candidate.scratch_bytes);
    out += ",\n  \"caller_workspace_bytes\": " +
           std::to_string(candidate.caller_workspace_bytes);
    out += ",\n  \"reduction_range\": [" + std::to_string(candidate.reduction_min) + ", " +
           std::to_string(candidate.reduction_max) + "]";
    out += ",\n  \"ntt_in_place\": ";
    out += candidate.ntt_in_place ? "true" : "false";
    out += ",\n  \"intt_in_place\": ";
    out += candidate.intt_in_place ? "true" : "false";
    out += ",\n  \"fixed_loop_structure\": ";
    out += candidate.fixed_loop_structure ? "true" : "false";
    out += ",\n  \"full_domain_reduction\": ";
    out += candidate.full_domain_reduction ? "true" : "false";
    out += ",\n  \"standard_mul_before_reduction\": ";
    out += candidate.standard_mul_before_reduction ? "true" : "false";
    out += ",\n  \"legal\": ";
    out += candidate.legal ? "true" : "false";
    out += ",\n  \"rejections\": ";
    append_strings(out, candidate.rejections);
    out += "\n}\n";
    return out;
}

std::string serialize_red32_candidates(std::span<const red32_candidate> candidates)
{
    std::string out = "[\n";
    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        std::string item = serialize_red32_candidate(candidates[i]);
        item.pop_back();
        out += "  ";
        for (const char value : item)
        {
            out.push_back(value);
            if (value == '\n')
            {
                out += "  ";
            }
        }
        out += i + 1U == candidates.size() ? "\n" : ",\n";
    }
    out += "]\n";
    return out;
}

const mlkem_measurement &select_measured_red32_plan(
    mlkem_level level, std::span<const red32_candidate> candidates,
    std::span<const mlkem_measurement> measurements)
{
    std::vector<std::string_view> required;
    required.reserve(24);
    for (const red32_candidate &candidate : candidates)
    {
        if (candidate.plan.level == level && candidate.legal)
        {
            required.push_back(candidate.id);
        }
    }
    if (required.size() != 24U)
    {
        throw mlkem_error("red32 candidate set is incomplete");
    }

    const mlkem_measurement *winner = nullptr;
    std::vector<std::string_view> seen;
    seen.reserve(24);
    for (const mlkem_measurement &measurement : measurements)
    {
        if (std::find(required.begin(), required.end(), measurement.plan_id) == required.end())
        {
            continue;
        }
        if (!measurement.verified)
        {
            throw mlkem_error("red32 measurement is not verified");
        }
        if (std::find(seen.begin(), seen.end(), measurement.plan_id) != seen.end())
        {
            throw mlkem_error("duplicate red32 measurement");
        }
        seen.push_back(measurement.plan_id);
        if (winner == nullptr ||
            std::tuple(complete_cycles(measurement), measurement.runtime_stack_bytes,
                       measurement.allocated_flash_bytes, measurement.plan_id) <
                std::tuple(complete_cycles(*winner), winner->runtime_stack_bytes,
                           winner->allocated_flash_bytes, winner->plan_id))
        {
            winner = &measurement;
        }
    }
    if (seen.size() != required.size() || winner == nullptr)
    {
        throw mlkem_error("red32 measurement set is incomplete");
    }
    return *winner;
}

}
