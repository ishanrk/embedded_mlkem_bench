#ifndef PQC_POLY_MLKEM_PLAN_HPP
#define PQC_POLY_MLKEM_PLAN_HPP

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pqc_poly
{

// prevents old planner evidence from being read with a newer layout
inline constexpr std::string_view mlkem_plan_schema = "pqc-poly-bench/mlkem-plan-v1";

// now enumerate over all possible choices like the mlkem version
// differetn optimizations like fusing 2 layers 
// modulo reduction after every layer or every 2 layers

enum class mlkem_level
{
    mlkem512,
    mlkem768,
    mlkem1024,
};

// stage major completes one NTT layer before the next layer
// fused traversal keeps four coefficients in local variables across two layers
enum class ntt_traversal
{
    stage_major,
    fuse_two_layers,
};

enum class intt_traversal
{
    stage_major,
    fuse_two_layers,
};

enum class intt_sum_reduction
{
    // delaying reduction removes work but allows larger sums
    every_layer,
    after_layer_pair,
};

// caching strategy
enum class basemul_schedule
{
    // cached modes store twiddle products and late mode delays their reduction
    cached_late32,
    cached_eager32,
    // direct mode recomputes twiddle products and needs no cache
    direct_eager32,
};

enum class mlkem_instruction
{
    none,
    fqmul,
};

// one plan stores every choice used to generate a benchmark backend
struct mlkem_plan
{
    mlkem_level level{mlkem_level::mlkem512};
    ntt_traversal forward{ntt_traversal::stage_major};
    intt_traversal inverse{intt_traversal::stage_major};
    intt_sum_reduction inverse_reduction{intt_sum_reduction::every_layer};
    basemul_schedule basemul{basemul_schedule::cached_late32};
    mlkem_instruction instruction{mlkem_instruction::none};

    // friend gives this comparison access to every field
    // default makes the compiler compare every field
    friend bool operator==(const mlkem_plan &, const mlkem_plan &) = default;
};

// constraints on user memory allocation
struct mlkem_request
{
    std::uint64_t scratch_limit{UINT64_MAX};
    std::uint64_t caller_workspace_limit{UINT64_MAX};

    friend bool operator==(const mlkem_request &, const mlkem_request &) = default;
};

// block of one NTT
struct mlkem_record
{
    // identifies one butterfly block and the zeta table entry it uses
    std::uint16_t layer{0};
    std::uint16_t block{0};
    std::uint16_t zeta_index{0};
    std::uint16_t left_base{0};
    std::uint16_t right_base{0};
    std::uint16_t length{0};

    friend bool operator==(const mlkem_record &, const mlkem_record &) = default;
};

// one candidate stores a plan and every value derived by analysis
struct mlkem_candidate
{
    std::string schema{mlkem_plan_schema};
    mlkem_plan plan{};
    std::string id{};
    std::vector<mlkem_record> forward_records{};
    std::vector<mlkem_record> inverse_records{};
    std::uint32_t forward_bound{0};
    std::uint32_t inverse_lazy_bound{0};
    // largest basemul sum before its final reduction
    std::uint64_t accumulator_bound{0};
    std::uint32_t mulcache_coefficients{0};
    std::uint32_t scratch_bytes{0};
    std::uint32_t caller_workspace_bytes{0};
    bool ntt_in_place{false};
    bool intt_in_place{false};
    bool fixed_loop_structure{false};
    bool legal{false};
    std::vector<std::string> rejections{};

    friend bool operator==(const mlkem_candidate &, const mlkem_candidate &) = default;
};

// measured costs and verification status for one candidate
struct mlkem_measurement
{
    std::string plan_id{};
    std::uint64_t keygen_cycles{0};
    std::uint64_t encapsulation_cycles{0};
    std::uint64_t decapsulation_cycles{0};
    std::uint64_t runtime_stack_bytes{0};
    std::uint64_t allocated_flash_bytes{0};
    bool verified{false};

    friend bool operator==(const mlkem_measurement &, const mlkem_measurement &) = default;
};

class mlkem_error final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// nodiscard warns when the caller ignores a returned value
// noexcept records that these name lookups do not throw
[[nodiscard]] std::string_view mlkem_level_name(mlkem_level value) noexcept;
[[nodiscard]] std::string_view ntt_traversal_name(ntt_traversal value) noexcept;
[[nodiscard]] std::string_view intt_traversal_name(intt_traversal value) noexcept;
[[nodiscard]] std::string_view intt_reduction_name(intt_sum_reduction value) noexcept;
[[nodiscard]] std::string_view basemul_schedule_name(basemul_schedule value) noexcept;
[[nodiscard]] std::string_view mlkem_instruction_name(mlkem_instruction value) noexcept;
[[nodiscard]] unsigned mlkem_k(mlkem_level level) noexcept;

// analyzer turns each plan into a checked candidate
// code generation turns each candidate into a C backend
// measurements from every backend are compared to select one winner
[[nodiscard]] mlkem_request parse_mlkem_request(std::string_view json);
[[nodiscard]] std::string mlkem_plan_id(const mlkem_plan &plan);
[[nodiscard]] std::vector<mlkem_plan> enumerate_mlkem_plans();
[[nodiscard]] mlkem_candidate analyze_mlkem_plan(const mlkem_request &request,
                                                 const mlkem_plan &plan);
[[nodiscard]] std::vector<std::string> check_mlkem_plan(const mlkem_request &request,
                                                        const mlkem_candidate &candidate);
[[nodiscard]] std::string serialize_mlkem_candidate(const mlkem_candidate &candidate);
[[nodiscard]] std::string serialize_mlkem_candidates(std::span<const mlkem_candidate> candidates);
[[nodiscard]] const mlkem_measurement &select_measured_mlkem_plan(
    mlkem_level level, std::span<const mlkem_candidate> candidates,
    std::span<const mlkem_measurement> measurements,
    mlkem_instruction instruction = mlkem_instruction::none);

}

#endif
