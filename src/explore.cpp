#include "pqc_poly/explore.hpp"

#include "pqc_poly/codegen.hpp"
#include "pqc_poly/host_tuner.hpp"

#include "json.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <ostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace pqc_poly
{

namespace
{

using detail::append_indent;
using detail::append_json_string;

struct arguments
{
    std::filesystem::path specification;
    std::filesystem::path out{"out"};
    std::string plan;
    bool has_plan{false};
    bool tune_host{false};
    latency_metric metric{latency_metric::nanoseconds};
    host_tuning_options tuning{};
};

constexpr std::string_view usage =
    "usage: pqc-poly-bench [-h] [-o out] [--plan plan] [--tune-host] "
    "[--metric nanoseconds|cycles] [--samples n] [--iterations n] spec\n";

void append_embedded_json(std::string &out, std::string_view value, std::size_t indent)
{
    for (std::size_t index = 0; index < value.size(); ++index)
    {
        out += value[index];
        if (value[index] == '\n' && index + 1 < value.size())
        {
            out.append(indent, ' ');
        }
    }
}

[[nodiscard]] std::string plan_json(const request &req, const candidate &selected,
                                    const benchmark_record *benchmark, latency_metric metric)
{
    const bool measured = benchmark != nullptr && selectable(*benchmark);
    std::string out;

    out.reserve(1536);
    out += "{\n  \"request\": ";
    std::string request_json = request_to_json(req);
    request_json.pop_back();
    append_embedded_json(out, request_json, 2);
    out += ",\n  \"candidate\": ";
    std::string candidate_json = candidate_to_json(selected);
    candidate_json.pop_back();
    append_embedded_json(out, candidate_json, 2);
    out += ",\n  \"verification\": {\n    \"plan_check\": \"pass\",\n";
    out += measured ? "    \"compile\": \"pass\",\n" : "    \"compile\": \"not_run\",\n";
    out += measured ? "    \"differential_tests\": \"pass\",\n"
                    : "    \"differential_tests\": \"not_run\",\n";
    out += measured ? "    \"sanitizers\": \"pass\",\n"
                    : "    \"sanitizers\": \"not_run\",\n";
    out += "    \"target_run\": \"not_run\"\n  },\n";
    out += "  \"scratch_accounting\": \"generated arrays only\",\n";
    out += measured ? "  \"selection\": \"measured host proxy; not a target run\",\n"
                    : "  \"selection\": \"static cost model; no target benchmark\",\n";
    out += "  \"host_measurement\": ";

    if (!measured)
    {
        out += "null\n}\n";
        return out;
    }

    out += "{\n    \"metric\": ";
    append_json_string(out, latency_metric_name(metric));
    out += ",\n    \"nanoseconds\": ";
    out += benchmark->nanoseconds ? std::to_string(*benchmark->nanoseconds) : "null";
    out += ",\n    \"cycles\": ";
    out += benchmark->cycles ? std::to_string(*benchmark->cycles) : "null";
    out += ",\n    \"scratch_bytes\": " + std::to_string(benchmark->scratch_bytes);
    out += ",\n    \"code_size_bytes\": " + std::to_string(benchmark->code_size_bytes);
    out += ",\n    \"target\": ";
    append_json_string(out, benchmark->provenance.target);
    out += ",\n    \"runner\": ";
    append_json_string(out, benchmark->provenance.runner);
    out += "\n  }\n}\n";
    return out;
}

void write_text(const std::filesystem::path &path, std::string_view text)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);

    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream)
    {
        throw explore_error("could not write " + path.string());
    }
}

[[nodiscard]] std::string join_errors(std::span<const std::string> errors)
{
    std::string out;

    for (std::size_t i = 0; i < errors.size(); ++i)
    {
        if (i != 0)
        {
            out += ", ";
        }
        out += errors[i];
    }
    return out;
}

[[nodiscard]] std::size_t parse_positive_count(std::string_view value, std::string_view option)
{
    std::size_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);

    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        result == 0)
    {
        throw explore_error(std::string(option) + " expects a positive integer");
    }
    return result;
}

[[nodiscard]] latency_metric parse_metric(std::string_view value)
{
    if (value == "nanoseconds")
    {
        return latency_metric::nanoseconds;
    }
    if (value == "cycles")
    {
        return latency_metric::cycles;
    }
    throw explore_error("--metric expects nanoseconds or cycles");
}

