#include "pqc_poly/mlkem_codegen.hpp"
#include "pqc_poly/target_measurement.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <ostream>
#include <span>
#include <sstream>

namespace pqc_poly
{
namespace
{

[[nodiscard]] std::string read(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw mlkem_error("cannot open request");
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write(const std::filesystem::path &path, std::string_view value)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!output)
    {
        throw mlkem_error("cannot write output");
    }
}

struct operation_summary
{
    std::string name{};
    std::uint64_t minimum_cycles{0};
    std::uint64_t cycles{0};
    std::uint64_t maximum_cycles{0};
    std::uint64_t minimum_instructions{0};
    std::uint64_t instructions{0};
    std::uint64_t maximum_instructions{0};
};

struct measured_plan
{
    mlkem_measurement selection{};
    std::vector<operation_summary> project{};
    std::vector<operation_summary> stock{};
    stack_measurement stack{};
    code_size_measurement size{};
};

[[nodiscard]] std::uint64_t complete_cycles(const mlkem_measurement &measurement)
{
    if (measurement.encapsulation_cycles >
            std::numeric_limits<std::uint64_t>::max() - measurement.keygen_cycles ||
        measurement.decapsulation_cycles > std::numeric_limits<std::uint64_t>::max() -
                                               measurement.keygen_cycles -
                                               measurement.encapsulation_cycles)
    {
        throw mlkem_error("cycle total overflow");
    }
    return measurement.keygen_cycles + measurement.encapsulation_cycles +
           measurement.decapsulation_cycles;
}

[[nodiscard]] std::uint64_t median(std::vector<std::uint64_t> values)
{
    std::sort(values.begin(), values.end());
    if (values.empty())
    {
        throw mlkem_error("empty measurement operation");
    }
    const std::size_t middle = values.size() / 2U;
    if ((values.size() & 1U) != 0U)
    {
        return values[middle];
    }
    if (values[middle] > std::numeric_limits<std::uint64_t>::max() - values[middle - 1U])
    {
        throw mlkem_error("measurement median overflow");
    }
    return (values[middle - 1U] + values[middle]) / 2U;
}

[[nodiscard]] std::vector<operation_summary> summarize(
    std::span<const mlkem_cycle_measurement> records, const mlkem_candidate &candidate,
    std::string_view multiplier)
{
    const std::string base = "base_dot_k" + std::to_string(mlkem_k(candidate.plan.level));
    const std::array<std::string_view, 8> operations{
        "forward_ntt", "inverse_ntt", "mulcache",      base,
        "poly_tomont", "keygen",      "encapsulation", "decapsulation"};
    std::vector<operation_summary> result;
    std::size_t cursor = 0;
    result.reserve(operations.size());
    for (const std::string_view operation : operations)
    {
        const unsigned inputs =
            operation == "keygen" || operation == "encapsulation" || operation == "decapsulation"
                ? 30U
                : 16U;
        std::vector<std::uint64_t> cycles;
        std::vector<std::uint64_t> instructions;
        cycles.reserve(inputs);
        instructions.reserve(inputs);
        for (unsigned input = 0; input < inputs; ++input)
        {
            std::uint64_t repeated_cycles = 0;
            std::uint64_t repeated_instructions = 0;
            for (unsigned repeat = 0; repeat < 3U; ++repeat)
            {
                if (cursor == records.size())
                {
                    throw mlkem_error("measurement set is incomplete");
                }
                const mlkem_cycle_measurement &record = records[cursor++];
                if (record.plan_id != candidate.id ||
                    record.level != mlkem_level_name(candidate.plan.level) ||
                    record.operation != operation || record.multiplier != multiplier ||
                    record.input != input || record.repeat != repeat)
                {
                    throw mlkem_error("measurement order or identity changed");
                }
                if (repeat != 0U && (record.calibrated_cycles != repeated_cycles ||
                                     record.instruction_count != repeated_instructions))
                {
                    throw mlkem_error("measurement repeat changed");
                }
                repeated_cycles = record.calibrated_cycles;
                repeated_instructions = record.instruction_count;
            }
            cycles.push_back(repeated_cycles);
            instructions.push_back(repeated_instructions);
        }
        const auto cycle_limits = std::minmax_element(cycles.begin(), cycles.end());
        const auto instruction_limits =
            std::minmax_element(instructions.begin(), instructions.end());
        result.push_back({std::string(operation), *cycle_limits.first, median(cycles),
                          *cycle_limits.second, *instruction_limits.first, median(instructions),
                          *instruction_limits.second});
    }
    if (cursor != records.size())
    {
        throw mlkem_error("measurement set has extra records");
    }
    return result;
}

[[nodiscard]] const operation_summary &operation(std::span<const operation_summary> values,
                                                 std::string_view name)
{
    const auto found =
        std::find_if(values.begin(), values.end(),
                     [name](const operation_summary &value) { return value.name == name; });
    if (found == values.end())
    {
        throw mlkem_error("measurement operation is missing");
    }
    return *found;
}

[[nodiscard]] measured_plan load_measured(const std::filesystem::path &directory,
                                          const mlkem_candidate &candidate,
                                          std::string_view cycle_suffix,
                                          std::string_view multiplier)
{
    measured_plan measured;
    const std::vector<mlkem_cycle_measurement> records = parse_mlkem_cycle_measurements(
        read(directory / (candidate.id + std::string(cycle_suffix))));
    measured.project = summarize(records, candidate, multiplier);
    measured.stack = parse_stack_measurement(read(directory / (candidate.id + "-stack.json")));
    measured.size = parse_code_size_measurement(read(directory / (candidate.id + "-size.json")));
    if (!measured.stack.compiler_callchain_bound_bytes ||
        measured.stack.explicit_scratch_bytes != candidate.scratch_bytes ||
        measured.stack.runtime_stack_high_water_bytes == 0U)
    {
        throw mlkem_error("incomplete plan memory measurement");
    }
    measured.selection = {
        .plan_id = candidate.id,
        .keygen_cycles = operation(measured.project, "keygen").cycles,
        .encapsulation_cycles = operation(measured.project, "encapsulation").cycles,
        .decapsulation_cycles = operation(measured.project, "decapsulation").cycles,
        .runtime_stack_bytes = measured.stack.runtime_stack_high_water_bytes,
        .allocated_flash_bytes = measured.size.allocated_flash_bytes,
        .verified = true,
    };
    return measured;
}

void append_operations(std::ostringstream &out, std::span<const operation_summary> values)
{
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        const operation_summary &value = values[i];
        out << "      {\"name\": \"" << value.name
            << "\", \"minimum_cycles\": " << value.minimum_cycles
            << ", \"median_cycles\": " << value.cycles
            << ", \"maximum_cycles\": " << value.maximum_cycles
            << ", \"minimum_instructions\": " << value.minimum_instructions
            << ", \"median_instructions\": " << value.instructions
            << ", \"maximum_instructions\": " << value.maximum_instructions << "}"
            << (i + 1U == values.size() ? "\n" : ",\n");
    }
}

