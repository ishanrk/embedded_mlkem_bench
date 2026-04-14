#include "pqc_poly/mlkem_codegen.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#ifndef PQC_POLY_TEST_CXX
#error PQC_POLY_TEST_CXX must name the host C++ compiler
#endif
#ifndef PQC_POLY_TEST_SOURCE_DIR
#error PQC_POLY_TEST_SOURCE_DIR must name the source directory
#endif

namespace
{

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "mlkem red32 codegen test failed: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message)
{
    if (!condition)
    {
        fail(message);
    }
}

void write(const std::filesystem::path &path, std::string_view value)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!output)
    {
        fail("cannot write generated test file");
    }
}

[[nodiscard]] std::string quote(const std::filesystem::path &path)
{
    std::string value = path.string();
    std::string out = "'";
    for (const char character : value)
    {
        if (character == '\'')
        {
            out += "'\\''";
        }
        else
        {
            out.push_back(character);
        }
    }
    out.push_back('\'');
    return out;
}

constexpr std::string_view driver = R"cpp(
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

void red_ntt(std::int16_t r[256]);
void red_intt(std::int16_t r[256]);
void red_tomont(std::int16_t r[256]);
void red_mulcache_one(std::int16_t *cache, const std::int16_t b[256]);
void red_mulcache(std::int16_t *cache, const std::int16_t b[PQC_K * 256]);
void red_basemul(std::int16_t r[256], const std::int16_t a[PQC_K * 256],
                 const std::int16_t b[PQC_K * 256], const std::int16_t *cache);
void sw_ntt(std::int16_t r[256]);
void sw_intt(std::int16_t r[256]);
void sw_tomont(std::int16_t r[256]);
void sw_mulcache_one(std::int16_t *cache, const std::int16_t b[256]);
void sw_mulcache(std::int16_t *cache, const std::int16_t b[PQC_K * 256]);
void sw_basemul(std::int16_t r[256], const std::int16_t a[PQC_K * 256],
                const std::int16_t b[PQC_K * 256], const std::int16_t *cache);

[[noreturn]] void fail(const char *name)
{
    std::cerr << name << '\n';
    std::exit(1);
}

std::uint32_t next(std::uint32_t &state)
{
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

template <std::size_t N>
void equal(const std::array<std::int16_t, N> &left, const std::array<std::int16_t, N> &right,
           const char *name)
{
    if (left != right)
    {
        fail(name);
    }
}

int main()
{
    std::uint32_t state = UINT32_C(0x243f6a88);
    for (unsigned trial = 0; trial < 5U; ++trial)
    {
        std::array<std::int16_t, 256> input{};
        for (std::int16_t &value : input)
        {
            value = static_cast<std::int16_t>(static_cast<std::int32_t>(next(state) % 6657U) - 3328);
        }
        auto red = input;
        auto sw = input;
        red_ntt(red.data());
        sw_ntt(sw.data());
        equal(red, sw, "ntt mismatch");

        red = input;
        sw = input;
        red_intt(red.data());
        sw_intt(sw.data());
        equal(red, sw, "intt mismatch");

        red = input;
        sw = input;
        red_tomont(red.data());
        sw_tomont(sw.data());
        equal(red, sw, "tomont mismatch");

        std::array<std::int16_t, PQC_K * 256> a{};
        std::array<std::int16_t, PQC_K * 256> b{};
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            a[i] = static_cast<std::int16_t>(static_cast<std::int32_t>(next(state) % 8193U) - 4096);
            b[i] = static_cast<std::int16_t>(static_cast<std::int32_t>(next(state) % 65536U) - 32768);
        }
        std::array<std::int16_t, PQC_K * 128> red_cache{};
        std::array<std::int16_t, PQC_K * 128> sw_cache{};
        red_mulcache(red_cache.data(), b.data());
        sw_mulcache(sw_cache.data(), b.data());
        equal(red_cache, sw_cache, "mulcache mismatch");

        std::array<std::int16_t, 128> red_one{};
        std::array<std::int16_t, 128> sw_one{};
        red_mulcache_one(red_one.data(), b.data());
        sw_mulcache_one(sw_one.data(), b.data());
        equal(red_one, sw_one, "mulcache one mismatch");

        std::array<std::int16_t, 256> red_product{};
        std::array<std::int16_t, 256> sw_product{};
        red_basemul(red_product.data(), a.data(), b.data(), red_cache.data());
        sw_basemul(sw_product.data(), a.data(), b.data(), sw_cache.data());
        equal(red_product, sw_product, "basemul mismatch");
    }
}
)cpp";

