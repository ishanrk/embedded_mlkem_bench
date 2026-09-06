#ifndef PQC_POLY_MLKEM_RED32_H
#define PQC_POLY_MLKEM_RED32_H

#include <stdint.h>

static inline int32_t pqc_mlk_red32_c(uint32_t value)
{
    const int64_t signed_value =
        value <= (uint32_t)INT32_MAX ? (int64_t)value : (int64_t)value - INT64_C(4294967296);
    const uint32_t inverse = ((value & UINT32_C(0xffff)) * UINT32_C(62209)) & UINT32_C(0xffff);
    const int32_t signed_inverse = (int32_t)(inverse ^ UINT32_C(0x8000)) - INT32_C(32768);
    const int64_t numerator = signed_value - (int64_t)signed_inverse * INT64_C(3329);
    return (int32_t)(numerator / INT64_C(65536));
}

#if defined(__CPROVER)
int32_t pqc_mlk_red32_model(uint32_t value);

static inline int32_t pqc_mlk_red32(uint32_t value)
{
    return pqc_mlk_red32_model(value);
}
#elif defined(__riscv) && defined(PQC_POLY_HAVE_MLK_RED32)
static inline int32_t pqc_mlk_red32(uint32_t value)
{
    int32_t result;
    __asm__ volatile(".insn r 0x0b, 1, 0, %0, %1, x0" : "=r"(result) : "r"(value));
    return result;
}
#else
static inline int32_t pqc_mlk_red32(uint32_t value)
{
    return pqc_mlk_red32_c(value);
}
#endif

static inline int32_t pqc_mlk_fqmul_red32(int16_t left, int16_t right)
{
    int32_t product;
#if defined(__riscv) && defined(PQC_POLY_HAVE_MLK_RED32)
    __asm__ volatile("mul %0, %1, %2"
                     : "=r"(product)
                     : "r"((int32_t)left), "r"((int32_t)right));
#else
    product = (int32_t)left * (int32_t)right;
#endif
    return pqc_mlk_red32((uint32_t)product);
}

#endif