[[nodiscard]] std::string winner_json(const mlkem_candidate &candidate,
                                      const measured_plan &measurement)
{
    const std::uint64_t project_total = measurement.selection.keygen_cycles +
                                        measurement.selection.encapsulation_cycles +
                                        measurement.selection.decapsulation_cycles;
    const std::uint64_t stock_total = operation(measurement.stock, "keygen").cycles +
                                      operation(measurement.stock, "encapsulation").cycles +
                                      operation(measurement.stock, "decapsulation").cycles;
    const double difference =
        100.0 * (static_cast<double>(project_total) / static_cast<double>(stock_total) - 1.0);
    std::ostringstream out;
    out << "{\n  \"schema\": \"pqc-poly-bench/mlkem-winner-v1\",\n  \"level\": \""
        << mlkem_level_name(candidate.plan.level) << "\",\n  \"plan_id\": \"" << candidate.id
        << "\",\n  \"project\": {\n    \"operations\": [\n";
    append_operations(out, measurement.project);
    out << "    ],\n    \"complete_total_median_cycles\": " << project_total
        << "\n  },\n  \"stock\": {\n    \"operations\": [\n";
    append_operations(out, measurement.stock);
    out << "    ],\n    \"complete_total_median_cycles\": " << stock_total
        << "\n  },\n  \"project_vs_stock_percent\": " << std::fixed << std::setprecision(6)
        << difference << ",\n  \"memory\": {\n    \"explicit_scratch_bytes\": "
        << measurement.stack.explicit_scratch_bytes
        << ",\n    \"caller_working_bytes\": " << measurement.stack.caller_working_bytes
        << ",\n    \"compiler_frame_bytes\": " << measurement.stack.compiler_frame_bytes
        << ",\n    \"static_callchain_stack_bytes\": "
        << *measurement.stack.compiler_callchain_bound_bytes
        << ",\n    \"runtime_stack_high_water_bytes\": "
        << measurement.stack.runtime_stack_high_water_bytes
        << "\n  },\n  \"code_size\": {\n    \"text_bytes\": " << measurement.size.code_text_bytes
        << ",\n    \"rodata_bytes\": " << measurement.size.code_rodata_bytes
        << ",\n    \"allocated_flash_bytes\": " << measurement.size.allocated_flash_bytes
        << ",\n    \"data_bytes\": " << measurement.size.data_bytes
        << ",\n    \"bss_bytes\": " << measurement.size.bss_bytes
        << "\n  },\n  \"verified\": true\n}\n";
    return out.str();
}

void append_result(std::ostringstream &out, std::string_view name, const mlkem_candidate &candidate,
                   const measured_plan &measurement, std::string_view indent)
{
    out << indent << '"' << name << "\": {\n"
        << indent << "  \"plan_id\": \"" << candidate.id << "\",\n"
        << indent << "  \"operations\": {\n";
    for (const std::string_view operation_name : {"keygen", "encapsulation", "decapsulation"})
    {
        const operation_summary &value = operation(measurement.project, operation_name);
        out << indent << "    \"" << operation_name << "\": " << value.cycles
            << (operation_name == std::string_view("decapsulation") ? "\n" : ",\n");
    }
    out << indent << "  },\n"
        << indent
        << "  \"complete_total_median_cycles\": " << complete_cycles(measurement.selection) << ",\n"
        << indent << "  \"runtime_stack_high_water_bytes\": "
        << measurement.stack.runtime_stack_high_water_bytes << ",\n"
        << indent << "  \"explicit_scratch_bytes\": " << measurement.stack.explicit_scratch_bytes
        << ",\n"
        << indent << "  \"caller_working_bytes\": " << measurement.stack.caller_working_bytes
        << ",\n"
        << indent << "  \"allocated_flash_bytes\": " << measurement.size.allocated_flash_bytes
        << "\n"
        << indent << '}';
}

