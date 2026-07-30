#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#if !defined(PQC_MLKEM_K)
#error missing vector width
#endif

extern "C"
{
void pqc_mlkem_ntt(std::int16_t values[256]);
void pqc_mlkem_intt(std::int16_t values[256]);
void pqc_mlkem_tomont(std::int16_t values[256]);
void pqc_mlkem_mulcache_one(std::int16_t cache[128], const std::int16_t values[256]);
void pqc_mlkem_basemul(std::int16_t result[256], const std::int16_t *left,
                       const std::int16_t *right, const std::int16_t *cache);
}

// catches changes to the fixed upstream arithmetic schedule
namespace
{

[[nodiscard]] std::uint32_t next_random(std::uint32_t &state)
{
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

template <std::size_t Size>
void fill(std::array<std::int16_t, Size> &values, std::uint32_t seed)
{
    for (std::int16_t &value : values)
    {
        value = static_cast<std::int16_t>(next_random(seed) % 3329U);
    }
}

template <std::size_t Size>
[[nodiscard]] std::uint64_t hash(const std::array<std::int16_t, Size> &values)
{
    std::uint64_t result = UINT64_C(1469598103934665603);
    for (const std::int16_t value : values)
    {
        const std::uint16_t bits = static_cast<std::uint16_t>(value);
        result ^= bits & UINT16_C(0xff);
        result *= UINT64_C(1099511628211);
        result ^= bits >> 8U;
        result *= UINT64_C(1099511628211);
    }
    return result;
}

void require_hash(std::uint64_t actual, std::uint64_t expected, const char *name)
{
    if (actual != expected)
    {
        std::cerr << name << " hash mismatch actual " << actual << '\n';
        std::exit(1);
    }
}

}

int main()
{
    std::array<std::int16_t, 256> polynomial{};
    fill(polynomial, UINT32_C(0x243f6a88));
    pqc_mlkem_ntt(polynomial.data());
    require_hash(hash(polynomial), UINT64_C(12926178265275373573), "ntt");

    pqc_mlkem_intt(polynomial.data());
    require_hash(hash(polynomial), UINT64_C(14834794734538324059), "intt");

    fill(polynomial, UINT32_C(0x13198a2e));
    pqc_mlkem_tomont(polynomial.data());
    require_hash(hash(polynomial), UINT64_C(7668335553948467243), "tomont");

    std::array<std::int16_t, 256> cache_input{};
    std::array<std::int16_t, 128> one_cache{};
    fill(cache_input, UINT32_C(0x03707344));
    pqc_mlkem_mulcache_one(one_cache.data(), cache_input.data());
    require_hash(hash(one_cache), UINT64_C(14069661187774501224), "mulcache");

    std::array<std::int16_t, 256U * PQC_MLKEM_K> left{};
    std::array<std::int16_t, 256U * PQC_MLKEM_K> right{};
    std::array<std::int16_t, 128U * PQC_MLKEM_K> cache{};
    std::array<std::int16_t, 256> result{};
    fill(left, UINT32_C(0xa4093822));
    fill(right, UINT32_C(0x299f31d0));
    for (unsigned lane = 0; lane < PQC_MLKEM_K; ++lane)
    {
        pqc_mlkem_mulcache_one(cache.data() + lane * 128U,
                               right.data() + lane * 256U);
    }
    pqc_mlkem_basemul(result.data(), left.data(), right.data(), cache.data());
#if PQC_MLKEM_K == 2
    require_hash(hash(result), UINT64_C(8782052292100032123), "basemul");
#elif PQC_MLKEM_K == 3
    require_hash(hash(result), UINT64_C(8973906868874500890), "basemul");
#else
    require_hash(hash(result), UINT64_C(14309811301560247266), "basemul");
#endif
}
