#ifndef PQC_POLY_BENCH_MMIO_H
#define PQC_POLY_BENCH_MMIO_H

#include <stddef.h>
#include <stdint.h>

// stores to these addresses become simulator events instead of RAM writes
#define PQC_BENCH_BEGIN_ADDRESS UINT32_C(0x10000000)
#define PQC_BENCH_END_ADDRESS UINT32_C(0x10000004)
#define PQC_BENCH_STATUS_ADDRESS UINT32_C(0x10000008)
#define PQC_BENCH_TERMINATE_ADDRESS UINT32_C(0x1000000c)

void pqc_bench_begin(void);
void pqc_bench_end(void);
void pqc_status(uint32_t value);



// execution not returned to caller because pqc_terminate and pqc_trap are used to signal the simulator to stop the benchmark and report an error
_Noreturn void pqc_terminate(uint32_t value);
_Noreturn void pqc_trap(uint32_t value);
#endif
