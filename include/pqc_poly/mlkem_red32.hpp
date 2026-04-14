#ifndef PQC_POLY_MLKEM_RED32_HPP
#define PQC_POLY_MLKEM_RED32_HPP

#include "pqc_poly/mlkem_plan.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pqc_poly
{

inline constexpr std::string_view red32_candidate_schema =
    "pqc-poly-bench/mlkem-red32-candidate-v1";

struct red32_plan
{
    mlkem_level level{mlkem_level::mlkem512};
    ntt_traversal forward{ntt_traversal::stage_major};
    intt_traversal inverse{intt_traversal::stage_major};
    intt_sum_reduction inverse_reduction{intt_sum_reduction::every_layer};
    basemul_schedule basemul{basemul_schedule::cached_late32};

    friend bool operator==(const red32_plan &, const red32_plan &) = default;
};

struct red32_candidate
{
    std::string schema{red32_candidate_schema};
    red32_plan plan{};
    std::string id{};
    std::vector<mlkem_record> forward_records{};
    std::vector<mlkem_record> inverse_records{};
    std::uint32_t forward_bound{0};
    std::uint32_t inverse_lazy_bound{0};
    std::uint64_t accumulator_bound{0};
    std::uint32_t mulcache_coefficients{0};
    std::uint32_t scratch_bytes{0};
    std::uint32_t caller_workspace_bytes{0};
    std::int32_t reduction_min{-34432};
    std::int32_t reduction_max{34432};
    bool ntt_in_place{false};
    bool intt_in_place{false};
    bool fixed_loop_structure{false};
    bool full_domain_reduction{false};
    bool canonical_rs2_zero{false};
    bool standard_mul_before_reduction{false};
    bool legal{false};
    std::vector<std::string> rejections{};

    friend bool operator==(const red32_candidate &, const red32_candidate &) = default;
};

[[nodiscard]] std::int32_t red32_reference(std::uint32_t value) noexcept;
[[nodiscard]] mlkem_plan red32_schedule_plan(const red32_plan &plan) noexcept;
[[nodiscard]] std::string red32_plan_id(const red32_plan &plan);
[[nodiscard]] std::vector<red32_plan> enumerate_red32_comparison_plans();
[[nodiscard]] red32_candidate analyze_red32_plan(const mlkem_request &request,
                                                 const red32_plan &plan);
[[nodiscard]] std::vector<std::string> check_red32_candidate(
    const mlkem_request &request, const red32_candidate &candidate);
[[nodiscard]] std::string serialize_red32_candidate(const red32_candidate &candidate);
[[nodiscard]] std::string serialize_red32_candidates(
    std::span<const red32_candidate> candidates);
[[nodiscard]] const mlkem_measurement &select_measured_red32_plan(
    mlkem_level level, std::span<const red32_candidate> candidates,
    std::span<const mlkem_measurement> measurements);

}

#endif
