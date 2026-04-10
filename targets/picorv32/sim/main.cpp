#include <verilated.h>

#include "pqc_poly/target_measurement.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
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

[[nodiscard]] std::uint32_t multiply_oracle(std::uint32_t left, std::uint32_t right,
                                            std::uint32_t funct3)
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

[[nodiscard]] std::int32_t signed_low(std::uint32_t value)
{
    const std::uint32_t low = value & UINT32_C(0xffff);
    return low < UINT32_C(0x8000) ? static_cast<std::int32_t>(low)
                                  : static_cast<std::int32_t>(low) - INT32_C(65536);
}

[[nodiscard]] std::uint32_t fqmul_oracle(std::uint32_t left, std::uint32_t right)
{
    const std::int64_t product = static_cast<std::int64_t>(signed_low(left)) * signed_low(right);
    const std::uint32_t low =
        static_cast<std::uint32_t>(static_cast<std::uint64_t>(product) & UINT64_C(0xffff));
    const std::uint32_t inverse = low * UINT32_C(62209) & UINT32_C(0xffff);
    const std::int64_t numerator =
        product - static_cast<std::int64_t>(signed_low(inverse)) * INT64_C(3329);
    require(numerator % INT64_C(65536) == 0, "fqmul oracle numerator is not divisible");
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(numerator / INT64_C(65536)));
}

void run_multiply_request(Vpqc_pcpi_mlkem &model, std::uint32_t left, std::uint32_t right,
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
            require(model.pcpi_rd == multiply_oracle(left, right, funct3),
                    "multiply result mismatch");
        }
    }
    require(ready_count == 1, "request did not produce exactly one response");
    require(ready_cycle == 2, "multiply latency changed");
    model.pcpi_valid = 0;
    tick(model);
}

