#include "../targets/picorv32/mlkem/fsri.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << message << '\n';
    std::exit(1);
}

[[nodiscard]] std::uint32_t funnel(std::uint32_t a, std::uint32_t b, unsigned s)
{
    const std::uint64_t value = (static_cast<std::uint64_t>(b) << 32U) | a;
    return static_cast<std::uint32_t>(value >> (s & 31U));
}

[[nodiscard]] std::uint32_t mul_funnel(std::uint32_t a, std::uint32_t b, unsigned s)
{
    s &= 31U;
    if (s == 0U)
    {
        return a;
    }
    const std::uint64_t factor = UINT64_C(1) << (32U - s);
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(a) * factor) >> 32U) |
           static_cast<std::uint32_t>(static_cast<std::uint64_t>(b) * factor);
}

[[nodiscard]] std::uint64_t rol(std::uint64_t value, unsigned shift)
{
    shift &= 63U;
    return shift == 0U ? value : (value << shift) | (value >> (64U - shift));
}

[[nodiscard]] std::uint32_t next(std::uint32_t &state)
{
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

void check(std::uint32_t a, std::uint32_t b)
{
    for (unsigned s = 0; s < 32U; ++s)
    {
        const std::uint32_t expected = funnel(a, b, s);
        if (pqc_fsri_c(a, b, s) != expected || mul_funnel(a, b, s) != expected)
        {
            fail("fsri mismatch");
        }
    }
    const std::uint64_t value = (static_cast<std::uint64_t>(b) << 32U) | a;
    for (unsigned s = 0; s < 64U; ++s)
    {
        if (MLK_KECCAK_ROL(value, s) != rol(value, s))
        {
            fail("rotate mismatch");
        }
    }
}

}

int main()
{
    constexpr std::array<std::uint32_t, 7> values{
        0U,
        1U,
        UINT32_MAX,
        UINT32_C(0x7fffffff),
        UINT32_C(0x80000000),
        UINT32_C(0x55555555),
        UINT32_C(0xaaaaaaaa),
    };
    for (const std::uint32_t a : values)
    {
        for (const std::uint32_t b : values)
        {
            check(a, b);
        }
    }

    std::uint32_t state = UINT32_C(0x243f6a88);
    for (unsigned i = 0; i < 20000U; ++i)
    {
        const std::uint32_t a = next(state);
        const std::uint32_t b = next(state);
        check(a, b);
    }
}
