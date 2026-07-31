#include <verilated.h>

#include "Vpqc_pcpi_mlkem.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

#ifndef PQC_TEST_VARIANT
#error missing test variant
#endif

// compares each hardware setting with the matching software result
namespace
{

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "pcpi test failed " << message << '\n';
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

[[nodiscard]] std::uint32_t next_random(std::uint32_t &state)
{
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

[[nodiscard]] std::int32_t signed_low(std::uint32_t value)
{
    const std::uint32_t low = value & UINT32_C(0xffff);
    return low < UINT32_C(0x8000) ? static_cast<std::int32_t>(low)
                                  : static_cast<std::int32_t>(low) - INT32_C(65536);
}

[[nodiscard]] std::uint32_t fqmul_reference(std::uint32_t left,
                                            std::uint32_t right)
{
    const std::int64_t product =
        static_cast<std::int64_t>(signed_low(left)) * signed_low(right);
    const std::uint32_t inverse =
        (static_cast<std::uint32_t>(product) * UINT32_C(62209)) &
        UINT32_C(0xffff);
    const std::int64_t numerator =
        product - static_cast<std::int64_t>(signed_low(inverse)) * INT64_C(3329);
    require(numerator % INT64_C(65536) == 0, "bad fqmul reference");
    return static_cast<std::uint32_t>(
        static_cast<std::int32_t>(numerator / INT64_C(65536)));
}

[[nodiscard]] std::uint32_t red32_reference(std::uint32_t value)
{
    const std::int64_t signed_value =
        value <= static_cast<std::uint32_t>(INT32_MAX)
            ? static_cast<std::int64_t>(value)
            : static_cast<std::int64_t>(value) - INT64_C(4294967296);
    const std::uint32_t inverse =
        ((value & UINT32_C(0xffff)) * UINT32_C(62209)) & UINT32_C(0xffff);
    const std::int64_t numerator =
        signed_value - static_cast<std::int64_t>(signed_low(inverse)) * INT64_C(3329);
    require(numerator % INT64_C(65536) == 0, "bad red32 reference");
    return static_cast<std::uint32_t>(
        static_cast<std::int32_t>(numerator / INT64_C(65536)));
}

[[nodiscard]] std::uint32_t fsri_reference(std::uint32_t first,
                                           std::uint32_t second,
                                           unsigned shift)
{
    const std::uint64_t joined =
        (static_cast<std::uint64_t>(second) << 32U) | first;
    return static_cast<std::uint32_t>(joined >> (shift & 31U));
}

[[nodiscard]] std::uint32_t multiply_reference(std::uint32_t left,
                                               std::uint32_t right,
                                               unsigned operation)
{
    const std::uint64_t unsigned_product =
        static_cast<std::uint64_t>(left) * right;
    const std::int64_t signed_product =
        static_cast<std::int64_t>(static_cast<std::int32_t>(left)) *
        static_cast<std::int32_t>(right);
    const std::int64_t mixed_product =
        static_cast<std::int64_t>(static_cast<std::int32_t>(left)) * right;
    if (operation == 0U)
    {
        return static_cast<std::uint32_t>(unsigned_product);
    }
    if (operation == 1U)
    {
        return static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(signed_product) >> 32U);
    }
    if (operation == 2U)
    {
        return static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(mixed_product) >> 32U);
    }
    return static_cast<std::uint32_t>(unsigned_product >> 32U);
}

void clear_request(Vpqc_pcpi_mlkem &model)
{
    model.pcpi_valid = 0;
    tick(model);
}

void run_sequential(Vpqc_pcpi_mlkem &model, std::uint32_t instruction,
                    std::uint32_t left, std::uint32_t right,
                    std::uint32_t expected, unsigned latency)
{
    // holds one request until the expected response cycle
    model.pcpi_valid = 1;
    model.pcpi_insn = instruction;
    model.pcpi_rs1 = left;
    model.pcpi_rs2 = right;
    model.eval();
    require(model.pcpi_wait != 0 && model.pcpi_ready == 0,
            "request was not claimed");
    unsigned responses = 0;
    unsigned response_cycle = 0;
    for (unsigned cycle = 1; cycle <= latency + 2U; ++cycle)
    {
        tick(model);
        if (model.pcpi_ready != 0)
        {
            ++responses;
            response_cycle = cycle;
            require(model.pcpi_wr != 0 && model.pcpi_rd == expected,
                    "wrong response");
        }
    }
    require(responses == 1U && response_cycle == latency,
            "response latency changed");
    clear_request(model);
}

void test_multiply(Vpqc_pcpi_mlkem &model)
{
    // ordinary multiplication must stay identical in every processor
    std::uint32_t random = UINT32_C(0x243f6a88);
    for (unsigned operation = 0; operation < 4U; ++operation)
    {
        for (unsigned index = 0; index < 5000U; ++index)
        {
            const std::uint32_t left = next_random(random);
            const std::uint32_t right = next_random(random);
            run_sequential(model,
                           UINT32_C(0x02000033) | (operation << 12U), left,
                           right, multiply_reference(left, right, operation), 2U);
        }
    }
}

