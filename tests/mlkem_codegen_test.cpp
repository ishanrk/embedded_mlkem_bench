#include "pqc_poly/mlkem_codegen.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

#ifndef PQC_POLY_TEST_CXX
#define PQC_POLY_TEST_CXX "c++"
#endif

#ifndef PQC_POLY_TEST_SANITIZE
#define PQC_POLY_TEST_SANITIZE 0
#endif

#ifndef PQC_POLY_TEST_SOURCE_DIR
#define PQC_POLY_TEST_SOURCE_DIR "."
#endif

namespace
{

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "mlkem codegen test failed: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message)
{
    if (!condition)
    {
        fail(message);
    }
}

void compile_and_run(const pqc_poly::mlkem_plan &plan, std::string_view name)
{
    const pqc_poly::mlkem_request request{};
    const pqc_poly::mlkem_candidate candidate = pqc_poly::analyze_mlkem_plan(request, plan);
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("pqc-mlkem-" + std::string(name));
    std::filesystem::create_directories(directory);
    const std::filesystem::path source = directory / "test.cpp";
    const std::filesystem::path executable = directory / "test";
    std::ofstream out(source, std::ios::binary | std::ios::trunc);
    out << pqc_poly::generate_mlkem_backend(request, candidate);
    out << R"pqc(
#include <cstring>

static int16_t ref_montgomery_reduce(int32_t a)
{
    const uint16_t inverted = (uint16_t)((uint32_t)(uint16_t)a * UINT32_C(62209));
    const int32_t t = inverted <= INT16_MAX ? (int32_t)inverted : (int32_t)inverted - 65536;
    return (int16_t)((a - t * 3329) >> 16);
}

static int16_t ref_fqmul(int16_t a, int16_t b)
{
    return ref_montgomery_reduce((int32_t)a * (int32_t)b);
}

static int16_t ref_barrett_reduce(int16_t a)
{
    const int32_t t = (INT32_C(20159) * a + (INT32_C(1) << 25)) >> 26;
    return (int16_t)(a - t * 3329);
}

static void reference_ntt(int16_t r[256])
{
    unsigned zeta_index = 1;
    for (unsigned length = 128; length >= 2; length >>= 1U)
    {
        for (unsigned start = 0; start < 256; start += 2U * length)
        {
            const int16_t zeta = pqc_zetas[zeta_index++];
            for (unsigned j = start; j < start + length; ++j)
            {
                const int16_t t = ref_fqmul(r[j + length], zeta);
                const int16_t left = r[j];
                r[j] = (int16_t)(left + t);
                r[j + length] = (int16_t)(left - t);
            }
        }
    }
}

static void reference_intt(int16_t r[256])
{
    unsigned zeta_index = 127;
    for (unsigned j = 0; j < 256; ++j)
    {
        r[j] = ref_fqmul(r[j], 1441);
    }
    for (unsigned length = 2; length <= 128; length <<= 1U)
    {
        const int reduce = TEST_REDUCE_EACH || length == 4U || length == 16U ||
                           length == 64U || length == 128U;
        for (unsigned start = 0; start < 256; start += 2U * length)
        {
            const int16_t zeta = pqc_zetas[zeta_index--];
            for (unsigned j = start; j < start + length; ++j)
            {
                const int16_t left = r[j];
                const int16_t right = r[j + length];
                const int16_t sum = (int16_t)(left + right);
                r[j] = reduce != 0 ? ref_barrett_reduce(sum) : sum;
                r[j + length] = ref_fqmul((int16_t)(right - left), zeta);
            }
        }
    }
}

int main()
{
    int16_t input[256];
    int16_t actual[256];
    int16_t expected[256];
    for (unsigned i = 0; i < 256; ++i)
    {
        input[i] = (int16_t)((i * 17U) % 3329U - 1664);
    }
    std::memcpy(actual, input, sizeof(actual));
    std::memcpy(expected, input, sizeof(expected));
    pqc_mlkem_ntt(actual);
    reference_ntt(expected);
    if (std::memcmp(actual, expected, sizeof(actual)) != 0)
    {
        return 1;
    }
    pqc_mlkem_intt(actual);
    reference_intt(expected);
    if (std::memcmp(actual, expected, sizeof(actual)) != 0)
    {
        return 2;
    }

    for (unsigned i = 0; i < 256; ++i)
    {
        const int residue = (actual[i] % 3329 + 3329) % 3329;
        const int scaled = ((int32_t)input[i] * 2285 % 3329 + 3329) % 3329;
        if (residue != scaled)
        {
            return 4;
        }
    }

    int16_t a[TEST_K * 256];
    int16_t b[TEST_K * 256];
    int16_t cache[TEST_K * 128];
    int16_t product[256];
    for (unsigned i = 0; i < TEST_K * 256; ++i)
    {
        a[i] = (int16_t)((i * 29U) % 4096U);
        b[i] = (int16_t)((i * 31U) % 3329U - 1664);
    }
    pqc_mlkem_mulcache(cache, b);
    if (TEST_DIRECT == 0)
    {
        for (unsigned lane = 0; lane < TEST_K; ++lane)
        {
            for (unsigned i = 0; i < 128; ++i)
            {
                const unsigned p = lane * 256U + 2U * i;
                const int16_t gamma = (i & 1U) == 0U ? pqc_zetas[64U + i / 2U]
                                                     : (int16_t)-pqc_zetas[64U + i / 2U];
                if (cache[lane * 128U + i] != ref_fqmul(b[p + 1U], gamma))
                {
                    return 5;
                }
            }
        }
    }
    pqc_mlkem_basemul(product, a, b, cache);
    const int64_t rinv = 169;
    for (unsigned i = 0; i < 128; ++i)
    {
        const int64_t gamma = (i & 1U) == 0U ? pqc_zetas[64U + i / 2U]
                                             : -pqc_zetas[64U + i / 2U];
        int64_t t0 = 0;
        int64_t t1 = 0;
        for (unsigned lane = 0; lane < TEST_K; ++lane)
        {
            const unsigned p = lane * 256U + 2U * i;
            t0 += (int64_t)a[p + 1U] * b[p + 1U] * gamma * rinv * rinv;
            t0 += (int64_t)a[p] * b[p] * rinv;
            t1 += ((int64_t)a[p] * b[p + 1U] + (int64_t)a[p + 1U] * b[p]) * rinv;
        }
        const int expected0 = (int)((t0 % 3329 + 3329) % 3329);
        const int expected1 = (int)((t1 % 3329 + 3329) % 3329);
        const int actual0 = (product[2U * i] % 3329 + 3329) % 3329;
        const int actual1 = (product[2U * i + 1U] % 3329 + 3329) % 3329;
        if (actual0 != expected0 || actual1 != expected1)
        {
            return 3;
        }
    }

    std::memcpy(actual, input, sizeof(actual));
    pqc_mlkem_tomont(actual);
    for (unsigned i = 0; i < 256; ++i)
    {
        if (actual[i] != ref_fqmul(input[i], 1353))
        {
            return 6;
        }
    }
    return 0;
}
)pqc";
    out.close();
    const std::string command =
        std::string(PQC_POLY_TEST_CXX) + " -std=c++20 -O2 -Wall -Wextra -Werror -DTEST_K=" +
        std::to_string(pqc_poly::mlkem_k(plan.level)) + " -DTEST_REDUCE_EACH=" +
        (plan.inverse_reduction == pqc_poly::intt_sum_reduction::every_layer ? "1" : "0") +
        " -DTEST_DIRECT=" +
        (plan.basemul == pqc_poly::basemul_schedule::direct_eager32 ? "1" : "0") + " -I\"" +
        std::string(PQC_POLY_TEST_SOURCE_DIR) + "/targets/picorv32/mlkem\"" +
        (PQC_POLY_TEST_SANITIZE != 0 ? " -fsanitize=address,undefined -fno-omit-frame-pointer"
                                     : "") +
        " \"" + source.string() + "\" -o \"" + executable.string() + "\"";
    require(std::system(command.c_str()) == 0, "generated backend did not compile");
    require(std::system(executable.c_str()) == 0, "generated backend mismatch");
}

}

int main()
{
    for (const pqc_poly::mlkem_plan &plan : pqc_poly::enumerate_mlkem_plans())
    {
        compile_and_run(plan, pqc_poly::mlkem_plan_id(plan));
    }
}
