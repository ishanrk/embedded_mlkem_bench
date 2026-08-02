#ifndef PQC_POLY_MLKEM_DOT2X_H
#define PQC_POLY_MLKEM_DOT2X_H

#include <stdint.h>

static inline int32_t pqc_mlk_dot2x_signed16(uint32_t value)
{
    const uint32_t low = value & UINT32_C(0xffff);
    return (int32_t)(low ^ UINT32_C(0x8000)) - INT32_C(32768);
}

static inline int32_t pqc_mlk_dot2x_c(uint32_t left, uint32_t right)
{
    const int32_t a0 = pqc_mlk_dot2x_signed16(left);
    const int32_t a1 = pqc_mlk_dot2x_signed16(left >> 16);
    const int32_t b0 = pqc_mlk_dot2x_signed16(right);
    const int32_t b1 = pqc_mlk_dot2x_signed16(right >> 16);
    const int64_t sum = (int64_t)a0 * b1 + (int64_t)a1 * b0;
    const uint32_t bits = (uint32_t)sum;
    return bits <= UINT32_C(0x7fffffff)
               ? (int32_t)bits
               : (int32_t)((int64_t)bits - INT64_C(4294967296));
}

#if defined(__riscv) && defined(PQC_USE_DOT2X)
static inline int32_t pqc_mlk_dot2x(uint32_t left, uint32_t right)
{
    int32_t result;
    __asm__ volatile(".insn r 0x0b, 3, 0, %0, %1, %2"
                     : "=r"(result)
                     : "r"(left), "r"(right));
    return result;
}
#else
static inline int32_t pqc_mlk_dot2x(uint32_t left, uint32_t right)
{
    return pqc_mlk_dot2x_c(left, right);
}
#endif

#endif
