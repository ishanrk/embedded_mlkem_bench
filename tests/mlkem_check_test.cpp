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
    unsigned unavailable = 0;
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
            require(!candidate.legal && has(errors, "instruction_unavailable"),
                    "custom plan lacks stable rejection");
            ++unavailable;
        }
    }
    require(safe == 72 && unavailable == 72, "safe or rejected plan count changed");

    pqc_poly::mlkem_candidate candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    candidate.schema = "bad";
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "bad_schema"),
            "schema mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    candidate.id += 'x';
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "bad_plan_id"),
            "id mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    candidate.forward_records.front().zeta_index = 0;
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "bad_twiddle_schedule"),
            "twiddle mutation passed");
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
    candidate.ntt_in_place = false;
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "alias"), "alias mutation passed");
    candidate = pqc_poly::analyze_mlkem_plan(request, plans.front());
    candidate.fixed_loop_structure = false;
    require(has(pqc_poly::check_mlkem_plan(request, candidate), "constant_time_structure"),
            "loop mutation passed");
}