[[nodiscard]] std::string comparison_json(
    std::span<const std::pair<const mlkem_candidate *, const measured_plan *>> portable,
    std::span<const std::pair<const mlkem_candidate *, const measured_plan *>> software,
    std::span<const std::pair<const mlkem_candidate *, const measured_plan *>> staged,
    std::span<const std::pair<const mlkem_candidate *, const measured_plan *>> joint)
{
    double logarithm = 0.0;
    double staged_logarithm = 0.0;
    double largest_regression = -std::numeric_limits<double>::infinity();
    bool alternate_continuation = true;
    std::ostringstream out;
    out << "{\n  \"schema\": \"pqc-poly-bench/fqmul-final-comparison-v1\",\n"
           "  \"repository_base_sha\": "
           "\"fd8035940c25912bccb9c6d7c73611a5290fcee4\",\n"
           "  \"step1_baseline_sha\": "
           "\"e9b50a67dca6faafc8b8b2d1edcc79beff5eb99b\",\n"
           "  \"picorv32_sha\": \"a473fc8fca393771d83b0ffcf0b14db3393339d8\",\n"
           "  \"mlkem_native_sha\": \"69d24e37b8a04c6050ec55bc84a4228d7051bb4b\",\n"
           "  \"kernel_inputs\": 16,\n  \"complete_operation_inputs\": 30,\n"
           "  \"repeats\": 3,\n  \"levels\": [\n";
    for (std::size_t i = 0; i < software.size(); ++i)
    {
        out << "    {\n      \"level\": \"" << mlkem_level_name(software[i].first->plan.level)
            << "\",\n";
        append_result(out, "portable", *portable[i].first, *portable[i].second, "      ");
        out << ",\n";
        append_result(out, "software", *software[i].first, *software[i].second, "      ");
        out << ",\n";
        append_result(out, "staged", *staged[i].first, *staged[i].second, "      ");
        out << ",\n";
        append_result(out, "joint", *joint[i].first, *joint[i].second, "      ");
        out << ",\n      \"joint_vs_software_percent\": {\n";
        for (const std::string_view operation_name : {"keygen", "encapsulation", "decapsulation"})
        {
            const double baseline =
                static_cast<double>(operation(software[i].second->project, operation_name).cycles);
            const double candidate =
                static_cast<double>(operation(joint[i].second->project, operation_name).cycles);
            const double change = 100.0 * (candidate / baseline - 1.0);
            logarithm += std::log(baseline / candidate);
            largest_regression = std::max(largest_regression, change);
            out << "        \"" << operation_name << "\": " << std::fixed << std::setprecision(6)
                << change << (operation_name == std::string_view("decapsulation") ? "\n" : ",\n");
        }
        const double staged_total =
            static_cast<double>(complete_cycles(staged[i].second->selection));
        const double joint_total = static_cast<double>(complete_cycles(joint[i].second->selection));
        staged_logarithm += std::log(staged_total / joint_total);
        alternate_continuation = alternate_continuation &&
                                 staged[i].first->id != joint[i].first->id &&
                                 joint_total < staged_total;
        out << "      },\n      \"joint_vs_staged_percent\": " << std::fixed << std::setprecision(6)
            << 100.0 * (joint_total / staged_total - 1.0) << "\n    }"
            << (i + 1U == software.size() ? "\n" : ",\n");
    }
    const double geometric_speedup = std::exp(logarithm / 9.0);
    const double staged_geometric_speedup = std::exp(staged_logarithm / 3.0);
    const bool no_regression = largest_regression <= 2.0;
    const bool material_improvement = geometric_speedup >= 1.10;
    const bool joint_three_percent = staged_geometric_speedup >= 1.03;
    const bool joint_selection = joint_three_percent || alternate_continuation;
    out << "  ],\n  \"nine_point_geometric_mean_speedup\": " << std::fixed << std::setprecision(9)
        << geometric_speedup
        << ",\n  \"largest_complete_operation_regression_percent\": " << std::setprecision(6)
        << largest_regression
        << ",\n  \"joint_vs_staged_geometric_mean_speedup\": " << std::setprecision(9)
        << staged_geometric_speedup
        << ",\n  \"no_regression_gate_passed\": " << (no_regression ? "true" : "false")
        << ",\n  \"material_improvement_gate_passed\": "
        << (material_improvement ? "true" : "false")
        << ",\n  \"joint_three_percent_gate_passed\": " << (joint_three_percent ? "true" : "false")
        << ",\n  \"joint_alternate_continuation_passed\": "
        << (alternate_continuation ? "true" : "false")
        << ",\n  \"joint_selection_gate_passed\": " << (joint_selection ? "true" : "false")
        << ",\n  \"all_performance_gates_passed\": "
        << (no_regression && material_improvement && joint_selection ? "true" : "false") << "\n}\n";
    return out.str();
}

[[nodiscard]] std::string selections_json(
    std::string_view schema,
    std::span<const std::pair<const mlkem_candidate *, const measured_plan *>> values)
{
    std::ostringstream out;
    out << "{\n  \"schema\": \"" << schema << "\",\n  \"selections\": [\n";
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        out << "    {\n      \"level\": \"" << mlkem_level_name(values[i].first->plan.level)
            << "\",\n";
        append_result(out, "result", *values[i].first, *values[i].second, "      ");
        out << "\n    }" << (i + 1U == values.size() ? "\n" : ",\n");
    }
    out << "  ]\n}\n";
    return out.str();
}