[[nodiscard]] arguments parse_arguments(std::span<const std::string_view> values)
{
    arguments result;
    bool has_specification = false;
    bool positional_only = false;

    for (std::size_t i = 0; i < values.size(); ++i)
    {
        const std::string_view value = values[i];

        if (!positional_only && value == "--")
        {
            positional_only = true;
            continue;
        }
        if (!positional_only && (value == "-h" || value == "--help"))
        {
            throw explore_error("help");
        }
        if (!positional_only && value == "--tune-host")
        {
            result.tune_host = true;
            continue;
        }
        if (!positional_only &&
            (value == "--metric" || value == "--samples" || value == "--iterations"))
        {
            if (++i == values.size() || values[i].empty())
            {
                throw explore_error("expected one argument after " + std::string(value));
            }
            if (value == "--metric")
            {
                result.metric = parse_metric(values[i]);
            }
            else if (value == "--samples")
            {
                result.tuning.samples = parse_positive_count(values[i], value);
            }
            else
            {
                result.tuning.iterations = parse_positive_count(values[i], value);
            }
            continue;
        }
        if (!positional_only && value.starts_with("--metric="))
        {
            result.metric = parse_metric(value.substr(9));
            continue;
        }
        if (!positional_only && value.starts_with("--samples="))
        {
            result.tuning.samples = parse_positive_count(value.substr(10), "--samples");
            continue;
        }
        if (!positional_only && value.starts_with("--iterations="))
        {
            result.tuning.iterations = parse_positive_count(value.substr(13), "--iterations");
            continue;
        }
        if (!positional_only && (value == "-o" || value == "--out" || value == "--plan"))
        {
            if (++i == values.size() || values[i].empty())
            {
                throw explore_error("expected one argument after " + std::string(value));
            }
            if (value == "--plan")
            {
                result.plan = values[i];
                result.has_plan = true;
            }
            else
            {
                result.out = std::string(values[i]);
            }
            continue;
        }
        if (!positional_only && value.starts_with("--out="))
        {
            const std::string_view out = value.substr(6);

            if (out.empty())
            {
                throw explore_error("--out expects a nonempty path");
            }
            result.out = std::string(out);
            continue;
        }
        if (!positional_only && value.starts_with("--plan="))
        {
            result.plan = value.substr(7);
            if (result.plan.empty())
            {
                throw explore_error("--plan expects a nonempty id");
            }
            result.has_plan = true;
            continue;
        }
        if (!positional_only && value.starts_with("-o") && value.size() > 2)
        {
            std::string_view out = value.substr(2);

            if (out.starts_with('='))
            {
                out.remove_prefix(1);
            }
            if (out.empty())
            {
                throw explore_error("-o expects a nonempty path");
            }
            result.out = std::string(out);
            continue;
        }
        if (!positional_only && value.starts_with('-'))
        {
            throw explore_error("unrecognized argument: " + std::string(value));
        }
        if (has_specification)
        {
            throw explore_error("expected exactly one request specification");
        }

        result.specification = std::string(value);
        has_specification = true;
    }

    if (!has_specification)
    {
        throw explore_error("the following argument is required: spec");
    }
    return result;
}

[[nodiscard]] const candidate &select_plan(std::span<const candidate> candidates,
                                           const arguments &args)
{
    if (!args.has_plan)
    {
        return pick_static(candidates);
    }

    const auto found =
        std::find_if(candidates.begin(), candidates.end(),
                     [&](const auto &item) { return plan_id(item.analysis.plan) == args.plan; });

    if (found == candidates.end())
    {
        throw explore_error("unknown plan: " + args.plan);
    }
    if (!found->analysis.legal)
    {
        throw explore_error("plan is illegal: " + join_errors(found->analysis.rejections));
    }
    return *found;
}

[[nodiscard]] const candidate &candidate_for_id(std::span<const candidate> candidates,
                                                std::string_view id)
{
    const auto found = std::find_if(candidates.begin(), candidates.end(),
                                    [id](const candidate &item)
                                    { return plan_id(item.analysis.plan) == id; });

    if (found == candidates.end())
    {
        throw explore_error("benchmark refers to an unknown plan: " + std::string(id));
    }
    return *found;
}

[[nodiscard]] const benchmark_record *benchmark_for_id(std::span<const benchmark_record> benchmarks,
                                                       std::string_view id) noexcept
{
    const auto found =
        std::find_if(benchmarks.begin(), benchmarks.end(),
                     [id](const benchmark_record &record) { return record.plan_id == id; });
    return found == benchmarks.end() ? nullptr : &*found;
}

[[nodiscard]] std::vector<benchmark_record> pending_benchmarks(
    const request &req, std::span<const candidate> candidates)
{
    std::vector<benchmark_record> records;

    records.reserve(candidates.size());
    for (const candidate &candidate : candidates)
    {
        benchmark_record record;
        record.plan_id = plan_id(candidate.analysis.plan);
        record.status =
            candidate.analysis.legal ? benchmark_status::pending : benchmark_status::rejected;
        record.verification.plan_check = check_candidate(req, candidate).empty();
        record.verification.ram_check = candidate.analysis.scratch_bytes <= req.limits.ram;
        record.scratch_bytes =
            candidate.analysis.scratch_bytes > std::numeric_limits<std::uint64_t>::max()
                ? std::numeric_limits<std::uint64_t>::max()
                : static_cast<std::uint64_t>(candidate.analysis.scratch_bytes);
        record.provenance.compiler = "not_run";
        record.provenance.target = req.target.name;
        record.provenance.runner = "not_run";
        record.rejection_reasons = candidate.analysis.rejections;
        records.push_back(std::move(record));
    }
    return records;
}

