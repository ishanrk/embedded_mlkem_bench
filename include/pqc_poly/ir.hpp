#ifndef PQC_POLY_IR_HPP
#define PQC_POLY_IR_HPP

#include "pqc_poly/selector.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pqc_poly
{

__extension__ typedef __int128 wide_int;

using ir_id = std::uint32_t;
inline constexpr ir_id invalid_ir_id = std::numeric_limits<ir_id>::max();

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

enum class ir_operation_kind
{
    bind_input,
    clear,
    convolve,
    fold,
    reduce,
    write_output,
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

struct ir_value
{
    ir_id id{invalid_ir_id};
    ir_value_kind kind{ir_value_kind::zero};
    std::uint64_t extent{0};
    coefficient_interval range{};
    std::uint16_t required_bits{0};
    reduction_state reduction{reduction_state::unreduced};
    storage_kind storage{storage_kind::registers};
    ir_id producer{invalid_ir_id};
    ir_id last_use{invalid_ir_id};
    ir_id scratch_region{invalid_ir_id};

    friend bool operator==(const ir_value &, const ir_value &) = default;
};

struct ir_operation
{
    ir_id id{invalid_ir_id};
    ir_operation_kind kind{ir_operation_kind::bind_input};
    std::vector<ir_id> inputs{};
    ir_id output{invalid_ir_id};
    std::vector<ir_id> dependencies{};
    ring_wrap wrap{ring_wrap::none};
    std::uint64_t coefficient_count{0};
    std::uint64_t maximum_terms{0};
    std::uint16_t accumulator_bits{0};
    bool in_place{false};

    friend bool operator==(const ir_operation &, const ir_operation &) = default;
};

struct scratch_allocation
{
    ir_id id{invalid_ir_id};
    wide_uint offset{0};
    wide_uint bytes{0};
    std::uint32_t alignment{1};
    ir_id first_operation{invalid_ir_id};
    ir_id last_operation{invalid_ir_id};

    friend bool operator==(const scratch_allocation &, const scratch_allocation &) = default;
};

struct polynomial_ir
{
    operation ring_operation{operation::negacyclic_mul};
    std::uint64_t n{0};
    std::uint32_t q{0};
    schedule sched{schedule::full};
    std::uint16_t accumulator_bits{0};
    std::uint64_t block{0};
    wide_uint estimated_cost{0};
    wide_uint peak_scratch_bytes{0};
    std::vector<ir_value> values{};
    std::vector<ir_operation> operations{};
    std::vector<scratch_allocation> scratch{};

    friend bool operator==(const polynomial_ir &, const polynomial_ir &) = default;
};

class ir_error final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::string_view ir_value_kind_name(ir_value_kind value) noexcept;
[[nodiscard]] std::string_view ir_operation_kind_name(ir_operation_kind value) noexcept;
[[nodiscard]] std::string_view reduction_state_name(reduction_state value) noexcept;
[[nodiscard]] std::string_view storage_kind_name(storage_kind value) noexcept;
[[nodiscard]] std::string_view ring_wrap_name(ring_wrap value) noexcept;

[[nodiscard]] polynomial_ir lower_ir(const request &req, const candidate_trial &trial);
[[nodiscard]] std::vector<std::string> verify_ir(const request &req, const candidate_trial &trial,
                                                 const polynomial_ir &graph);
[[nodiscard]] std::string ir_to_json(const polynomial_ir &graph);

}

#endif
