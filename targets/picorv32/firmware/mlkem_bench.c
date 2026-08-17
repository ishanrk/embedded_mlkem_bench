#include "bench_mmio.h"

#include <mlkem_native.h>

#include <stddef.h>
#include <stdint.h>

#if defined(PQC_MLKEM_PORTABLE)
#include "src/poly.h"
#include "src/poly_k.h"
#endif

#ifndef PQC_MLKEM_K
#error PQC_MLKEM_K is required
#endif

#ifndef PQC_MLKEM_SCRATCH_BYTES
#error PQC_MLKEM_SCRATCH_BYTES is required
#endif

#ifndef PQC_MLKEM_INPUTS
#define PQC_MLKEM_INPUTS 16U
#endif

#ifndef PQC_MLKEM_OPERATION_INPUTS
#define PQC_MLKEM_OPERATION_INPUTS 30U
#endif
#define PQC_MLKEM_REPEATS 3U
#define PQC_INSTRET_TAG UINT32_C(0x494e5300)
#define PQC_STACK_CALLER_TAG UINT32_C(0x53544103)
#define PQC_STACK_SCRATCH_TAG UINT32_C(0x53544104)

void pqc_mlkem_ntt(int16_t r[256]);
void pqc_mlkem_intt(int16_t r[256]);
void pqc_mlkem_tomont(int16_t r[256]);
void pqc_mlkem_mulcache(int16_t *cache, const int16_t *b);
void pqc_mlkem_basemul(int16_t r[256], const int16_t *a, const int16_t *b, const int16_t *cache);

#if defined(PQC_MLKEM_PORTABLE)
void pqc_mlkem_ntt(int16_t r[256])
{
    mlk_poly_ntt((mlk_poly *)(void *)r);
}

void pqc_mlkem_intt(int16_t r[256])
{
    mlk_poly_invntt_tomont((mlk_poly *)(void *)r);
}

void pqc_mlkem_tomont(int16_t r[256])
{
    mlk_poly_tomont((mlk_poly *)(void *)r);
}

void pqc_mlkem_mulcache(int16_t *cache, const int16_t *b)
{
    for (unsigned lane = 0; lane < PQC_MLKEM_K; ++lane)
    {
        mlk_poly_mulcache_compute((mlk_poly_mulcache *)(void *)(cache + lane * 128U),
                                  (const mlk_poly *)(const void *)(b + lane * 256U));
    }
}

void pqc_mlkem_basemul(int16_t r[256], const int16_t *a, const int16_t *b, const int16_t *cache)
{
    mlk_polyvec_basemul_acc_montgomery_cached(
        (mlk_poly *)(void *)r, (const mlk_polyvec *)(const void *)a,
        (const mlk_polyvec *)(const void *)b, (const mlk_polyvec_mulcache *)(const void *)cache);
}
#endif

struct mlkem_bench_state
{
    uint8_t pk[MLKEM_PUBLICKEYBYTES(MLK_CONFIG_PARAMETER_SET)];
    uint8_t sk[MLKEM_SECRETKEYBYTES(MLK_CONFIG_PARAMETER_SET)];
    uint8_t ct[MLKEM_CIPHERTEXTBYTES(MLK_CONFIG_PARAMETER_SET)];
    uint8_t ss[MLKEM_BYTES];
    uint8_t other_ss[MLKEM_BYTES];
    uint8_t key_coins[2U * MLKEM_SYMBYTES];
    uint8_t enc_coins[MLKEM_SYMBYTES];
    uint8_t expected[MLKEM_PUBLICKEYBYTES(MLK_CONFIG_PARAMETER_SET) +
                     MLKEM_SECRETKEYBYTES(MLK_CONFIG_PARAMETER_SET)];
    int16_t input[256];
    int16_t poly[256];
    int16_t a[PQC_MLKEM_K * 256U];
    int16_t b[PQC_MLKEM_K * 256U];
    int16_t cache[PQC_MLKEM_K * 128U];
    int16_t product[256];
};

#if defined(PQC_MLKEM_PORTABLE)
static _Alignas(32) struct mlkem_bench_state state;
#else
static struct mlkem_bench_state state;
#endif
static volatile uint32_t checksum;

