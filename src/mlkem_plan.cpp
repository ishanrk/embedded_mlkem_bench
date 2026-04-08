#include "pqc_poly/mlkem_plan.hpp"

#include "pqc_poly/selector.hpp"

#include "json.hpp"

#include <array>
#include <algorithm>
#include <limits>
#include <tuple>

namespace pqc_poly
{
namespace
{

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

void append_strings(std::string &out, std::span<const std::string> values)
{
    out += '[';
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (i != 0)
        {
            out += ", ";
        }
        detail::append_json_string(out, values[i]);
    }
    out += ']';
}

}

std::string_view mlkem_level_name(mlkem_level value) noexcept
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

std::string_view ntt_traversal_name(ntt_traversal value) noexcept
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

std::string_view intt_traversal_name(intt_traversal value) noexcept
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

std::string_view intt_reduction_name(intt_sum_reduction value) noexcept
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

std::string_view basemul_schedule_name(basemul_schedule value) noexcept
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

std::string_view mlkem_instruction_name(mlkem_instruction value) noexcept
{
    switch (value)
    {
        case mlkem_instruction::none:
            return "none";
        case mlkem_instruction::fqmul:
            return "fqmul";
    }
    return "invalid";
}

unsigned mlkem_k(mlkem_level level) noexcept
{
    switch (level)
    {
        case mlkem_level::mlkem512:
            return 2;
        case mlkem_level::mlkem768:
            return 3;
        case mlkem_level::mlkem1024:
            return 4;
    }
    return 0;
}

mlkem_request parse_mlkem_request(std::string_view json)
{
    const request parsed = parse_request(json);
    if (parsed.op != operation::negacyclic_mul || parsed.n != 256 || parsed.q != 3329)
    {
        throw mlkem_error("mlkem request must use negacyclic_mul n 256 q 3329");
    }
    return {.scratch_limit = parsed.limits.ram,
            .caller_workspace_limit = std::numeric_limits<std::uint64_t>::max()};
}

std::string mlkem_plan_id(const mlkem_plan &plan)
{
    std::string out = "mlk";
    out += mlkem_level_name(plan.level);
    out += "_f";
    out += ntt_traversal_name(plan.forward);
    out += "_i";
    out += intt_traversal_name(plan.inverse);
    out += "_r";
    out += intt_reduction_name(plan.inverse_reduction);
    out += "_b";
    out += basemul_schedule_name(plan.basemul);
    out += "_x";
    out += mlkem_instruction_name(plan.instruction);
    return out;
}

std::vector<mlkem_plan> enumerate_mlkem_plans()
{
    constexpr std::array levels{mlkem_level::mlkem512, mlkem_level::mlkem768,
                                mlkem_level::mlkem1024};
    constexpr std::array forwards{ntt_traversal::stage_major, ntt_traversal::fuse_two_layers};
    constexpr std::array inverses{intt_traversal::stage_major, intt_traversal::fuse_two_layers};
    constexpr std::array reductions{intt_sum_reduction::every_layer,
                                    intt_sum_reduction::after_layer_pair};
    constexpr std::array bases{basemul_schedule::cached_late32, basemul_schedule::cached_eager32,
                               basemul_schedule::direct_eager32};
    constexpr std::array instructions{mlkem_instruction::none, mlkem_instruction::fqmul};
    std::vector<mlkem_plan> out;
    out.reserve(144);
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
                        for (const mlkem_instruction instruction : instructions)
                        {
                            out.push_back(
                                {level, forward, inverse, reduction, basemul, instruction});
                        }
                    }
                }
            }
        }
    }
    return out;
}

mlkem_candidate analyze_mlkem_plan(const mlkem_request &request, const mlkem_plan &plan)
{
    const unsigned k = mlkem_k(plan.level);
    mlkem_candidate out{
        .plan = plan,
        .id = mlkem_plan_id(plan),
        .forward_records = forward_records(),
        .inverse_records = inverse_records(),
        .forward_bound = 8U * 3329U,
        .inverse_lazy_bound = 4U * 3329U,
        .accumulator_bound = static_cast<std::uint64_t>(k) * 2U * 4096U * 32768U,
        .mulcache_coefficients = plan.basemul == basemul_schedule::direct_eager32 ? 0U : k * 128U,
        .scratch_bytes = plan.basemul == basemul_schedule::direct_eager32 ? 0U : k * 256U,
        .caller_workspace_bytes = static_cast<std::uint32_t>((2U * k + 1U) * 512U),
        .ntt_in_place = true,
        .intt_in_place = true,
        .fixed_loop_structure = true,
    };
    if (out.scratch_bytes > request.scratch_limit)
    {
        out.rejections.emplace_back("scratch_limit");
    }
    if (out.caller_workspace_bytes > request.caller_workspace_limit)
    {
        out.rejections.emplace_back("caller_workspace_limit");
    }
    if (plan.instruction == mlkem_instruction::fqmul)
    {
        out.rejections.emplace_back("instruction_unavailable");
    }
    out.legal = out.rejections.empty();
    return out;
}

