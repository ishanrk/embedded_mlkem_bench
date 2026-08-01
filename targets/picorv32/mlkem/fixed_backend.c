#include "fqmul.h"
#include "red32.h"

#include <limits.h>
#include <stdint.h>

// standard errors
#if defined(PQC_USE_FQMUL) && defined(PQC_USE_RED32)
#error select one arithmetic instruction
#endif

#if !defined(PQC_MLKEM_K) || PQC_MLKEM_K < 2 || PQC_MLKEM_K > 4
#error PQC_MLKEM_K must be 2 3 or 4
#endif

#define PQC_MLKEM_N 256
#define PQC_MLKEM_Q 3329

#if defined(__GNUC__) || defined(__clang__)

// inline the functions since ntt related functions are called many times and are small enough to be inlined
#define PQC_FORCE_INLINE static inline __attribute__((always_inline))
#else
#define PQC_FORCE_INLINE static inline
#endif

// stores complex roots * 2^16 reduced mod 3329 (which is q)
static const int16_t pqc_zetas[128] = {
    -1044, -758, -359, -1517, 1493, 1422, 287, 202, -171, 622, 1577, 182, 962,
    -1202, -1474, 1468, 573, -1325, 264, 383, -829, 1458, -1602, -130, -681,
    1017, 732, 608, -1542, 411, -205, -1571, 1223, 652, -552, 1015, -1293,
    1491, -282, -1544, 516, -8, -320, -666, -1618, -1162, 126, 1469, -853,
    -90, -271, 830, 107, -1421, -247, -951, -398, 961, -1508, -725, 448,
    -1065, 677, -1275, -1103, 430, 555, 843, -1251, 871, 1550, 105, 422,
    587, 177, -235, -291, -460, 1574, 1653, -246, 778, 1159, -147, -777,
    1483, -602, 1119, -1590, 644, -872, 349, 418, 329, -156, -75, 817,
    1097, 603, 610, 1322, -1285, -1465, 384, -1215, -136, 1218, -1335,
    -874, 220, -1187, -1659, -1185, -1530, -1278, 794, -1510, -854, -870,
    478, -108, -308, 996, 991, 958, -1460, 1522, 1628,
};

// turns one wide product back into the coefficient domain
PQC_FORCE_INLINE int16_t pqc_montgomery_reduce(int32_t value)
{
// checks flag to see if this build uses the actual RED32 hardware instruction, otherwise it
// does the usual Montgomery reduction
#if defined(PQC_USE_RED32)
    return (int16_t)pqc_mlk_red32((uint32_t)value);
#else
    // quick note use cast to 16 bit to do quick mod 2^16
    // have u = value * q^-1 mod 2^16
    const uint16_t inverted =
        (uint16_t)((uint32_t)(uint16_t)value * UINT32_C(62209));

    // interpret u as a signed 16 bit integer
    const int32_t signed_inverted = inverted <= INT16_MAX
                                        ? (int32_t)inverted
                                        : (int32_t)inverted - INT32_C(65536);

    // divide by 2^16 to get the final result
    return (int16_t)((value - signed_inverted * PQC_MLKEM_Q) >> 16);
#endif
}

// this is the only field multiply changed by FQMUL 
PQC_FORCE_INLINE int16_t pqc_fqmul(int16_t left, int16_t right)
{
#if defined(PQC_USE_FQMUL)
    return (int16_t)pqc_mlk_fqmul((uint32_t)(int32_t)left,
                                  (uint32_t)(int32_t)right);
#else
    return pqc_montgomery_reduce((int32_t)left * (int32_t)right);
#endif
}

PQC_FORCE_INLINE int16_t pqc_barrett_reduce(int16_t value)
{
    const int32_t quotient =
        (INT32_C(20159) * value + (INT32_C(1) << 25)) >> 26;
    return (int16_t)(value - quotient * PQC_MLKEM_Q);
}

