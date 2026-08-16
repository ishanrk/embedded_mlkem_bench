#include "pqc_poly/selector.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
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

template <class function_type>
std::string expect_spec_error(function_type &&function)
{
    try
    {
        function();
    }
    catch (const pqc_poly::spec_error &error)
    {
        return error.what();
    }
    throw std::runtime_error("expected spec_error");
}

pqc_poly::request make_request(std::uint64_t n, std::uint32_t q, std::uint64_t ram,
                               std::string_view input, std::string_view alias,
                               std::string_view acc_bits)
{
    return pqc_poly::parse_request(
        "{\"op\":\"negacyclic_mul\",\"n\":" + std::to_string(n) + ",\"q\":" + std::to_string(q) +
        ",\"input\":\"" + std::string(input) + "\",\"alias\":\"" + std::string(alias) +
        "\",\"target\":{\"name\":\"test\",\"word_bits\":32,\"acc_bits\":[" + std::string(acc_bits) +
        "]},\"limits\":{\"ram\":" + std::to_string(ram) + "}}");
}

std::vector<std::string> legal_ids(const pqc_poly::request &req)
{
    std::vector<std::string> ids;
    for (const pqc_poly::candidate &trial : pqc_poly::find_candidates(req))
    {
        if (trial.analysis.legal)
        {
            ids.push_back(pqc_poly::plan_id(trial.analysis.plan));
        }
    }
    return ids;
}

void test_request_parser()
{
    const pqc_poly::request req =
        pqc_poly::parse_request(R"({"op":"negacyclic_mul","n":8,"q":17,"limits":{"ram":1000}})");
    expect(req.input == pqc_poly::input_representation::centered, "bad input default");
    expect(req.output == pqc_poly::output_representation::canonical, "bad output default");
    expect(req.alias == pqc_poly::aliasing::no, "bad alias default");
    expect(req.target.name == "host", "bad target name default");
    expect(req.target.acc_bits == std::vector<std::uint16_t>({32, 64}), "bad acc default");
    expect(pqc_poly::input_lower_bound(req) == -8, "bad lower bound");
    expect(pqc_poly::input_upper_bound(req) == 8, "bad upper bound");

    const pqc_poly::request normalized = pqc_poly::parse_request(
        R"({"op":"cyclic_mul","n":8,"q":17,"target":{"acc_bits":[64,32,64]}})");
    expect(normalized.target.acc_bits == std::vector<std::uint16_t>({32, 64}),
           "accumulator widths were not normalized");

    static_cast<void>(expect_spec_error(
        []
        {
            static_cast<void>(pqc_poly::parse_request(
                R"({"op":"negacyclic_mul","op":"cyclic_mul","n":8,"q":17})"));
        }));

    const std::string error = expect_spec_error(
        []
        {
            static_cast<void>(
                pqc_poly::parse_request(R"({"op":"cyclic_mul","n":8,"q":17,"zeta":1,"alpha":2})"));
        });
    expect(error == "unknown request field: alpha, zeta", "unknown fields are not sorted");

    constexpr std::array invalid{
        std::string_view{R"([])"},
        std::string_view{R"({"op":"cyclic_mul","n":1,"q":17})"},
        std::string_view{R"({"op":"cyclic_mul","n":8,"q":1})"},
        std::string_view{R"({"op":"cyclic_mul","n":8,"q":17,"input":"wide"})"},
        std::string_view{R"({"op":"cyclic_mul","n":8,"q":17,"target":[]})"},
        std::string_view{R"({"op":"cyclic_mul","n":8,"q":17,"limits":{"ram":-1}})"},
        std::string_view{R"({"op":"cyclic_mul","n":true,"q":17})"},
        std::string_view{R"({"op":"cyclic_mul","n":8,"q":17,"target":{"acc_bits":48}})"},
    };
    for (const std::string_view json : invalid)
    {
        static_cast<void>(
            expect_spec_error([json] { static_cast<void>(pqc_poly::parse_request(json)); }));
    }
}

void test_ranking_and_frontier()
{
    const pqc_poly::request req = make_request(8, 17, 60, "centered", "no", "32");
    const std::vector<pqc_poly::candidate> trials = pqc_poly::find_candidates(req);
    const std::array expected_ids{
        std::string_view{"sb_full_i32"},
        std::string_view{"sb_fold_b4_i32"},
        std::string_view{"sb_fold_b8_i32"},
        std::string_view{"sb_out_i32"},
    };
    const std::array<pqc_poly::wide_uint, 4> expected_costs{391, 408, 402, 440};
    expect(trials.size() == expected_ids.size(), "bad candidate count");
    for (std::size_t index = 0; index < trials.size(); ++index)
    {
        expect(pqc_poly::plan_id(trials[index].analysis.plan) == expected_ids[index],
               "bad candidate order");
        expect(trials[index].estimated_cost == expected_costs[index], "bad candidate cost");
        expect(pqc_poly::check_candidate(req, trials[index]).empty(), "checker rejected a trial");
    }
    expect(pqc_poly::plan_id(pqc_poly::pick_static(trials).analysis.plan) == "sb_full_i32",
           "bad selected plan");

    const std::vector<const pqc_poly::candidate *> frontier = pqc_poly::static_frontier(trials);
    const std::array wanted{
        std::string_view{"sb_out_i32"},
        std::string_view{"sb_fold_b8_i32"},
        std::string_view{"sb_full_i32"},
    };
    expect(frontier.size() == wanted.size(), "bad frontier size");
    for (std::size_t index = 0; index < frontier.size(); ++index)
    {
        expect(pqc_poly::plan_id(frontier[index]->analysis.plan) == wanted[index], "bad frontier");
    }
}

