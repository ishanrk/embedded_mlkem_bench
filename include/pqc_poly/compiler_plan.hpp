#ifndef PQC_POLY_COMPILER_PLAN_HPP
#define PQC_POLY_COMPILER_PLAN_HPP

#include "pqc_poly/selector.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pqc_poly
{

enum class algorithm_family
{
    schoolbook,
    blocked,
    karatsuba,
    mixed_karatsuba,
    toom_cook,
    hybrid,
    ntt,
    ntt_crt,
};

enum class reduction_placement
{
    after_convolution,
    per_output,
    after_leaf,
    after_recursion_level,
    after_transform,
    after_crt,
};

enum class memory_schedule
{
    full_product,
    ring_accumulator,
    direct_output,
    reuse_branches,
    separate_branches,
    recompute,
    in_place_transform,
    crt_streaming,
};

struct algorithm_tree
{
    algorithm_family family{algorithm_family::schoolbook};
    std::uint64_t degree{0};
    std::uint16_t recursion_depth{0};
    std::uint64_t leaf_size{0};
    std::uint64_t block_size{0};
    std::vector<algorithm_tree> branches{};

    friend bool operator==(const algorithm_tree &, const algorithm_tree &) = default;
};

struct compiler_range_estimate
{
    wide_uint input_magnitude{0};
    wide_uint product_bound{0};
    wide_uint output_accumulator_bound{0};
    wide_uint peak_intermediate_bound{0};
    std::uint16_t required_bits{0};
    bool proven{false};
    bool saturated{false};

    friend bool operator==(const compiler_range_estimate &,
                           const compiler_range_estimate &) = default;
};

struct compiler_scratch_estimate
{
    wide_uint temporary_bytes{0};
    wide_uint peak_live_coefficients{0};
    bool exact{false};

    friend bool operator==(const compiler_scratch_estimate &,
                           const compiler_scratch_estimate &) = default;
};

struct compiler_plan
{
    algorithm_tree tree{};
    reduction_placement reduction{reduction_placement::after_convolution};
    memory_schedule memory{memory_schedule::full_product};
    std::uint16_t accumulator_bits{0};
    compiler_range_estimate range{};
    compiler_scratch_estimate scratch{};

    // a lowering is present only when the existing code generator accepts the mapping.
    bool has_schoolbook_lowering{false};
    schoolbook_plan schoolbook_lowering{};
    bool legal{false};
    bool emit_supported{false};
    std::vector<std::string> legality_reasons{};
    std::vector<std::string> support_reasons{};

    friend bool operator==(const compiler_plan &, const compiler_plan &) = default;
};

[[nodiscard]] std::string_view algorithm_family_name(algorithm_family value) noexcept;
[[nodiscard]] std::string_view reduction_placement_name(reduction_placement value) noexcept;
[[nodiscard]] std::string_view memory_schedule_name(memory_schedule value) noexcept;

[[nodiscard]] std::string compiler_plan_id(const compiler_plan &plan);
[[nodiscard]] bool compiler_plan_ready(const compiler_plan &plan) noexcept;

[[nodiscard]] std::vector<compiler_plan> enumerate_compiler_plans(const request &req);

// this checker reconstructs tree metrics and lowering constraints from the request.
[[nodiscard]] std::vector<std::string> check_compiler_plan(const request &req,
                                                           const compiler_plan &plan);

[[nodiscard]] std::string compiler_plan_to_json(const compiler_plan &plan);
[[nodiscard]] std::string compiler_plans_to_json(std::span<const compiler_plan> plans);

}

#endif
