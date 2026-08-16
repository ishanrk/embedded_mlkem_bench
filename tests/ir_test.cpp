#include "pqc_poly/ir.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "ir test failed: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message)
{
    if (!condition)
    {
        fail(message);
    }
}

[[nodiscard]] pqc_poly::request make_request(std::string_view op = "negacyclic_mul",
                                             std::string_view input = "centered")
{
    return pqc_poly::parse_request("{\"op\":\"" + std::string(op) +
                                   "\",\"n\":8,\"q\":17,\"input\":\"" + std::string(input) +
                                   "\",\"target\":{\"acc_bits\":[32]},"
                                   "\"limits\":{\"ram\":4096}}");
}

[[nodiscard]] const pqc_poly::candidate_trial &find_trial(
    const std::vector<pqc_poly::candidate_trial> &trials, pqc_poly::schedule sched,
    std::uint64_t block = 0)
{
    const auto trial = std::find_if(
        trials.begin(), trials.end(),
        [sched, block](const pqc_poly::candidate_trial &candidate)
        {
            return candidate.analysis.legal && candidate.analysis.plan.sched == sched &&
                   (sched != pqc_poly::schedule::fold || candidate.analysis.plan.block == block);
        });
    if (trial == trials.end())
    {
        fail("missing legal trial");
    }
    return *trial;
}

void require_error(const std::vector<std::string> &errors, std::string_view wanted)
{
    require(std::find(errors.begin(), errors.end(), wanted) != errors.end(), wanted);
}

void test_schedule_shapes()
{
    const pqc_poly::request req = make_request();
    const std::vector<pqc_poly::candidate_trial> trials = pqc_poly::find(req);

    const pqc_poly::candidate_trial &full = find_trial(trials, pqc_poly::schedule::full);
    const pqc_poly::polynomial_ir full_ir = pqc_poly::lower_ir(req, full);
    require(pqc_poly::verify_ir(req, full, full_ir).empty(), "full ir was rejected");
    require(full_ir.stage_count == 7, "full graph shape changed");
    require(full_ir.scratch_alignment == 64 && full_ir.peak_scratch_bytes == 60,
            "full scratch changed");
    require(full_ir.stages[0].range == pqc_poly::coefficient_interval{-8, 8},
            "centered input range changed");
    require(full_ir.stages[3].range == pqc_poly::coefficient_interval{-512, 512},
            "convolution range changed");
    require(full_ir.stages[4].required_bits == 11, "ring result width changed");
    require(full_ir.stages[4].operation == pqc_poly::ir_operation_kind::fold,
            "full fold operation missing");
    require(full_ir.stages[4].wrap == pqc_poly::ring_wrap::subtract, "negacyclic wrap changed");
    require(full_ir.stages[3].last_use == 4 && full_ir.scratch_last_use == 5,
            "full lifetime changed");

    const pqc_poly::candidate_trial &fold = find_trial(trials, pqc_poly::schedule::fold, 4);
    const pqc_poly::polynomial_ir fold_ir = pqc_poly::lower_ir(req, fold);
    require(pqc_poly::verify_ir(req, fold, fold_ir).empty(), "fold ir was rejected");
    require(fold_ir.stage_count == 6, "fold graph shape changed");
    require(fold_ir.scratch_alignment == 64 && fold_ir.peak_scratch_bytes == 32,
            "fold scratch changed");
    require(fold_ir.stages[3].wrap == pqc_poly::ring_wrap::subtract && fold_ir.stages[3].in_place,
            "fold accumulation changed");

    const pqc_poly::candidate_trial &output = find_trial(trials, pqc_poly::schedule::output);
    const pqc_poly::polynomial_ir output_ir = pqc_poly::lower_ir(req, output);
    require(pqc_poly::verify_ir(req, output, output_ir).empty(), "output ir was rejected");
    require(output_ir.stage_count == 5, "output graph shape changed");
    require(output_ir.scratch_alignment == 0 && output_ir.peak_scratch_bytes == 0,
            "output schedule allocated scratch");
    require(output_ir.stages[2].dependencies == 0x03U, "output dependencies changed");
}

