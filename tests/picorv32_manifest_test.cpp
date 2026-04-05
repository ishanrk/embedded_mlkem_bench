#include "pqc_poly/target_measurement.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#ifndef PQC_POLY_SOURCE_DIR
#error "PQC_POLY_SOURCE_DIR must name the project source tree"
#endif

namespace
{

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "picorv32 manifest test failed: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message)
{
    if (!condition)
    {
        fail(message);
    }
}

[[nodiscard]] std::string read(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        fail("cannot read source file");
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void test_manifest_load()
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "pqc-poly-picorv32-manifest-test.json";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << R"({
  "repository_sha": "0fdc1dae3f8d02e9d8d3c8f8683ce871bd96f4fa",
  "picorv32_sha": "a473fc8fca393771d83b0ffcf0b14db3393339d8",
  "riscv_toolchain_release": "2026.07.15",
  "oss_cad_suite_release": "2026-07-29",
  "gcc_version": "riscv32-unknown-elf-gcc 16.1",
  "binutils_version": "GNU Binutils 2.46",
  "verilator_version": "Verilator 5.040",
  "yosys_version": "Yosys 0.52",
  "nextpnr_version": "nextpnr 0.9",
  "solver_version": "z3 4.15",
  "cmake_version": "cmake 3.22.1",
  "ninja_version": "1.10.1",
  "python_version": "Python 3.10.12",
  "compiler_flags": ["-march=rv32imc", "-mabi=ilp32"],
  "linker_flags": ["-nostdlib", "-static"],
  "core_parameters": ["ENABLE_COUNTERS=1", "ENABLE_FAST_MUL=0"],
  "fpga_part": "LFE5U-45F-6BG381C",
  "synthesis_seeds": [1, 2, 3, 4, 5],
  "source_archives": [
    {
      "name": "picorv32",
      "sha256": "050ba03d03eaacadb5953f3ba2218b49866c71d505c2476e49a0c0f5fe14e36c"
    }
  ],
  "container_digest": null
})";
    }

    const pqc_poly::picorv32_manifest manifest = pqc_poly::load_picorv32_manifest(path);
    require(manifest.picorv32_sha == "a473fc8fca393771d83b0ffcf0b14db3393339d8",
            "picorv32 pin changed");
    require(manifest.synthesis_seeds == std::vector<std::uint32_t>({1, 2, 3, 4, 5}),
            "synthesis seeds changed");
    require(manifest.source_archives.front().sha256 ==
                "050ba03d03eaacadb5953f3ba2218b49866c71d505c2476e49a0c0f5fe14e36c",
            "archive hash changed");
    std::filesystem::remove(path);
}

void test_source_pins()
{
    const std::filesystem::path root{PQC_POLY_SOURCE_DIR};
    const std::string fetch = read(root / "cmake/FetchPinned.cmake");
    require(fetch.find("a473fc8fca393771d83b0ffcf0b14db3393339d8") != std::string::npos,
            "fetch helper lacks picorv32 commit");
    require(fetch.find("050ba03d03eaacadb5953f3ba2218b49866c71d505c2476e49a0c0f5fe14e36c") !=
                std::string::npos,
            "fetch helper lacks archive hash");
    require(
        fetch.find("URL_HASH \"SHA256=${PQC_POLY_PICORV32_ARCHIVE_SHA256}\"") != std::string::npos,
        "fetch helper lacks hash verification");

    const std::string target_cmake = read(root / "targets/picorv32/CMakeLists.txt");
    require(target_cmake.find("ae36abbec394b29643154c1b4a1322e829937d04e82f41b47f9c27d3bd68e543") !=
                std::string::npos,
            "target manifest lacks toolchain archive hash");
    require(target_cmake.find("89ea1152ea84bc600f18cc685f721d534d1f018e09831662787865a3d79ce4aa") !=
                std::string::npos,
            "target manifest lacks cad archive hash");

    const std::string top = read(root / "targets/picorv32/rtl/pqc_picorv32_core_top.sv");
    for (const std::string_view parameter : {
             ".ENABLE_COUNTERS(1)",
             ".ENABLE_COUNTERS64(1)",
             ".ENABLE_REGS_16_31(1)",
             ".ENABLE_REGS_DUALPORT(1)",
             ".LATCHED_MEM_RDATA(0)",
             ".TWO_STAGE_SHIFT(1)",
             ".BARREL_SHIFTER(1)",
             ".TWO_CYCLE_COMPARE(0)",
             ".TWO_CYCLE_ALU(0)",
             ".COMPRESSED_ISA(1)",
             ".CATCH_MISALIGN(1)",
             ".CATCH_ILLINSN(1)",
             ".ENABLE_PCPI(1)",
             ".ENABLE_MUL(0)",
             ".ENABLE_FAST_MUL(STOCK_MUL)",
             ".ENABLE_DIV(1)",
             ".ENABLE_IRQ(0)",
             ".ENABLE_IRQ_QREGS(0)",
             ".ENABLE_IRQ_TIMER(0)",
             ".ENABLE_TRACE(0)",
             ".REGS_INIT_ZERO(0)",
             ".PROGADDR_RESET(32'h0000_0000)",
             ".PROGADDR_IRQ(32'h0000_0010)",
             ".STACKADDR(32'h0008_0000)",
         })
    {
        require(top.find(parameter) != std::string::npos, "required core parameter missing");
    }

    const std::string sim = read(root / "targets/picorv32/rtl/pqc_picorv32_sim_top.sv");
    for (const std::string_view address : {
             "32'h0008_0000",
             "32'h1000_0000",
             "32'h1000_0004",
             "32'h1000_0008",
             "32'h1000_000c",
         })
    {
        require(sim.find(address) != std::string::npos, "required memory address missing");
    }

    for (const std::string_view option : {
             "-std=c11",
             "-march=rv32imc",
             "-mabi=ilp32",
             "-O3",
             "-ffreestanding",
             "-fstack-usage",
             "-Wconversion",
             "-Werror",
             "-nostdlib",
             "-Wl,--orphan-handling=error",
         })
    {
        require(target_cmake.find(option) != std::string::npos, "required firmware option missing");
    }
}

}

int main()
{
    test_manifest_load();
    test_source_pins();
}