static uint32_t read_instret(void)
{
    uint32_t value;
    __asm__ volatile("rdinstret %0" : "=r"(value));
    return value;
}

static uint32_t next_random(uint32_t *value)
{
    uint32_t x = *value;
    x ^= x << 13U;
    x ^= x >> 17U;
    x ^= x << 5U;
    *value = x;
    return x;
}

static void fill_bytes(uint8_t *r, size_t count, uint32_t seed)
{
    for (size_t i = 0; i < count; ++i)
    {
        r[i] = (uint8_t)next_random(&seed);
    }
}

static void fill_poly(int16_t *r, size_t count, uint32_t seed)
{
    for (size_t i = 0; i < count; ++i)
    {
        r[i] = (int16_t)((int32_t)(next_random(&seed) % 6657U) - 3328);
    }
}

static void fill_base_left(int16_t *r, size_t count, uint32_t seed)
{
    for (size_t i = 0; i < count; ++i)
    {
        r[i] = (int16_t)(next_random(&seed) % 4096U);
    }
}

static void copy_i16(int16_t *r, const int16_t *a, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        r[i] = a[i];
    }
}

static void copy_bytes(uint8_t *r, const uint8_t *a, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        r[i] = a[i];
    }
}

static uint32_t hash_bytes(const uint8_t *a, size_t count)
{
    uint32_t value = UINT32_C(2166136261);
    for (size_t i = 0; i < count; ++i)
    {
        value = (value ^ a[i]) * UINT32_C(16777619);
    }
    return value;
}

static uint32_t hash_i16(const int16_t *a, size_t count)
{
    uint32_t value = UINT32_C(2166136261);
    for (size_t i = 0; i < count; ++i)
    {
        value = (value ^ (uint16_t)a[i]) * UINT32_C(16777619);
    }
    return value;
}

static void record_instret(uint32_t begin, uint32_t end)
{
    pqc_status(PQC_INSTRET_TAG);
    pqc_status(begin);
    pqc_status(end);
}

#define PQC_MEASURE(statement)           \
    do                                   \
    {                                    \
        uint32_t begin = read_instret(); \
        uint32_t end;                    \
        pqc_bench_begin();               \
        statement;                       \
        pqc_bench_end();                 \
        end = read_instret();            \
        record_instret(begin, end);      \
    } while (0)

static int same_bytes(const uint8_t *a, const uint8_t *b, size_t count)
{
    uint8_t difference = 0U;
    for (size_t i = 0; i < count; ++i)
    {
        difference |= (uint8_t)(a[i] ^ b[i]);
    }
    return difference == 0U;
}

