#include "pqc_poly/mlkem_codegen.hpp"
#include "pqc_poly/target_measurement.hpp"

#include <algorithm>
#include <array>
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
    std::uint64_t cycles{0};
    std::uint64_t instructions{0};
};

struct measured_plan
{
    mlkem_measurement selection{};
    std::vector<operation_summary> project{};
    std::vector<operation_summary> stock{};
    stack_measurement stack{};
    code_size_measurement size{};
};

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
        "forward_ntt", "inverse_ntt", "mulcache", base, "poly_tomont", "keygen",
        "encapsulation", "decapsulation"};
    std::vector<operation_summary> result;
    std::size_t cursor = 0;
    result.reserve(operations.size());
    for (const std::string_view operation : operations)
    {
        const unsigned inputs = operation == "keygen" || operation == "encapsulation" ||
                                        operation == "decapsulation"
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
                if (repeat != 0U &&
                    (record.calibrated_cycles != repeated_cycles ||
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
        result.push_back({std::string(operation), median(std::move(cycles)),
                          median(std::move(instructions))});
    }
    if (cursor != records.size())
    {
        throw mlkem_error("measurement set has extra records");
    }
    return result;
}

[[nodiscard]] const operation_summary &operation(
    std::span<const operation_summary> values, std::string_view name)
{
    const auto found = std::find_if(values.begin(), values.end(),
                                    [name](const operation_summary &value)
                                    { return value.name == name; });
    if (found == values.end())
    {
        throw mlkem_error("measurement operation is missing");
    }
    return *found;
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
    for (std::size_t i = 0; i < measurement.project.size(); ++i)
    {
        const operation_summary &value = measurement.project[i];
        out << "      {\"name\": \"" << value.name << "\", \"median_cycles\": "
            << value.cycles << ", \"median_instructions\": " << value.instructions << "}"
            << (i + 1U == measurement.project.size() ? "\n" : ",\n");
    }
    out << "    ],\n    \"complete_total_median_cycles\": " << project_total
        << "\n  },\n  \"stock\": {\n    \"keygen_median_cycles\": "
        << operation(measurement.stock, "keygen").cycles
        << ",\n    \"encapsulation_median_cycles\": "
        << operation(measurement.stock, "encapsulation").cycles
        << ",\n    \"decapsulation_median_cycles\": "
        << operation(measurement.stock, "decapsulation").cycles
        << ",\n    \"complete_total_median_cycles\": " << stock_total
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
        measured_plan measured;
        const std::vector<mlkem_cycle_measurement> project = parse_mlkem_cycle_measurements(
            read(directory / (candidate.id + "-project.jsonl")));
        const std::vector<mlkem_cycle_measurement> stock = parse_mlkem_cycle_measurements(
            read(directory / (candidate.id + "-stock.jsonl")));
        measured.project = summarize(project, candidate, "project");
        measured.stock = summarize(stock, candidate, "stock");
        measured.stack =
            parse_stack_measurement(read(directory / (candidate.id + "-stack.json")));
        measured.size =
            parse_code_size_measurement(read(directory / (candidate.id + "-size.json")));
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
        measurements.push_back(std::move(measured));
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
                                        [&winner](const measured_plan &measurement)
                                        { return measurement.selection.plan_id == winner.plan_id; });
        const auto candidate = std::find_if(candidates.begin(), candidates.end(),
                                            [&winner](const mlkem_candidate &value)
                                            { return value.id == winner.plan_id; });
        if (found == measurements.end() || candidate == candidates.end())
        {
            throw mlkem_error("selected measurement is missing");
        }
        const std::uint64_t project_total = winner.keygen_cycles + winner.encapsulation_cycles +
                                            winner.decapsulation_cycles;
        const std::uint64_t stock_total = operation(found->stock, "keygen").cycles +
                                          operation(found->stock, "encapsulation").cycles +
                                          operation(found->stock, "decapsulation").cycles;
        if (static_cast<unsigned __int128>(project_total) * 100U >
            static_cast<unsigned __int128>(stock_total) * 102U)
        {
            throw mlkem_error("project multiplier exceeds stock by more than two percent");
        }
        outputs.emplace_back(directory / ("mlkem" + std::string(mlkem_level_name(level)) +
                                           "-software-winner.json"),
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
                      "       pqc-poly-mlkem --finalize spec results\n";
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
        output << "generated 144 plans including 72 software backends\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        error << "error: " << exception.what() << '\n';
        return 2;
    }
}

}
