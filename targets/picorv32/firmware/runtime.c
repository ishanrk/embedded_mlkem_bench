#include "bench_mmio.h"

extern unsigned char __measured_stack_bottom[];
extern unsigned char __measured_stack_top[];
extern void pqc_call_measured(pqc_bench_fn function, void *context, void *stack_top);

static volatile uint32_t *const begin_mmio =
    (volatile uint32_t *)(uintptr_t)PQC_BENCH_BEGIN_ADDRESS;
static volatile uint32_t *const end_mmio = (volatile uint32_t *)(uintptr_t)PQC_BENCH_END_ADDRESS;
static volatile uint32_t *const status_mmio =
    (volatile uint32_t *)(uintptr_t)PQC_BENCH_STATUS_ADDRESS;
static volatile uint32_t *const terminate_mmio =
    (volatile uint32_t *)(uintptr_t)PQC_BENCH_TERMINATE_ADDRESS;

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

static void empty_call(void *context)
{
    __asm__ volatile("" : : "r"(context) : "memory");
}

static void fill_stack(void)
{
    volatile unsigned char *cursor = __measured_stack_bottom;

    while (cursor != __measured_stack_top)
    {
        *cursor++ = UINT8_C(0xa5);
    }
}

static uint32_t used_stack(void)
{
    volatile const unsigned char *cursor = __measured_stack_bottom;

    while (cursor != __measured_stack_top && *cursor == UINT8_C(0xa5))
    {
        ++cursor;
    }
    return (uint32_t)(__measured_stack_top - cursor);
}

struct pqc_stack_result pqc_measure_stack(pqc_bench_fn function, void *context)
{
    struct pqc_stack_result result;

    fill_stack();
    pqc_call_measured(empty_call, NULL, __measured_stack_top);
    result.wrapper_bytes = used_stack();

    fill_stack();
    pqc_call_measured(function, context, __measured_stack_top);
    result.raw_bytes = used_stack();
    result.calibrated_bytes =
        result.raw_bytes > result.wrapper_bytes ? result.raw_bytes - result.wrapper_bytes : 0U;
    return result;
}