static void run_kernels(void)
{
    for (unsigned input = 0; input < PQC_MLKEM_INPUTS; ++input)
    {
        fill_poly(state.input, 256U, UINT32_C(0x10000000) + input);
        for (unsigned repeat = 0; repeat < PQC_MLKEM_REPEATS; ++repeat)
        {
            copy_i16(state.poly, state.input, 256U);
            PQC_MEASURE(pqc_mlkem_ntt(state.poly));
            if (repeat == 0U)
            {
                copy_bytes(state.expected, (const uint8_t *)state.poly, sizeof(state.poly));
            }
            else if (!same_bytes(state.expected, (const uint8_t *)state.poly, sizeof(state.poly)))
            {
                pqc_trap(UINT32_C(0xbad00001));
            }
            checksum ^= hash_i16(state.poly, 256U);
        }
    }
    for (unsigned input = 0; input < PQC_MLKEM_INPUTS; ++input)
    {
        fill_poly(state.input, 256U, UINT32_C(0x20000000) + input);
        for (unsigned repeat = 0; repeat < PQC_MLKEM_REPEATS; ++repeat)
        {
            copy_i16(state.poly, state.input, 256U);
            PQC_MEASURE(pqc_mlkem_intt(state.poly));
            if (repeat == 0U)
            {
                copy_bytes(state.expected, (const uint8_t *)state.poly, sizeof(state.poly));
            }
            else if (!same_bytes(state.expected, (const uint8_t *)state.poly, sizeof(state.poly)))
            {
                pqc_trap(UINT32_C(0xbad00002));
            }
            checksum ^= hash_i16(state.poly, 256U);
        }
    }
    for (unsigned input = 0; input < PQC_MLKEM_INPUTS; ++input)
    {
        fill_poly(state.b, PQC_MLKEM_K * 256U, UINT32_C(0x30000000) + input);
        for (unsigned repeat = 0; repeat < PQC_MLKEM_REPEATS; ++repeat)
        {
            PQC_MEASURE(pqc_mlkem_mulcache(state.cache, state.b));
            if (repeat == 0U)
            {
                copy_bytes(state.expected, (const uint8_t *)state.cache, sizeof(state.cache));
            }
            else if (!same_bytes(state.expected, (const uint8_t *)state.cache, sizeof(state.cache)))
            {
                pqc_trap(UINT32_C(0xbad00003));
            }
            checksum ^= hash_i16(state.cache, PQC_MLKEM_K * 128U);
        }
    }
    for (unsigned input = 0; input < PQC_MLKEM_INPUTS; ++input)
    {
        fill_base_left(state.a, PQC_MLKEM_K * 256U, UINT32_C(0x40000000) + input);
        fill_poly(state.b, PQC_MLKEM_K * 256U, UINT32_C(0x50000000) + input);
        pqc_mlkem_mulcache(state.cache, state.b);
        for (unsigned repeat = 0; repeat < PQC_MLKEM_REPEATS; ++repeat)
        {
            PQC_MEASURE(pqc_mlkem_basemul(state.product, state.a, state.b, state.cache));
            if (repeat == 0U)
            {
                copy_bytes(state.expected, (const uint8_t *)state.product, sizeof(state.product));
            }
            else if (!same_bytes(state.expected, (const uint8_t *)state.product,
                                 sizeof(state.product)))
            {
                pqc_trap(UINT32_C(0xbad00004));
            }
            checksum ^= hash_i16(state.product, 256U);
        }
    }
    for (unsigned input = 0; input < PQC_MLKEM_INPUTS; ++input)
    {
        fill_poly(state.input, 256U, UINT32_C(0x60000000) + input);
        for (unsigned repeat = 0; repeat < PQC_MLKEM_REPEATS; ++repeat)
        {
            copy_i16(state.poly, state.input, 256U);
            PQC_MEASURE(pqc_mlkem_tomont(state.poly));
            if (repeat == 0U)
            {
                copy_bytes(state.expected, (const uint8_t *)state.poly, sizeof(state.poly));
            }
            else if (!same_bytes(state.expected, (const uint8_t *)state.poly, sizeof(state.poly)))
            {
                pqc_trap(UINT32_C(0xbad00005));
            }
            checksum ^= hash_i16(state.poly, 256U);
        }
    }
}

static void prepare_operation(unsigned input)
{
    fill_bytes(state.key_coins, sizeof(state.key_coins), UINT32_C(0x70000000) + input);
    fill_bytes(state.enc_coins, sizeof(state.enc_coins), UINT32_C(0x71000000) + input);
}

static int keygen(void)
{
    return mlkem_keypair_derand(state.pk, state.sk, state.key_coins);
}

static int encapsulate(void)
{
    return mlkem_enc_derand(state.ct, state.ss, state.pk, state.enc_coins);
}

static int decapsulate(void)
{
    return mlkem_dec(state.other_ss, state.ct, state.sk);
}

static void run_keygen(void)
{
    for (unsigned input = 0; input < PQC_MLKEM_OPERATION_INPUTS; ++input)
    {
        prepare_operation(input);
        for (unsigned repeat = 0; repeat < PQC_MLKEM_REPEATS; ++repeat)
        {
            int result;
            PQC_MEASURE(result = keygen());
            if (result != 0)
            {
                pqc_trap(UINT32_C(0xbad00100));
            }
            const uint32_t value =
                hash_bytes(state.pk, sizeof(state.pk)) ^ hash_bytes(state.sk, sizeof(state.sk));
            if (repeat == 0U)
            {
                copy_bytes(state.expected, state.pk, sizeof(state.pk));
                copy_bytes(state.expected + sizeof(state.pk), state.sk, sizeof(state.sk));
            }
            else if (!same_bytes(state.expected, state.pk, sizeof(state.pk)) ||
                     !same_bytes(state.expected + sizeof(state.pk), state.sk, sizeof(state.sk)))
            {
                pqc_trap(UINT32_C(0xbad00101));
            }
            checksum ^= value;
        }
    }
}