template <typename Value, typename Get>
[[nodiscard]] Value synthesis_median(std::span<const synthesis_seed> seeds, Get get)
{
    std::vector<Value> values;
    values.reserve(seeds.size());
    for (const synthesis_seed &seed : seeds)
    {
        values.push_back(get(seed));
    }
    std::sort(values.begin(), values.end());
    if (values.size() != 5U)
    {
        throw mlkem_error("synthesis requires five seeds");
    }
    return values[2];
}

[[nodiscard]] std::string synthesis_json(const synthesis_measurement &baseline,
                                         const synthesis_measurement &custom,
                                         std::string_view baseline_record,
                                         std::string_view custom_record)
{
    const std::uint64_t baseline_lut = synthesis_median<std::uint64_t>(
        baseline.seeds, [](const synthesis_seed &seed) { return seed.lut4; });
    const std::uint64_t custom_lut = synthesis_median<std::uint64_t>(
        custom.seeds, [](const synthesis_seed &seed) { return seed.lut4; });
    const std::uint64_t baseline_ff = synthesis_median<std::uint64_t>(
        baseline.seeds, [](const synthesis_seed &seed) { return seed.flip_flops; });
    const std::uint64_t custom_ff = synthesis_median<std::uint64_t>(
        custom.seeds, [](const synthesis_seed &seed) { return seed.flip_flops; });
    const double baseline_frequency = synthesis_median<double>(
        baseline.seeds, [](const synthesis_seed &seed) { return seed.maximum_frequency_mhz; });
    const double custom_frequency = synthesis_median<double>(
        custom.seeds, [](const synthesis_seed &seed) { return seed.maximum_frequency_mhz; });
    bool dsp_gate = true;
    bool bram_gate = true;
    bool timing = true;
    for (std::size_t i = 0; i < baseline.seeds.size() && i < custom.seeds.size(); ++i)
    {
        if (baseline.seeds[i].seed != i + 1U || custom.seeds[i].seed != i + 1U)
        {
            throw mlkem_error("synthesis seed order changed");
        }
        dsp_gate = dsp_gate && baseline.seeds[i].dsp == custom.seeds[i].dsp;
        bram_gate = bram_gate && baseline.seeds[i].bram == custom.seeds[i].bram;
        timing = timing && custom.seeds[i].meets_50mhz;
    }
    const bool lut_gate = static_cast<unsigned __int128>(custom_lut) * 100U <=
                          static_cast<unsigned __int128>(baseline_lut) * 105U;
    const bool ff_gate = static_cast<unsigned __int128>(custom_ff) * 100U <=
                         static_cast<unsigned __int128>(baseline_ff) * 105U;
    const bool frequency_gate = custom_frequency >= 0.98 * baseline_frequency;
    const bool all_gates = lut_gate && ff_gate && dsp_gate && bram_gate && timing && frequency_gate;

    std::ostringstream out;
    out << "{\n  \"schema\": \"pqc-poly-bench/fqmul-synthesis-v1\",\n"
           "  \"baseline\": "
        << baseline_record << ",\n  \"fqmul\": " << custom_record
        << ",\n  \"comparison\": {\n    \"median_baseline_lut4\": " << baseline_lut
        << ",\n    \"median_fqmul_lut4\": " << custom_lut
        << ",\n    \"median_lut4_increase_percent\": " << std::fixed << std::setprecision(6)
        << 100.0 * (static_cast<double>(custom_lut) / static_cast<double>(baseline_lut) - 1.0)
        << ",\n    \"median_baseline_flip_flops\": " << baseline_ff
        << ",\n    \"median_fqmul_flip_flops\": " << custom_ff
        << ",\n    \"median_flip_flop_increase_percent\": "
        << 100.0 * (static_cast<double>(custom_ff) / static_cast<double>(baseline_ff) - 1.0)
        << ",\n    \"median_baseline_frequency_mhz\": " << baseline_frequency
        << ",\n    \"median_fqmul_frequency_mhz\": " << custom_frequency
        << ",\n    \"median_frequency_ratio\": " << custom_frequency / baseline_frequency
        << ",\n    \"lut_gate_passed\": " << (lut_gate ? "true" : "false")
        << ",\n    \"flip_flop_gate_passed\": " << (ff_gate ? "true" : "false")
        << ",\n    \"dsp_gate_passed\": " << (dsp_gate ? "true" : "false")
        << ",\n    \"bram_gate_passed\": " << (bram_gate ? "true" : "false")
        << ",\n    \"all_seeds_meet_50mhz\": " << (timing ? "true" : "false")
        << ",\n    \"median_frequency_gate_passed\": " << (frequency_gate ? "true" : "false")
        << ",\n    \"all_gates_passed\": " << (all_gates ? "true" : "false") << "\n  }\n}\n";
    return out.str();
}

struct instruction_scan
{
    std::size_t executable_words{0};
    std::size_t fqmul_words{0};
};