[[nodiscard]] std::string summary_json(const candidate &selected,
                                       std::span<const candidate> candidates,
                                       std::span<const benchmark_record> benchmarks,
                                       latency_metric metric, const std::filesystem::path &out_path)
{
    const benchmark_record *measurement =
        benchmark_for_id(benchmarks, plan_id(selected.analysis.plan));
    const bool measured = measurement != nullptr && selectable(*measurement) &&
                          measured_latency(*measurement, metric).has_value();
    std::string out;

    out += "{\n  \"selected\": ";
    append_json_string(out, plan_id(selected.analysis.plan));
    out += ",\n  \"acc_bits\": " + std::to_string(selected.analysis.plan.acc_bits);
    out += ",\n  \"scratch_bytes\": ";
    out += wide_to_string(selected.analysis.scratch_bytes);
    out += ",\n  \"required_bits\": " + std::to_string(selected.analysis.required_bits);
    out += ",\n  \"selection_mode\": ";
    append_json_string(out, measured ? "measured_host_proxy" : "static_model");
    out += ",\n  \"metric\": ";
    append_json_string(out, latency_metric_name(metric));
    out += ",\n  \"measured_latency\": ";
    out += measured ? std::to_string(*measured_latency(*measurement, metric)) : "null";
    out += ",\n  \"frontier\": [";

    if (measured)
    {
        const std::vector<const benchmark_record *> points = measured_frontier(benchmarks, metric);
        if (!points.empty())
        {
            out += '\n';
        }
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            append_indent(out, 4);
            append_json_string(out, points[i]->plan_id);
            out += i + 1 == points.size() ? "\n" : ",\n";
        }
        if (!points.empty())
        {
            append_indent(out, 2);
        }
    }
    else
    {
        const std::vector<const candidate *> points = static_frontier(candidates);
        if (!points.empty())
        {
            out += '\n';
        }
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            append_indent(out, 4);
            append_json_string(out, plan_id(points[i]->analysis.plan));
            out += i + 1 == points.size() ? "\n" : ",\n";
        }
        if (!points.empty())
        {
            append_indent(out, 2);
        }
    }
    out += "],\n  \"out\": ";
    append_json_string(out, out_path.string());
    out += "\n}\n";
    return out;
}

void emit(const std::filesystem::path &out, const request &req, const candidate &selected,
          std::span<const candidate> candidates, std::span<const benchmark_record> benchmarks,
          latency_metric metric)
{
    const auto errors = check_candidate(req, selected);

    if (!errors.empty())
    {
        throw explore_error("bad plan: " + join_errors(errors));
    }
    if (!selected.analysis.legal)
    {
        throw explore_error("cannot emit an illegal plan");
    }

    const benchmark_record *measurement =
        benchmark_for_id(benchmarks, plan_id(selected.analysis.plan));

    std::error_code error;

    std::filesystem::create_directories(out, error);
    if (error)
    {
        throw explore_error("could not create " + out.string() + ": " + error.message());
    }

    write_text(out / "kernel.hpp", generate_header(req, selected));
    write_text(out / "kernel.cpp", generate_source(req, selected));
    write_text(out / "plan.json", plan_json(req, selected, measurement, metric));
    write_text(out / "candidates.json", candidates_to_json(candidates));
    write_text(out / "benchmarks.json", benchmarks_to_json(benchmarks, metric));
}

}

int run(std::span<const std::string_view> arguments, std::ostream &standard_output,
        std::ostream &standard_error) noexcept
{
    try
    {
        const auto args = parse_arguments(arguments);
        const request req = load_request(args.specification);
        const auto candidates = find_candidates(req);
        const candidate *selected = args.has_plan ? &select_plan(candidates, args) : nullptr;
        std::vector<benchmark_record> benchmarks;

        if (args.tune_host)
        {
            benchmarks = tune_on_host(req, candidates, args.tuning);
            if (selected == nullptr)
            {
                const benchmark_record &winner = pick_measured(benchmarks, args.metric);
                selected = &candidate_for_id(candidates, winner.plan_id);
            }
            else
            {
                const benchmark_record *forced =
                    benchmark_for_id(benchmarks, plan_id(selected->analysis.plan));
                if (forced == nullptr || !selectable(*forced) ||
                    !measured_latency(*forced, args.metric).has_value())
                {
                    throw explore_error(
                        "forced plan did not produce a selectable host measurement");
                }
            }
        }
        else
        {
            if (selected == nullptr)
            {
                selected = &select_plan(candidates, args);
            }
            benchmarks = pending_benchmarks(req, candidates);
        }

        emit(args.out, req, *selected, candidates, benchmarks, args.metric);
        standard_output << summary_json(*selected, candidates, benchmarks, args.metric, args.out);
        return standard_output ? 0 : 2;
    }
    catch (const explore_error &error)
    {
        if (std::string_view(error.what()) == "help")
        {
            standard_output << usage;
            return standard_output ? 0 : 2;
        }

        standard_error << usage << "error: " << error.what() << '\n';
    }
    catch (const std::exception &error)
    {
        standard_error << "error: " << error.what() << '\n';
    }
    catch (...)
    {
        standard_error << "error: unknown failure\n";
    }

    return 2;
}

}
