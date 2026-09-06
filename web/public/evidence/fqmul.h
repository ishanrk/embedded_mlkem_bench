#ifndef PQC_POLY_MLKEM_FQMUL_H
#define PQC_POLY_MLKEM_FQMUL_H

#include <stdint.h>

static inline int32_t pqc_mlk_signed16(uint32_t value)
{
    const uint32_t low = value & UINT32_C(0xffff);
    return (int32_t)(low ^ UINT32_C(0x8000)) - INT32_C(32768);
}

static inline int32_t pqc_mlk_fqmul_c(uint32_t left, uint32_t right)
{
    const int32_t a = pqc_mlk_signed16(left);
    const int32_t b = pqc_mlk_signed16(right);
    const int32_t product = a * b;
    const uint32_t low =
        ((left & UINT32_C(0xffff)) * (right & UINT32_C(0xffff))) & UINT32_C(0xffff);
    const uint32_t inverse = (low * UINT32_C(62209)) & UINT32_C(0xffff);
    const int32_t signed_inverse = pqc_mlk_signed16(inverse);
    const int32_t numerator = product - signed_inverse * INT32_C(3329);
    return numerator / INT32_C(65536);
}

#if defined(__CPROVER)
int32_t pqc_mlk_fqmul_model(uint32_t left, uint32_t right);

static inline int32_t pqc_mlk_fqmul(uint32_t left, uint32_t right)
{
    return pqc_mlk_fqmul_model(left, right);
}
#elif defined(PQC_POLY_HAVE_MLK_FQMUL)
static inline int32_t pqc_mlk_fqmul(uint32_t left, uint32_t right)
{
    int32_t result;
    __asm__ volatile(".insn r 0x0b, 0, 0, %0, %1, %2" : "=r"(result) : "r"(left), "r"(right));
    return result;
}
#else
static inline int32_t pqc_mlk_fqmul(uint32_t left, uint32_t right)
{
    return pqc_mlk_fqmul_c(left, right);
}
#endif

#endif
