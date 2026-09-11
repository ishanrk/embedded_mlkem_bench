#include <verilated.h>

#include "Vpqc_picorv32_sim_top.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// runs bare metal firmware inside the Verilator processor model
namespace
{

struct options
{
    std::string output;
    std::string disassembly;
    std::string variant;
    std::string level;
    unsigned inputs = 30;
    bool smoke = false;
};

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "firmware simulation failed " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message)
{
    if (!condition)
    {
        fail(message);
    }
}

[[nodiscard]] options parse_options(int argc, char **argv)
{
    options result;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument{argv[index]};
        if (argument.starts_with("+firmware="))
        {
            continue;
        }
        if (argument == "--smoke")
        {
            result.smoke = true;
            continue;
        }
        if (index + 1 >= argc)
        {
            fail("missing option value");
        }
        const std::string value = argv[++index];
        if (argument == "--output")
        {
            result.output = value;
        }
        else if (argument == "--disassembly")
        {
            result.disassembly = value;
        }
        else if (argument == "--variant")
        {
            result.variant = value;
        }
        else if (argument == "--level")
        {
            result.level = value;
        }
        else if (argument == "--inputs")
        {
            const auto parsed = std::from_chars(value.data(),
                                                value.data() + value.size(),
                                                result.inputs);
            if (parsed.ec != std::errc{} ||
                parsed.ptr != value.data() + value.size() || result.inputs == 0U)
            {
                fail("invalid input count");
            }
        }
        else
        {
            fail("unknown option");
        }
    }
    if (result.smoke)
    {
        return result;
    }
    require(!result.output.empty() && !result.disassembly.empty(),
            "missing output path");
    require(result.variant == "baseline" || result.variant == "fqmul" ||
                result.variant == "red32" || result.variant == "fsri",
            "invalid variant");
    require(result.level == "512" || result.level == "768" ||
                result.level == "1024",
            "invalid level");
    return result;
}

[[nodiscard]] std::string read_file(const std::string &path)
{
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "cannot open disassembly");
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::array<std::size_t, 3> instruction_counts(
    const std::string &path)
{
    // checks which custom encodings were emitted in the firmware
    std::array<std::size_t, 3> counts{};
    std::string_view text = read_file(path);
    while (!text.empty())
    {
        const std::size_t line_end = text.find('\n');
        const std::string_view line = text.substr(0, line_end);
        const std::size_t colon = line.find(':');
        if (colon != std::string_view::npos)
        {
            std::string_view word = line.substr(colon + 1U);
            const std::size_t begin = word.find_first_not_of(" \t");
            if (begin != std::string_view::npos)
            {
                word.remove_prefix(begin);
                const std::size_t end = word.find_first_of(" \t");
                word = word.substr(0, end);
                if (word.size() == 8U)
                {
                    std::uint32_t value = 0;
                    const auto parsed = std::from_chars(
                        word.data(), word.data() + word.size(), value, 16);
                    if (parsed.ec == std::errc{} &&
                        parsed.ptr == word.data() + word.size())
                    {
                        if ((value & UINT32_C(0xfe00707f)) ==
                            UINT32_C(0x0000000b))
                        {
                            ++counts[0];
                        }
                        if ((value & UINT32_C(0xfe00707f)) ==
                            UINT32_C(0x0000100b))
                        {
                            ++counts[1];
                        }
                        if ((value & UINT32_C(0xc000707f)) ==
                            UINT32_C(0x0000200b))
                        {
                            ++counts[2];
                        }
                    }
                }
            }
        }
        if (line_end == std::string_view::npos)
        {
            break;
        }
        text.remove_prefix(line_end + 1U);
    }
    return counts;
}

void validate_instruction_counts(const options &settings,
                                 const std::array<std::size_t, 3> &counts)
{
    const std::array<std::string_view, 3> names{"fqmul", "red32", "fsri"};
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        const bool expected = settings.variant == names[index];
        require(expected ? counts[index] != 0U : counts[index] == 0U,
                "custom instruction count does not match variant");
    }
}

void tick(Vpqc_picorv32_sim_top &model)
{
    model.clk = 0;
    model.eval();
    model.clk = 1;
    model.eval();
}

[[nodiscard]] std::uint64_t median(std::vector<std::uint64_t> values)
{
    require(!values.empty(), "missing cycle samples");
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2U;
    return values.size() % 2U != 0U
               ? values[middle]
               : (values[middle - 1U] + values[middle]) / 2U;
}

void write_array(std::ofstream &output,
                 const std::vector<std::uint64_t> &values)
{
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        if (index != 0U)
        {
            output << ',';
        }
        output << values[index];
    }
    output << ']';
}

