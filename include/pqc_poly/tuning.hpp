#ifndef PQC_POLY_TUNING_HPP
#define PQC_POLY_TUNING_HPP

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pqc_poly
{

enum class benchmark_status
{
    pending,
    rejected,
    measured,
};

enum class latency_metric
{
    cycles,
    nanoseconds,
};

struct verification_status
{
    bool differential_tests{false};
    bool independent_plan{false};
    bool memory_safety{false};
    bool ram_bound{false};

    friend bool operator==(const verification_status &, const verification_status &) = default;
};

struct benchmark_provenance
{
    std::string compiler{};
    std::string compiler_version{};
    std::string compiler_flags{};
    std::string target{};
    std::string runner{};

    friend bool operator==(const benchmark_provenance &, const benchmark_provenance &) = default;
};

struct benchmark_record
{
    std::string plan_id{};
    benchmark_status status{benchmark_status::pending};
    verification_status verification{};
    std::optional<std::uint64_t> nanoseconds{};
    std::optional<std::uint64_t> cycles{};
    std::uint64_t peak_scratch_bytes{0};
    std::uint64_t code_size_bytes{0};
    benchmark_provenance provenance{};
    std::vector<std::string> rejection_reasons{};

    friend bool operator==(const benchmark_record &, const benchmark_record &) = default;
};

class tuning_error final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::string_view benchmark_status_name(benchmark_status value) noexcept;
[[nodiscard]] std::string_view latency_metric_name(latency_metric value) noexcept;

// this local gate does not imply cbmc or real target execution
[[nodiscard]] bool fully_verified(const benchmark_record &record) noexcept;
void validate_benchmark_record(const benchmark_record &record);
[[nodiscard]] std::optional<std::uint64_t> measured_latency(const benchmark_record &record,
                                                            latency_metric metric) noexcept;

// callers provide records from one comparable target and toolchain experiment
[[nodiscard]] const benchmark_record &pick_measured(std::span<const benchmark_record> records,
                                                    latency_metric metric);
[[nodiscard]] std::vector<const benchmark_record *> measured_frontier(
    std::span<const benchmark_record> records, latency_metric metric);

[[nodiscard]] std::string benchmarks_to_json(std::span<const benchmark_record> records,
                                             latency_metric metric);
[[nodiscard]] std::string report_to_html(std::span<const benchmark_record> records,
                                         latency_metric metric);

}

#endif
