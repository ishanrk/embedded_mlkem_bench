#include "pqc_poly/mlkem_red32.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>
#include <string_view>

namespace
{

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "mlkem red32 test failed: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message)
{
    if (!condition)
    {
        fail(message);
    }
}

[[nodiscard]] std::int64_t signed_value(std::uint32_t value)
{
    return value <= static_cast<std::uint32_t>(INT32_MAX)
               ? static_cast<std::int64_t>(value)
               : static_cast<std::int64_t>(value) - INT64_C(4294967296);
}

[[nodiscard]] std::int32_t oracle(std::uint32_t value)
{
    const std::uint32_t inverse =
        ((value & UINT32_C(0xffff)) * UINT32_C(62209)) & UINT32_C(0xffff);
    const std::int32_t signed_inverse =
        static_cast<std::int32_t>(inverse ^ UINT32_C(0x8000)) - INT32_C(32768);
    const std::int64_t numerator =
        signed_value(value) - static_cast<std::int64_t>(signed_inverse) * INT64_C(3329);
    require(numerator % INT64_C(65536) == 0, "oracle numerator is not divisible");
    return static_cast<std::int32_t>(numerator / INT64_C(65536));
}

void check_value(std::uint32_t value)
{
    const std::int32_t actual = pqc_poly::red32_reference(value);
    require(actual == oracle(value), "red32 value mismatch");
    require(actual >= -34432 && actual <= 34432, "red32 range exceeded");
    const std::int64_t congruence =
        static_cast<std::int64_t>(actual) * INT64_C(65536) - signed_value(value);
    require(congruence % INT64_C(3329) == 0, "red32 congruence mismatch");
}

[[nodiscard]] std::uint32_t next_random(std::uint32_t &state)
{
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

}

int main()
{
    constexpr std::array<std::uint32_t, 13> boundaries{
        UINT32_C(0x00000000), UINT32_C(0x00000001), UINT32_C(0xffffffff),
        UINT32_C(0x7fffffff), UINT32_C(0x80000000), UINT32_C(0x00007fff),
        UINT32_C(0x00008000), UINT32_C(0xffff7fff), UINT32_C(0xffff8000),
        UINT32_C(0x0000ffff), UINT32_C(0x7fff0000), UINT32_C(0x80000001),
        UINT32_C(0x89abcdef)};
    for (const std::uint32_t value : boundaries)
    {
        check_value(value);
    }
    for (std::uint32_t low = 0; low <= UINT32_C(0xffff); ++low)
    {
        check_value(UINT32_C(0x89ab0000) | low);
    }
    std::uint32_t state = UINT32_C(0x243f6a88);
    for (unsigned i = 0; i < 100000U; ++i)
    {
        check_value(next_random(state));
    }
    require(pqc_poly::red32_reference(UINT32_C(0x00000001)) !=
                pqc_poly::red32_reference(UINT32_C(0x00010001)),
            "red32 ignored the upper input half");

    const std::vector<pqc_poly::red32_plan> plans =
        pqc_poly::enumerate_red32_comparison_plans();
    require(plans.size() == 72U, "red32 plan count changed");
    std::set<std::string> ids;
    unsigned levels[3]{};
    std::vector<pqc_poly::red32_candidate> candidates;
    candidates.reserve(plans.size());
    const pqc_poly::mlkem_request request{};
    for (const pqc_poly::red32_plan &plan : plans)
    {
        const std::string id = pqc_poly::red32_plan_id(plan);
        ids.insert(id);
        const unsigned index = plan.level == pqc_poly::mlkem_level::mlkem512   ? 0U
                               : plan.level == pqc_poly::mlkem_level::mlkem768 ? 1U
                                                                               : 2U;
        ++levels[index];
        candidates.push_back(pqc_poly::analyze_red32_plan(request, plan));
        require(candidates.back().legal, "red32 plan was rejected");
        require(pqc_poly::check_red32_candidate(request, candidates.back()).empty(),
                "red32 checker rejected a valid candidate");
        const pqc_poly::mlkem_plan schedule = pqc_poly::red32_schedule_plan(plan);
        require(schedule.instruction == pqc_poly::mlkem_instruction::none,
                "red32 leaked into the primary instruction enum");
    }
    require(ids.size() == 72U, "red32 plan ids are not unique");
    require(levels[0] == 24U && levels[1] == 24U && levels[2] == 24U,
            "red32 level count changed");
    require(ids.contains("mlk512_fstage_istage_reach_bcachelate_xred32"),
            "stable red32 stage id changed");
    require(ids.contains("mlk768_ffuse2_ifuse2_rpair_bcacheeager_xred32"),
            "stable red32 fused id changed");
    require(ids.contains("mlk1024_fstage_ifuse2_reach_bdirecteager_xred32"),
            "stable red32 direct id changed");

    pqc_poly::red32_candidate mutation = candidates.front();
    mutation.id += "-bad";
    require(!pqc_poly::check_red32_candidate(request, mutation).empty(),
            "red32 checker accepted a bad id");
    mutation = candidates.front();
    mutation.forward_records.pop_back();
    require(!pqc_poly::check_red32_candidate(request, mutation).empty(),
            "red32 checker accepted a missing butterfly");
    mutation = candidates.front();
    mutation.scratch_bytes += 2U;
    require(!pqc_poly::check_red32_candidate(request, mutation).empty(),
            "red32 checker accepted bad scratch accounting");
    mutation = candidates.front();
    mutation.canonical_rs2_zero = false;
    require(!pqc_poly::check_red32_candidate(request, mutation).empty(),
            "red32 checker accepted a noncanonical encoding");

    const pqc_poly::mlkem_request tight{.scratch_limit = 0U,
                                       .caller_workspace_limit = UINT64_MAX};
    const pqc_poly::red32_candidate rejected =
        pqc_poly::analyze_red32_plan(tight, plans.front());
    require(!rejected.legal && rejected.rejections.size() == 1U &&
                rejected.rejections.front() == "scratch_limit",
            "red32 scratch rejection changed");
    require(pqc_poly::check_red32_candidate(tight, rejected).empty(),
            "red32 checker rejected a correct rejection");

    std::vector<pqc_poly::mlkem_measurement> measurements;
    for (const pqc_poly::red32_candidate &candidate : candidates)
    {
        if (candidate.plan.level == pqc_poly::mlkem_level::mlkem512)
        {
            const std::uint64_t cycles = measurements.size() + 1U;
            measurements.push_back({.plan_id = candidate.id,
                                    .keygen_cycles = cycles,
                                    .encapsulation_cycles = cycles,
                                    .decapsulation_cycles = cycles,
                                    .runtime_stack_bytes = 100U,
                                    .allocated_flash_bytes = 1000U,
                                    .verified = true});
        }
    }
    require(pqc_poly::select_measured_red32_plan(pqc_poly::mlkem_level::mlkem512,
                                                 candidates, measurements)
                    .plan_id == measurements.front().plan_id,
            "red32 winner order changed");
    measurements.pop_back();
    try
    {
        static_cast<void>(pqc_poly::select_measured_red32_plan(
            pqc_poly::mlkem_level::mlkem512, candidates, measurements));
        fail("partial red32 measurements selected a winner");
    }
    catch (const pqc_poly::mlkem_error &)
    {
    }


    const std::string golden = R"json({
  "schema": "pqc-poly-bench/mlkem-red32-candidate-v1",
  "id": "mlk512_fstage_istage_reach_bcachelate_xred32",
  "plan": {
    "level": "512",
    "forward": "stage",
    "inverse": "stage",
    "inverse_reduction": "each",
    "basemul": "cachelate"
  },
  "instruction": {
    "name": "red32",
    "encoding_mask": "0xfe00707f",
    "encoding_match": "0x0000100b",
    "canonical_rs2_zero": true
  },
  "forward_record_count": 127,
  "inverse_record_count": 127,
  "forward_bound": 26632,
  "inverse_lazy_bound": 13316,
  "accumulator_bound": 536870912,
  "mulcache_coefficients": 256,
  "scratch_bytes": 512,
  "caller_workspace_bytes": 2560,
  "reduction_range": [-34432, 34432],
  "ntt_in_place": true,
  "intt_in_place": true,
  "fixed_loop_structure": true,
  "full_domain_reduction": true,
  "standard_mul_before_reduction": true,
  "legal": true,
  "rejections": []
}
)json";
    require(pqc_poly::serialize_red32_candidate(candidates.front()) == golden,
            "red32 candidate json changed");

    const std::string serialized = pqc_poly::serialize_red32_candidates(candidates);
    require(serialized.starts_with("[\n  {\n"), "red32 json prefix changed");
    require(serialized.find("\"encoding_match\": \"0x0000100b\"") != std::string::npos,
            "red32 encoding was not serialized");
    require(serialized.ends_with("]\n"), "red32 json suffix changed");
}
