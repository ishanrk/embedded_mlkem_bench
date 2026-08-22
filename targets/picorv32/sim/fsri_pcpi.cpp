#include <verilated.h>

#include "Vpqc_pcpi_mlkem.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "fsri pcpi test failed: " << message << '\n';
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

[[nodiscard]] std::uint32_t oracle(std::uint32_t a, std::uint32_t b, unsigned s)
{
    const std::uint64_t value = (static_cast<std::uint64_t>(b) << 32U) | a;
    return static_cast<std::uint32_t>(value >> (s & 31U));
}

[[nodiscard]] std::uint32_t insn(unsigned s, unsigned rd = 7U)
{
    return UINT32_C(0x0000200b) | ((s & 31U) << 25U) | (rd << 7U) | (5U << 15U) |
           (6U << 20U);
}

void run_fsri(Vpqc_pcpi_mlkem &model, std::uint32_t a, std::uint32_t b, unsigned s)
{
    model.pcpi_valid = 1;
    model.pcpi_insn = insn(s);
    model.pcpi_rs1 = a;
    model.pcpi_rs2 = b;
    model.eval();
    require(model.pcpi_wait != 0 && model.pcpi_ready == 0,
            "request was not claimed immediately");

    unsigned ready_count = 0;
    unsigned ready_cycle = 0;
    for (unsigned cycle = 1; cycle <= 5U; ++cycle)
    {
        tick(model);
        if (model.pcpi_ready != 0)
        {
            ++ready_count;
            ready_cycle = cycle;
            require(model.pcpi_wait == 0, "response kept wait asserted");
            require(model.pcpi_wr != 0, "response did not write");
            require(model.pcpi_rd == oracle(a, b, s), "result mismatch");
        }
        else if (cycle < 3U)
        {
            require(model.pcpi_wait != 0, "wait dropped before response");
        }
    }
    require(ready_count == 1U, "request did not produce one response");
    require(ready_cycle == 3U, "latency changed");
    model.pcpi_valid = 0;
    tick(model);
}

void run_mul(Vpqc_pcpi_mlkem &model, std::uint32_t a, std::uint32_t b)
{
    model.pcpi_valid = 1;
    model.pcpi_insn = UINT32_C(0x02000033);
    model.pcpi_rs1 = a;
    model.pcpi_rs2 = b;
    model.eval();
    require(model.pcpi_wait != 0, "mul was not claimed");
    tick(model);
    tick(model);
    require(model.pcpi_ready != 0 && model.pcpi_wr != 0,
            "mul did not respond");
    require(model.pcpi_rd == static_cast<std::uint32_t>(
                                 static_cast<std::uint64_t>(a) * b),
            "mul result changed");
    model.pcpi_valid = 0;
    tick(model);
}

[[nodiscard]] std::uint32_t next(std::uint32_t &state)
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

    constexpr std::array<std::uint32_t, 7> values{
        0U,
        1U,
        UINT32_MAX,
        UINT32_C(0x7fffffff),
        UINT32_C(0x80000000),
        UINT32_C(0x55555555),
        UINT32_C(0xaaaaaaaa),
    };
    for (unsigned s = 0; s < 32U; ++s)
    {
        for (const std::uint32_t a : values)
        {
            for (const std::uint32_t b : values)
            {
                run_fsri(model, a, b, s);
            }
        }
    }

    std::uint32_t state = UINT32_C(0x243f6a88);
    for (unsigned i = 0; i < 100000U; ++i)
    {
        const std::uint32_t a = next(state);
        const std::uint32_t b = next(state);
        const unsigned s = next(state) & 31U;
        run_fsri(model, a, b, s);
    }

    run_mul(model, UINT32_C(0x89abcdef), UINT32_C(0x12345678));

    model.pcpi_valid = 1;
    model.pcpi_insn = insn(13U);
    model.pcpi_rs1 = UINT32_C(0x12345678);
    model.pcpi_rs2 = UINT32_C(0x89abcdef);
    model.eval();
    require(model.pcpi_wait != 0, "reset request was not claimed");
    tick(model);
    model.resetn = 0;
    tick(model);
    model.resetn = 1;
    model.pcpi_valid = 0;
    tick(model);
    require(model.pcpi_ready == 0 && model.pcpi_wr == 0,
            "reset did not cancel request");

    model.pcpi_valid = 1;
    model.pcpi_insn = insn(7U);
    model.pcpi_rs1 = UINT32_C(0x01234567);
    model.pcpi_rs2 = UINT32_C(0x89abcdef);
    tick(model);
    tick(model);
    tick(model);
    require(model.pcpi_ready != 0 &&
                model.pcpi_rd == oracle(model.pcpi_rs1, model.pcpi_rs2, 7U),
            "first back to back response failed");
    model.pcpi_insn = insn(19U);
    model.pcpi_rs1 = UINT32_C(0xfedcba98);
    model.pcpi_rs2 = UINT32_C(0x76543210);
    model.eval();
    require(model.pcpi_ready == 0 && model.pcpi_wait != 0,
            "second request observed a stale response");
    tick(model);
    tick(model);
    tick(model);
    require(model.pcpi_ready != 0 &&
                model.pcpi_rd == oracle(model.pcpi_rs1, model.pcpi_rs2, 19U),
            "second back to back response failed");
    model.pcpi_valid = 0;
    tick(model);

    for (const std::uint32_t instruction :
         {UINT32_C(0x0000000b), UINT32_C(0x0000100b), UINT32_C(0x4000200b),
          UINT32_C(0x0000002b)})
    {
        model.pcpi_valid = 1;
        model.pcpi_insn = instruction;
        model.pcpi_rs1 = 1;
        model.pcpi_rs2 = 2;
        model.eval();
        require(model.pcpi_wait == 0 && model.pcpi_ready == 0,
                "unsupported instruction was claimed");
        model.pcpi_valid = 0;
        tick(model);
    }
}