[[nodiscard]] instruction_scan scan_instructions(std::string_view text, bool expect_fqmul)
{
    const std::array<std::string_view, 6> approved{"pqc_mlkem_ntt",          "pqc_mlkem_intt",
                                                   "pqc_mlkem_mulcache_one", "pqc_mlkem_mulcache",
                                                   "pqc_mlkem_basemul",      "pqc_mlkem_tomont"};
    std::istringstream input{std::string(text)};
    std::string line;
    std::string function;
    instruction_scan scan;
    while (std::getline(input, line))
    {
        const std::size_t left = line.find('<');
        const std::size_t header = line.find(">:");
        if (left != std::string::npos && header != std::string::npos && left < header)
        {
            function = line.substr(left + 1U, header - left - 1U);
            if (function.starts_with("__div") || function.starts_with("__udiv") ||
                function.starts_with("__mod") || function.starts_with("__umod"))
            {
                throw mlkem_error("software division helper found in mlkem firmware");
            }
            continue;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos)
        {
            continue;
        }
        std::string_view instruction(line);
        instruction.remove_prefix(colon + 1U);
        const std::size_t first = instruction.find_first_not_of(" \t");
        if (first == std::string_view::npos)
        {
            continue;
        }
        instruction.remove_prefix(first);
        const std::size_t end = instruction.find_first_of(" \t");
        const std::string_view word_text = instruction.substr(0, end);
        if (word_text.size() != 8U)
        {
            continue;
        }
        std::uint32_t word = 0;
        const auto parsed =
            std::from_chars(word_text.data(), word_text.data() + word_text.size(), word, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != word_text.data() + word_text.size())
        {
            throw mlkem_error("invalid raw instruction word");
        }
        ++scan.executable_words;
        if ((word & UINT32_C(0xfe00707f)) == UINT32_C(0x0000000b))
        {
            ++scan.fqmul_words;
            const bool allowed = std::any_of(
                approved.begin(), approved.end(),
                [&function](std::string_view name)
                { return function == name || function.starts_with(std::string(name) + '.'); });
            if (!allowed)
            {
                throw mlkem_error("fqmul word is outside an approved function");
            }
        }
        const std::uint32_t decoded = word & UINT32_C(0xfe00707f);
        if (decoded == UINT32_C(0x02004033) || decoded == UINT32_C(0x02005033) ||
            decoded == UINT32_C(0x02006033) || decoded == UINT32_C(0x02007033))
        {
            throw mlkem_error("division instruction found in mlkem firmware");
        }
        const bool indirect_call =
            (word & UINT32_C(0x7f)) == UINT32_C(0x67) && ((word >> 7U) & UINT32_C(0x1f)) == 1U;
        if (indirect_call && function != "pqc_call_measured")
        {
            throw mlkem_error("unexpected indirect call in mlkem firmware");
        }
    }
    if ((scan.fqmul_words != 0U) != expect_fqmul)
    {
        throw mlkem_error("custom instruction presence does not match plan");
    }
    return scan;
}

[[nodiscard]] std::string disassembly_json(
    const std::filesystem::path &directory,
    std::span<const std::pair<const mlkem_candidate *, const measured_plan *>> software,
    std::span<const mlkem_candidate> candidates)
{
    std::ostringstream out;
    out << "{\n  \"schema\": \"pqc-poly-bench/fqmul-disassembly-v1\",\n"
           "  \"mask\": \"0xfe00707f\",\n  \"match\": \"0x0000000b\",\n"
           "  \"software_winners\": [\n";
    for (std::size_t i = 0; i < software.size(); ++i)
    {
        const mlkem_candidate &candidate = *software[i].first;
        const instruction_scan scan =
            scan_instructions(read(directory / (candidate.id + ".dis")), false);
        out << "    {\"plan_id\": \"" << candidate.id
            << "\", \"executable_words\": " << scan.executable_words << ", \"fqmul_matches\": 0}"
            << (i + 1U == software.size() ? "\n" : ",\n");
    }
    out << "  ],\n  \"custom_plans\": [\n";
    std::size_t custom_index = 0;
    for (const mlkem_candidate &candidate : candidates)
    {
        if (!candidate.legal || candidate.plan.instruction != mlkem_instruction::fqmul)
        {
            continue;
        }
        const instruction_scan scan =
            scan_instructions(read(directory / (candidate.id + ".dis")), true);
        out << "    {\"plan_id\": \"" << candidate.id
            << "\", \"executable_words\": " << scan.executable_words
            << ", \"fqmul_matches\": " << scan.fqmul_words << '}';
        ++custom_index;
        out << (custom_index == 72U ? "\n" : ",\n");
    }
    if (custom_index != 72U)
    {
        throw mlkem_error("custom disassembly set is incomplete");
    }
    out << "  ],\n  \"division_or_remainder_matches\": 0,\n"
           "  \"software_division_helpers\": 0,\n"
           "  \"unexpected_indirect_calls\": 0,\n"
           "  \"approved_functions_only\": true\n}\n";
    return out.str();
}

