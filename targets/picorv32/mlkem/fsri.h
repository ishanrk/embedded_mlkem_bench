#ifndef PQC_POLY_BENCH_FSRI_H
#define PQC_POLY_BENCH_FSRI_H

#include <stdint.h>

// fsri is used to build keccak's 64 bit rotate on rv32 using two 32 bit operations
// example: v = 0xaabbccdd11223344, rotate left by 8
// high = 0xaabbccdd, low = 0x11223344
// fsri(high, low, 24) = 0x223344aa   // new low
// fsri(low, high, 24)  = 0xbbccdd11   // new high
// join them to get 0xbbccdd11223344aa
// this is used inside MLK_KECCAK_ROL, which keccak calls during sha3 and shake

static inline uint32_t pqc_fsri_c(uint32_t a, uint32_t b, unsigned s)
{
    // returns the low word after shifting the joined source register pair
    s &= 31U;
    return s == 0U ? a : (a >> s) | (b << (32U - s));
}

#if defined(__riscv) && defined(PQC_USE_FSRI)
#if !defined(__riscv_xlen) || __riscv_xlen != 32
#error fsri requires rv32
#endif
#define PQC_FSRI(a, b, s)                                                        \
    /* GNU statement expression makes the instruction return a C value */        \
    __extension__({                                                              \
        uint32_t r;                                                              \
        /* funct7 stores the shift amount */                                      \
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
        /* two RV32 funnel shifts build one 64 bit Keccak rotation */             \
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
