#ifndef PQC_POLY_TARGET_MEASUREMENT_HPP
#define PQC_POLY_TARGET_MEASUREMENT_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pqc_poly
{

struct archive_hash
{
    std::string name{};
    std::string sha256{};

    friend bool operator==(const archive_hash &, const archive_hash &) = default;
};

struct picorv32_manifest
{
    std::string repository_sha{};
    std::string picorv32_sha{};
    std::string riscv_toolchain_release{};
    std::string oss_cad_suite_release{};
    std::string gcc_version{};
    std::string binutils_version{};
    std::string verilator_version{};
    std::string yosys_version{};
    std::string nextpnr_version{};
    std::string solver_version{};
    std::string cmake_version{};
    std::string ninja_version{};
    std::string python_version{};
    std::vector<std::string> compiler_flags{};
    std::vector<std::string> linker_flags{};
    std::vector<std::string> core_parameters{};
    std::string fpga_part{};
    std::vector<std::uint32_t> synthesis_seeds{};
    std::vector<archive_hash> source_archives{};
    std::optional<std::string> container_digest{};

    friend bool operator==(const picorv32_manifest &, const picorv32_manifest &) = default;
};

struct cycle_measurement
{
    std::uint64_t begin_cycle{0};
    std::uint64_t end_cycle{0};
    std::uint64_t marker_overhead_cycles{0};
    std::uint64_t calibrated_cycles{0};
    bool terminated{false};
    bool trapped{false};

    friend bool operator==(const cycle_measurement &, const cycle_measurement &) = default;
};

struct code_size_measurement
{
    std::uint64_t code_text_bytes{0};
    std::uint64_t code_rodata_bytes{0};
    std::uint64_t data_bytes{0};
    std::uint64_t bss_bytes{0};
    std::uint64_t allocated_flash_bytes{0};

    friend bool operator==(const code_size_measurement &, const code_size_measurement &) = default;
};

struct stack_frame
{
    std::string function{};
    std::uint64_t bytes{0};
    bool bounded_dynamic{false};

    friend bool operator==(const stack_frame &, const stack_frame &) = default;
};

struct stack_measurement
{
    std::uint64_t explicit_scratch_bytes{0};
    std::uint64_t caller_working_bytes{0};
    std::uint64_t compiler_frame_bytes{0};
    std::optional<std::uint64_t> compiler_callchain_bound_bytes{};
    std::uint64_t runtime_stack_high_water_bytes{0};
    std::uint64_t raw_stack_high_water_bytes{0};
    std::uint64_t raw_wrapper_high_water_bytes{0};

    friend bool operator==(const stack_measurement &, const stack_measurement &) = default;
};

struct synthesis_seed
{
    std::uint32_t seed{0};
    std::uint64_t lut4{0};
    std::uint64_t flip_flops{0};
    std::uint64_t dsp{0};
    std::uint64_t bram{0};
    double maximum_frequency_mhz{0.0};
    bool meets_50mhz{false};
    std::string command{};

    friend bool operator==(const synthesis_seed &, const synthesis_seed &) = default;
};

struct synthesis_measurement
{
    std::string yosys_version{};
    std::string nextpnr_version{};
    std::vector<synthesis_seed> seeds{};

    friend bool operator==(const synthesis_measurement &, const synthesis_measurement &) = default;
};

class target_measurement_error final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] picorv32_manifest load_picorv32_manifest(const std::filesystem::path &path);
[[nodiscard]] std::vector<stack_frame> parse_stack_usage(std::string_view text);
[[nodiscard]] std::optional<std::uint64_t> compute_callchain_stack_bound(
    std::span<const stack_frame> frames, std::string_view disassembly, std::string_view root);
[[nodiscard]] code_size_measurement parse_elf_size(std::string_view text);
[[nodiscard]] cycle_measurement parse_simulation_measurement(std::string_view text);
[[nodiscard]] synthesis_measurement parse_synthesis_measurement(std::string_view text);

}

#endif