void write_result(const options &settings,
                  const std::array<std::size_t, 3> &counts,
                  const std::vector<std::uint64_t> &begins,
                  const std::vector<std::uint64_t> &ends,
                  const std::vector<std::uint32_t> &status)
{
    // separates marker intervals into the three complete MLKEM operations
    constexpr unsigned repeats = 3;
    const std::size_t samples_per_operation =
        static_cast<std::size_t>(settings.inputs) * repeats;
    const std::size_t expected_markers = 1U + 3U * samples_per_operation;
    require(begins.size() == expected_markers && ends.size() == expected_markers,
            "benchmark marker count changed");
    require(ends[0] >= begins[0], "invalid marker calibration");
    const std::uint64_t overhead = ends[0] - begins[0];

    std::array<std::vector<std::uint64_t>, 3> samples;
    for (std::size_t operation = 0; operation < samples.size(); ++operation)
    {
        samples[operation].reserve(samples_per_operation);
        for (std::size_t sample = 0; sample < samples_per_operation; ++sample)
        {
            const std::size_t marker =
                1U + operation * samples_per_operation + sample;
            require(ends[marker] >= begins[marker] + overhead,
                    "invalid cycle interval");
            const std::uint64_t cycles =
                ends[marker] - begins[marker] - overhead;
            samples[operation].push_back(cycles);
            if (sample % repeats != 0U)
            {
                require(cycles == samples[operation][sample - 1U],
                        "repeat cycle count changed");
            }
        }
    }

    std::uint32_t checksum = 0;
    bool have_checksum = false;
    for (std::size_t index = 0; index + 1U < status.size(); ++index)
    {
        if (status[index] == UINT32_C(0x48415348))
        {
            checksum = status[index + 1U];
            have_checksum = true;
        }
    }
    require(have_checksum && !status.empty() && status.back() == 0U,
            "firmware correctness status missing");

    const std::array<std::uint64_t, 3> medians{
        median(samples[0]), median(samples[1]), median(samples[2])};
    std::ofstream output(settings.output, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "cannot open result output");
    output << "{\n"
           << "  \"schema\": \"pqc-poly-bench/mlkem-run-v2\",\n"
           << "  \"variant\": \"" << settings.variant << "\",\n"
           << "  \"level\": \"" << settings.level << "\",\n"
           << "  \"simulator\": \"Verilator PicoRV32 RTL\",\n"
           << "  \"operation_inputs\": " << settings.inputs << ",\n"
           << "  \"repeats\": " << repeats << ",\n"
           << "  \"marker_overhead_cycles\": " << overhead << ",\n"
           << "  \"verified\": true,\n"
           << "  \"output_checksum\": " << checksum << ",\n"
           << "  \"custom_instruction_count\": "
           << counts[settings.variant == "fqmul" ? 0U
                     : settings.variant == "red32" ? 1U
                                                   : 2U]
           << ",\n"
           << "  \"cycles\": {\n"
           << "    \"keygen\": {\"median\": " << medians[0]
           << ", \"samples\": ";
    write_array(output, samples[0]);
    output << "},\n    \"encapsulation\": {\"median\": " << medians[1]
           << ", \"samples\": ";
    write_array(output, samples[1]);
    output << "},\n    \"decapsulation\": {\"median\": " << medians[2]
           << ", \"samples\": ";
    write_array(output, samples[2]);
    output << "},\n    \"total\": " << medians[0] + medians[1] + medians[2]
           << "\n  }\n}\n";
}

void simulate(const options &settings)
{
    std::array<std::size_t, 3> counts{};
    if (!settings.smoke)
    {
        counts = instruction_counts(settings.disassembly);
        validate_instruction_counts(settings, counts);
    }

    // clock reset then run until firmware writes the terminate address
    Vpqc_picorv32_sim_top model;
    std::vector<std::uint64_t> begins;
    std::vector<std::uint64_t> ends;
    std::vector<std::uint32_t> status;

    model.resetn = 0;
    for (unsigned cycle = 0; cycle < 8U; ++cycle)
    {
        tick(model);
    }
    model.resetn = 1;

    constexpr std::uint64_t cycle_limit = UINT64_C(5000000000);
    for (std::uint64_t cycle = 0;
         cycle < cycle_limit && model.trap == 0 && model.terminate == 0;
         ++cycle)
    {
        tick(model);
        if (model.benchmark_begin != 0)
        {
            begins.push_back(model.cycle_count);
        }
        if (model.benchmark_end != 0)
        {
            ends.push_back(model.cycle_count);
        }
        if (model.status_valid != 0)
        {
            status.push_back(model.status_value);
        }
    }

    require(model.trap == 0, "processor trapped");
    require(model.terminate != 0, "firmware did not terminate");
    if (settings.smoke)
    {
        require(!status.empty() && status.back() == 0U,
                "smoke status missing");
        return;
    }
    write_result(settings, counts, begins, ends, status);
}

}

int main(int argc, char **argv)
{
    Verilated::commandArgs(argc, argv);
    simulate(parse_options(argc, argv));
}
