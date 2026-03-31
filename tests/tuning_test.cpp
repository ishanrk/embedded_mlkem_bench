#include "pqc_poly/tuning.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "tuning test failed: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message)
{
    if (!condition)
    {
        fail(message);
    }
}

template <class function_type>
std::string expect_tuning_error(function_type &&function)
{
    try
    {
        function();
    }
    catch (const pqc_poly::tuning_error &error)
    {
        return error.what();
    }
    fail("expected tuning error");
}

[[nodiscard]] pqc_poly::benchmark_provenance provenance()
{
    return {
        .compiler = "clang",
        .compiler_version = "20.1.0",
        .compiler_flags = "-O3 -target riscv32",
        .target = "rv32im",
        .runner = "qemu",
    };
}

[[nodiscard]] pqc_poly::benchmark_record measured(std::string id,
                                                  std::optional<std::uint64_t> cycles,
                                                  std::optional<std::uint64_t> nanoseconds,
                                                  std::uint64_t scratch, std::uint64_t code)
{
    return {
        .plan_id = std::move(id),
        .status = pqc_poly::benchmark_status::measured,
        .verification =
            {
                .differential_tests = true,
                .independent_plan = true,
                .memory_safety = true,
                .ram_bound = true,
            },
        .nanoseconds = nanoseconds,
        .cycles = cycles,
        .peak_scratch_bytes = scratch,
        .code_size_bytes = code,
        .provenance = provenance(),
    };
}

[[nodiscard]] pqc_poly::benchmark_record pending(std::string id)
{
    return {
        .plan_id = std::move(id),
        .status = pqc_poly::benchmark_status::pending,
        .provenance = provenance(),
    };
}

[[nodiscard]] pqc_poly::benchmark_record rejected(std::string id)
{
    return {
        .plan_id = std::move(id),
        .status = pqc_poly::benchmark_status::rejected,
        .provenance = provenance(),
        .rejection_reasons = {"compile failed"},
    };
}

void test_validation()
{
    pqc_poly::benchmark_record record = measured("good", 100, 50, 64, 320);

    pqc_poly::validate_benchmark_record(record);
    pqc_poly::validate_benchmark_record(pending("later"));
    pqc_poly::validate_benchmark_record(rejected("bad"));
    require(pqc_poly::fully_verified(record), "verified measurement was rejected");

    record.plan_id.clear();
    require(expect_tuning_error([&record] { pqc_poly::validate_benchmark_record(record); }) ==
                "benchmark plan id must not be empty",
            "empty plan id error changed");

    record = measured("good", 100, 50, 64, 320);
    record.provenance.compiler.clear();
    require(expect_tuning_error([&record] { pqc_poly::validate_benchmark_record(record); }) ==
                "benchmark compiler must not be empty for plan good",
            "empty compiler was accepted");

    record = measured("good", 100, 50, 64, 320);
    record.provenance.target.clear();
    require(expect_tuning_error([&record] { pqc_poly::validate_benchmark_record(record); }) ==
                "benchmark target must not be empty for plan good",
            "empty target was accepted");

    record = measured("good", 0, 50, 64, 320);
    require(expect_tuning_error([&record] { pqc_poly::validate_benchmark_record(record); }) ==
                "benchmark latency must be positive for plan good",
            "zero latency was accepted");

    record = measured("good", std::nullopt, std::nullopt, 64, 320);
    require(expect_tuning_error([&record] { pqc_poly::validate_benchmark_record(record); }) ==
                "measured benchmark has invalid terminal data for plan good",
            "measurement without latency was accepted");

    record = measured("good", 100, 50, 64, 0);
    require(expect_tuning_error([&record] { pqc_poly::validate_benchmark_record(record); }) ==
                "measured benchmark has invalid terminal data for plan good",
            "measurement without code size was accepted");

    record = measured("good", 100, 50, 64, 320);
    record.rejection_reasons = {"failure"};
    require(expect_tuning_error([&record] { pqc_poly::validate_benchmark_record(record); }) ==
                "measured benchmark has invalid terminal data for plan good",
            "measured rejection was accepted");

    record = rejected("bad");
    record.rejection_reasons.clear();
    require(expect_tuning_error([&record] { pqc_poly::validate_benchmark_record(record); }) ==
                "rejected benchmark has invalid terminal data for plan bad",
            "reasonless rejection was accepted");

    record = rejected("bad");
    record.cycles = 40;
    require(expect_tuning_error([&record] { pqc_poly::validate_benchmark_record(record); }) ==
                "rejected benchmark has invalid terminal data for plan bad",
            "measured rejection state was accepted");

    record = pending("later");
    record.rejection_reasons = {"early"};
    require(expect_tuning_error([&record] { pqc_poly::validate_benchmark_record(record); }) ==
                "pending benchmark has terminal data for plan later",
            "terminal pending state was accepted");

    record = rejected("bad");
    record.rejection_reasons = {""};
    require(expect_tuning_error([&record] { pqc_poly::validate_benchmark_record(record); }) ==
                "benchmark rejection reason must not be empty for plan bad",
            "empty rejection reason was accepted");

    record = measured("good", 100, 50, 64, 320);
    record.status = static_cast<pqc_poly::benchmark_status>(255);
    require(expect_tuning_error([&record] { pqc_poly::validate_benchmark_record(record); }) ==
                "invalid benchmark status for plan good",
            "invalid status was accepted");
}

