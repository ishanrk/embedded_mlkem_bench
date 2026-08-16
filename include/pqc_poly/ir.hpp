#ifndef PQC_POLY_IR_HPP
#define PQC_POLY_IR_HPP

#include "pqc_poly/selector.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pqc_poly
{

__extension__ typedef __int128 wide_int;

using ir_id = std::uint8_t;
inline constexpr ir_id invalid_ir_id = 0xffU;
inline constexpr std::size_t maximum_ir_stages = 7;

enum class ir_operation_kind
{
    bind_input,
    clear,
    convolve,
    fold,
    reduce,
    write_output,
};

enum class ir_value_kind
{
    input_a,
    input_b,
    zero,
    convolution,
    ring_result,
    reduced,
    output,
};

enum class reduction_state
{
    centered,
    canonical,
    unreduced,
};

enum class storage_kind
{
    input,
    scratch,
    registers,
    output,
};

enum class ring_wrap
{
    none,
    add,
    subtract,
};

struct coefficient_interval
{
    wide_int lower{0};
    wide_int upper{0};

    friend bool operator==(const coefficient_interval &, const coefficient_interval &) = default;
};

struct ir_stage
{
    ir_operation_kind operation{ir_operation_kind::bind_input};
    ir_value_kind value{ir_value_kind::zero};
    coefficient_interval range{};
    std::uint64_t extent{0};
    std::uint64_t maximum_terms{0};
    std::uint16_t required_bits{0};
    reduction_state reduction{reduction_state::unreduced};
    storage_kind storage{storage_kind::registers};
    ring_wrap wrap{ring_wrap::none};
    std::uint8_t dependencies{0};
    ir_id last_use{invalid_ir_id};
    bool in_place{false};

    friend bool operator==(const ir_stage &, const ir_stage &) = default;
};

struct polynomial_ir
{
    operation ring_operation{operation::negacyclic_mul};
    schedule sched{schedule::full};
    std::uint64_t n{0};
    std::uint32_t q{0};
    std::uint64_t block{0};
    std::uint16_t accumulator_bits{0};
    wide_uint estimated_cost{0};
    wide_uint peak_scratch_bytes{0};
    std::uint32_t scratch_alignment{0};
    ir_id scratch_first_use{invalid_ir_id};
    ir_id scratch_last_use{invalid_ir_id};
    std::array<ir_stage, maximum_ir_stages> stages{};
    std::uint8_t stage_count{0};

    friend bool operator==(const polynomial_ir &, const polynomial_ir &) = default;
};

class ir_error final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::string_view ir_operation_kind_name(ir_operation_kind value) noexcept;
[[nodiscard]] std::string_view ir_value_kind_name(ir_value_kind value) noexcept;
[[nodiscard]] std::string_view reduction_state_name(reduction_state value) noexcept;
[[nodiscard]] std::string_view storage_kind_name(storage_kind value) noexcept;
[[nodiscard]] std::string_view ring_wrap_name(ring_wrap value) noexcept;

[[nodiscard]] polynomial_ir lower_ir(const request &req, const candidate_trial &trial);
[[nodiscard]] std::vector<std::string> verify_ir(const request &req, const candidate_trial &trial,
                                                 const polynomial_ir &graph);
[[nodiscard]] std::string ir_to_json(const polynomial_ir &graph);

}

#endif
