#ifndef PQC_POLY_BENCH_FSRI_H
#define PQC_POLY_BENCH_FSRI_H

#include <stdint.h>

static inline uint32_t pqc_fsri_c(uint32_t a, uint32_t b, unsigned s)
{
    s &= 31U;
    return s == 0U ? a : (a >> s) | (b << (32U - s));
}

#if defined(__riscv)
#if !defined(__riscv_xlen) || __riscv_xlen != 32
#error fsri requires rv32
#endif
#define PQC_FSRI(a, b, s)                                                        \
    __extension__({                                                              \
        uint32_t r;                                                              \
        __asm__ volatile(".insn r 0x0b, 2, %3, %0, %1, %2"                      \
                         : "=r"(r)                                               \
                         : "r"(a), "r"(b), "i"(s));                             \
        r;                                                                       \
    })
#else
#define PQC_FSRI(a, b, s) pqc_fsri_c((a), (b), (s))
#endif

#define MLK_KECCAK_ROL(v, n)                                                     \
    __extension__({                                                              \
        uint64_t pqc_fsri_x = (v);                                               \
        uint32_t pqc_fsri_lo = (uint32_t)pqc_fsri_x;                             \
        uint32_t pqc_fsri_hi = (uint32_t)(pqc_fsri_x >> 32U);                    \
        uint32_t pqc_fsri_rl;                                                    \
        uint32_t pqc_fsri_rh;                                                    \
        if ((n) == 0)                                                            \
        {                                                                        \
            pqc_fsri_rl = pqc_fsri_lo;                                           \
            pqc_fsri_rh = pqc_fsri_hi;                                           \
        }                                                                        \
        else if ((n) <= 32)                                                      \
        {                                                                        \
            pqc_fsri_rl =                                                       \
                PQC_FSRI(pqc_fsri_hi, pqc_fsri_lo, (32 - (n)) & 31);             \
            pqc_fsri_rh =                                                       \
                PQC_FSRI(pqc_fsri_lo, pqc_fsri_hi, (32 - (n)) & 31);             \
        }                                                                        \
        else                                                                     \
        {                                                                        \
            pqc_fsri_rl =                                                       \
                PQC_FSRI(pqc_fsri_lo, pqc_fsri_hi, (64 - (n)) & 31);             \
            pqc_fsri_rh =                                                       \
                PQC_FSRI(pqc_fsri_hi, pqc_fsri_lo, (64 - (n)) & 31);             \
        }                                                                        \
        ((uint64_t)pqc_fsri_rh << 32U) | pqc_fsri_rl;                            \
    })

#endif
