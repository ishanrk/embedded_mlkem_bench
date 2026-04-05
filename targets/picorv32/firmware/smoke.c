#include "bench_mmio.h"

#include <stdint.h>

#if !defined(PQC_TEST_MISALIGNED) && !defined(PQC_TEST_ILLEGAL)

static volatile uint8_t byte_probe;
static volatile uint16_t half_probe;
static volatile uint32_t word_probe;
static volatile uint32_t random_checksum;
static volatile uint32_t arithmetic_probe;

static uint64_t mul_bits(uint32_t left, uint32_t right, int left_signed, int right_signed)
{
    const int negative = (left_signed != 0 && (left & UINT32_C(0x80000000)) != 0U) ^
                         (right_signed != 0 && (right & UINT32_C(0x80000000)) != 0U);
    uint32_t left_magnitude = left;
    uint32_t right_magnitude = right;
    volatile uint64_t product = 0U;

    if (left_signed != 0 && (left & UINT32_C(0x80000000)) != 0U)
    {
        left_magnitude = ~left + 1U;
    }
    if (right_signed != 0 && (right & UINT32_C(0x80000000)) != 0U)
    {
        right_magnitude = ~right + 1U;
    }
    for (unsigned i = 0; i < 32U; ++i)
    {
        if (((right_magnitude >> i) & 1U) != 0U)
        {
            product += (uint64_t)left_magnitude << i;
        }
    }
    return negative != 0 ? ~product + 1U : product;
}

static uint32_t instruction_mul(uint32_t left, uint32_t right)
{
    uint32_t result;
    __asm__ volatile("mul %0, %1, %2" : "=r"(result) : "r"(left), "r"(right));
    return result;
}

static uint32_t instruction_mulh(uint32_t left, uint32_t right)
{
    uint32_t result;
    __asm__ volatile("mulh %0, %1, %2" : "=r"(result) : "r"(left), "r"(right));
    return result;
}

static uint32_t instruction_mulhsu(uint32_t left, uint32_t right)
{
    uint32_t result;
    __asm__ volatile("mulhsu %0, %1, %2" : "=r"(result) : "r"(left), "r"(right));
    return result;
}

static uint32_t instruction_mulhu(uint32_t left, uint32_t right)
{
    uint32_t result;
    __asm__ volatile("mulhu %0, %1, %2" : "=r"(result) : "r"(left), "r"(right));
    return result;
}

static uint32_t next_random(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    *state = value;
    return value;
}

static int check_multiply_pair(uint32_t left, uint32_t right)
{
    const uint64_t uu = mul_bits(left, right, 0, 0);
    const uint64_t ss = mul_bits(left, right, 1, 1);
    const uint64_t su = mul_bits(left, right, 1, 0);

    return instruction_mul(left, right) == (uint32_t)uu &&
           instruction_mulh(left, right) == (uint32_t)(ss >> 32U) &&
           instruction_mulhsu(left, right) == (uint32_t)(su >> 32U) &&
           instruction_mulhu(left, right) == (uint32_t)(uu >> 32U);
}

static int check_multiply(void)
{
    static const uint32_t values[] = {
        0U,
        1U,
        UINT32_MAX,
        UINT32_C(0x7fffffff),
        UINT32_C(0x80000000),
        UINT32_C(0x55555555),
        UINT32_C(0xaaaaaaaa),
    };
    uint32_t state = UINT32_C(0x9e3779b9);
    uint32_t checksum = 0U;

    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
    {
        for (unsigned j = 0; j < sizeof(values) / sizeof(values[0]); ++j)
        {
            if (!check_multiply_pair(values[i], values[j]))
            {
                return 0;
            }
        }
    }
    for (unsigned i = 0; i < 4096U; ++i)
    {
        const uint32_t left = next_random(&state);
        const uint32_t right = next_random(&state);
        if (!check_multiply_pair(left, right))
        {
            return 0;
        }
        checksum ^= instruction_mul(left, right);
    }
    random_checksum = checksum;
    return 1;
}

