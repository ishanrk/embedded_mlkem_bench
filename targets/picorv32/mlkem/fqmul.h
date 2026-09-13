#ifndef PQC_POLY_MLKEM_FQMUL_H
#define PQC_POLY_MLKEM_FQMUL_H

#include <stdint.h>

// the idea behind this file is that any of this code is only called if PQC_USE_FQMUL is defined.
// FQMUL is simply ONE hardware instruction doing Montgomery_Reduce(a*b)

// implement fqmul in C (we will need this if we want to test the C implementation of FQMUL against the hardware instruction)

// note: fixed_backend.c already has a software fqmul path, this is not a duplicate
// this version models the custom instruction's exact register level behavior so we can test it against the hardware
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

// the actual hardware instruction is implemented in assembly in the file mlkem_native.S, and is called pqc_mlk_fqmul

#if defined(__riscv) && defined(PQC_USE_FQMUL)
static inline int32_t pqc_mlk_fqmul(uint32_t left, uint32_t right)
{
    int32_t result;
    // inline assembly call
    // insn places left and right in source registers
    // the destination register receives result
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
