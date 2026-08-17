#ifndef PQC_POLY_BENCH_MMIO_H
#define PQC_POLY_BENCH_MMIO_H

#include <stddef.h>
#include <stdint.h>

#define PQC_BENCH_BEGIN_ADDRESS UINT32_C(0x10000000)
#define PQC_BENCH_END_ADDRESS UINT32_C(0x10000004)
#define PQC_BENCH_STATUS_ADDRESS UINT32_C(0x10000008)
#define PQC_BENCH_TERMINATE_ADDRESS UINT32_C(0x1000000c)

typedef void (*pqc_bench_fn)(void *context);

struct pqc_stack_result
{
    uint32_t wrapper_bytes;
    uint32_t raw_bytes;
    uint32_t calibrated_bytes;
};

void pqc_bench_begin(void);
void pqc_bench_end(void);
void pqc_status(uint32_t value);
_Noreturn void pqc_terminate(uint32_t value);
_Noreturn void pqc_trap(uint32_t value);
struct pqc_stack_result pqc_measure_stack(pqc_bench_fn function, void *context);

#endif
