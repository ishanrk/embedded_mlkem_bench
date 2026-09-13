#include "bench_mmio.h"

#include <mlkem_native.h>

#include <stddef.h>
#include <stdint.h>

#ifndef PQC_MLKEM_OPERATION_INPUTS
#define PQC_MLKEM_OPERATION_INPUTS 30U
#endif

#define PQC_MLKEM_REPEATS 3U
#define PQC_CHECKSUM_TAG UINT32_C(0x48415348)


// this file essentially sets up the input and benchmarking for the four firmware builds, which are:
// 1. mlkem-native: uses the native fixed backend for all arithmetic operations
// 2. mlkem-soft: uses the C backend for all arithmetic operations
// 3. mlkem-mixed: uses the native fixed backend for NTT and INTT, and the C backend for all other arithmetic operations
// 4. mlkem-mixed-ntt: uses the native fixed backend for NTT, and the C backend for all other arithmetic operations, including INTT

struct mlkem_bench_state
{
    // static buffers keep setup identical across the four firmware builds
    // buffers are needed for the mlkem key gen, key encaps calls etc.
    uint8_t pk[MLKEM_PUBLICKEYBYTES(MLK_CONFIG_PARAMETER_SET)];
    uint8_t sk[MLKEM_SECRETKEYBYTES(MLK_CONFIG_PARAMETER_SET)];
    uint8_t ct[MLKEM_CIPHERTEXTBYTES(MLK_CONFIG_PARAMETER_SET)];
    uint8_t ss[MLKEM_BYTES];
    uint8_t other_ss[MLKEM_BYTES];
    uint8_t key_coins[2U * MLKEM_SYMBYTES];
    uint8_t enc_coins[MLKEM_SYMBYTES];
    uint8_t expected[MLKEM_PUBLICKEYBYTES(MLK_CONFIG_PARAMETER_SET) +
                     MLKEM_SECRETKEYBYTES(MLK_CONFIG_PARAMETER_SET)];
};

static struct mlkem_bench_state state;
static volatile uint32_t checksum = UINT32_C(2166136261);


// pseudorandom generator
static uint32_t next_random(uint32_t *value)
{
    uint32_t current = *value;
    current ^= current << 13U;
    current ^= current >> 17U;
    current ^= current << 5U;
    *value = current;
    return current;
}

static void fill_bytes(uint8_t *output, size_t count, uint32_t seed)
{
    for (size_t index = 0; index < count; ++index)
    {
        output[index] = (uint8_t)next_random(&seed);
    }
}

static void copy_bytes(uint8_t *output, const uint8_t *input, size_t count)
{
    for (size_t index = 0; index < count; ++index)
    {
        output[index] = input[index];
    }
}

static int same_bytes(const uint8_t *left, const uint8_t *right, size_t count)
{
    uint8_t difference = 0U;
    for (size_t index = 0; index < count; ++index)
    {
        difference |= (uint8_t)(left[index] ^ right[index]);
    }
    return difference == 0U;
}

// not a hash function btw, just a simple checksum to keep track of the output of each operation
static uint32_t hash_bytes(const uint8_t *input, size_t count)
{
    uint32_t value = UINT32_C(2166136261);
    for (size_t index = 0; index < count; ++index)
    {
        value = (value ^ input[index]) * UINT32_C(16777619);
    }
    return value;
}

static void keep_output(uint32_t value)
{
    checksum = (checksum ^ value) * UINT32_C(16777619);
}

static void prepare_input(unsigned input)
{
    // every variant receives the same key and encapsulation coins
    fill_bytes(state.key_coins, sizeof(state.key_coins),
               UINT32_C(0x70000000) + input);
    fill_bytes(state.enc_coins, sizeof(state.enc_coins),
               UINT32_C(0x71000000) + input);
}


// pqc_bench_begin and bench_end place markers in the MMIO space, which the simulator can use to measure the time taken by the operation
// pqc_bench_begin writes to the special begin address 0x10000000
// pqc_bench_end writes to the special end address 0x10000004
// sim_top hardware module detects these writes and raises benchmark_begin or benchmark_end
// firmware_sim.cpp sees those signals and stores the current simulator cycle_count
#define PQC_MEASURE(statement) \
    do                         \
    {                          \
        pqc_bench_begin();     \
        statement;             \
        pqc_bench_end();       \
    } while (0)



