#include "pqc_poly/mlkem_plan.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "mlkem check test failed: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message)
{
    if (!condition)
    {
        fail(message);
    }
}

bool has(std::span<const std::string> errors, std::string_view value)
{
    return std::find(errors.begin(), errors.end(), value) != errors.end();
}

}

int main()
{
    const pqc_poly::mlkem_request request{};
    const std::vector<pqc_poly::mlkem_plan> plans = pqc_poly::enumerate_mlkem_plans();
    unsigned safe = 0;
    unsigned custom_count = 0;
    for (const pqc_poly::mlkem_plan &plan : plans)
    {
        const pqc_poly::mlkem_candidate candidate = pqc_poly::analyze_mlkem_plan(request, plan);
        const std::vector<std::string> errors = pqc_poly::check_mlkem_plan(request, candidate);
        if (plan.instruction == pqc_poly::mlkem_instruction::none)
        {
            require(candidate.legal && errors.empty(), "software plan was rejected");
            ++safe;
        }
        else
        {
            require(candidate.legal && errors.empty(), "custom plan was rejected");
            ++custom_count;
        }
    }
    require(safe == 72 && custom_count == 72, "software or custom plan count changed");

    pqc_poly::mlkem_candidate candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    candidate.schema = "bad";
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "bad_schema"),
            "schema mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    candidate.id += 'x';
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "bad_plan_id"),
            "id mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    candidate.plan.level = static_cast<pqc_poly::mlkem_level>(99);
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "bad_plan_id"),
            "level mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    candidate.plan.forward = static_cast<pqc_poly::ntt_traversal>(99);
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "bad_plan_id"),
            "forward enum mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    candidate.plan.inverse = static_cast<pqc_poly::intt_traversal>(99);
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "bad_plan_id"),
            "inverse enum mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    candidate.plan.inverse_reduction = static_cast<pqc_poly::intt_sum_reduction>(99);
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "bad_plan_id"),
            "reduction enum mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    candidate.plan.basemul = static_cast<pqc_poly::basemul_schedule>(99);
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "bad_plan_id"),
            "base enum mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    candidate.plan.instruction = static_cast<pqc_poly::mlkem_instruction>(99);
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "bad_plan_id"),
            "instruction enum mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    candidate.forward_records.front().zeta_index = 0;
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "bad_twiddle_schedule"),
            "twiddle mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    ++candidate.forward_records.front().layer;
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "bad_twiddle_schedule"),
            "forward layer mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    ++candidate.forward_records.front().block;
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "bad_twiddle_schedule"),
            "forward block mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    ++candidate.forward_records.front().left_base;
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "array_index"),
            "forward left index mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    ++candidate.forward_records.front().right_base;
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "array_index"),
            "forward right index mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    ++candidate.forward_records.front().length;
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "array_index"),
            "forward length mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    candidate.forward_records.pop_back();
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "missing_butterfly"),
            "butterfly deletion passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    candidate.forward_records.push_back(candidate.forward_records.back());
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "duplicate_butterfly"),
            "butterfly duplication passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    candidate.inverse_records.front().right_base = 256;
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "array_index"),
            "index mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    ++candidate.inverse_records.front().zeta_index;
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "bad_twiddle_schedule"),
            "inverse twiddle mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    ++candidate.forward_bound;
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "coefficient_storage_overflow"),
            "coefficient bound mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    ++candidate.inverse_lazy_bound;
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "barrett_input_range"),
            "inverse bound mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    ++candidate.accumulator_bound;
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "accumulator_overflow"),
            "accumulator mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    ++candidate.scratch_bytes;
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "scratch_limit"),
            "scratch mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    ++candidate.mulcache_coefficients;
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "array_index"),
            "cache mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    ++candidate.caller_workspace_bytes;
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "caller_workspace_limit"),
            "caller mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    candidate.ntt_in_place = false;
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "alias"), "alias mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    candidate.intt_in_place = false;
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "alias"),
            "inverse alias mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    candidate.fixed_loop_structure = false;
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "constant_time_structure"),
            "loop mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    candidate.legal = false;
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "analysis_overflow"),
            "legality mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    candidate.rejections.emplace_back("alias");
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "analysis_overflow"),
            "rejection mutation passed");

    pqc_poly::mlkem_request limited{.scratch_limit = 0, .caller_workspace_limit = 0};
    const auto custom =
        std::find_if(plans.begin(), plans.end(),
                     [](const pqc_poly::mlkem_plan &plan)
                     {
                         return plan.basemul == pqc_poly::basemul_schedule::cached_late32 &&
                                plan.instruction == pqc_poly::mlkem_instruction::fqmul;
                     });
    require(custom != plans.end(), "custom plan is missing");
    candidate = pqc_poly::analyze_mlkem_plan(limited, *custom);
    require(
        candidate.rejections == std::vector<std::string>{"scratch_limit", "caller_workspace_limit"},
        "rejection order changed");
    require(pqc_poly::check_mlkem_plan(limited, candidate).size() == 2,
            "checked rejection order changed");
}