void test_legality_and_checker()
{
    const pqc_poly::request req = make_request(8, 17, 1000, "centered", "may", "32");
    std::vector<pqc_poly::candidate> trials = pqc_poly::find_candidates(req);
    const auto output =
        std::find_if(trials.begin(), trials.end(),
                     [](const pqc_poly::candidate &trial)
                     { return trial.analysis.plan.sched == pqc_poly::schedule::output; });
    expect(output != trials.end(), "missing output plan");
    expect(!output->analysis.legal, "alias-unsafe plan was legal");
    expect(output->analysis.rejections == std::vector<std::string>{"alias"},
           "bad alias failure");

    pqc_poly::candidate bad = trials.front();
    ++bad.analysis.accumulator_bound;
    const std::vector<std::string> range_errors = pqc_poly::check_candidate(req, bad);
    expect(std::ranges::find(range_errors, "bad range") != range_errors.end(),
           "checker accepted a bad range");

    bad = trials.front();
    ++bad.estimated_cost;
    const std::vector<std::string> errors = pqc_poly::check_candidate(req, bad);
    expect(std::ranges::find(errors, "bad score") != errors.end(), "checker accepted a bad score");
}

void test_boundaries()
{
    const pqc_poly::request canonical = make_request(193, 3329, 20000, "canonical", "no", "32");
    const pqc_poly::request canonical_bad = make_request(194, 3329, 20000, "canonical", "no", "32");
    expect(std::ranges::any_of(pqc_poly::find_candidates(canonical),
                               [](const auto &trial) { return trial.analysis.legal; }),
           "canonical threshold rejected");
    expect(std::ranges::none_of(pqc_poly::find_candidates(canonical_bad),
                                [](const auto &trial) { return trial.analysis.legal; }),
           "canonical overflow threshold accepted");

    const pqc_poly::request maximum =
        make_request(std::numeric_limits<std::uint64_t>::max() / 4, 2, 0, "centered", "no", "64");
    const std::vector<pqc_poly::candidate> maximum_trials = pqc_poly::find_candidates(maximum);
    expect(!maximum_trials.empty(), "maximum request produced no trials");
    for (const pqc_poly::candidate &trial : maximum_trials)
    {
        expect(!trial.analysis.legal, "oversized target object was legal");
        expect(std::ranges::find(trial.analysis.rejections, "size_t") !=
                   trial.analysis.rejections.end(),
               "oversized target object lacks size_t failure");
    }
}

void test_reference_boundary_matrix()
{
    const pqc_poly::request even_centered = pqc_poly::parse_request(
        R"({"op":"cyclic_mul","n":8,"q":256,"target":{"word_bits":8,"acc_bits":[32]}})");
    expect(pqc_poly::input_lower_bound(even_centered) == -128, "even centered lower bound changed");
    expect(pqc_poly::input_upper_bound(even_centered) == 127, "even centered upper bound changed");
    expect(pqc_poly::input_bound(even_centered) == 128, "even centered magnitude changed");

    struct ram_case
    {
        std::uint64_t ram;
        std::vector<std::string> ids;
    };
    const std::array ram_cases{
        ram_case{31, {"sb_out_i32"}},
        ram_case{32, {"sb_fold_b4_i32", "sb_fold_b8_i32", "sb_out_i32"}},
        ram_case{59, {"sb_fold_b4_i32", "sb_fold_b8_i32", "sb_out_i32"}},
        ram_case{60, {"sb_full_i32", "sb_fold_b4_i32", "sb_fold_b8_i32", "sb_out_i32"}},
    };
    for (const ram_case &entry : ram_cases)
    {
        const pqc_poly::request req = make_request(8, 17, entry.ram, "centered", "no", "32");
        expect(legal_ids(req) == entry.ids, "ram legality edge changed");
    }

    const std::vector<pqc_poly::candidate> centered_legal =
        pqc_poly::find_candidates(make_request(775, 3329, 20000, "centered", "no", "32"));
    const std::vector<pqc_poly::candidate> centered_illegal =
        pqc_poly::find_candidates(make_request(776, 3329, 20000, "centered", "no", "32"));
    expect(
        std::ranges::any_of(centered_legal, [](const auto &trial) { return trial.analysis.legal; }),
        "centered width boundary rejected n 775");
    expect(std::ranges::none_of(centered_illegal,
                                [](const auto &trial) { return trial.analysis.legal; }),
           "centered width boundary accepted n 776");
    for (const pqc_poly::candidate &trial : centered_illegal)
    {
        expect(std::ranges::find(trial.analysis.rejections, "acc_width") !=
                   trial.analysis.rejections.end(),
               "centered overflow lacks acc_width failure");
    }

    const pqc_poly::request no_scratch = make_request(8, 17, 0, "centered", "may", "32");
    const std::vector<pqc_poly::candidate> no_scratch_trials =
        pqc_poly::find_candidates(no_scratch);
    expect(std::ranges::none_of(no_scratch_trials,
                                [](const auto &trial) { return trial.analysis.legal; }),
           "zero ram alias request found a legal plan");
    bool no_plan = false;
    try
    {
        static_cast<void>(pqc_poly::pick_static(no_scratch_trials));
    }
    catch (const pqc_poly::selection_error &)
    {
        no_plan = true;
    }
    expect(no_plan, "zero ram alias request did not report no legal plan");
}

