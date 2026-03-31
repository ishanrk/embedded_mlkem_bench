#include "pqc_poly/compiler_plan.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{

void expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] pqc_poly::request make_request(std::uint64_t n = 256, std::uint64_t ram = 8192,
                                             std::string_view alias = "no")
{
    return pqc_poly::parse_request(
        "{\"op\":\"negacyclic_mul\",\"n\":" + std::to_string(n) + ",\"q\":3329,\"alias\":\"" +
        std::string(alias) +
        "\",\"target\":{\"name\":\"rv32im\",\"word_bits\":32,\"size_bits\":32,"
        "\"acc_bits\":[32,64]},\"limits\":{\"ram\":" +
        std::to_string(ram) + "}}");
}

void test_enumeration_and_support_boundary()
{
    const pqc_poly::request req = make_request();
    const std::vector<pqc_poly::compiler_plan> plans = pqc_poly::enumerate_compiler_plans(req);
    const std::vector<pqc_poly::candidate_trial> schoolbook = pqc_poly::find(req);
    expect(plans.size() == schoolbook.size() + 7, "unexpected compiler plan count");

    std::array<bool, 8> saw_family{};
    std::size_t supported = 0;
    std::vector<std::string> ids;
    ids.reserve(plans.size());
    for (const pqc_poly::compiler_plan &plan : plans)
    {
        ids.push_back(pqc_poly::compiler_plan_id(plan));
        const std::size_t family = static_cast<std::size_t>(plan.tree.family);
        expect(family < saw_family.size(), "algorithm family is outside the enum");
        saw_family[family] = true;
        expect(!plan.legality_reasons.empty(), "plan has no legality explanation");
        expect(!plan.support_reasons.empty(), "plan has no support explanation");
        expect(pqc_poly::check_compiler_plan(req, plan).empty(),
               "independent checker rejected an enumerated plan");

        if (plan.emit_supported)
        {
            ++supported;
            expect(plan.has_schoolbook_lowering, "supported plan has no lowering mapping");
            expect(plan.tree.family == pqc_poly::algorithm_family::schoolbook ||
                       plan.tree.family == pqc_poly::algorithm_family::blocked,
                   "unsupported family was marked emit supported");
            expect(plan.range.proven && plan.scratch.exact, "supported plan lacks exact analysis");
        }
        else
        {
            expect(!plan.has_schoolbook_lowering, "blocked plan has a lowering mapping");
            expect(!plan.legal && !pqc_poly::compiler_plan_ready(plan),
                   "blocked plan was marked runnable");
            expect(!plan.range.proven && !plan.scratch.exact, "blocked plan claims exact analysis");
        }
    }
    expect(std::ranges::all_of(saw_family, [](bool seen) { return seen; }),
           "enumeration omitted an algorithm family");
    expect(supported == schoolbook.size(), "support boundary does not match existing lowerings");
    std::ranges::sort(ids);
    expect(std::ranges::adjacent_find(ids) == ids.end(), "compiler plan ids are not unique");
}

void test_tree_shape_and_determinism()
{
    const pqc_poly::request req = make_request(255);
    const std::vector<pqc_poly::compiler_plan> first = pqc_poly::enumerate_compiler_plans(req);
    const std::vector<pqc_poly::compiler_plan> second = pqc_poly::enumerate_compiler_plans(req);
    expect(first == second, "enumeration is not deterministic");
    expect(pqc_poly::compiler_plans_to_json(first) == pqc_poly::compiler_plans_to_json(second),
           "json serialization is not deterministic");

    const auto mixed = std::ranges::find_if(
        first, [](const pqc_poly::compiler_plan &plan)
        { return plan.tree.family == pqc_poly::algorithm_family::mixed_karatsuba; });
    expect(mixed != first.end(), "mixed karatsuba tree is missing");
    expect(mixed->tree.branches.size() == 3 && mixed->tree.recursion_depth == 2,
           "mixed karatsuba tree shape changed");

    const auto hybrid =
        std::ranges::find_if(first, [](const pqc_poly::compiler_plan &plan)
                             { return plan.tree.family == pqc_poly::algorithm_family::hybrid; });
    expect(hybrid != first.end(), "hybrid tree is missing");
    expect(hybrid->tree.branches.size() == 5 && hybrid->tree.recursion_depth == 2,
           "hybrid tree shape changed");

    const std::string json = pqc_poly::compiler_plan_to_json(*hybrid);
    expect(json.find("\"family\": \"hybrid\"") != std::string::npos, "json omits the family");
    expect(json.find("\"emit_supported\": false") != std::string::npos,
           "json omits the capability blocker");
    expect(json.find("\"legality_reasons\": [") != std::string::npos,
           "json omits legality explanations");
    expect(json.find("\"schoolbook_lowering\": null") != std::string::npos,
           "json invents an unavailable lowering");
}

void test_independent_checker_mutations()
{
    const pqc_poly::request req = make_request();
    const std::vector<pqc_poly::compiler_plan> plans = pqc_poly::enumerate_compiler_plans(req);

    pqc_poly::compiler_plan bad = plans.front();
    ++bad.tree.degree;
    expect(!pqc_poly::check_compiler_plan(req, bad).empty(),
           "checker accepted a wrong root degree");

    bad = plans.front();
    ++bad.scratch.temporary_bytes;
    expect(!pqc_poly::check_compiler_plan(req, bad).empty(),
           "checker accepted a wrong scratch proof");

    bad = plans.front();
    bad.schoolbook_lowering.acc_bits = 0;
    expect(!pqc_poly::check_compiler_plan(req, bad).empty(),
           "checker accepted an invalid accumulator type");

    const auto recursive =
        std::ranges::find_if(plans, [](const pqc_poly::compiler_plan &plan)
                             { return plan.tree.family == pqc_poly::algorithm_family::karatsuba; });
    expect(recursive != plans.end(), "karatsuba plan is missing");

    bad = *recursive;
    bad.tree.recursion_depth = 9;
    expect(!pqc_poly::check_compiler_plan(req, bad).empty(),
           "checker accepted an inconsistent recursion depth");

    bad = *recursive;
    bad.tree.branches.front().family = pqc_poly::algorithm_family::ntt;
    expect(!pqc_poly::check_compiler_plan(req, bad).empty(),
           "checker accepted an invalid recursive child family");

    bad = *recursive;
    bad.emit_supported = true;
    bad.legal = true;
    expect(!pqc_poly::check_compiler_plan(req, bad).empty(),
           "checker accepted an invented recursive lowering");

    bad = *recursive;
    bad.legality_reasons.clear();
    expect(!pqc_poly::check_compiler_plan(req, bad).empty(),
           "checker accepted a plan without an explanation");

    bad = *recursive;
    bad.memory = pqc_poly::memory_schedule::direct_output;
    expect(!pqc_poly::check_compiler_plan(req, bad).empty(),
           "checker accepted an inconsistent recursive schedule");
}

void test_ram_and_alias_constraints()
{
    const pqc_poly::request req = make_request(256, 0, "may");
    const std::vector<pqc_poly::compiler_plan> plans = pqc_poly::enumerate_compiler_plans(req);
    expect(std::ranges::none_of(plans, pqc_poly::compiler_plan_ready),
           "zero-ram alias request has a runnable plan");

    for (const pqc_poly::compiler_plan &plan : plans)
    {
        expect(pqc_poly::check_compiler_plan(req, plan).empty(),
               "checker rejected a constrained candidate");
    }
}

}

int main()
{
    try
    {
        test_enumeration_and_support_boundary();
        test_tree_shape_and_determinism();
        test_independent_checker_mutations();
        test_ram_and_alias_constraints();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