void run_fqmul_request(Vpqc_pcpi_mlkem &model, std::uint32_t left, std::uint32_t right)
{
    model.pcpi_valid = 1;
    model.pcpi_insn = UINT32_C(0x01f5850b);
    model.pcpi_rs1 = left;
    model.pcpi_rs2 = right;
    model.eval();
    require(model.pcpi_wait != 0, "fqmul request did not assert wait immediately");

    unsigned ready_count = 0;
    unsigned ready_cycle = 0;
    for (unsigned cycle = 1; cycle <= 6; ++cycle)
    {
        tick(model);
        if (model.pcpi_ready != 0)
        {
            ++ready_count;
            ready_cycle = cycle;
            require(model.pcpi_wr != 0, "fqmul response did not write a result");
            require(model.pcpi_rd == fqmul_oracle(left, right), "fqmul result mismatch");
        }
        else if (cycle < 4)
        {
            require(model.pcpi_wait != 0, "fqmul wait dropped before response");
        }
    }
    require(ready_count == 1, "fqmul request did not produce exactly one response");
    require(ready_cycle == 4, "fqmul latency changed");
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
                run_multiply_request(model, left, right, funct3);
            }
        }
    }

    std::uint32_t state = UINT32_C(0x243f6a88);
    for (unsigned i = 0; i < 10000; ++i)
    {
        run_multiply_request(model, next_random(state), next_random(state), i & 3U);
    }

    constexpr std::array<std::int32_t, 9> fqmul_values{0,
                                                       1,
                                                       -1,
                                                       3328,
                                                       -3328,
                                                       -4096,
                                                       4096,
                                                       std::numeric_limits<std::int16_t>::min(),
                                                       std::numeric_limits<std::int16_t>::max()};
    for (const std::int32_t left : fqmul_values)
    {
        for (const std::int32_t right : fqmul_values)
        {
            run_fqmul_request(model, static_cast<std::uint32_t>(left),
                              static_cast<std::uint32_t>(right));
        }
    }
    for (unsigned i = 0; i < 10000U; ++i)
    {
        const std::uint32_t left = next_random(state);
        const std::uint32_t right = next_random(state);
        run_fqmul_request(model, left, right);
        run_fqmul_request(model, left ^ UINT32_C(0xffff0000), right ^ UINT32_C(0x55550000));
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
    model.pcpi_insn = UINT32_C(0x0000000b);
    model.pcpi_rs1 = UINT32_C(0x12348000);
    model.pcpi_rs2 = UINT32_C(0x56787fff);
    for (unsigned cycle = 0; cycle < 4; ++cycle)
    {
        tick(model);
    }
    require(model.pcpi_ready != 0 && model.pcpi_rd == fqmul_oracle(model.pcpi_rs1, model.pcpi_rs2),
            "first back to back fqmul response failed");
    tick(model);
    model.pcpi_rs1 = UINT32_C(0xaaaaffff);
    model.pcpi_rs2 = UINT32_C(0x55550001);
    model.eval();
    require(model.pcpi_wait != 0 && model.pcpi_ready == 0,
            "second back to back fqmul was not accepted");
    for (unsigned cycle = 0; cycle < 4; ++cycle)
    {
        tick(model);
    }
    require(model.pcpi_ready != 0 && model.pcpi_rd == fqmul_oracle(model.pcpi_rs1, model.pcpi_rs2),
            "second back to back fqmul response failed");
    model.pcpi_valid = 0;
    tick(model);

    model.pcpi_valid = 1;
    model.pcpi_insn = UINT32_C(0x0000000b);
    model.pcpi_rs1 = 7;
    model.pcpi_rs2 = 9;
    tick(model);
    model.resetn = 0;
    tick(model);
    model.resetn = 1;
    model.pcpi_valid = 0;
    tick(model);
    require(model.pcpi_ready == 0 && model.pcpi_wr == 0, "reset did not cancel request");

    for (const std::uint32_t instruction :
         {UINT32_C(0x00000013), UINT32_C(0x0200000b), UINT32_C(0x0000100b), UINT32_C(0x0000002b)})
    {
        model.pcpi_valid = 1;
        model.pcpi_insn = instruction;
        model.eval();
        require(model.pcpi_wait == 0 && model.pcpi_ready == 0,
                "unsupported instruction received a response");
        tick(model);
    }
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
    std::string plan_id{};
    std::string level{};
    std::string stack_root{"measured_multiply"};
    unsigned repeat_count{0};
    unsigned kernel_inputs{16};
    unsigned operation_inputs{30};
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
                  argument == "--size-input" || argument == "--size-output" ||
                  argument == "--plan-id" || argument == "--level" || argument == "--stack-root" ||
                  argument == "--repeat-count" || argument == "--kernel-inputs" ||
                  argument == "--operation-inputs") &&
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
            else if (argument == "--size-output")
            {
                result.size_output = value;
            }
            else if (argument == "--plan-id")
            {
                result.plan_id = value;
            }
            else if (argument == "--level")
            {
                result.level = value;
            }
            else if (argument == "--stack-root")
            {
                result.stack_root = value;
            }
            else if (argument == "--repeat-count")
            {
                if (value != "3")
                {
                    fail("invalid repeat count");
                }
                result.repeat_count = 3;
            }
            else if (argument == "--kernel-inputs")
            {
                if (value != "1" && value != "16")
                {
                    fail("invalid kernel input count");
                }
                result.kernel_inputs = value == "1" ? 1U : 16U;
            }
            else
            {
                if (value != "1" && value != "30")
                {
                    fail("invalid operation input count");
                }
                result.operation_inputs = value == "1" ? 1U : 30U;
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
    if (result.plan_id.empty() != result.level.empty())
    {
        fail("incomplete mlkem simulation metadata");
    }
    if (!result.plan_id.empty() && result.repeat_count != 3)
    {
        fail("mlkem simulation requires three repeats");
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

void validate_instructions(const options &settings)
{
    if (settings.plan_id.empty() || settings.disassembly.empty())
    {
        return;
    }
    const bool expect_fqmul = settings.plan_id.ends_with("_xfqmul");
    const std::array<std::string_view, 6> approved{"pqc_mlkem_ntt",          "pqc_mlkem_intt",
                                                   "pqc_mlkem_mulcache_one", "pqc_mlkem_mulcache",
                                                   "pqc_mlkem_basemul",      "pqc_mlkem_tomont"};
    std::istringstream input(read_file(settings.disassembly));
    std::string line;
    std::string function;
    std::size_t fqmul_count = 0;
    while (std::getline(input, line))
    {
        const std::size_t left = line.find('<');
        const std::size_t header = line.find(">:");
        if (left != std::string::npos && header != std::string::npos && left < header)
        {
            function = line.substr(left + 1U, header - left - 1U);
            require(!function.starts_with("__div") && !function.starts_with("__udiv") &&
                        !function.starts_with("__mod") && !function.starts_with("__umod"),
                    "software division helper found in mlkem firmware");
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
            fail("invalid raw instruction word");
        }
        if ((word & UINT32_C(0xfe00707f)) == UINT32_C(0x0000000b))
        {
            ++fqmul_count;
            const bool allowed = std::any_of(
                approved.begin(), approved.end(),
                [&function](std::string_view name)
                { return function == name || function.starts_with(std::string(name) + '.'); });
            require(allowed, "fqmul word is outside an approved function");
        }
        const std::uint32_t decoded = word & UINT32_C(0xfe00707f);
        const bool divide = decoded == UINT32_C(0x02004033) || decoded == UINT32_C(0x02005033) ||
                            decoded == UINT32_C(0x02006033) || decoded == UINT32_C(0x02007033);
        require(!divide, "division instruction found in mlkem firmware");
        const bool indirect_call =
            (word & UINT32_C(0x7f)) == UINT32_C(0x67) && ((word >> 7U) & UINT32_C(0x1f)) == 1U;
        require(!indirect_call || function == "pqc_call_measured",
                "unexpected indirect call in mlkem firmware");
    }
    require(expect_fqmul ? fqmul_count != 0U : fqmul_count == 0U,
            "custom instruction presence does not match plan");
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
    std::uint32_t caller = 0;
    std::uint32_t scratch = 0;
    bool have_wrapper = false;
    bool have_raw = false;
    bool have_calibrated = false;
    bool have_caller = false;
    bool have_scratch = false;
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
        else if (status[i] == UINT32_C(0x53544103))
        {
            caller = status[++i];
            have_caller = true;
        }
        else if (status[i] == UINT32_C(0x53544104))
        {
            scratch = status[++i];
            have_scratch = true;
        }
    }
    require(have_wrapper && have_raw && have_calibrated, "missing stack status records");
    require(settings.plan_id.empty() || (have_caller && have_scratch),
            "missing mlkem memory status records");
    require(raw >= wrapper && calibrated == raw - wrapper, "invalid stack calibration");

    const std::vector<pqc_poly::stack_frame> frames =
        pqc_poly::parse_stack_usage(read_file(settings.stack_usage));
    const auto measured = std::find_if(frames.begin(), frames.end(),
                                       [&settings](const pqc_poly::stack_frame &frame)
                                       { return frame.function == settings.stack_root; });
    require(measured != frames.end(), "measured compiler stack frame missing");
    const std::optional<std::uint64_t> callchain = pqc_poly::compute_callchain_stack_bound(
        frames, read_file(settings.disassembly), settings.stack_root);

    std::ofstream output(settings.stack_output, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "cannot open stack output");
    output << "{\n"
           << "  \"explicit_scratch_bytes\": " << scratch << ",\n"
           << "  \"caller_working_bytes\": " << (have_caller ? caller : 4U) << ",\n"
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

struct operation_position
{
    std::string_view name{};
    unsigned input{0};
    unsigned repeat{0};
};

[[nodiscard]] operation_position mlkem_position(std::size_t index, std::string_view level,
                                                unsigned kernel_inputs, unsigned operation_inputs)
{
    const std::size_t kernel_span = static_cast<std::size_t>(kernel_inputs) * 3U;
    const std::size_t operation_span = static_cast<std::size_t>(operation_inputs) * 3U;
    if (index < kernel_span)
    {
        return {"forward_ntt", static_cast<unsigned>(index / 3U),
                static_cast<unsigned>(index % 3U)};
    }
    index -= kernel_span;
    if (index < kernel_span)
    {
        return {"inverse_ntt", static_cast<unsigned>(index / 3U),
                static_cast<unsigned>(index % 3U)};
    }
    index -= kernel_span;
    if (index < kernel_span)
    {
        return {"mulcache", static_cast<unsigned>(index / 3U), static_cast<unsigned>(index % 3U)};
    }
    index -= kernel_span;
    if (index < kernel_span)
    {
        return {level == "512"   ? "base_dot_k2"
                : level == "768" ? "base_dot_k3"
                                 : "base_dot_k4",
                static_cast<unsigned>(index / 3U), static_cast<unsigned>(index % 3U)};
    }
    index -= kernel_span;
    if (index < kernel_span)
    {
        return {"poly_tomont", static_cast<unsigned>(index / 3U),
                static_cast<unsigned>(index % 3U)};
    }
    index -= kernel_span;
    if (index < operation_span)
    {
        return {"keygen", static_cast<unsigned>(index / 3U), static_cast<unsigned>(index % 3U)};
    }
    index -= operation_span;
    if (index < operation_span)
    {
        return {"encapsulation", static_cast<unsigned>(index / 3U),
                static_cast<unsigned>(index % 3U)};
    }
    index -= operation_span;
    if (index < operation_span)
    {
        return {"decapsulation", static_cast<unsigned>(index / 3U),
                static_cast<unsigned>(index % 3U)};
    }
    fail("unexpected mlkem measurement count");
}

[[nodiscard]] std::vector<std::pair<std::uint32_t, std::uint32_t> > instret_records(
    const std::vector<std::uint32_t> &status, std::size_t count)
{
    std::vector<std::pair<std::uint32_t, std::uint32_t> > out;
    require(status.size() >= count * 3U, "mlkem instruction status is incomplete");
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::size_t offset = i * 3U;
        if (status[offset] != UINT32_C(0x494e5300))
        {
            fail("mlkem instruction status order changed at " + std::to_string(offset) + " value " +
                 std::to_string(status[offset]));
        }
        out.emplace_back(status[offset + 1U], status[offset + 2U]);
    }
    return out;
}

void write_mlkem(const options &settings, std::span<const std::uint64_t> begins,
                 std::span<const std::uint64_t> ends, const std::vector<std::uint32_t> &status)
{
    const std::size_t measurement_count =
        5U * static_cast<std::size_t>(settings.kernel_inputs) * 3U +
        3U * static_cast<std::size_t>(settings.operation_inputs) * 3U;
    if (begins.size() != measurement_count + 1U || ends.size() != begins.size())
    {
        fail("mlkem benchmark marker count changed begins " + std::to_string(begins.size()) +
             " ends " + std::to_string(ends.size()));
    }
    const std::vector<std::pair<std::uint32_t, std::uint32_t> > instret =
        instret_records(status, begins.size());
    if (instret.size() != begins.size())
    {
        fail("mlkem instruction record count changed records " + std::to_string(instret.size()) +
             " markers " + std::to_string(begins.size()) + " status " +
             std::to_string(status.size()));
    }
    require(ends[0] >= begins[0], "mlkem marker calibration is reversed");
    const std::uint64_t cycle_overhead = ends[0] - begins[0];
    const std::uint32_t instruction_overhead = instret[0].second - instret[0].first;

    std::ofstream output(settings.output, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "cannot open simulation output");
    std::uint64_t prior_cycles = 0;
    std::uint32_t prior_instructions = 0;
    for (std::size_t i = 0; i < measurement_count; ++i)
    {
        const std::size_t marker = i + 1U;
        require(ends[marker] >= begins[marker] && ends[marker] - begins[marker] >= cycle_overhead,
                "invalid mlkem cycle interval");
        const std::uint64_t cycles = ends[marker] - begins[marker] - cycle_overhead;
        const std::uint32_t raw_instructions = instret[marker].second - instret[marker].first;
        require(raw_instructions >= instruction_overhead, "invalid mlkem instruction interval");
        const std::uint32_t instructions = raw_instructions - instruction_overhead;
        const operation_position position =
            mlkem_position(i, settings.level, settings.kernel_inputs, settings.operation_inputs);
        if (position.repeat != 0U)
        {
            require(cycles == prior_cycles && instructions == prior_instructions,
                    "mlkem repeated measurement changed");
        }
        prior_cycles = cycles;
        prior_instructions = instructions;
        output << "{\"schema\":\"pqc-poly-bench/mlkem-measurement-v1\",\"plan_id\":\""
               << settings.plan_id << "\",\"level\":\"" << settings.level << "\",\"operation\":\""
               << position.name << "\",\"input\":" << position.input
               << ",\"repeat\":" << position.repeat << ",\"begin_cycle\":" << begins[marker]
               << ",\"end_cycle\":" << ends[marker]
               << ",\"marker_overhead_cycles\":" << cycle_overhead
               << ",\"calibrated_cycles\":" << cycles << ",\"instruction_count\":" << instructions
#if defined(PQC_STOCK_MUL)
               << ",\"multiplier\":\"stock\"}\n";
#elif defined(PQC_FQMUL)
               << ",\"multiplier\":\"fqmul\"}\n";
#else
               << ",\"multiplier\":\"project\"}\n";
#endif
    }
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
    validate_instructions(settings);
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

    constexpr std::uint64_t cycle_limit = UINT64_C(5000000000);
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
        require(begins.size() == ends.size() && !begins.empty(), "benchmark markers missing");
    }

    if (!settings.plan_id.empty() && !settings.expect_trap)
    {
        write_mlkem(settings, begins, ends, status);
        write_stack(settings, status);
        write_size(settings);
        return;
    }

    require(settings.expect_trap || begins.size() == 2, "smoke benchmark marker count changed");
    const std::uint64_t overhead = settings.expect_trap ? 0 : ends[0] - begins[0];
    const std::uint64_t begin = settings.expect_trap ? 0 : begins[1];
    const std::uint64_t end = settings.expect_trap ? 0 : ends[1];
    require(settings.expect_trap || end - begin >= overhead, "marker overhead exceeds workload");

    std::ofstream output(settings.output, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "cannot open simulation output");
    output << "{\"begin_cycle\":" << begin << ",\"end_cycle\":" << end
#if defined(PQC_STOCK_MUL)
           << ",\"multiplier\":\"stock\""
#elif defined(PQC_FQMUL)
           << ",\"multiplier\":\"fqmul\""
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