void test_winner_and_tie_breaks()
{
    pqc_poly::benchmark_record unverified = measured("unverified", 1, 1, 1, 1);
    unverified.verification.memory_safety = false;

    std::vector<pqc_poly::benchmark_record> records{
        rejected("rejected"),
        pending("pending"),
        measured("nanoseconds-only", std::nullopt, 10, 1, 1),
        measured("slow", 101, 1, 1, 1),
        measured("more-scratch", 100, 500, 60, 1),
        measured("more-code", 100, 500, 50, 60),
        measured("z-plan", 100, 500, 50, 50),
        measured("a-plan", 100, 500, 50, 50),
        unverified,
    };

    require(!pqc_poly::fully_verified(unverified), "failed verification was treated as complete");
    require(pqc_poly::pick_measured(records, pqc_poly::latency_metric::cycles).plan_id == "a-plan",
            "cycle winner tie breaks changed");
    require(
        pqc_poly::pick_measured(records, pqc_poly::latency_metric::nanoseconds).plan_id == "slow",
        "nanosecond winner changed");

    std::reverse(records.begin(), records.end());
    require(pqc_poly::pick_measured(records, pqc_poly::latency_metric::cycles).plan_id == "a-plan",
            "cycle winner depends on record order");

    const std::array unavailable{
        rejected("rejected"),
        pending("pending"),
        unverified,
    };
    require(expect_tuning_error(
                [&unavailable] {
                    static_cast<void>(
                        pqc_poly::pick_measured(unavailable, pqc_poly::latency_metric::cycles));
                }) == "no fully verified cycles benchmark",
            "missing measurement error changed");
}

void test_pareto_frontier()
{
    pqc_poly::benchmark_record unverified = measured("unsafe", 1, 1, 1, 1);
    unverified.verification.memory_safety = false;

    std::vector<pqc_poly::benchmark_record> records{
        measured("dominated", 130, 130, 110, 110),
        measured("small", 120, 120, 50, 50),
        measured("fast", 80, 80, 200, 200),
        measured("code", 110, 110, 150, 20),
        measured("balanced", 100, 100, 100, 100),
        measured("nanoseconds-only", std::nullopt, 5, 1, 1),
        unverified,
    };

    const std::vector<const pqc_poly::benchmark_record *> frontier =
        pqc_poly::measured_frontier(records, pqc_poly::latency_metric::cycles);
    const std::array expected{
        std::string_view{"fast"},
        std::string_view{"balanced"},
        std::string_view{"code"},
        std::string_view{"small"},
    };

    require(frontier.size() == expected.size(), "pareto frontier size changed");
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        require(frontier[index]->plan_id == expected[index], "pareto frontier order changed");
    }

    std::reverse(records.begin(), records.end());
    const std::vector<const pqc_poly::benchmark_record *> reversed =
        pqc_poly::measured_frontier(records, pqc_poly::latency_metric::cycles);
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        require(reversed[index]->plan_id == expected[index],
                "pareto frontier depends on record order");
    }
}