[[nodiscard]] std::string formal_json(const std::filesystem::path &directory)
{
    for (const std::string_view task : {"pcpi", "m", "rvfi", "rvfi_cover"})
    {
        const std::string status = read(directory / ("fqmul_" + std::string(task)) / "status");
        if (!status.starts_with("PASS"))
        {
            throw mlkem_error("fqmul formal task did not pass");
        }
    }
    const std::array<std::string_view, 8> harnesses{"conversion",      "fallback", "upper",
                                                    "wrapper",         "callsite", "butterfly",
                                                    "butterfly-value", "base"};
    for (const std::string_view harness : harnesses)
    {
        const std::string result = read(directory / ("cbmc-" + std::string(harness) + ".json"));
        if (result.find("\"program\": \"CBMC 6.10.0 (cbmc-6.10.0)\"") == std::string::npos ||
            result.find("\"cProverStatus\": \"success\"") == std::string::npos ||
            result.find("\"status\": \"FAILURE\"") != std::string::npos)
        {
            throw mlkem_error("fqmul cbmc task did not pass");
        }
    }
    return R"pqc({
  "schema": "pqc-poly-bench/fqmul-formal-v1",
  "sby": {
    "pcpi": "pass",
    "standard_m_noninterference": "pass",
    "rvfi_bounded": "pass",
    "rvfi_nonvacuity_cover": "pass"
  },
  "properties": [
    "exact matching decode",
    "no response for nonmatching words",
    "ready exactly four cycles after acceptance",
    "no earlier ready",
    "wait held during work",
    "write asserted with result",
    "result equals exact mathematical equation",
    "ignored upper source halves cannot affect result",
    "reset cancels work",
    "no post reset stale response",
    "back to back requests cannot mix operands",
    "standard m results unchanged",
    "custom and m decode mutually exclusive",
    "custom and divider claims disjoint",
    "no memory side effect",
    "custom rvfi retirement"
  ],
  "cbmc_version": "6.10.0",
  "cbmc_harnesses": [
    "conversion",
    "fallback",
    "upper",
    "wrapper",
    "callsite",
    "butterfly",
    "butterfly-value",
    "base"
  ],
  "cbmc_checks": [
    "bounds",
    "pointers",
    "signed overflow",
    "unsigned overflow",
    "conversions",
    "shift legality",
    "division by zero",
    "unwinding assertions"
  ]
}
)pqc";
}