PQC_FORCE_INLINE void pqc_ntt_layer(int16_t values[256], unsigned length,
                                    unsigned zeta_index)
{
    for (unsigned start = 0; start < PQC_MLKEM_N; start += 2U * length)
    {
        const int16_t zeta = pqc_zetas[zeta_index++];
        for (unsigned index = start; index < start + length; ++index)
        {
            const int16_t product = pqc_fqmul(values[index + length], zeta);
            const int16_t left = values[index];
            values[index] = (int16_t)(left + product);
            values[index + length] = (int16_t)(left - product);
        }
    }
}

PQC_FORCE_INLINE void pqc_intt_layer(int16_t values[256], unsigned length,
                                     unsigned zeta_index)
{
    for (unsigned start = 0; start < PQC_MLKEM_N; start += 2U * length)
    {
        const int16_t zeta = pqc_zetas[zeta_index--];
        for (unsigned index = start; index < start + length; ++index)
        {
            const int16_t left = values[index];
            const int16_t right = values[index + length];
            values[index] = pqc_barrett_reduce((int16_t)(left + right));
            values[index + length] =
                pqc_fqmul((int16_t)(right - left), zeta);
        }
    }
}

// fixed forward transform schedule (referenced from mlkem native)
void pqc_mlkem_ntt(int16_t values[256])
{
    pqc_ntt_layer(values, 128, 1);
    pqc_ntt_layer(values, 64, 2);
    pqc_ntt_layer(values, 32, 4);
    pqc_ntt_layer(values, 16, 8);
    pqc_ntt_layer(values, 8, 16);
    pqc_ntt_layer(values, 4, 32);
    pqc_ntt_layer(values, 2, 64);
}

// fixed inverse transform schedule (referenced from mlkem native)
void pqc_mlkem_intt(int16_t values[256])
{
    for (unsigned index = 0; index < PQC_MLKEM_N; ++index)
    {
        values[index] = pqc_fqmul(values[index], 1441);
    }
    pqc_intt_layer(values, 2, 127);
    pqc_intt_layer(values, 4, 63);
    pqc_intt_layer(values, 8, 31);
    pqc_intt_layer(values, 16, 15);
    pqc_intt_layer(values, 32, 7);
    pqc_intt_layer(values, 64, 3);
    pqc_intt_layer(values, 128, 1);
}

// caches the two zeta products reused by base multiplication
void pqc_mlkem_mulcache_one(int16_t cache[128], const int16_t values[256])
{
    for (unsigned index = 0; index < 64; ++index)
    {
        cache[2U * index] =
            pqc_fqmul(values[4U * index + 1U], pqc_zetas[64U + index]);
        cache[2U * index + 1U] =
            pqc_fqmul(values[4U * index + 3U],
                      (int16_t)-pqc_zetas[64U + index]);
    }
}

//  post ntt multiplication of two polynomials in the NTT domain, using cached zeta products
void pqc_mlkem_basemul(int16_t result[256], const int16_t *left,
                       const int16_t *right, const int16_t *cache)
{
    for (unsigned index = 0; index < 128; ++index)
    {
        int32_t first = 0;
        int32_t second = 0;
        for (unsigned lane = 0; lane < PQC_MLKEM_K; ++lane)
        {
            const unsigned offset = lane * 256U + 2U * index;
            const int16_t cached = cache[lane * 128U + index];
            first += (int32_t)left[offset + 1U] * cached +
                     (int32_t)left[offset] * right[offset];
            second += (int32_t)left[offset] * right[offset + 1U] +
                      (int32_t)left[offset + 1U] * right[offset];
        }
        result[2U * index] = pqc_montgomery_reduce(first);
        result[2U * index + 1U] = pqc_montgomery_reduce(second);
    }
}

// converts  all coefficients to the Montgomery representation
void pqc_mlkem_tomont(int16_t values[256])
{
    for (unsigned index = 0; index < PQC_MLKEM_N; ++index)
    {
        values[index] = pqc_fqmul(values[index], 1353);
    }
}
