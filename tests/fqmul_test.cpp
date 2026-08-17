#include "../targets/picorv32/mlkem/fqmul.h"

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
    std::cerr << message << '\n';
    std::exit(1);
}

[[nodiscard]] std::int32_t signed_low(std::uint32_t value)
{
    const std::uint32_t low = value & UINT32_C(0xffff);
    return low < UINT32_C(0x8000) ? static_cast<std::int32_t>(low)
                                  : static_cast<std::int32_t>(low) - INT32_C(65536);
}

[[nodiscard]] std::int32_t oracle(std::uint32_t left, std::uint32_t right)
{
    const std::int64_t product = static_cast<std::int64_t>(signed_low(left)) * signed_low(right);
    const std::uint32_t low =
        static_cast<std::uint32_t>(static_cast<std::uint64_t>(product) & UINT64_C(0xffff));
    const std::uint32_t inverse = low * UINT32_C(62209) & UINT32_C(0xffff);
    const std::int64_t numerator =
        product - static_cast<std::int64_t>(signed_low(inverse)) * INT64_C(3329);
    if (numerator % INT64_C(65536) != 0)
    {
        fail("fqmul numerator is not divisible");
    }
    const std::int64_t result = numerator / INT64_C(65536);
    if (result < std::numeric_limits<std::int16_t>::min() ||
        result > std::numeric_limits<std::int16_t>::max())
    {
        fail("fqmul result exceeds signed 16 bits");
    }
    return static_cast<std::int32_t>(result);
}

void check(std::uint32_t left, std::uint32_t right)
{
    const std::int32_t expected = oracle(left, right);
    if (pqc_mlk_fqmul_c(left, right) != expected || pqc_mlk_fqmul(left, right) != expected)
    {
        fail("fqmul result mismatch");
    }
    const std::int64_t congruence = static_cast<std::int64_t>(expected) * INT64_C(65536) -
                                    static_cast<std::int64_t>(signed_low(left)) * signed_low(right);
    if (congruence % INT64_C(3329) != 0)
    {
        fail("fqmul congruence mismatch");
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
    constexpr std::array<std::int32_t, 9> values{0,
                                                 1,
                                                 -1,
                                                 3328,
                                                 -3328,
                                                 -4096,
                                                 4096,
                                                 std::numeric_limits<std::int16_t>::min(),
                                                 std::numeric_limits<std::int16_t>::max()};
    for (const std::int32_t left : values)
    {
        for (const std::int32_t right : values)
        {
            check(static_cast<std::uint32_t>(left), static_cast<std::uint32_t>(right));
        }
    }

    for (std::uint32_t low = 0; low <= UINT32_C(0xffff); ++low)
    {
        check(low, UINT32_C(0x89abcdef));
    }

    std::uint32_t state = UINT32_C(0x243f6a88);
    for (unsigned i = 0; i < 20000U; ++i)
    {
        const std::uint32_t left = next_random(state);
        const std::uint32_t right = next_random(state);
        check(left, right);
        check(left ^ UINT32_C(0xffff0000), right ^ UINT32_C(0x55550000));
    }
}
