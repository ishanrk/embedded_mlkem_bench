#include <verilated.h>

#include "pqc_poly/target_measurement.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(PQC_PCPI_UNIT)
#include "Vpqc_pcpi_mlkem.h"
#else
#include "Vpqc_picorv32_sim_top.h"
#endif

namespace
{

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "picorv32 simulation failed: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message)
{
    if (!condition)
    {
        fail(message);
    }
}

#if defined(PQC_PCPI_UNIT)

void tick(Vpqc_pcpi_mlkem &model)
{
    model.clk = 0;
    model.eval();
    model.clk = 1;
    model.eval();
}

[[nodiscard]] std::uint32_t oracle(std::uint32_t left, std::uint32_t right, std::uint32_t funct3)
{
    const std::uint64_t unsigned_product =
        static_cast<std::uint64_t>(left) * static_cast<std::uint64_t>(right);
    const std::int64_t signed_product = static_cast<std::int64_t>(static_cast<std::int32_t>(left)) *
                                        static_cast<std::int64_t>(static_cast<std::int32_t>(right));
    const std::int64_t mixed_product = static_cast<std::int64_t>(static_cast<std::int32_t>(left)) *
                                       static_cast<std::int64_t>(right);
    switch (funct3)
    {
        case 0:
            return static_cast<std::uint32_t>(unsigned_product);
        case 1:
            return static_cast<std::uint32_t>(static_cast<std::uint64_t>(signed_product) >> 32U);
        case 2:
            return static_cast<std::uint32_t>(static_cast<std::uint64_t>(mixed_product) >> 32U);
        case 3:
            return static_cast<std::uint32_t>(unsigned_product >> 32U);
        default:
            fail("invalid oracle operation");
    }
}

void run_request(Vpqc_pcpi_mlkem &model, std::uint32_t left, std::uint32_t right,
                 std::uint32_t funct3)
{
    model.pcpi_valid = 1;
    model.pcpi_insn = UINT32_C(0x02000033) | (funct3 << 12U);
    model.pcpi_rs1 = left;
    model.pcpi_rs2 = right;
    model.eval();
    require(model.pcpi_wait != 0, "claimed request did not assert wait immediately");

    unsigned ready_count = 0;
    unsigned ready_cycle = 0;
    for (unsigned cycle = 1; cycle <= 4; ++cycle)
    {
        tick(model);
        if (model.pcpi_ready != 0)
        {
            ++ready_count;
            ready_cycle = cycle;
            require(model.pcpi_wr != 0, "ready response did not write a result");
            require(model.pcpi_rd == oracle(left, right, funct3), "multiply result mismatch");
        }
    }
    require(ready_count == 1, "request did not produce exactly one response");
    require(ready_cycle == 2, "multiply latency changed");
    model.pcpi_valid = 0;
    tick(model);
}

[[nodiscard]] std::uint32_t next_random(std::uint32_t &state) noexcept
{
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

void pcpi_test()
{
    Vpqc_pcpi_mlkem model;
    model.resetn = 0;
    model.pcpi_valid = 0;
    tick(model);
    tick(model);
    model.resetn = 1;

    const std::uint32_t values[]{
        0U,
        1U,
        std::numeric_limits<std::uint32_t>::max(),
        UINT32_C(0x7fffffff),
        UINT32_C(0x80000000),
        UINT32_C(0x55555555),
        UINT32_C(0xaaaaaaaa),
    };
    for (std::uint32_t funct3 = 0; funct3 != 4; ++funct3)
    {
        for (const std::uint32_t left : values)
        {
            for (const std::uint32_t right : values)
            {
                run_request(model, left, right, funct3);
            }
        }
    }

    std::uint32_t state = UINT32_C(0x243f6a88);
    for (unsigned i = 0; i < 10000; ++i)
    {
        run_request(model, next_random(state), next_random(state), i & 3U);
    }

    model.pcpi_valid = 1;
    model.pcpi_insn = UINT32_C(0x02000033);
    model.pcpi_rs1 = 3;
    model.pcpi_rs2 = 5;
    tick(model);
    tick(model);
    require(model.pcpi_ready != 0 && model.pcpi_rd == 15, "first back to back response failed");
    model.pcpi_rs1 = 7;
    model.pcpi_rs2 = 11;
    model.eval();
    require(model.pcpi_ready == 0 && model.pcpi_wait != 0,
            "new request observed stale ready response");
    tick(model);
    tick(model);
    require(model.pcpi_ready != 0 && model.pcpi_rd == 77, "second back to back response failed");
    model.pcpi_valid = 0;
    tick(model);

    model.pcpi_valid = 1;
    model.pcpi_insn = UINT32_C(0x02000033);
    model.pcpi_rs1 = 7;
    model.pcpi_rs2 = 9;
    tick(model);
    model.resetn = 0;
    tick(model);
    model.resetn = 1;
    model.pcpi_valid = 0;
    tick(model);
    require(model.pcpi_ready == 0 && model.pcpi_wr == 0, "reset did not cancel request");

    model.pcpi_valid = 1;
    model.pcpi_insn = UINT32_C(0x00000013);
    model.eval();
    require(model.pcpi_wait == 0 && model.pcpi_ready == 0,
            "unsupported instruction received a response");
}

#else

struct options
{
    std::string output{};
    std::string stack_output{};
    std::string stack_usage{};
    std::string disassembly{};
    std::string size_input{};
    std::string size_output{};
    bool expect_trap{false};
};

[[nodiscard]] options parse_options(int argc, char **argv)
{
    options result;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view argument{argv[i]};
        if (argument.starts_with("+firmware="))
        {
            continue;
        }
        if (argument == "--expect-trap")
        {
            result.expect_trap = true;
        }
        else if ((argument == "--output" || argument == "--stack-output" ||
                  argument == "--stack-usage" || argument == "--disassembly" ||
                  argument == "--size-input" || argument == "--size-output") &&
                 i + 1 < argc)
        {
            const std::string value = argv[++i];
            if (argument == "--output")
            {
                result.output = value;
            }
            else if (argument == "--stack-output")
            {
                result.stack_output = value;
            }
            else if (argument == "--stack-usage")
            {
                result.stack_usage = value;
            }
            else if (argument == "--disassembly")
            {
                result.disassembly = value;
            }
            else if (argument == "--size-input")
            {
                result.size_input = value;
            }
            else
            {
                result.size_output = value;
            }
        }
        else
        {
            fail("invalid simulation argument");
        }
    }
    if (result.output.empty())
    {
        fail("missing --output");
    }
    return result;
}

void tick(Vpqc_picorv32_sim_top &model)
{
    model.clk = 0;
    model.eval();
    model.clk = 1;
    model.eval();
}

[[nodiscard]] std::string read_file(const std::string &path)
{
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "cannot open measurement input");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_stack(const options &settings, const std::vector<std::uint32_t> &status)
{
    if (settings.stack_output.empty())
    {
        return;
    }
    std::uint32_t wrapper = 0;
    std::uint32_t raw = 0;
    std::uint32_t calibrated = 0;
    bool have_wrapper = false;
    bool have_raw = false;
    bool have_calibrated = false;
    for (std::size_t i = 0; i + 1 < status.size(); ++i)
    {
        if (status[i] == UINT32_C(0x53544100))
        {
            wrapper = status[++i];
            have_wrapper = true;
        }
        else if (status[i] == UINT32_C(0x53544101))
        {
            raw = status[++i];
            have_raw = true;
        }
        else if (status[i] == UINT32_C(0x53544102))
        {
            calibrated = status[++i];
            have_calibrated = true;
        }
    }
    require(have_wrapper && have_raw && have_calibrated, "missing stack status records");
    require(raw >= wrapper && calibrated == raw - wrapper, "invalid stack calibration");

    const std::vector<pqc_poly::stack_frame> frames =
        pqc_poly::parse_stack_usage(read_file(settings.stack_usage));
    const auto measured = std::find_if(frames.begin(), frames.end(),
                                       [](const pqc_poly::stack_frame &frame)
                                       { return frame.function == "measured_multiply"; });
    require(measured != frames.end(), "measured compiler stack frame missing");
    const std::optional<std::uint64_t> callchain = pqc_poly::compute_callchain_stack_bound(
        frames, read_file(settings.disassembly), "measured_multiply");

    std::ofstream output(settings.stack_output, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "cannot open stack output");
    output << "{\n"
           << "  \"explicit_scratch_bytes\": 0,\n"
           << "  \"caller_working_bytes\": 4,\n"
           << "  \"compiler_frame_bytes\": " << measured->bytes << ",\n"
           << "  \"compiler_callchain_bound_bytes\": ";
    if (callchain)
    {
        output << *callchain;
    }
    else
    {
        output << "null";
    }
    output << ",\n"
           << "  \"runtime_stack_high_water_bytes\": " << calibrated << ",\n"
           << "  \"raw_stack_high_water_bytes\": " << raw << ",\n"
           << "  \"raw_wrapper_high_water_bytes\": " << wrapper << "\n"
           << "}\n";
}

void write_size(const options &settings)
{
    if (settings.size_output.empty())
    {
        return;
    }
    const pqc_poly::code_size_measurement size =
        pqc_poly::parse_elf_size(read_file(settings.size_input));
    std::ofstream output(settings.size_output, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "cannot open code size output");
    output << "{\n"
           << "  \"code_text_bytes\": " << size.code_text_bytes << ",\n"
           << "  \"code_rodata_bytes\": " << size.code_rodata_bytes << ",\n"
           << "  \"data_bytes\": " << size.data_bytes << ",\n"
           << "  \"bss_bytes\": " << size.bss_bytes << ",\n"
           << "  \"allocated_flash_bytes\": " << size.allocated_flash_bytes << "\n"
           << "}\n";
}

void simulate(const options &settings)
{
    Vpqc_picorv32_sim_top model;
    std::vector<std::uint64_t> begins;
    std::vector<std::uint64_t> ends;
    std::vector<std::uint32_t> status;

    model.resetn = 0;
    for (unsigned i = 0; i < 8; ++i)
    {
        tick(model);
    }
    model.resetn = 1;

    constexpr std::uint64_t cycle_limit = UINT64_C(250000000);
    for (std::uint64_t i = 0; i < cycle_limit && model.trap == 0 && model.terminate == 0; ++i)
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

    if (settings.expect_trap)
    {
        require(model.trap != 0, "expected trap did not occur");
    }
    else
    {
        require(model.trap == 0, "unexpected processor trap");
        require(model.terminate != 0, "firmware did not terminate");
        require(begins.size() == 2 && ends.size() == 2, "benchmark markers missing");
        require(ends[0] >= begins[0] && ends[1] >= begins[1], "marker order changed");
    }

    const std::uint64_t overhead = settings.expect_trap ? 0 : ends[0] - begins[0];
    const std::uint64_t begin = settings.expect_trap ? 0 : begins[1];
    const std::uint64_t end = settings.expect_trap ? 0 : ends[1];
    require(settings.expect_trap || end - begin >= overhead, "marker overhead exceeds workload");

    std::ofstream output(settings.output, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "cannot open simulation output");
    output << "{\"begin_cycle\":" << begin << ",\"end_cycle\":" << end
#if defined(PQC_STOCK_MUL)
           << ",\"multiplier\":\"stock\""
#else
           << ",\"multiplier\":\"project\""
#endif
           << ",\"marker_overhead_cycles\":" << overhead
           << ",\"calibrated_cycles\":" << (end - begin - overhead)
           << ",\"terminated\":" << (model.terminate != 0 ? "true" : "false")
           << ",\"trapped\":" << (model.trap != 0 ? "true" : "false") << ",\"status\":[";
    for (std::size_t i = 0; i < status.size(); ++i)
    {
        if (i != 0)
        {
            output << ',';
        }
        output << status[i];
    }
    output << "]}\n";

    if (!settings.expect_trap)
    {
        write_stack(settings, status);
        write_size(settings);
    }
}

#endif

}

int main(int argc, char **argv)
{
    Verilated::commandArgs(argc, argv);
#if defined(PQC_PCPI_UNIT)
    static_cast<void>(argc);
    static_cast<void>(argv);
    pcpi_test();
#else
    simulate(parse_options(argc, argv));
#endif
}