static void measure_keygen(void)
{
    for (unsigned input = 0; input < PQC_MLKEM_OPERATION_INPUTS; ++input)
    {
        prepare_input(input);
        for (unsigned repeat = 0; repeat < PQC_MLKEM_REPEATS; ++repeat)
        {
            int result;
            PQC_MEASURE(result =
                            mlkem_keypair_derand(state.pk, state.sk, state.key_coins));
            if (result != 0)
            {
                pqc_trap(UINT32_C(0xbad00100));
            }
            if (repeat == 0U)
            {
                copy_bytes(state.expected, state.pk, sizeof(state.pk));
                copy_bytes(state.expected + sizeof(state.pk), state.sk,
                           sizeof(state.sk));
            }
            else if (!same_bytes(state.expected, state.pk, sizeof(state.pk)) ||
                     !same_bytes(state.expected + sizeof(state.pk), state.sk,
                                 sizeof(state.sk)))
            {
                pqc_trap(UINT32_C(0xbad00101));
            }
            keep_output(hash_bytes(state.pk, sizeof(state.pk)) ^
                        hash_bytes(state.sk, sizeof(state.sk)));
        }
    }
}

static void measure_encapsulation(void)
{
    for (unsigned input = 0; input < PQC_MLKEM_OPERATION_INPUTS; ++input)
    {
        prepare_input(input);
        if (mlkem_keypair_derand(state.pk, state.sk, state.key_coins) != 0)
        {
            pqc_trap(UINT32_C(0xbad00110));
        }
        for (unsigned repeat = 0; repeat < PQC_MLKEM_REPEATS; ++repeat)
        {
            int result;
            PQC_MEASURE(result = mlkem_enc_derand(state.ct, state.ss, state.pk,
                                                  state.enc_coins));
            if (result != 0)
            {
                pqc_trap(UINT32_C(0xbad00111));
            }
            if (repeat == 0U)
            {
                copy_bytes(state.expected, state.ct, sizeof(state.ct));
                copy_bytes(state.expected + sizeof(state.ct), state.ss,
                           sizeof(state.ss));
            }
            else if (!same_bytes(state.expected, state.ct, sizeof(state.ct)) ||
                     !same_bytes(state.expected + sizeof(state.ct), state.ss,
                                 sizeof(state.ss)))
            {
                pqc_trap(UINT32_C(0xbad00112));
            }
            keep_output(hash_bytes(state.ct, sizeof(state.ct)) ^
                        hash_bytes(state.ss, sizeof(state.ss)));
        }
    }
}

static void measure_decapsulation(void)
{
    for (unsigned input = 0; input < PQC_MLKEM_OPERATION_INPUTS; ++input)
    {
        prepare_input(input);
        if (mlkem_keypair_derand(state.pk, state.sk, state.key_coins) != 0 ||
            mlkem_enc_derand(state.ct, state.ss, state.pk, state.enc_coins) != 0)
        {
            pqc_trap(UINT32_C(0xbad00120));
        }
        for (unsigned repeat = 0; repeat < PQC_MLKEM_REPEATS; ++repeat)
        {
            int result;
            PQC_MEASURE(result = mlkem_dec(state.other_ss, state.ct, state.sk));
            if (result != 0 ||
                !same_bytes(state.ss, state.other_ss, sizeof(state.ss)))
            {
                pqc_trap(UINT32_C(0xbad00121));
            }
            if (repeat == 0U)
            {
                copy_bytes(state.expected, state.other_ss, sizeof(state.other_ss));
            }
            else if (!same_bytes(state.expected, state.other_ss,
                                 sizeof(state.other_ss)))
            {
                pqc_trap(UINT32_C(0xbad00122));
            }
            keep_output(hash_bytes(state.other_ss, sizeof(state.other_ss)));
        }

        // corrupted ciphertext must not recover the valid shared secret
        state.ct[input % sizeof(state.ct)] ^= UINT8_C(1);
        if (mlkem_dec(state.other_ss, state.ct, state.sk) != 0 ||
            same_bytes(state.ss, state.other_ss, sizeof(state.ss)))
        {
            pqc_trap(UINT32_C(0xbad00123));
        }
    }
}

int main(void)
{
    // first pair measures only the MMIO marker cost
    PQC_MEASURE((void)0);
    measure_keygen();
    measure_encapsulation();
    measure_decapsulation();
    pqc_status(PQC_CHECKSUM_TAG);
    pqc_status(checksum);
    pqc_terminate(0U);
}
