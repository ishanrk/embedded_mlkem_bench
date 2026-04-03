#include "pqc_poly/host_tuner.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "host tuner test failed: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message)
{
    if (!condition)
    {
        fail(message);
    }
}

void test_host_measurement()
{
    const pqc_poly::request req = pqc_poly::parse_request(
        R"({"op":"negacyclic_mul","n":4,"q":17,"alias":"may",)"
        R"("target":{"name":"test","acc_bits":[32]},"limits":{"ram":128}})");
    const std::vector<pqc_poly::candidate> candidates = pqc_poly::find_candidates(req);
    const auto full =
        std::find_if(candidates.begin(), candidates.end(),
                     [](const pqc_poly::candidate &candidate)
                     { return candidate.analysis.plan.sched == pqc_poly::schedule::full; });
    const auto output =
        std::find_if(candidates.begin(), candidates.end(),
                     [](const pqc_poly::candidate &candidate)
                     { return candidate.analysis.plan.sched == pqc_poly::schedule::output; });

    require(full != candidates.end() && full->analysis.legal, "missing legal full plan");
    require(output != candidates.end() && !output->analysis.legal, "missing illegal output plan");

    const std::vector<pqc_poly::candidate> selected{*full, *output};
    const std::vector<pqc_poly::benchmark_record> records =
        pqc_poly::tune_on_host(req, selected, {3, 4});

    require(records.size() == 2, "host record count changed");
    require(records[0].status == pqc_poly::benchmark_status::measured,
            "legal plan was not measured");
    require(pqc_poly::selectable(records[0]), "measured plan is not selectable");
    require(records[0].nanoseconds.has_value() && *records[0].nanoseconds > 0,
            "nanosecond measurement is absent");
    require(records[0].code_size_bytes > 0, "code size is absent");
    require(records[0].provenance.target == "host-proxy-for-test", "host proxy provenance changed");
    require(records[1].status == pqc_poly::benchmark_status::rejected,
            "illegal plan was not rejected");
    require(records[1].verification.plan_check, "consistent illegal plan failed its check");
    require(records[1].verification.ram_check, "zero-scratch plan failed its ram check");
    require(records[1].rejection_reasons == std::vector<std::string>{"alias"},
            "illegal plan has the wrong rejection reason");
    require(pqc_poly::pick_measured(records, pqc_poly::latency_metric::nanoseconds).plan_id ==
                records[0].plan_id,
            "measured winner changed");
}

void test_invalid_options()
{
    const pqc_poly::request req = pqc_poly::parse_request(
        R"({"op":"cyclic_mul","n":4,"q":17,"target":{"acc_bits":[32]},"limits":{"ram":128}})");
    const std::vector<pqc_poly::candidate> candidates = pqc_poly::find_candidates(req);
    bool rejected = false;

    try
    {
        static_cast<void>(pqc_poly::tune_on_host(req, candidates, {0, 4}));
    }
    catch (const std::invalid_argument &)
    {
        rejected = true;
    }
    require(rejected, "zero host samples were accepted");
}

}

int main()
{
    test_host_measurement();
    test_invalid_options();
    return 0;
}