static void run_encapsulation(void)
{
    for (unsigned input = 0; input < PQC_MLKEM_OPERATION_INPUTS; ++input)
    {
        prepare_operation(input);
        if (keygen() != 0)
        {
            pqc_trap(UINT32_C(0xbad00110));
        }
        for (unsigned repeat = 0; repeat < PQC_MLKEM_REPEATS; ++repeat)
        {
            int result;
            PQC_MEASURE(result = encapsulate());
            if (result != 0)
            {
                pqc_trap(UINT32_C(0xbad00111));
            }
            const uint32_t value =
                hash_bytes(state.ct, sizeof(state.ct)) ^ hash_bytes(state.ss, sizeof(state.ss));
            if (repeat == 0U)
            {
                copy_bytes(state.expected, state.ct, sizeof(state.ct));
                copy_bytes(state.expected + sizeof(state.ct), state.ss, sizeof(state.ss));
            }
            else if (!same_bytes(state.expected, state.ct, sizeof(state.ct)) ||
                     !same_bytes(state.expected + sizeof(state.ct), state.ss, sizeof(state.ss)))
            {
                pqc_trap(UINT32_C(0xbad00112));
            }
            checksum ^= value;
        }
    }
}

static void run_decapsulation(void)
{
    for (unsigned input = 0; input < PQC_MLKEM_OPERATION_INPUTS; ++input)
    {
        prepare_operation(input);
        if (keygen() != 0 || encapsulate() != 0)
        {
            pqc_trap(UINT32_C(0xbad00120));
        }
        for (unsigned repeat = 0; repeat < PQC_MLKEM_REPEATS; ++repeat)
        {
            int result;
            PQC_MEASURE(result = decapsulate());
            if (result != 0 || !same_bytes(state.ss, state.other_ss, sizeof(state.ss)))
            {
                pqc_trap(UINT32_C(0xbad00121));
            }
            const uint32_t value = hash_bytes(state.other_ss, sizeof(state.other_ss));
            if (repeat == 0U)
            {
                copy_bytes(state.expected, state.other_ss, sizeof(state.other_ss));
            }
            else if (!same_bytes(state.expected, state.other_ss, sizeof(state.other_ss)))
            {
                pqc_trap(UINT32_C(0xbad00122));
            }
            checksum ^= value;
        }
        state.ct[input % sizeof(state.ct)] ^= UINT8_C(1);
        if (decapsulate() != 0 || same_bytes(state.ss, state.other_ss, sizeof(state.ss)))
        {
            pqc_trap(UINT32_C(0xbad00123));
        }
    }
}

static void measured_complete(void *context)
{
    (void)context;
    if (keygen() != 0 || encapsulate() != 0 || decapsulate() != 0 ||
        !same_bytes(state.ss, state.other_ss, sizeof(state.ss)))
    {
        pqc_trap(UINT32_C(0xbad00130));
    }
    checksum ^= hash_bytes(state.other_ss, sizeof(state.other_ss));
}

int main(void)
{
    struct pqc_stack_result stack;

    PQC_MEASURE((void)0);
    run_kernels();
    run_keygen();
    run_encapsulation();
    run_decapsulation();

    prepare_operation(0U);
    stack = pqc_measure_stack(measured_complete, &state);
    if (stack.raw_bytes == 0U || stack.raw_bytes < stack.wrapper_bytes)
    {
        pqc_trap(UINT32_C(0xbad00140));
    }
    pqc_status(UINT32_C(0x53544100));
    pqc_status(stack.wrapper_bytes);
    pqc_status(UINT32_C(0x53544101));
    pqc_status(stack.raw_bytes);
    pqc_status(UINT32_C(0x53544102));
    pqc_status(stack.calibrated_bytes);
    pqc_status(PQC_STACK_CALLER_TAG);
    pqc_status((uint32_t)sizeof(state));
    pqc_status(PQC_STACK_SCRATCH_TAG);
    pqc_status(PQC_MLKEM_SCRATCH_BYTES);
    pqc_status(checksum);
    pqc_terminate(0U);
}
