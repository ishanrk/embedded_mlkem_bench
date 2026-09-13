#include "../targets/picorv32/mlkem/red32.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

// catches signed input and Montgomery reduction mistakes
namespace
{

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << message << '\n';
    std::exit(1);
}

[[nodiscard]] std::int64_t signed_value(std::uint32_t value)
{
    return value <= static_cast<std::uint32_t>(INT32_MAX)
               ? static_cast<std::int64_t>(value)
               : static_cast<std::int64_t>(value) - INT64_C(4294967296);
}

[[nodiscard]] std::int32_t oracle(std::uint32_t value)
{
    const std::uint32_t inverse =
        ((value & UINT32_C(0xffff)) * UINT32_C(62209)) & UINT32_C(0xffff);
    const std::int32_t signed_inverse =
        static_cast<std::int32_t>(inverse ^ UINT32_C(0x8000)) - INT32_C(32768);
    const std::int64_t numerator =
        signed_value(value) - static_cast<std::int64_t>(signed_inverse) * INT64_C(3329);
    if (numerator % INT64_C(65536) != 0)
    {
        fail("red32 numerator is not divisible");
    }
    return static_cast<std::int32_t>(numerator / INT64_C(65536));
}

void check(std::uint32_t value)
{
    const std::int32_t expected = oracle(value);
    if (pqc_mlk_red32_c(value) != expected || pqc_mlk_red32(value) != expected)
    {
        fail("red32 result mismatch");
    }
    const std::int64_t congruence =
        static_cast<std::int64_t>(expected) * INT64_C(65536) - signed_value(value);
    if (congruence % INT64_C(3329) != 0)
    {
        fail("red32 congruence mismatch");
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
    // values around both signed boundaries catch extension mistakes
    constexpr std::array<std::uint32_t, 13> boundaries{
        UINT32_C(0x00000000), UINT32_C(0x00000001), UINT32_C(0xffffffff),
        UINT32_C(0x7fffffff), UINT32_C(0x80000000), UINT32_C(0x00007fff),
        UINT32_C(0x00008000), UINT32_C(0xffff7fff), UINT32_C(0xffff8000),
        UINT32_C(0x0000ffff), UINT32_C(0x7fff0000), UINT32_C(0x80000001),
        UINT32_C(0x89abcdef)};
    for (const std::uint32_t value : boundaries)
    {
        check(value);
    }

    for (std::uint32_t low = 0; low <= UINT32_C(0xffff); ++low)
    {
        check(UINT32_C(0x89ab0000) | low);
    }

    std::uint32_t state = UINT32_C(0x243f6a88);
    for (unsigned index = 0; index < 100000U; ++index)
    {
        check(next_random(state));
    }
}
