#include "../targets/picorv32/mlkem/fqmul.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

uint32_t nondet_uint32_t(void);
uint16_t nondet_uint16_t(void);
int16_t nondet_int16_t(void);
unsigned nondet_unsigned(void);

static int64_t fqmul_numerator(uint32_t left, uint32_t right)
{
    const uint32_t left_low = left & UINT32_C(0xffff);
    const uint32_t right_low = right & UINT32_C(0xffff);
    const int64_t a =
        left_low < UINT32_C(0x8000) ? (int64_t)left_low : (int64_t)left_low - INT64_C(65536);
    const int64_t b =
        right_low < UINT32_C(0x8000) ? (int64_t)right_low : (int64_t)right_low - INT64_C(65536);
    const int64_t product = a * b;
    const uint32_t low = (left_low * right_low) & UINT32_C(0xffff);
    const uint32_t inverse = low * UINT32_C(62209) & UINT32_C(0xffff);
    const int64_t u =
        inverse < UINT32_C(0x8000) ? (int64_t)inverse : (int64_t)inverse - INT64_C(65536);
    return product - u * INT64_C(3329);
}

static int32_t fqmul_oracle(uint32_t left, uint32_t right)
{
    const int64_t numerator = fqmul_numerator(left, right);
    __CPROVER_assert(numerator % INT64_C(65536) == 0, "montgomery numerator is divisible");
    return (int32_t)(numerator / INT64_C(65536));
}

int32_t pqc_mlk_fqmul_model(uint32_t left, uint32_t right)
{
    int32_t result;
    __CPROVER_assume(result == fqmul_oracle(left, right));
    return result;
}

void pqc_fqmul_conversion_harness(void)
{
    const uint32_t value = nondet_uint32_t();
    const uint32_t low = value & UINT32_C(0xffff);
    const int32_t expected = low < UINT32_C(0x8000) ? (int32_t)low : (int32_t)low - INT32_C(65536);

    __CPROVER_assert(pqc_mlk_signed16(value) == expected, "signed low half conversion matches");
}

void pqc_fqmul_fallback_harness(void)
{
    const uint32_t left = nondet_uint16_t();
    const uint32_t right = nondet_uint16_t();
    const int32_t portable = pqc_mlk_fqmul_c(left, right);

    __CPROVER_assert((int64_t)portable * INT64_C(65536) == fqmul_numerator(left, right),
                     "portable fallback matches oracle numerator");
    __CPROVER_assert(portable >= INT16_MIN && portable <= INT16_MAX,
                     "instruction result fits signed 16 bits");
}

void pqc_fqmul_upper_harness(void)
{
    const uint32_t left = nondet_uint32_t();
    const uint32_t right = nondet_uint32_t();
    const int32_t portable = pqc_mlk_fqmul_c(left, right);

    __CPROVER_assert(
        pqc_mlk_fqmul_c(left ^ UINT32_C(0xffff0000), right ^ UINT32_C(0xffff0000)) == portable,
        "upper source halves are ignored");
}

void pqc_fqmul_wrapper_harness(void)
{
    const uint32_t left = nondet_uint32_t();
    const uint32_t right = nondet_uint32_t();

    __CPROVER_assert(pqc_mlk_fqmul(left, right) == fqmul_oracle(left, right),
                     "wrapper model matches oracle");
}

void pqc_fqmul_callsite_harness(void)
{
    const unsigned callsite = nondet_unsigned();
    int32_t left = nondet_int16_t();
    int32_t right = nondet_int16_t();
    __CPROVER_assume(callsite < 4U);
    if (callsite == 0U)
    {
        __CPROVER_assume(left >= -26632 && left <= 26632);
        __CPROVER_assume(right >= -1664 && right <= 1664);
    }
    else if (callsite == 1U)
    {
        __CPROVER_assume(left >= -26632 && left <= 26632);
        right = 1441;
    }
    else if (callsite == 2U)
    {
        __CPROVER_assume(left >= -4096 && left <= 4096);
        __CPROVER_assume(right >= -4096 && right <= 4096);
    }
    else
    {
        __CPROVER_assume(left >= -3328 && left <= 3328);
        right = 1353;
    }
    const uint32_t left_low = (uint32_t)(left < 0 ? left + 65536 : left);
    const uint32_t right_low = (uint32_t)(right < 0 ? right + 65536 : right);
    const int32_t result = pqc_mlk_fqmul_c(left_low, right_low);
    __CPROVER_assert(result > -3329 && result < 3329, "mlkem fqmul result is below q");
}

void pqc_fqmul_butterfly_harness(void)
{
    const unsigned length = nondet_unsigned();
    __CPROVER_assume(length == 2U || length == 4U || length == 8U || length == 16U ||
                     length == 32U || length == 64U || length == 128U);
    for (unsigned ordinal = 0; ordinal < 128U; ++ordinal)
    {
        const unsigned start = (ordinal / length) * 2U * length;
        const unsigned j = start + ordinal % length;
        __CPROVER_assert(j < 256U && j + length < 256U, "butterfly index is in bounds");
    }
}

void pqc_fqmul_butterfly_value_harness(void)
{
    const int32_t left = nondet_int16_t();
    const int32_t right = nondet_int16_t();
    __CPROVER_assume(left >= -13316 && left <= 13316);
    __CPROVER_assume(right >= -13316 && right <= 13316);
    __CPROVER_assert(left + right >= INT16_MIN && left + right <= INT16_MAX,
                     "butterfly sum fits signed 16 bits");
    __CPROVER_assert(right - left >= INT16_MIN && right - left <= INT16_MAX,
                     "butterfly difference fits signed 16 bits");
}

void pqc_fqmul_base_harness(void)
{
    const unsigned k = nondet_unsigned();
    int64_t accumulator = 0;
    __CPROVER_assume(k >= 2U && k <= 4U);
    for (unsigned lane = 0; lane < k; ++lane)
    {
        int32_t a0 = nondet_int16_t();
        int32_t a1 = nondet_int16_t();
        int32_t b0 = nondet_int16_t();
        int32_t gb = nondet_int16_t();
        __CPROVER_assume(a0 >= -4096 && a0 <= 4096);
        __CPROVER_assume(a1 >= -4096 && a1 <= 4096);
        __CPROVER_assume(b0 >= -3328 && b0 <= 3328);
        __CPROVER_assume(gb >= -3328 && gb <= 3328);
        accumulator += (int64_t)a1 * gb + (int64_t)a0 * b0;
        __CPROVER_assert(accumulator >= INT32_MIN && accumulator <= INT32_MAX,
                         "late base accumulator fits signed 32 bits");
    }
}