static int check_division(void)
{
    uint32_t div_result;
    uint32_t divu_result;
    uint32_t rem_result;
    uint32_t remu_result;
    const uint32_t minimum = UINT32_C(0x80000000);
    const uint32_t minus_one = UINT32_MAX;
    const uint32_t zero = 0U;

    __asm__ volatile("div %0, %1, %2" : "=r"(div_result) : "r"(minimum), "r"(minus_one));
    __asm__ volatile("divu %0, %1, %2" : "=r"(divu_result) : "r"(minus_one), "r"(zero));
    __asm__ volatile("rem %0, %1, %2" : "=r"(rem_result) : "r"(minimum), "r"(minus_one));
    __asm__ volatile("remu %0, %1, %2" : "=r"(remu_result) : "r"(minus_one), "r"(zero));

    return div_result == minimum && divu_result == UINT32_MAX && rem_result == 0U &&
           remu_result == UINT32_MAX;
}

static int check_base(void)
{
    arithmetic_probe = 7U;
    arithmetic_probe = (arithmetic_probe + 5U) << 2U;
    arithmetic_probe ^= 3U;
    byte_probe = UINT8_C(0xa7);
    half_probe = UINT16_C(0x5aa5);
    word_probe = UINT32_C(0x12345678);
    __asm__ volatile(".option push\n.option rvc\nc.nop\n.option pop");

    return arithmetic_probe == 51U && byte_probe == UINT8_C(0xa7) &&
           half_probe == UINT16_C(0x5aa5) && word_probe == UINT32_C(0x12345678);
}

static void measured_multiply(void *context)
{
    volatile uint32_t *result = context;
    volatile uint32_t lane[8];
    uint32_t value = UINT32_C(0x12345678);

    for (unsigned i = 0; i < 8U; ++i)
    {
        lane[i] = value ^ i;
    }
    for (unsigned i = 0; i < 64U; ++i)
    {
        const unsigned index = i & 7U;
        value = instruction_mul(value ^ lane[index], UINT32_C(0x9e3779b9));
        lane[index] = value;
    }
    *result = value ^ lane[0];
}

#endif

int main(void)
{
#if defined(PQC_TEST_MISALIGNED)
    uint32_t value;
    const uintptr_t address = 1U;
    __asm__ volatile("lw %0, 0(%1)" : "=r"(value) : "r"(address) : "memory");
    pqc_status(value);
    pqc_trap(UINT32_C(0xbad00001));
#elif defined(PQC_TEST_ILLEGAL)
    __asm__ volatile(".word 0x0000000b");
    pqc_trap(UINT32_C(0xbad00002));
#else
    struct pqc_stack_result stack;
    uint32_t measured = 0U;

    if (!check_base())
    {
        pqc_trap(UINT32_C(0xbad00010));
    }
    if (!check_division())
    {
        pqc_trap(UINT32_C(0xbad00011));
    }
    if (!check_multiply())
    {
        pqc_trap(UINT32_C(0xbad00012));
    }

    pqc_bench_begin();
    pqc_bench_end();
    pqc_bench_begin();
    measured_multiply(&measured);
    pqc_bench_end();

    stack = pqc_measure_stack(measured_multiply, &measured);
    if (stack.raw_bytes == 0U || stack.raw_bytes < stack.wrapper_bytes)
    {
        pqc_trap(UINT32_C(0xbad00013));
    }
    pqc_status(UINT32_C(0x53544100));
    pqc_status(stack.wrapper_bytes);
    pqc_status(UINT32_C(0x53544101));
    pqc_status(stack.raw_bytes);
    pqc_status(UINT32_C(0x53544102));
    pqc_status(stack.calibrated_bytes);
    pqc_status(random_checksum ^ measured);
    pqc_terminate(0U);
#endif
}