[[nodiscard]] std::string wrapper(std::string_view prefix, std::string_view source_name)
{
    std::string out;
    out += "#define pqc_mlkem_ntt ";
    out += prefix;
    out += "_ntt\n#define pqc_mlkem_intt ";
    out += prefix;
    out += "_intt\n#define pqc_mlkem_tomont ";
    out += prefix;
    out += "_tomont\n#define pqc_mlkem_mulcache_one ";
    out += prefix;
    out += "_mulcache_one\n#define pqc_mlkem_mulcache ";
    out += prefix;
    out += "_mulcache\n#define pqc_mlkem_basemul ";
    out += prefix;
    out += "_basemul\n#include \"";
    out += source_name;
    out += "\"\n";
    return out;
}

}

int main()
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "pqc-poly-red32-codegen";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const pqc_poly::mlkem_request request{};
    const std::vector<pqc_poly::red32_plan> plans =
        pqc_poly::enumerate_red32_comparison_plans();
    require(plans.size() == 72U, "red32 plan count changed");
    for (std::size_t index = 0; index < plans.size(); ++index)
    {
        const pqc_poly::red32_candidate red_candidate =
            pqc_poly::analyze_red32_plan(request, plans[index]);
        const pqc_poly::mlkem_plan schedule = pqc_poly::red32_schedule_plan(plans[index]);
        const pqc_poly::mlkem_candidate software_candidate =
            pqc_poly::analyze_mlkem_plan(request, schedule);
        const std::string red_source = pqc_poly::generate_red32_backend(request, red_candidate);
        const std::string software_source =
            pqc_poly::generate_mlkem_backend(request, software_candidate);
        require(red_source.find("#include \"red32.h\"") != std::string::npos,
                "red32 header is missing");
        require(red_source.find("pqc_mlk_fqmul_red32") != std::string::npos,
                "red32 multiply bridge is missing");
        require(red_source.find("pqc_mlk_red32((uint32_t)a)") != std::string::npos,
                "red32 reduction bridge is missing");
        require(red_source.find("fqmul.h") == std::string::npos,
                "fqmul leaked into red32 backend");

        const std::filesystem::path directory = root / std::to_string(index);
        std::filesystem::create_directories(directory);
        write(directory / "red.c", red_source);
        write(directory / "software.c", software_source);
        write(directory / "red_wrap.cpp", wrapper("red", "red.c"));
        write(directory / "software_wrap.cpp", wrapper("sw", "software.c"));
        write(directory / "driver.cpp", driver);

        const unsigned k = pqc_poly::mlkem_k(plans[index].level);
        std::string command = "\"" PQC_POLY_TEST_CXX "\" -std=c++20 -O0 -Wall -Wextra -Werror ";
#ifdef PQC_POLY_TEST_SANITIZE
        command += "-fno-omit-frame-pointer -fsanitize=address,undefined ";
#endif
        command += "-DPQC_K=" + std::to_string(k) + " -I" +
                   quote(std::filesystem::path(PQC_POLY_TEST_SOURCE_DIR) /
                         "targets/picorv32/mlkem") +
                   " " + quote(directory / "red_wrap.cpp") + " " +
                   quote(directory / "software_wrap.cpp") + " " +
                   quote(directory / "driver.cpp") + " -o " + quote(directory / "test");
        require(std::system(command.c_str()) == 0, "generated backend did not compile");
        const std::string run = quote(directory / "test");
        require(std::system(run.c_str()) == 0, "generated backend differential test failed");
    }
    std::filesystem::remove_all(root);
}