void test_fqmul(Vpqc_pcpi_mlkem &model)
{
    constexpr std::array<std::int32_t, 9> values{
        0, 1, -1, 3328, -3328, -4096, 4096,
        std::numeric_limits<std::int16_t>::min(),
        std::numeric_limits<std::int16_t>::max()};
    for (const std::int32_t left : values)
    {
        for (const std::int32_t right : values)
        {
            run_sequential(model, UINT32_C(0x0000000b),
                           static_cast<std::uint32_t>(left),
                           static_cast<std::uint32_t>(right),
                           fqmul_reference(static_cast<std::uint32_t>(left),
                                           static_cast<std::uint32_t>(right)),
                           4U);
        }
    }
    std::uint32_t random = UINT32_C(0x13198a2e);
    for (unsigned index = 0; index < 20000U; ++index)
    {
        const std::uint32_t left = next_random(random);
        const std::uint32_t right = next_random(random);
        run_sequential(model, UINT32_C(0x0000000b), left, right,
                       fqmul_reference(left, right), 4U);
    }
}

void test_red32(Vpqc_pcpi_mlkem &model)
{
    constexpr std::array<std::uint32_t, 9> values{
        0U, 1U, UINT32_MAX, UINT32_C(0x7fffffff), UINT32_C(0x80000000),
        UINT32_C(0x00007fff), UINT32_C(0x00008000),
        UINT32_C(0xffff8000), UINT32_C(0x89abcdef)};
    for (const std::uint32_t value : values)
    {
        run_sequential(model, UINT32_C(0x0000100b), value,
                       value ^ UINT32_C(0xa5a5a5a5),
                       red32_reference(value), 3U);
    }
    std::uint32_t random = UINT32_C(0x03707344);
    for (unsigned index = 0; index < 100000U; ++index)
    {
        const std::uint32_t value = next_random(random);
        run_sequential(model, UINT32_C(0x0000100b), value,
                       next_random(random), red32_reference(value), 3U);
    }
}

void test_fsri(Vpqc_pcpi_mlkem &model)
{
    std::uint32_t random = UINT32_C(0xa4093822);
    for (unsigned shift = 0; shift < 32U; ++shift)
    {
        for (unsigned index = 0; index < 3000U; ++index)
        {
            const std::uint32_t first = next_random(random);
            const std::uint32_t second = next_random(random);
            model.pcpi_valid = 1;
            model.pcpi_insn = UINT32_C(0x0000200b) | (shift << 25U);
            model.pcpi_rs1 = first;
            model.pcpi_rs2 = second;
            model.eval();
            require(model.pcpi_ready != 0 && model.pcpi_wr != 0 &&
                        model.pcpi_wait == 0 &&
                        model.pcpi_rd == fsri_reference(first, second, shift),
                    "wrong direct fsri response");
        }
    }
    clear_request(model);
}

void require_unclaimed(Vpqc_pcpi_mlkem &model, std::uint32_t instruction)
{
    model.pcpi_valid = 1;
    model.pcpi_insn = instruction;
    model.pcpi_rs1 = UINT32_C(0x12345678);
    model.pcpi_rs2 = UINT32_C(0x89abcdef);
    model.eval();
    require(model.pcpi_wait == 0 && model.pcpi_ready == 0 &&
                model.pcpi_wr == 0,
            "disabled instruction was claimed");
    clear_request(model);
}

void test_reset(Vpqc_pcpi_mlkem &model)
{
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
    require(model.pcpi_ready == 0 && model.pcpi_wr == 0,
            "reset did not cancel work");
}

void test_back_to_back(Vpqc_pcpi_mlkem &model)
{
    model.pcpi_valid = 1;
    model.pcpi_insn = UINT32_C(0x02000033);
    model.pcpi_rs1 = 3;
    model.pcpi_rs2 = 5;
    tick(model);
    tick(model);
    require(model.pcpi_ready != 0 && model.pcpi_rd == 15,
            "first consecutive result failed");
    model.pcpi_rs1 = 7;
    model.pcpi_rs2 = 11;
    model.eval();
    require(model.pcpi_ready == 0 && model.pcpi_wait != 0,
            "second request saw an old result");
    tick(model);
    tick(model);
    require(model.pcpi_ready != 0 && model.pcpi_rd == 77,
            "second consecutive result failed");
    clear_request(model);
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

    test_multiply(model);
    test_reset(model);
    test_back_to_back(model);

#if PQC_TEST_VARIANT == 0
    require_unclaimed(model, UINT32_C(0x0000000b));
    require_unclaimed(model, UINT32_C(0x0000100b));
    require_unclaimed(model, UINT32_C(0x0000200b));
#elif PQC_TEST_VARIANT == 1
    test_fqmul(model);
    require_unclaimed(model, UINT32_C(0x0000100b));
    require_unclaimed(model, UINT32_C(0x0000200b));
#elif PQC_TEST_VARIANT == 2
    test_red32(model);
    require_unclaimed(model, UINT32_C(0x0000000b));
    require_unclaimed(model, UINT32_C(0x0000200b));
#elif PQC_TEST_VARIANT == 3
    test_fsri(model);
    require_unclaimed(model, UINT32_C(0x0000000b));
    require_unclaimed(model, UINT32_C(0x0000100b));
#else
#error invalid test variant
#endif

    require_unclaimed(model, UINT32_C(0x00000013));
}
