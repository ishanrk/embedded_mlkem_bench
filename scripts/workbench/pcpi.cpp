#include "Vpqc_pcpi_mlkem.h"
#include <verilated.h>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace
{
// one compact driver is rebuilt for disabled, FQMUL, RED32, and all FSRI variants
constexpr unsigned feature = PQC_FEATURE;
constexpr unsigned implementation = PQC_FSRI_IMPL;
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
void tick(Vpqc_pcpi_mlkem &model)
{
    model.clk = 0;
    model.eval();
    model.clk = 1;
    model.eval();
}
void reset(Vpqc_pcpi_mlkem &model)
{
    model.resetn = 0;
    model.pcpi_valid = 0;
    tick(model);
    model.resetn = 1;
    model.eval();
    require(!model.pcpi_ready && !model.pcpi_wr, "reset left a response");
}
std::int64_t sign(std::uint32_t value, unsigned width)
{
    const std::uint64_t mask = (UINT64_C(1) << width) - 1;
    const auto low = static_cast<std::int64_t>(value & mask);
    return (value & (UINT64_C(1) << (width - 1))) ? low - (INT64_C(1) << width) : low;
}
std::uint32_t oracle(std::uint32_t insn, std::uint32_t a, std::uint32_t b)
{
    if ((insn & 127) == 0x33)
    {
        const unsigned kind = (insn >> 12) & 3;
        const __int128 left = kind == 1 || kind == 2 ? sign(a, 32) : static_cast<std::int64_t>(a);
        const __int128 right = kind == 1 ? sign(b, 32) : static_cast<std::int64_t>(b);
        const auto bits = static_cast<unsigned __int128>(left * right);
        return static_cast<std::uint32_t>(bits >> (kind ? 32 : 0));
    }
    if (feature == 3)
        return static_cast<std::uint32_t>(((static_cast<std::uint64_t>(b) << 32) | a) >> ((insn >> 25) & 31));
    const std::int64_t t = feature == 2 ? sign(a, 32) : sign(a, 16) * sign(b, 16);
    const auto low = static_cast<std::uint32_t>(static_cast<std::uint64_t>(t) * 62209U);
    return static_cast<std::uint32_t>((t - sign(low, 16) * 3329) / 65536);
}
std::uint32_t instruction(unsigned shift)
{
    return (feature == 3 ? 0x200bU | (shift << 25) : feature == 2 ? 0x100bU : 0xbU) |
           (7U << 7) | (5U << 15) | (6U << 20);
}
unsigned latency(std::uint32_t insn)
{
    // response edge expected from the selected RTL parameterization
    if ((insn & 127) == 0x33) return 2;
    if (feature == 1) return 4;
    if (feature == 3 && implementation == 2) return 0;
    return 3;
}
void request(Vpqc_pcpi_mlkem &model, std::uint32_t insn, std::uint32_t a, std::uint32_t b)
{
    // drive a request until its fixed response point and compare against the C++ oracle
    model.pcpi_valid = 1;
    model.pcpi_insn = insn;
    model.pcpi_rs1 = a;
    model.pcpi_rs2 = b;
    model.eval();
    const unsigned cycles = latency(insn);
    for (unsigned i = 0; i < cycles; ++i)
    {
        require(model.pcpi_wait && !model.pcpi_ready && !model.pcpi_wr, "claim or fixed latency changed");
        tick(model);
    }
    require(model.pcpi_ready && model.pcpi_wr && !model.pcpi_wait, "missing response");
    require(model.pcpi_rd == oracle(insn, a, b), "oracle mismatch");
}
void release(Vpqc_pcpi_mlkem &model)
{
    model.pcpi_valid = 0;
    tick(model);
    tick(model);
}
}
int main()
{
    try
    {
        Vpqc_pcpi_mlkem model;
        reset(model);
        constexpr std::array<std::uint32_t, 7> values{0, 1, 0xffffffffU, 0x7fffffffU, 0x80000000U, 0x55555555U, 0xaaaaaaaaU};
        unsigned transactions = 0;
        if (feature)
        {
            for (unsigned shift = 0; shift < (feature == 3 ? 32U : 1U); ++shift)
                for (const auto a : values)
                    for (const auto b : values)
                    {
                        request(model, instruction(shift), a, b);
                        release(model);
                        ++transactions;
                    }
            std::uint32_t seed = 0x243f6a88U;
            const auto next = [&]() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; };
            for (unsigned i = 0; i < 256; ++i)
            {
                const auto a = next(), b = next();
                request(model, instruction(next() & 31), a, b);
                release(model);
                ++transactions;
            }
            // reset at every active edge to look for stale state or a late response
            for (unsigned edge = 0; edge <= latency(instruction(13)); ++edge)
            {
                reset(model);
                model.pcpi_valid = 1;
                model.pcpi_insn = instruction(13);
                model.pcpi_rs1 = 0x89abcdefU;
                model.pcpi_rs2 = 0x12345678U;
                model.eval();
                for (unsigned i = 0; i < edge; ++i) tick(model);
                reset(model);
                request(model, instruction(13), 0x89abcdefU, 0x12345678U);
                release(model);
            }
            // change operands while valid remains high: the old response must not leak forward
            request(model, instruction(13), 0x89abcdefU, 0x12345678U);
            request(model, instruction(19), 0x76543210U, 0xfedcba98U);
            if (latency(instruction(19)))
            {
                tick(model);
                require(!model.pcpi_ready, "held request served twice");
                tick(model);
                require(!model.pcpi_ready && !model.pcpi_wait, "identical held request restarted");
            }
            release(model);
            request(model, instruction(19), 0x76543210U, 0xfedcba98U);
            release(model);
        }
        for (unsigned kind = 0; kind < 4; ++kind)
            for (const auto a : values)
                for (const auto b : values)
                {
                    request(model, 0x02000033U | (kind << 12), a, b);
                    release(model);
                    ++transactions;
                }
        for (const auto insn : {0x0000000bU, 0x0000100bU, 0x0000200bU, 0x4000200bU, 0x8000200bU, 0x0000002bU, 0x02004033U})
        {
            const bool enabled = (feature == 1 && insn == 0xb) || (feature == 2 && insn == 0x100b) || (feature == 3 && insn == 0x200b);
            if (enabled) continue;
            model.pcpi_valid = 1;
            model.pcpi_insn = insn;
            model.eval();
            for (unsigned i = 0; i < 5; ++i)
            {
                require(!model.pcpi_wait && !model.pcpi_ready && !model.pcpi_wr, "disabled or unsupported decode claimed");
                tick(model);
            }
            release(model);
        }
        std::cout << transactions << " deterministic arithmetic transactions plus reset held identical back to back and decode checks passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
