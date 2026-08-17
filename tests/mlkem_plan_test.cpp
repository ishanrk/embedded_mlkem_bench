#include "pqc_poly/mlkem_plan.hpp"

#include <cstdlib>
#include <iostream>
#include <set>
#include <string_view>

namespace
{

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "mlkem plan test failed: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message)
{
    if (!condition)
    {
        fail(message);
    }
}

}

int main()
{
    const std::vector<pqc_poly::mlkem_plan> plans = pqc_poly::enumerate_mlkem_plans();
    std::set<std::string> ids;
    unsigned level_count[3]{};
    unsigned software_count[3]{};

    require(plans.size() == 144, "plan count changed");
    for (const pqc_poly::mlkem_plan &plan : plans)
    {
        ids.insert(pqc_poly::mlkem_plan_id(plan));
        const unsigned index = plan.level == pqc_poly::mlkem_level::mlkem512   ? 0U
                               : plan.level == pqc_poly::mlkem_level::mlkem768 ? 1U
                                                                               : 2U;
        ++level_count[index];
        if (plan.instruction == pqc_poly::mlkem_instruction::none)
        {
            ++software_count[index];
        }
    }
    require(ids.size() == 144, "plan ids are not unique");
    for (unsigned i = 0; i < 3; ++i)
    {
        require(level_count[i] == 48, "level plan count changed");
        require(software_count[i] == 24, "software plan count changed");
    }
    require(ids.contains("mlk512_fstage_istage_reach_bcachelate_xnone"), "stable stage id changed");
    require(ids.contains("mlk768_ffuse2_ifuse2_rpair_bcacheeager_xfqmul"),
            "stable fused id changed");
    require(ids.contains("mlk1024_fstage_ifuse2_reach_bdirecteager_xfqmul"),
            "stable direct id changed");

    const pqc_poly::mlkem_request request{};
    std::vector<pqc_poly::mlkem_candidate> candidates;
    std::vector<pqc_poly::mlkem_measurement> measurements;
    for (const pqc_poly::mlkem_plan &plan : plans)
    {
        candidates.push_back(pqc_poly::analyze_mlkem_plan(request, plan));
        if (plan.level == pqc_poly::mlkem_level::mlkem512 &&
            plan.instruction == pqc_poly::mlkem_instruction::none)
        {
            const std::uint64_t cycles = static_cast<std::uint64_t>(measurements.size() + 1U);
            measurements.push_back({.plan_id = pqc_poly::mlkem_plan_id(plan),
                                    .keygen_cycles = cycles,
                                    .encapsulation_cycles = cycles,
                                    .decapsulation_cycles = cycles,
                                    .runtime_stack_bytes = 100,
                                    .allocated_flash_bytes = 1000,
                                    .verified = true});
        }
    }
    require(pqc_poly::select_measured_mlkem_plan(pqc_poly::mlkem_level::mlkem512, candidates,
                                                 measurements)
                    .plan_id == measurements.front().plan_id,
            "measured winner order changed");
    measurements.pop_back();
    try
    {
        static_cast<void>(pqc_poly::select_measured_mlkem_plan(pqc_poly::mlkem_level::mlkem512,
                                                               candidates, measurements));
        fail("partial measurements selected a winner");
    }
    catch (const pqc_poly::mlkem_error &)
    {
    }

    measurements.clear();
    for (const pqc_poly::mlkem_plan &plan : plans)
    {
        if (plan.level == pqc_poly::mlkem_level::mlkem512 &&
            plan.instruction == pqc_poly::mlkem_instruction::fqmul)
        {
            const std::uint64_t cycles = static_cast<std::uint64_t>(measurements.size() + 1U);
            measurements.push_back({.plan_id = pqc_poly::mlkem_plan_id(plan),
                                    .keygen_cycles = cycles,
                                    .encapsulation_cycles = cycles,
                                    .decapsulation_cycles = cycles,
                                    .runtime_stack_bytes = 100,
                                    .allocated_flash_bytes = 1000,
                                    .verified = true});
        }
    }
    require(pqc_poly::select_measured_mlkem_plan(pqc_poly::mlkem_level::mlkem512, candidates,
                                                 measurements, pqc_poly::mlkem_instruction::fqmul)
                    .plan_id == measurements.front().plan_id,
            "custom measured winner order changed");
}