void finalize_fqmul(const mlkem_request &request, const std::filesystem::path &software_directory,
                    const std::filesystem::path &fqmul_directory,
                    const std::filesystem::path &synthesis_directory,
                    const std::filesystem::path &disassembly_directory,
                    const std::filesystem::path &formal_directory,
                    const std::filesystem::path &output_directory)
{
    const std::vector<mlkem_plan> plans = enumerate_mlkem_plans();
    std::vector<mlkem_candidate> candidates;
    std::vector<measured_plan> software_measurements;
    std::vector<measured_plan> custom_measurements;
    std::string raw_measurements;
    candidates.reserve(plans.size());
    software_measurements.reserve(72);
    custom_measurements.reserve(72);
    for (const mlkem_plan &plan : plans)
    {
        candidates.push_back(analyze_mlkem_plan(request, plan));
        const mlkem_candidate &candidate = candidates.back();
        if (!candidate.legal || !check_mlkem_plan(request, candidate).empty())
        {
            throw mlkem_error("fqmul candidate set contains an illegal plan");
        }
        if (plan.instruction == mlkem_instruction::none)
        {
            software_measurements.push_back(
                load_measured(software_directory, candidate, "-project.jsonl", "project"));
        }
        else
        {
            const std::string cycles = read(fqmul_directory / (candidate.id + "-fqmul.jsonl"));
            raw_measurements += cycles;
            custom_measurements.push_back(
                load_measured(fqmul_directory, candidate, "-fqmul.jsonl", "fqmul"));
            const measured_plan &measured = custom_measurements.back();
            std::ostringstream resource;
            resource << "{\"schema\":\"pqc-poly-bench/fqmul-resource-v1\",\"plan_id\":\""
                     << candidate.id
                     << "\",\"explicit_scratch_bytes\":" << measured.stack.explicit_scratch_bytes
                     << ",\"caller_working_bytes\":" << measured.stack.caller_working_bytes
                     << ",\"compiler_frame_bytes\":" << measured.stack.compiler_frame_bytes
                     << ",\"static_callchain_stack_bytes\":"
                     << *measured.stack.compiler_callchain_bound_bytes
                     << ",\"runtime_stack_high_water_bytes\":"
                     << measured.stack.runtime_stack_high_water_bytes
                     << ",\"text_bytes\":" << measured.size.code_text_bytes
                     << ",\"rodata_bytes\":" << measured.size.code_rodata_bytes
                     << ",\"allocated_flash_bytes\":" << measured.size.allocated_flash_bytes
                     << ",\"data_bytes\":" << measured.size.data_bytes
                     << ",\"bss_bytes\":" << measured.size.bss_bytes << "}\n";
            raw_measurements += resource.str();
        }
    }
    if (candidates.size() != 144U || software_measurements.size() != 72U ||
        custom_measurements.size() != 72U)
    {
        throw mlkem_error("fqmul experiment count changed");
    }

    std::vector<mlkem_measurement> software_selection;
    std::vector<mlkem_measurement> custom_selection;
    for (const measured_plan &measurement : software_measurements)
    {
        software_selection.push_back(measurement.selection);
    }
    for (const measured_plan &measurement : custom_measurements)
    {
        custom_selection.push_back(measurement.selection);
    }
    const auto find_candidate = [&candidates](std::string_view id) -> const mlkem_candidate &
    {
        const auto found =
            std::find_if(candidates.begin(), candidates.end(),
                         [id](const mlkem_candidate &value) { return value.id == id; });
        if (found == candidates.end())
        {
            throw mlkem_error("selected candidate is missing");
        }
        return *found;
    };
    const auto find_measurement = [](std::span<const measured_plan> values,
                                     std::string_view id) -> const measured_plan &
    {
        const auto found = std::find_if(values.begin(), values.end(),
                                        [id](const measured_plan &value)
                                        { return value.selection.plan_id == id; });
        if (found == values.end())
        {
            throw mlkem_error("selected measurement is missing");
        }
        return *found;
    };

    using selected_plan = std::pair<const mlkem_candidate *, const measured_plan *>;
    std::array<mlkem_candidate, 3> portable_candidates{};
    std::array<measured_plan, 3> portable_measurements{};
    std::vector<selected_plan> portable;
    std::vector<selected_plan> software;
    std::vector<selected_plan> staged;
    std::vector<selected_plan> joint;
    constexpr std::array levels{mlkem_level::mlkem512, mlkem_level::mlkem768,
                                mlkem_level::mlkem1024};
    for (std::size_t i = 0; i < levels.size(); ++i)
    {
        const mlkem_level level = levels[i];
        mlkem_candidate &portable_candidate = portable_candidates[i];
        portable_candidate.plan.level = level;
        portable_candidate.id = "mlk" + std::string(mlkem_level_name(level)) + "_portable";
        portable_candidate.scratch_bytes = mlkem_k(level) * 256U;
        portable_measurements[i] =
            load_measured(software_directory, portable_candidate, "-project.jsonl", "project");
        portable.emplace_back(&portable_candidate, &portable_measurements[i]);
        const mlkem_measurement &software_winner =
            select_measured_mlkem_plan(level, candidates, software_selection);
        const mlkem_measurement &joint_winner = select_measured_mlkem_plan(
            level, candidates, custom_selection, mlkem_instruction::fqmul);
        const mlkem_candidate &software_candidate = find_candidate(software_winner.plan_id);
        mlkem_plan staged_plan = software_candidate.plan;
        staged_plan.instruction = mlkem_instruction::fqmul;
        const mlkem_candidate &staged_candidate = find_candidate(mlkem_plan_id(staged_plan));
        const mlkem_candidate &joint_candidate = find_candidate(joint_winner.plan_id);
        software.emplace_back(&software_candidate,
                              &find_measurement(software_measurements, software_candidate.id));
        staged.emplace_back(&staged_candidate,
                            &find_measurement(custom_measurements, staged_candidate.id));
        joint.emplace_back(&joint_candidate,
                           &find_measurement(custom_measurements, joint_candidate.id));
    }

    const std::string baseline_synthesis = read(synthesis_directory / "baseline-synthesis.json");
    const std::string custom_synthesis = read(synthesis_directory / "fqmul-synthesis.json");
    const std::string synthesis = synthesis_json(parse_synthesis_measurement(baseline_synthesis),
                                                 parse_synthesis_measurement(custom_synthesis),
                                                 baseline_synthesis, custom_synthesis);
    const std::string formal = formal_json(formal_directory);
    const std::string disassembly = disassembly_json(disassembly_directory, software, candidates);
    const std::array<std::pair<std::filesystem::path, std::string>, 8> outputs{
        std::pair{output_directory / "fqmul-formal.json", formal},
        std::pair{output_directory / "fqmul-disassembly.json", disassembly},
        std::pair{output_directory / "fqmul-candidates.json",
                  serialize_mlkem_candidates(candidates)},
        std::pair{output_directory / "fqmul-measurements.jsonl", raw_measurements},
        std::pair{output_directory / "fqmul-joint-winners.json",
                  selections_json("pqc-poly-bench/fqmul-joint-winners-v1", joint)},
        std::pair{output_directory / "fqmul-staged-results.json",
                  selections_json("pqc-poly-bench/fqmul-staged-results-v1", staged)},
        std::pair{output_directory / "fqmul-synthesis.json", synthesis},
        std::pair{output_directory / "fqmul-final-comparison.json",
                  comparison_json(portable, software, staged, joint)}};
    std::filesystem::create_directories(output_directory);
    for (const auto &[path, value] : outputs)
    {
        write(path, value);
    }
}