void test_failure_ordering()
{
    const pqc_poly::request req =
        make_request((std::uint64_t{1} << 30) + 1, 3329, 0, "centered", "may", "32");
    const std::vector<pqc_poly::candidate> trials = pqc_poly::find_candidates(req);
    const auto full =
        std::ranges::find_if(trials, [](const auto &trial)
                             { return trial.analysis.plan.sched == pqc_poly::schedule::full; });
    const auto output =
        std::ranges::find_if(trials, [](const auto &trial)
                             { return trial.analysis.plan.sched == pqc_poly::schedule::output; });
    expect(full != trials.end(), "missing full plan for failure order");
    expect(output != trials.end(), "missing output plan for failure order");
    expect(
        full->analysis.rejections == std::vector<std::string>({"ram", "acc_width", "size_t"}),
        "full failure order changed");
    expect(output->analysis.rejections ==
               std::vector<std::string>({"acc_width", "alias", "size_t"}),
           "output failure order changed");
}

void test_direct_validation()
{
    pqc_poly::request req = pqc_poly::parse_request(
        R"({"op":"cyclic_mul","n":8,"q":256,"target":{"word_bits":8,"acc_bits":[32]}})");
    pqc_poly::validate_request(req);

    req.n = 1;
    expect(expect_spec_error([&req] { pqc_poly::validate_request(req); }) == "n must be at least 2",
           "direct n lower boundary changed");

    req.n = std::numeric_limits<std::uint64_t>::max() / 4 + 1;
    expect(expect_spec_error([&req] { pqc_poly::validate_request(req); }) ==
               "n is too large for exact analysis",
           "direct n upper boundary changed");

    req.n = 8;
    req.q = 257;
    expect(expect_spec_error([&req] { pqc_poly::validate_request(req); }) ==
               "input representatives do not fit target word_bits",
           "direct representative boundary changed");
}

void test_json()
{
    pqc_poly::request req = pqc_poly::parse_request(
        R"({"op":"cyclic_mul","n":8,"q":17,)"
        R"("target":{"name":"rv32-\u03bc-\ud83d\ude80","acc_bits":[32]},)"
        R"("limits":{"ram":60}})");
    const std::string request_json = pqc_poly::request_to_json(req);
    expect(request_json.find("rv32-\\u03bc-\\ud83d\\ude80") != std::string::npos,
           "json was not ascii escaped");

    const std::vector<pqc_poly::candidate> trials = pqc_poly::find_candidates(req);
    const std::string expected =
        "{\n"
        "  \"plan\": {\n"
        "    \"id\": \"sb_full_i32\",\n"
        "    \"algorithm\": \"schoolbook\",\n"
        "    \"schedule\": \"sb_full\",\n"
        "    \"acc_bits\": 32,\n"
        "    \"block\": 0\n"
        "  },\n"
        "  \"analysis\": {\n"
        "    \"scratch_bytes\": 60,\n"
        "    \"alias_safe\": true,\n"
        "    \"accumulator_bound\": 512,\n"
        "    \"required_bits\": 11,\n"
        "    \"multiplications\": 64,\n"
        "    \"additions\": 71,\n"
        "    \"reductions\": 8,\n"
        "    \"legal\": true,\n"
        "    \"rejections\": []\n"
        "  },\n"
        "  \"estimated_cost\": 391\n"
        "}\n";
    expect(pqc_poly::candidate_to_json(trials.front()) == expected, "candidate json changed");
}

}

int main()
{
    try
    {
        test_request_parser();
        test_ranking_and_frontier();
        test_legality_and_checker();
        test_boundaries();
        test_reference_boundary_matrix();
        test_failure_ordering();
        test_direct_validation();
        test_json();
    }
    catch (const std::exception &error)
    {
        std::cerr << "selector test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
