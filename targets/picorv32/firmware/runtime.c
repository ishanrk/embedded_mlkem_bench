#include "bench_mmio.h"

static volatile uint32_t *const begin_mmio =
    (volatile uint32_t *)(uintptr_t)PQC_BENCH_BEGIN_ADDRESS;
static volatile uint32_t *const end_mmio = (volatile uint32_t *)(uintptr_t)PQC_BENCH_END_ADDRESS;
static volatile uint32_t *const status_mmio =
    (volatile uint32_t *)(uintptr_t)PQC_BENCH_STATUS_ADDRESS;
static volatile uint32_t *const terminate_mmio =
    (volatile uint32_t *)(uintptr_t)PQC_BENCH_TERMINATE_ADDRESS;

// firmware has no standard library so these memory routines are provided here
void *memcpy(void *restrict destination, const void *restrict source, size_t count)
{
    unsigned char *output = destination;
    const unsigned char *input = source;

    while (count != 0U)
    {
        *output++ = *input++;
        --count;
    }
    return destination;
}

void *memset(void *destination, int value, size_t count)
{
    unsigned char *output = destination;

    while (count != 0U)
    {
        *output++ = (unsigned char)value;
        --count;
    }
    return destination;
}

void pqc_bench_begin(void)
{
    // volatile keeps the MMIO store visible to the simulator
    *begin_mmio = 1U;
}

void pqc_bench_end(void)
{
    *end_mmio = 1U;
}

void pqc_status(uint32_t value)
{
    *status_mmio = value;
}

_Noreturn void pqc_terminate(uint32_t value)
{
    pqc_status(value);
    *terminate_mmio = value;
    for (;;)
    {
    }
}

_Noreturn void pqc_trap(uint32_t value)
{
    pqc_status(UINT32_C(0x54524150));
    pqc_terminate(value);
}
