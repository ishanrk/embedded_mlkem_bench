#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace
{
// calculates expected results for the proposed packed butterfly
std::int32_t sign16(std::uint32_t value)
{
    const auto low = static_cast<std::int32_t>(value & 65535U);
    return low >= 32768 ? low - 65536 : low;
}
std::uint32_t butterfly(std::uint32_t packed, std::uint32_t zeta)
{
    const auto a = sign16(packed), b = sign16(packed >> 16), z = sign16(zeta);
    if (a < -26632 || a > 26632 || b < -26632 || b > 26632 || z < -1664 || z > 1664)
        throw std::out_of_range("outside the proposed forward butterfly contract");
    const std::int64_t product = static_cast<std::int64_t>(b) * z;
    const auto inverse = sign16(static_cast<std::uint32_t>(product * 62209));
    const auto t = static_cast<std::int32_t>((product - static_cast<std::int64_t>(inverse) * 3329) / 65536);
    const std::int32_t lo = a + t, hi = a - t;
    if (lo < -32768 || lo > 32767 || hi < -32768 || hi > 32767)
        throw std::overflow_error("packed output does not fit signed16");
    return static_cast<std::uint16_t>(lo) | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(hi)) << 16);
}
}
int main()
{
    // checks every zeta and selected coefficient limits
    unsigned count = 0;
    for (const int a : {-26632, -3328, 0, 3328, 26632})
        for (const int b : {-26632, -3328, 0, 3328, 26632})
            for (int z = -1664; z <= 1664; ++z)
            {
                const auto packed = static_cast<std::uint16_t>(a) | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(b)) << 16);
                const auto result = butterfly(packed, static_cast<std::uint32_t>(z));
                const auto lo = sign16(result), hi = sign16(result >> 16);
                // sum and difference checks expose swapped or truncated result halves
                if (lo + hi != 2 * a) return 1;
                const auto t = lo - a;
                if ((static_cast<std::int64_t>(t) * 65536 - static_cast<std::int64_t>(b) * z) % 3329 != 0) return 2;
                ++count;
            }
    bool rejected = false;
    try { (void)butterfly(32767U, 1); } catch (const std::out_of_range &) { rejected = true; }
    if (!rejected) return 3;
    std::cout << count << " packed range and independent modular identities passed\n";
}