void test_serializers()
{
    pqc_poly::benchmark_record evil = measured("plan<&\"'\n", 90, 45, 32, 128);
    evil.provenance.compiler = "clang<&\"";
    evil.provenance.compiler_version = "version '\n";
    evil.provenance.compiler_flags = "-DVALUE=\"<&\"";
    evil.provenance.target = "rv32-\u03bc<&";
    evil.provenance.runner = "board > lab";

    pqc_poly::benchmark_record bad = rejected("rejected");
    bad.rejection_reasons = {"zeta & issue", "</script>\"'"};

    std::vector<pqc_poly::benchmark_record> records{
        bad,
        evil,
        measured("plain", 100, 50, 16, 96),
    };
    const std::string json =
        pqc_poly::benchmarks_to_json(records, pqc_poly::latency_metric::cycles);
    const std::string html = pqc_poly::report_to_html(records, pqc_poly::latency_metric::cycles);

    std::reverse(records.begin(), records.end());
    std::reverse(records.back().rejection_reasons.begin(), records.back().rejection_reasons.end());
    require(json == pqc_poly::benchmarks_to_json(records, pqc_poly::latency_metric::cycles),
            "json depends on input ordering");
    require(html == pqc_poly::report_to_html(records, pqc_poly::latency_metric::cycles),
            "html depends on input ordering");

    require(json.starts_with("{\n  \"schema\": \"pqc-poly-bench/benchmarks-v1\""),
            "json schema is missing");
    require(json.ends_with("\n"), "json lacks final newline");
    require(json.find("\"selected\": \"plan<&\\\"'\\n\"") != std::string::npos,
            "json selection or escaping changed");
    require(json.find("rv32-\\u03bc<&") != std::string::npos, "json unicode escaping changed");
    require(json.find("</script>") != std::string::npos,
            "json content was unexpectedly html escaped");
    require(std::all_of(json.begin(), json.end(),
                        [](char value) { return static_cast<unsigned char>(value) < 0x80U; }),
            "json is not ascii reproducible");

    require(html.starts_with("<!doctype html>\n<html lang=\"en\">"), "html is not standalone");
    require(html.ends_with("</html>\n"), "html lacks closing document");
    require(html.find("plan&lt;&amp;&quot;&#39;\n") != std::string::npos,
            "html plan escaping changed");
    require(html.find("rv32-\u03bc&lt;&amp;") != std::string::npos, "html target escaping changed");
    require(html.find("-DVALUE=&quot;&lt;&amp;&quot;") != std::string::npos,
            "html compiler flag escaping changed");
    require(html.find("&lt;/script&gt;&quot;&#39;") != std::string::npos,
            "html rejection escaping changed");
    require(html.find("</script>") == std::string::npos, "html contains injected markup");
}

void test_names_and_empty_report()
{
    require(pqc_poly::benchmark_status_name(pqc_poly::benchmark_status::pending) == "pending",
            "pending name changed");
    require(pqc_poly::benchmark_status_name(pqc_poly::benchmark_status::rejected) == "rejected",
            "rejected name changed");
    require(pqc_poly::benchmark_status_name(pqc_poly::benchmark_status::measured) == "measured",
            "measured name changed");
    require(pqc_poly::latency_metric_name(pqc_poly::latency_metric::cycles) == "cycles",
            "cycles name changed");
    require(pqc_poly::latency_metric_name(pqc_poly::latency_metric::nanoseconds) == "nanoseconds",
            "nanoseconds name changed");

    const std::string json = pqc_poly::benchmarks_to_json(
        std::span<const pqc_poly::benchmark_record>{}, pqc_poly::latency_metric::cycles);
    const std::string html = pqc_poly::report_to_html(std::span<const pqc_poly::benchmark_record>{},
                                                      pqc_poly::latency_metric::cycles);

    require(json.find("\"selected\": null") != std::string::npos, "empty json has a selection");
    require(json.find("\"frontier\": []") != std::string::npos, "empty json has a frontier");
    require(html.find("no fully verified measurement is available") != std::string::npos,
            "empty html lacks explanation");
}

}

int main()
{
    test_validation();
    test_winner_and_tie_breaks();
    test_pareto_frontier();
    test_serializers();
    test_names_and_empty_report();
    return 0;
}
