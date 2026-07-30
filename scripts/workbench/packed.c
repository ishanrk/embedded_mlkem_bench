#include <stdint.h>
#include "fqmul.h"

// compile-only experiment comparing a proposed packed butterfly with normal FQMUL
static inline void butterfly(int16_t a, int16_t b, int16_t z, int16_t *left, int16_t *right)
{
#if defined(PQC_PACKED)
    // proposed instruction takes a/b in the two rs1 halves and returns both outputs in rd
    const uint32_t inputs = (uint16_t)a | ((uint32_t)(uint16_t)b << 16);
    uint32_t result;
    __asm__ volatile(".insn r 0x0b, 3, 0, %0, %1, %2" : "=r"(result) : "r"(inputs), "r"((int32_t)z));
    *left = (int16_t)(uint16_t)result;
    *right = (int16_t)(uint16_t)(result >> 16);
#else
    // existing sequence is the baseline whose loop body is inspected in disassembly
    const int32_t t = pqc_mlk_fqmul((uint32_t)(int32_t)b, (uint32_t)(int32_t)z);
    *left = (int16_t)((int32_t)a + t);
    *right = (int16_t)((int32_t)a - t);
#endif
}

void pqc_forward_layer(int16_t r[256], int16_t z)
{
    for (unsigned j = 0; j < 128; ++j)
        butterfly(r[j], r[j + 128], z, &r[j], &r[j + 128]);
}

void pqc_forward_pair(int16_t r[256], int16_t z0, int16_t z1, int16_t z2)
{
    for (unsigned j = 0; j < 64; ++j)
    {
        int16_t u0, v0, u1, v1;
        butterfly(r[j], r[j + 128], z0, &u0, &v0);
        butterfly(r[j + 64], r[j + 192], z0, &u1, &v1);
        butterfly(u0, u1, z1, &r[j], &r[j + 64]);
        butterfly(v0, v1, z2, &r[j + 128], &r[j + 192]);
    }
}