void finalize(const mlkem_request &request, const std::filesystem::path &directory)
{
    const std::vector<mlkem_plan> plans = enumerate_mlkem_plans();
    std::vector<mlkem_candidate> candidates;
    std::vector<measured_plan> measurements;
    candidates.reserve(plans.size());
    measurements.reserve(72);
    for (const mlkem_plan &plan : plans)
    {
        candidates.push_back(analyze_mlkem_plan(request, plan));
        const mlkem_candidate &candidate = candidates.back();
        if (!candidate.legal || plan.instruction != mlkem_instruction::none)
        {
            continue;
        }
        measured_plan measured = load_measured(directory, candidate, "-project.jsonl", "project");
        const std::vector<mlkem_cycle_measurement> stock =
            parse_mlkem_cycle_measurements(read(directory / (candidate.id + "-stock.jsonl")));
        measured.stock = summarize(stock, candidate, "stock");
        measurements.push_back(std::move(measured));
    }

    for (const mlkem_level level :
         {mlkem_level::mlkem512, mlkem_level::mlkem768, mlkem_level::mlkem1024})
    {
        mlkem_candidate portable;
        portable.plan.level = level;
        portable.id = "mlk" + std::string(mlkem_level_name(level)) + "_portable";
        portable.scratch_bytes = mlkem_k(level) * 256U;
        const std::vector<mlkem_cycle_measurement> project =
            parse_mlkem_cycle_measurements(read(directory / (portable.id + "-project.jsonl")));
        const std::vector<mlkem_cycle_measurement> stock =
            parse_mlkem_cycle_measurements(read(directory / (portable.id + "-stock.jsonl")));
        static_cast<void>(summarize(project, portable, "project"));
        static_cast<void>(summarize(stock, portable, "stock"));
        const stack_measurement stack =
            parse_stack_measurement(read(directory / (portable.id + "-stack.json")));
        static_cast<void>(
            parse_code_size_measurement(read(directory / (portable.id + "-size.json"))));
        if (!stack.compiler_callchain_bound_bytes ||
            stack.explicit_scratch_bytes != portable.scratch_bytes ||
            stack.runtime_stack_high_water_bytes == 0U)
        {
            throw mlkem_error("incomplete portable memory measurement");
        }
    }

    std::vector<std::pair<std::filesystem::path, std::string>> outputs;
    for (const mlkem_level level :
         {mlkem_level::mlkem512, mlkem_level::mlkem768, mlkem_level::mlkem1024})
    {
        std::vector<mlkem_measurement> selection;
        for (const measured_plan &measurement : measurements)
        {
            selection.push_back(measurement.selection);
        }
        const mlkem_measurement &winner = select_measured_mlkem_plan(level, candidates, selection);
        const auto found = std::find_if(measurements.begin(), measurements.end(),
                                        [&winner](const measured_plan &measurement) {
                                            return measurement.selection.plan_id == winner.plan_id;
                                        });
        const auto candidate = std::find_if(candidates.begin(), candidates.end(),
                                            [&winner](const mlkem_candidate &value)
                                            { return value.id == winner.plan_id; });
        if (found == measurements.end() || candidate == candidates.end())
        {
            throw mlkem_error("selected measurement is missing");
        }
        const std::uint64_t project_total =
            winner.keygen_cycles + winner.encapsulation_cycles + winner.decapsulation_cycles;
        const std::uint64_t stock_total = operation(found->stock, "keygen").cycles +
                                          operation(found->stock, "encapsulation").cycles +
                                          operation(found->stock, "decapsulation").cycles;
        if (static_cast<unsigned __int128>(project_total) * 100U >
            static_cast<unsigned __int128>(stock_total) * 102U)
        {
            throw mlkem_error("project multiplier exceeds stock by more than two percent");
        }
        outputs.emplace_back(
            directory / ("mlkem" + std::string(mlkem_level_name(level)) + "-software-winner.json"),
            winner_json(*candidate, *found));
    }
    for (const auto &[path, value] : outputs)
    {
        write(path, value);
    }
}

}

int mlkem_run(int argc, char **argv, std::ostream &output, std::ostream &error)
{
    try
    {
        if (argc == 9 && std::string_view(argv[1]) == "--finalize-fqmul")
        {
            finalize_fqmul(parse_mlkem_request(read(argv[2])), argv[3], argv[4], argv[5], argv[6],
                           argv[7], argv[8]);
            output << "selected staged and joint fqmul results for all levels\n";
            return 0;
        }
        if (argc == 4 && std::string_view(argv[1]) == "--finalize")
        {
            finalize(parse_mlkem_request(read(argv[2])), argv[3]);
            output << "selected complete software winners for all levels\n";
            return 0;
        }
        if (argc == 2 &&
            (std::string_view(argv[1]) == "-h" || std::string_view(argv[1]) == "--help"))
        {
            output << "usage: pqc-poly-mlkem spec [-o out]\n"
                      "       pqc-poly-mlkem --finalize spec results\n"
                      "       pqc-poly-mlkem --finalize-fqmul spec software fqmul synthesis "
                      "disassembly formal out\n";
            return 0;
        }
        if (argc != 2 && argc != 4)
        {
            throw mlkem_error("usage: pqc-poly-mlkem spec [-o out]");
        }
        std::filesystem::path out = "out";
        if (argc == 4)
        {
            if (std::string_view(argv[2]) != "-o" || std::string_view(argv[3]).empty())
            {
                throw mlkem_error("usage: pqc-poly-mlkem spec [-o out]");
            }
            out = argv[3];
        }
        const mlkem_request request = parse_mlkem_request(read(argv[1]));
        const std::vector<mlkem_plan> plans = enumerate_mlkem_plans();
        std::filesystem::create_directories(out / "backends");
        for (const mlkem_level level :
             {mlkem_level::mlkem512, mlkem_level::mlkem768, mlkem_level::mlkem1024})
        {
            std::vector<mlkem_candidate> candidates;
            candidates.reserve(48);
            for (const mlkem_plan &plan : plans)
            {
                if (plan.level != level)
                {
                    continue;
                }
                candidates.push_back(analyze_mlkem_plan(request, plan));
                if (candidates.back().legal)
                {
                    write(out / "backends" / (candidates.back().id + ".c"),
                          generate_mlkem_backend(request, candidates.back()));
                }
            }
            const std::string name =
                "mlkem" + std::string(mlkem_level_name(level)) + "-candidates.json";
            write(out / name, serialize_mlkem_candidates(candidates));
        }
        output << "generated 144 plans including 72 software and 72 fqmul backends\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        error << "error: " << exception.what() << '\n';
        return 2;
    }
}

}
