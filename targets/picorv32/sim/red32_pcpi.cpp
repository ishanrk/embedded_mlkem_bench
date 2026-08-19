#include <verilated.h>

#include "Vpqc_pcpi_mlkem.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "red32 pcpi test failed: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message)
{
    if (!condition)
    {
        fail(message);
    }
}

void tick(Vpqc_pcpi_mlkem &model)
{
    model.clk = 0;
    model.eval();
    model.clk = 1;
    model.eval();
}

[[nodiscard]] std::int64_t signed_value(std::uint32_t value) noexcept
{
    return value <= static_cast<std::uint32_t>(INT32_MAX)
               ? static_cast<std::int64_t>(value)
               : static_cast<std::int64_t>(value) - INT64_C(4294967296);
}

[[nodiscard]] std::uint32_t red32_oracle(std::uint32_t value)
{
    const std::uint32_t inverse =
        ((value & UINT32_C(0xffff)) * UINT32_C(62209)) & UINT32_C(0xffff);
    const std::int32_t signed_inverse =
        static_cast<std::int32_t>(inverse ^ UINT32_C(0x8000)) - INT32_C(32768);
    const std::int64_t numerator =
        signed_value(value) - static_cast<std::int64_t>(signed_inverse) * INT64_C(3329);
    require(numerator % INT64_C(65536) == 0, "oracle numerator is not divisible");
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(numerator / INT64_C(65536)));
}

[[nodiscard]] std::uint32_t multiply_oracle(std::uint32_t left, std::uint32_t right)
{
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(left) * right);
}

void run_red32(Vpqc_pcpi_mlkem &model, std::uint32_t value, std::uint32_t ignored,
               std::uint32_t rd = 7U)
{
    model.pcpi_valid = 1;
    model.pcpi_insn = UINT32_C(0x0000100b) | (rd << 7U) | (5U << 15U) | (31U << 20U);
    model.pcpi_rs1 = value;
    model.pcpi_rs2 = ignored;
    model.eval();
    require(model.pcpi_wait != 0 && model.pcpi_ready == 0,
            "red32 request was not claimed immediately");
    unsigned ready_count = 0;
    unsigned ready_cycle = 0;
    for (unsigned cycle = 1; cycle <= 5U; ++cycle)
    {
        tick(model);
        if (model.pcpi_ready != 0)
        {
            ++ready_count;
            ready_cycle = cycle;
            require(model.pcpi_wr != 0, "red32 response did not write");
            require(model.pcpi_rd == red32_oracle(value), "red32 result mismatch");
        }
        else if (cycle < 3U)
        {
            require(model.pcpi_wait != 0, "red32 wait dropped before response");
        }
    }
    require(ready_count == 1U, "red32 did not produce exactly one response");
    require(ready_cycle == 3U, "red32 latency changed");
    model.pcpi_valid = 0;
    tick(model);
}

void run_mul(Vpqc_pcpi_mlkem &model, std::uint32_t left, std::uint32_t right)
{
    model.pcpi_valid = 1;
    model.pcpi_insn = UINT32_C(0x02000033);
    model.pcpi_rs1 = left;
    model.pcpi_rs2 = right;
    model.eval();
    require(model.pcpi_wait != 0, "mul request was not claimed");
    tick(model);
    tick(model);
    require(model.pcpi_ready != 0 && model.pcpi_rd == multiply_oracle(left, right),
            "mul changed with red32 enabled");
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

}

int main()
{
    Vpqc_pcpi_mlkem model;
    model.resetn = 0;
    model.pcpi_valid = 0;
    tick(model);
    tick(model);
    model.resetn = 1;

    constexpr std::array<std::uint32_t, 13> boundaries{
        UINT32_C(0x00000000), UINT32_C(0x00000001), UINT32_C(0xffffffff),
        UINT32_C(0x7fffffff), UINT32_C(0x80000000), UINT32_C(0x00007fff),
        UINT32_C(0x00008000), UINT32_C(0xffff7fff), UINT32_C(0xffff8000),
        UINT32_C(0x0000ffff), UINT32_C(0x7fff0000), UINT32_C(0x80000001),
        UINT32_C(0x89abcdef)};
    for (const std::uint32_t value : boundaries)
    {
        run_red32(model, value, value ^ UINT32_C(0xa5a5a5a5));
    }
    for (std::uint32_t low = 0; low <= UINT32_C(0xffff); ++low)
    {
        run_red32(model, UINT32_C(0x89ab0000) | low, UINT32_C(0x55aa0000) | low);
    }
    std::uint32_t state = UINT32_C(0x243f6a88);
    for (unsigned i = 0; i < 100000U; ++i)
    {
        run_red32(model, next_random(state), next_random(state));
    }
    require(red32_oracle(UINT32_C(0x00000001)) != red32_oracle(UINT32_C(0x00010001)),
            "upper input half does not affect red32");

    run_mul(model, UINT32_C(0x89abcdef), UINT32_C(0x12345678));

    model.pcpi_valid = 1;
    model.pcpi_insn = UINT32_C(0x0000100b);
    model.pcpi_rs1 = UINT32_C(0x80000000);
    model.pcpi_rs2 = UINT32_C(0x11111111);
    tick(model);
    model.resetn = 0;
    tick(model);
    model.resetn = 1;
    model.pcpi_valid = 0;
    tick(model);
    require(model.pcpi_ready == 0 && model.pcpi_wr == 0,
            "reset did not cancel red32 request");

    model.pcpi_valid = 1;
    model.pcpi_insn = UINT32_C(0x0000100b);
    model.pcpi_rs1 = UINT32_C(0x12345678);
    model.pcpi_rs2 = UINT32_C(0xaaaaaaaa);
    tick(model);
    tick(model);
    tick(model);
    require(model.pcpi_ready != 0 && model.pcpi_rd == red32_oracle(model.pcpi_rs1),
            "first back-to-back red32 response failed");
    model.pcpi_rs1 = UINT32_C(0x87654321);
    model.pcpi_rs2 = UINT32_C(0x55555555);
    model.eval();
    require(model.pcpi_ready == 0 && model.pcpi_wait != 0,
            "second red32 request observed a stale response");
    tick(model);
    tick(model);
    tick(model);
    require(model.pcpi_ready != 0 && model.pcpi_rd == red32_oracle(model.pcpi_rs1),
            "second back-to-back red32 response failed");
    model.pcpi_valid = 0;
    tick(model);

    model.pcpi_valid = 1;
    model.pcpi_insn = UINT32_C(0x0000000b);
    model.pcpi_rs1 = 1;
    model.pcpi_rs2 = 2;
    model.eval();
    require(model.pcpi_wait == 0 && model.pcpi_ready == 0,
            "fqmul was claimed with only red32 enabled");
}
