#include "../targets/picorv32/mlkem/dot2x.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{

[[nodiscard]] std::int32_t signed_half(std::uint32_t value, unsigned shift)
{
    const std::uint32_t bits = (value >> shift) & UINT32_C(0xffff);
    return bits < UINT32_C(0x8000) ? static_cast<std::int32_t>(bits)
                                   : static_cast<std::int32_t>(bits) - INT32_C(65536);
}

[[nodiscard]] std::int32_t oracle(std::uint32_t left, std::uint32_t right)
{
    const std::int64_t value =
        static_cast<std::int64_t>(signed_half(left, 0U)) * signed_half(right, 16U) +
        static_cast<std::int64_t>(signed_half(left, 16U)) * signed_half(right, 0U);
    const std::uint32_t bits = static_cast<std::uint32_t>(value);
    return bits <= UINT32_C(0x7fffffff)
               ? static_cast<std::int32_t>(bits)
               : static_cast<std::int32_t>(static_cast<std::int64_t>(bits) -
                                           INT64_C(4294967296));
}

[[nodiscard]] std::uint32_t pack(std::int32_t low, std::int32_t high)
{
    return static_cast<std::uint32_t>(static_cast<std::uint16_t>(low)) |
           (static_cast<std::uint32_t>(static_cast<std::uint16_t>(high)) << 16U);
}

void check(std::uint32_t left, std::uint32_t right)
{
    const std::int32_t expected = oracle(left, right);
    if (pqc_mlk_dot2x_c(left, right) != expected ||
        pqc_mlk_dot2x(left, right) != expected)
    {
        std::cerr << "dot2x result mismatch\n";
        std::exit(1);
    }
}

[[nodiscard]] std::uint32_t next_random(std::uint32_t &state)
{
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

}

int main()
{
    constexpr std::int32_t minimum = std::numeric_limits<std::int16_t>::min();
    constexpr std::int32_t maximum = std::numeric_limits<std::int16_t>::max();
    const std::array<std::uint32_t, 11> values{
        UINT32_C(0x00000000), UINT32_C(0x00010001), UINT32_C(0xffffffff),
        pack(minimum, minimum), pack(maximum, maximum), pack(1, -1),
        pack(-1, 1), pack(minimum, maximum), pack(maximum, minimum),
        pack(12345, -23456), pack(-30000, 20000)};

    for (const std::uint32_t left : values)
    {
        for (const std::uint32_t right : values)
        {
            check(left, right);
        }
    }

    check(pack(maximum, maximum), pack(maximum, maximum));
    check(pack(minimum, minimum), pack(minimum, minimum));
    check(pack(maximum, maximum), pack(-maximum, -maximum));
    check(pack(minimum, maximum), pack(maximum, minimum));

    std::uint32_t state = UINT32_C(0x243f6a88);
    for (unsigned index = 0; index < 100000U; ++index)
    {
        check(next_random(state), next_random(state));
    }
}