void test_ring_and_reduction_ranges()
{
    const pqc_poly::request req = make_request("cyclic_mul", "canonical");
    const std::vector<pqc_poly::candidate_trial> trials = pqc_poly::find(req);
    const pqc_poly::candidate_trial &trial = find_trial(trials, pqc_poly::schedule::full);
    const pqc_poly::polynomial_ir graph = pqc_poly::lower_ir(req, trial);

    require(graph.stages[0].range == pqc_poly::coefficient_interval{0, 16},
            "canonical input range changed");
    require(graph.stages[0].reduction == pqc_poly::reduction_state::canonical,
            "canonical input state changed");
    require(graph.stages[3].range == pqc_poly::coefficient_interval{0, 2048},
            "canonical convolution range changed");
    require(graph.stages[4].range == pqc_poly::coefficient_interval{0, 2048},
            "cyclic ring range changed");
    require(graph.stages[4].required_bits == 13, "canonical signed width changed");
    require(graph.stages[4].wrap == pqc_poly::ring_wrap::add, "cyclic wrap changed");
    require(graph.stages[5].range == pqc_poly::coefficient_interval{0, 16} &&
                graph.stages[5].reduction == pqc_poly::reduction_state::canonical,
            "reduction result changed");

    const pqc_poly::request even = pqc_poly::parse_request(
        R"({"op":"negacyclic_mul","n":8,"q":256,"target":{"word_bits":16,"acc_bits":[32]},"limits":{"ram":4096}})");
    const std::vector<pqc_poly::candidate_trial> even_trials = pqc_poly::find(even);
    const pqc_poly::candidate_trial &even_trial = find_trial(even_trials, pqc_poly::schedule::full);
    const pqc_poly::polynomial_ir even_graph = pqc_poly::lower_ir(even, even_trial);
    require(even_graph.stages[0].range == pqc_poly::coefficient_interval{-128, 127},
            "even centered range changed");
    require(even_graph.stages[4].range == pqc_poly::coefficient_interval{-130944, 131072},
            "even negacyclic range changed");
    require(even_graph.stages[4].required_bits == 19, "even negacyclic width changed");
}

void test_independent_verifier_mutations()
{
    const pqc_poly::request req = make_request();
    const std::vector<pqc_poly::candidate_trial> trials = pqc_poly::find(req);
    const pqc_poly::candidate_trial &trial = find_trial(trials, pqc_poly::schedule::full);
    const pqc_poly::polynomial_ir clean = pqc_poly::lower_ir(req, trial);

    pqc_poly::polynomial_ir bad = clean;
    ++bad.q;
    require_error(pqc_poly::verify_ir(req, trial, bad), "bad ir metadata");

    bad = clean;
    ++bad.stages[4].range.upper;
    require_error(pqc_poly::verify_ir(req, trial, bad), "bad value range");

    bad = clean;
    bad.stages[5].reduction = pqc_poly::reduction_state::unreduced;
    require_error(pqc_poly::verify_ir(req, trial, bad), "bad stage properties");

    bad = clean;
    --bad.stages[3].last_use;
    require_error(pqc_poly::verify_ir(req, trial, bad), "bad value lifetime");

    bad = clean;
    bad.stages[3].dependencies &= 0x03U;
    require_error(pqc_poly::verify_ir(req, trial, bad), "bad dependency graph");

    bad = clean;
    bad.stages[4].wrap = pqc_poly::ring_wrap::add;
    require_error(pqc_poly::verify_ir(req, trial, bad), "bad stage properties");

    bad = clean;
    bad.scratch_alignment = 1;
    require_error(pqc_poly::verify_ir(req, trial, bad), "bad scratch lifetime");

    bad = clean;
    bad.stages[3].dependencies = 0x80U;
    require_error(pqc_poly::verify_ir(req, trial, bad), "bad dependency graph");

    pqc_poly::candidate_trial invalid = trial;
    ++invalid.score.cost;
    bool rejected = false;
    try
    {
        static_cast<void>(pqc_poly::lower_ir(req, invalid));
    }
    catch (const pqc_poly::ir_error &)
    {
        rejected = true;
    }
    require(rejected, "lowering accepted a damaged trial");
}

void test_json()
{
    const pqc_poly::request req = make_request();
    const std::vector<pqc_poly::candidate_trial> trials = pqc_poly::find(req);
    const pqc_poly::candidate_trial &trial = find_trial(trials, pqc_poly::schedule::fold, 4);
    const pqc_poly::polynomial_ir graph = pqc_poly::lower_ir(req, trial);
    const std::string first = pqc_poly::ir_to_json(graph);
    const std::string second = pqc_poly::ir_to_json(graph);

    require(first == second, "ir json was not deterministic");
    require(first.starts_with("{\n  \"version\": 2,\n"), "ir json prefix changed");
    require(first.find("\"range\": [-512, 512]") != std::string::npos, "ir json lacks range");
    require(first.find("\"dependencies\": [0, 1, 2]") != std::string::npos,
            "ir json lacks dependencies");
    require(first.find("\"first_use\": 2, \"last_use\": 4") != std::string::npos,
            "ir json lacks scratch lifetime");
    require(first.back() == '\n', "ir json lacks final newline");
}

}

int main()
{
    test_schedule_shapes();
    test_ring_and_reduction_ranges();
    test_independent_verifier_mutations();
    test_json();
    return 0;
}
