#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pqc_poly
{

#if defined(__SIZEOF_INT128__)
// model arithmetic stays exact even when a valid request exceeds 64-bit counts.
__extension__ typedef unsigned __int128 wide_uint;
#else
#error "pqc-poly-bench requires a compiler with an unsigned 128-bit integer type"
#endif

enum class operation
{
    negacyclic_mul,
    cyclic_mul,
};

enum class input_representation
{
    centered,
    canonical,
};

enum class output_representation
{
    canonical,
};

enum class aliasing
{
    no,
    may,
};

enum class schedule
{
    full,
    fold,
    output,
};

struct target_spec
{
    std::string name{"host"};
    std::uint16_t word_bits{32};
    std::uint16_t size_bits{32};
    std::vector<std::uint16_t> acc_bits{32, 64};

    friend bool operator==(const target_spec &, const target_spec &) = default;
};

struct limits_spec
{
    std::uint64_t ram{0};

    friend bool operator==(const limits_spec &, const limits_spec &) = default;
};

struct request
{
    // field names mirror the request wire format and keep serialization direct.
    operation op{operation::negacyclic_mul};
    std::uint64_t n{0};
    std::uint32_t q{0};
    input_representation input{input_representation::centered};
    output_representation output{output_representation::canonical};
    aliasing alias{aliasing::no};
    target_spec target{};
    limits_spec limits{};

    friend bool operator==(const request &, const request &) = default;
};

struct schoolbook_plan
{
    // zero block means the schedule does not tile its accumulation.
    schedule sched{schedule::full};
    std::uint16_t acc_bits{32};
    std::uint64_t block{0};

    friend bool operator==(const schoolbook_plan &, const schoolbook_plan &) = default;
};

struct plan_analysis
{
    // derived values make every selection decision visible in the artifacts
    schoolbook_plan plan{};
    wide_uint scratch_bytes{0};
    bool alias_safe{false};
    wide_uint accumulator_bound{0};
    std::uint16_t required_bits{0};
    wide_uint multiplications{0};
    wide_uint additions{0};
    wide_uint reductions{0};
    bool legal{false};
    std::vector<std::string> rejections{};

    friend bool operator==(const plan_analysis &, const plan_analysis &) = default;
};

struct candidate
{
    plan_analysis analysis{};
    wide_uint estimated_cost{0};

    friend bool operator==(const candidate &, const candidate &) = default;
};

class spec_error final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

class selection_error final : public std::runtime_error
{
public:
    selection_error();
};

[[nodiscard]] std::string_view operation_name(operation value) noexcept;
[[nodiscard]] std::string_view input_name(input_representation value) noexcept;
[[nodiscard]] std::string_view output_name(output_representation value) noexcept;
[[nodiscard]] std::string_view aliasing_name(aliasing value) noexcept;
[[nodiscard]] std::string_view schedule_name(schedule value) noexcept;

[[nodiscard]] request parse_request(std::string_view json);
[[nodiscard]] request load_request(const std::filesystem::path &path);
void validate_request(const request &req);

[[nodiscard]] std::int64_t input_lower_bound(const request &req) noexcept;
[[nodiscard]] std::int64_t input_upper_bound(const request &req) noexcept;
[[nodiscard]] std::uint64_t input_bound(const request &req) noexcept;
[[nodiscard]] wide_uint product_bound(const request &req) noexcept;
[[nodiscard]] wide_uint accumulator_bound(const request &req) noexcept;
[[nodiscard]] std::uint16_t required_signed_bits(wide_uint bound) noexcept;
[[nodiscard]] bool signed_width_fits(wide_uint bound, std::uint16_t bits) noexcept;

[[nodiscard]] std::string plan_id(const schoolbook_plan &plan);
[[nodiscard]] std::string wide_to_string(wide_uint value);
[[nodiscard]] std::string request_to_json(const request &req);
[[nodiscard]] std::string candidate_to_json(const candidate &candidate);
[[nodiscard]] std::string candidates_to_json(std::span<const candidate> candidates);

[[nodiscard]] std::vector<candidate> find_candidates(const request &req);
// returned references and pointers remain valid while the input span remains alive.
[[nodiscard]] const candidate &pick_static(std::span<const candidate> candidates);
[[nodiscard]] std::vector<const candidate *> static_frontier(
    std::span<const candidate> candidates);

[[nodiscard]] std::vector<std::string> check_candidate(const request &req,
                                                       const candidate &selected);

}