std::string serialize_mlkem_candidate(const mlkem_candidate &candidate)
{
    std::string out;
    out.reserve(32768);
    out += "{\n  \"schema\": ";
    detail::append_json_string(out, candidate.schema);
    out += ",\n  \"id\": ";
    detail::append_json_string(out, candidate.id);
    out += ",\n  \"plan\": {\n    \"level\": ";
    detail::append_json_string(out, mlkem_level_name(candidate.plan.level));
    out += ",\n    \"forward\": ";
    detail::append_json_string(out, ntt_traversal_name(candidate.plan.forward));
    out += ",\n    \"inverse\": ";
    detail::append_json_string(out, intt_traversal_name(candidate.plan.inverse));
    out += ",\n    \"inverse_reduction\": ";
    detail::append_json_string(out, intt_reduction_name(candidate.plan.inverse_reduction));
    out += ",\n    \"basemul\": ";
    detail::append_json_string(out, basemul_schedule_name(candidate.plan.basemul));
    out += ",\n    \"instruction\": ";
    detail::append_json_string(out, mlkem_instruction_name(candidate.plan.instruction));
    out += "\n  },\n  \"forward_record_count\": ";
    out += std::to_string(candidate.forward_records.size());
    out += ",\n  \"inverse_record_count\": ";
    out += std::to_string(candidate.inverse_records.size());
    out += ",\n  \"forward_bound\": " + std::to_string(candidate.forward_bound);
    out += ",\n  \"inverse_lazy_bound\": " + std::to_string(candidate.inverse_lazy_bound);
    out += ",\n  \"accumulator_bound\": " + std::to_string(candidate.accumulator_bound);
    out += ",\n  \"mulcache_coefficients\": " + std::to_string(candidate.mulcache_coefficients);
    out += ",\n  \"scratch_bytes\": " + std::to_string(candidate.scratch_bytes);
    out += ",\n  \"caller_workspace_bytes\": " + std::to_string(candidate.caller_workspace_bytes);
    out += ",\n  \"ntt_in_place\": ";
    out += candidate.ntt_in_place ? "true" : "false";
    out += ",\n  \"intt_in_place\": ";
    out += candidate.intt_in_place ? "true" : "false";
    out += ",\n  \"fixed_loop_structure\": ";
    out += candidate.fixed_loop_structure ? "true" : "false";
    out += ",\n  \"legal\": ";
    out += candidate.legal ? "true" : "false";
    out += ",\n  \"rejections\": ";
    append_strings(out, candidate.rejections);
    out += "\n}\n";
    return out;
}

std::string serialize_mlkem_candidates(std::span<const mlkem_candidate> candidates)
{
    std::string out = "[\n";
    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        std::string item = serialize_mlkem_candidate(candidates[i]);
        item.pop_back();
        for (std::size_t position = 0; position < item.size(); ++position)
        {
            out += item[position];
            if (item[position] == '\n')
            {
                out += "  ";
            }
        }
        out += i + 1 == candidates.size() ? "\n" : ",\n";
    }
    out += "]\n";
    return out;
}

const mlkem_measurement &select_measured_mlkem_plan(mlkem_level level,
                                                    std::span<const mlkem_candidate> candidates,
                                                    std::span<const mlkem_measurement> measurements)
{
    std::vector<std::string_view> required;
    required.reserve(24);
    for (const mlkem_candidate &candidate : candidates)
    {
        if (candidate.plan.level == level && candidate.legal &&
            candidate.plan.instruction == mlkem_instruction::none)
        {
            required.push_back(candidate.id);
        }
    }
    if (required.size() != 24)
    {
        throw mlkem_error("software candidate set is incomplete");
    }

    const mlkem_measurement *winner = nullptr;
    unsigned found = 0;
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
            throw mlkem_error("software measurement is not verified");
        }
        if (std::find(seen.begin(), seen.end(), measurement.plan_id) != seen.end())
        {
            throw mlkem_error("duplicate software measurement");
        }
        seen.push_back(measurement.plan_id);
        ++found;
        const auto total = [](const mlkem_measurement &value)
        {
            if (value.encapsulation_cycles >
                    std::numeric_limits<std::uint64_t>::max() - value.keygen_cycles ||
                value.decapsulation_cycles > std::numeric_limits<std::uint64_t>::max() -
                                                 value.keygen_cycles - value.encapsulation_cycles)
            {
                throw mlkem_error("software cycle total overflow");
            }
            return value.keygen_cycles + value.encapsulation_cycles + value.decapsulation_cycles;
        };
        if (winner == nullptr ||
            std::tuple(total(measurement), measurement.runtime_stack_bytes,
                       measurement.allocated_flash_bytes, measurement.plan_id) <
                std::tuple(total(*winner), winner->runtime_stack_bytes,
                           winner->allocated_flash_bytes, winner->plan_id))
        {
            winner = &measurement;
        }
    }
    if (found != required.size() || winner == nullptr)
    {
        throw mlkem_error("software measurement set is incomplete");
    }
    return *winner;
}

}
