#include "pqc_poly/mlkem_codegen.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

#ifndef PQC_POLY_TEST_CXX
#define PQC_POLY_TEST_CXX "c++"
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
    pqc_ntt_layer(expected, 128, 1);
    pqc_ntt_layer(expected, 64, 2);
    pqc_ntt_layer(expected, 32, 4);
    pqc_ntt_layer(expected, 16, 8);
    pqc_ntt_layer(expected, 8, 16);
    pqc_ntt_layer(expected, 4, 32);
    pqc_ntt_layer(expected, 2, 64);
    if (std::memcmp(actual, expected, sizeof(actual)) != 0)
    {
        return 1;
    }
    pqc_mlkem_intt(actual);
    for (unsigned i = 0; i < 256; ++i)
    {
        expected[i] = pqc_fqmul(expected[i], 1441);
    }
    pqc_intt_layer(expected, 2, 127, 1);
    pqc_intt_layer(expected, 4, 63, 1);
    pqc_intt_layer(expected, 8, 31, 1);
    pqc_intt_layer(expected, 16, 15, 1);
    pqc_intt_layer(expected, 32, 7, 1);
    pqc_intt_layer(expected, 64, 3, 1);
    pqc_intt_layer(expected, 128, 1, 1);
    if (std::memcmp(actual, expected, sizeof(actual)) != 0)
    {
        return 2;
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
    return 0;
}
)pqc";
    out.close();
    const std::string command = std::string(PQC_POLY_TEST_CXX) +
                                " -std=c++20 -O2 -Wall -Wextra -Werror -DTEST_K=" +
                                std::to_string(pqc_poly::mlkem_k(plan.level)) + " \"" +
                                source.string() + "\" -o \"" + executable.string() + "\"";
    require(std::system(command.c_str()) == 0, "generated backend did not compile");
    require(std::system(executable.c_str()) == 0, "generated backend mismatch");
}

}

int main()
{
    compile_and_run(
        {pqc_poly::mlkem_level::mlkem512, pqc_poly::ntt_traversal::stage_major,
         pqc_poly::intt_traversal::stage_major, pqc_poly::intt_sum_reduction::every_layer,
         pqc_poly::basemul_schedule::cached_late32, pqc_poly::mlkem_instruction::none},
        "stage");
    compile_and_run(
        {pqc_poly::mlkem_level::mlkem1024, pqc_poly::ntt_traversal::fuse_two_layers,
         pqc_poly::intt_traversal::fuse_two_layers, pqc_poly::intt_sum_reduction::after_layer_pair,
         pqc_poly::basemul_schedule::direct_eager32, pqc_poly::mlkem_instruction::none},
        "fused");
    compile_and_run(
        {pqc_poly::mlkem_level::mlkem768, pqc_poly::ntt_traversal::stage_major,
         pqc_poly::intt_traversal::fuse_two_layers, pqc_poly::intt_sum_reduction::every_layer,
         pqc_poly::basemul_schedule::cached_eager32, pqc_poly::mlkem_instruction::none},
        "eager");
}
