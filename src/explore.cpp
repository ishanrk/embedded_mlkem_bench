#include "pqc_poly/explore.hpp"

#include "pqc_poly/codegen.hpp"
#include "pqc_poly/compiler_plan.hpp"
#include "pqc_poly/host_tuner.hpp"
#include "pqc_poly/ir.hpp"

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

void append_indent(std::string &out, std::size_t level)
{
    out.append(level * 2, ' ');
}

void append_hex_escape(std::string &out, std::uint16_t value)
{
    constexpr std::string_view digits = "0123456789abcdef";

    out += "\\u";
    for (unsigned shift : {12U, 8U, 4U, 0U})
    {
        out += digits[(value >> shift) & 0xfU];
    }
}

void append_code_point(std::string &out, std::uint32_t value)
{
    if (value <= 0xffffU)
    {
        append_hex_escape(out, static_cast<std::uint16_t>(value));
        return;
    }

    value -= 0x10000U;
    append_hex_escape(out, static_cast<std::uint16_t>(0xd800U | (value >> 10U)));
    append_hex_escape(out, static_cast<std::uint16_t>(0xdc00U | (value & 0x3ffU)));
}

void append_json_string(std::string &out, std::string_view value)
{
    // ascii-only output mirrors python json and keeps every artifact byte reproducible
    out += '"';

    for (std::size_t i = 0; i < value.size();)
    {
        const std::uint8_t first = static_cast<std::uint8_t>(value[i]);

        if (first < 0x80U)
        {
            ++i;
            switch (first)
            {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\b':
                    out += "\\b";
                    break;
                case '\f':
                    out += "\\f";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (first < 0x20U)
                    {
                        append_hex_escape(out, first);
                    }
                    else
                    {
                        out += static_cast<char>(first);
                    }
                    break;
            }
            continue;
        }

        std::uint32_t code_point = 0;
        std::size_t width = 0;

        if ((first & 0xe0U) == 0xc0U)
        {
            code_point = first & 0x1fU;
            width = 2;
        }
        else if ((first & 0xf0U) == 0xe0U)
        {
            code_point = first & 0x0fU;
            width = 3;
        }
        else if ((first & 0xf8U) == 0xf0U)
        {
            code_point = first & 0x07U;
            width = 4;
        }
        else
        {
            append_hex_escape(out, first);
            ++i;
            continue;
        }

        if (i + width > value.size())
        {
            append_hex_escape(out, first);
            ++i;
            continue;
        }

        bool valid = true;
        for (std::size_t j = 1; j < width; ++j)
        {
            const std::uint8_t next = static_cast<std::uint8_t>(value[i + j]);

            if ((next & 0xc0U) != 0x80U)
            {
                valid = false;
                break;
            }
            code_point = (code_point << 6U) | (next & 0x3fU);
        }

        if (!valid)
        {
            append_hex_escape(out, first);
            ++i;
            continue;
        }

        append_code_point(out, code_point);
        i += width;
    }

    out += '"';
}

void append_wide(std::string &out, wide_uint value)
{
    out += wide_to_string(value);
}

void append_request(std::string &out, const request &req, std::size_t level)
{
    out += "{\n";
    append_indent(out, level + 1);
    out += "\"op\": ";
    append_json_string(out, operation_name(req.op));
    out += ",\n";
    append_indent(out, level + 1);
    out += "\"n\": " + std::to_string(req.n) + ",\n";
    append_indent(out, level + 1);
    out += "\"q\": " + std::to_string(req.q) + ",\n";
    append_indent(out, level + 1);
    out += "\"input\": ";
    append_json_string(out, input_name(req.input));
    out += ",\n";
    append_indent(out, level + 1);
    out += "\"output\": ";
    append_json_string(out, output_name(req.output));
    out += ",\n";
    append_indent(out, level + 1);
    out += "\"alias\": ";
    append_json_string(out, aliasing_name(req.alias));
    out += ",\n";
    append_indent(out, level + 1);
    out += "\"target\": {\n";
    append_indent(out, level + 2);
    out += "\"name\": ";
    append_json_string(out, req.target.name);
    out += ",\n";
    append_indent(out, level + 2);
    out += "\"word_bits\": " + std::to_string(req.target.word_bits) + ",\n";
    append_indent(out, level + 2);
    out += "\"size_bits\": " + std::to_string(req.target.size_bits) + ",\n";
    append_indent(out, level + 2);
    out += "\"acc_bits\": [\n";
    for (std::size_t i = 0; i < req.target.acc_bits.size(); ++i)
    {
        append_indent(out, level + 3);
        out += std::to_string(req.target.acc_bits[i]);
        out += i + 1 == req.target.acc_bits.size() ? "\n" : ",\n";
    }
    append_indent(out, level + 2);
    out += "]\n";
    append_indent(out, level + 1);
    out += "},\n";
    append_indent(out, level + 1);
    out += "\"limits\": {\n";
    append_indent(out, level + 2);
    out += "\"ram\": " + std::to_string(req.limits.ram) + "\n";
    append_indent(out, level + 1);
    out += "}\n";
    append_indent(out, level);
    out += '}';
}

void append_plan(std::string &out, const schoolbook_plan &plan, std::size_t level)
{
    out += "{\n";
    append_indent(out, level + 1);
    out += "\"id\": ";
    append_json_string(out, plan_id(plan));
    out += ",\n";
    append_indent(out, level + 1);
    out += "\"algo\": \"schoolbook\",\n";
    append_indent(out, level + 1);
    out += "\"sched\": ";
    append_json_string(out, schedule_name(plan.sched));
    out += ",\n";
    append_indent(out, level + 1);
    out += "\"acc_bits\": " + std::to_string(plan.acc_bits) + ",\n";
    append_indent(out, level + 1);
    out += "\"block\": " + std::to_string(plan.block) + "\n";
    append_indent(out, level);
    out += '}';
}

void append_string_array(std::string &out, std::span<const std::string> values, std::size_t level)
{
    if (values.empty())
    {
        out += "[]";
        return;
    }

    out += "[\n";
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        append_indent(out, level + 1);
        append_json_string(out, values[i]);
        out += i + 1 == values.size() ? "\n" : ",\n";
    }
    append_indent(out, level);
    out += ']';
}

void append_analysis(std::string &out, const analysis_verdict &v, std::size_t level)
{
    out += "{\n";
    append_indent(out, level + 1);
    out += "\"tmp_bytes\": ";
    append_wide(out, v.temporary_bytes);
    out += ",\n";
    append_indent(out, level + 1);
    out += std::string("\"alias_safe\": ") + (v.alias_safe ? "true" : "false") + ",\n";
    append_indent(out, level + 1);
    out += "\"acc_bound\": ";
    append_wide(out, v.accumulator_bound);
    out += ",\n";
    append_indent(out, level + 1);
    out += "\"need_bits\": " + std::to_string(v.required_bits) + ",\n";
    append_indent(out, level + 1);
    out += "\"muls\": ";
    append_wide(out, v.multiplications);
    out += ",\n";
    append_indent(out, level + 1);
    out += "\"adds\": ";
    append_wide(out, v.additions);
    out += ",\n";
    append_indent(out, level + 1);
    out += "\"reds\": ";
    append_wide(out, v.reductions);
    out += ",\n";
    append_indent(out, level + 1);
    out += std::string("\"legal\": ") + (v.legal ? "true" : "false") + ",\n";
    append_indent(out, level + 1);
    out += "\"fail\": ";
    append_string_array(out, v.failure_reasons, level + 1);
    out += '\n';
    append_indent(out, level);
    out += '}';
}

void append_score(std::string &out, const static_score &score, std::size_t level)
{
    out += "{\n";
    append_indent(out, level + 1);
    out += "\"cost\": ";
    append_wide(out, score.cost);
    out += ",\n";
    append_indent(out, level + 1);
    out += "\"model\": ";
    append_json_string(out, score.model);
    out += '\n';
    append_indent(out, level);
    out += '}';
}

void append_candidate(std::string &out, const candidate_trial &trial, std::size_t level)
{
    out += "{\n";
    append_indent(out, level + 1);
    out += "\"plan\": ";
    append_plan(out, trial.analysis.plan, level + 1);
    out += ",\n";
    append_indent(out, level + 1);
    out += "\"analysis\": ";
    append_analysis(out, trial.analysis, level + 1);
    out += ",\n";
    append_indent(out, level + 1);
    out += "\"score\": ";
    append_score(out, trial.score, level + 1);
    out += '\n';
    append_indent(out, level);
    out += '}';
}

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

[[nodiscard]] std::string plan_json(const request &req, const candidate_trial &trial,
                                    const compiler_plan &compiled,
                                    const benchmark_record *benchmark, latency_metric metric)
{
    const bool measured = benchmark != nullptr && fully_verified(*benchmark);
    std::string out;

    out.reserve(2048);
    out += "{\n  \"request\": ";
    append_request(out, req, 1);
    out += ",\n  \"plan\": ";
    append_plan(out, trial.analysis.plan, 1);
    out += ",\n  \"analysis\": ";
    append_analysis(out, trial.analysis, 1);
    out += ",\n  \"score\": ";
    append_score(out, trial.score, 1);
    out += ",\n  \"verification\": {\n    \"analysis_consistency\": \"pass\",\n";
    out += measured ? "    \"compile\": \"pass\",\n" : "    \"compile\": \"not_run\",\n";
    out += measured ? "    \"differential_test\": \"pass\",\n"
                    : "    \"differential_test\": \"not_run\",\n";
    out += measured ? "    \"dynamic_sanitizers\": \"pass\",\n"
                    : "    \"dynamic_sanitizers\": \"not_run\",\n";
    out += "    \"target_run\": \"not_run\"\n  },\n";
    out += "  \"scratch_accounting\": \"explicit arrays only\",\n";
    out += measured ? "  \"selection\": \"measured host proxy; not a target run\",\n"
                    : "  \"selection\": \"static bootstrap; no target benchmark\",\n";
    out += "  \"compiler_plan_id\": ";
    append_json_string(out, compiler_plan_id(compiled));
    out += ",\n  \"compiler_plan\": ";
    std::string compiled_json = compiler_plan_to_json(compiled);
    if (!compiled_json.empty() && compiled_json.back() == '\n')
    {
        compiled_json.pop_back();
    }
    append_embedded_json(out, compiled_json, 2);
    out +=
        ",\n  \"plan_space\": \"plans.json\",\n  \"ir\": \"ir.json\",\n"
        "  \"host_measurement\": ";

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
    out += ",\n    \"peak_scratch_bytes\": " + std::to_string(benchmark->peak_scratch_bytes);
    out += ",\n    \"code_size_bytes\": " + std::to_string(benchmark->code_size_bytes);
    out += ",\n    \"target\": ";
    append_json_string(out, benchmark->provenance.target);
    out += ",\n    \"runner\": ";
    append_json_string(out, benchmark->provenance.runner);
    out += "\n  }\n}\n";
    return out;
}

[[nodiscard]] std::string candidates_json(std::span<const candidate_trial> candidates)
{
    std::string out;

    out.reserve(candidates.size() * 512);
    out += "[";
    if (!candidates.empty())
    {
        out += '\n';
    }
    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        append_indent(out, 1);
        append_candidate(out, candidates[i], 1);
        out += i + 1 == candidates.size() ? "\n" : ",\n";
    }
    out += "]\n";
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

[[nodiscard]] const candidate_trial &select_plan(std::span<const candidate_trial> candidates,
                                                 const arguments &args)
{
    if (!args.has_plan)
    {
        return pick(candidates);
    }

    const auto found =
        std::find_if(candidates.begin(), candidates.end(),
                     [&](const auto &trial) { return plan_id(trial.analysis.plan) == args.plan; });

    if (found == candidates.end())
    {
        throw explore_error("unknown plan: " + args.plan);
    }
    if (!found->analysis.legal)
    {
        throw explore_error("plan is illegal: " + join_errors(found->analysis.failure_reasons));
    }
    return *found;
}

[[nodiscard]] const candidate_trial &candidate_for_id(std::span<const candidate_trial> candidates,
                                                      std::string_view id)
{
    const auto found = std::find_if(candidates.begin(), candidates.end(),
                                    [id](const candidate_trial &trial)
                                    { return plan_id(trial.analysis.plan) == id; });

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
    const request &req, std::span<const candidate_trial> candidates)
{
    std::vector<benchmark_record> records;

    records.reserve(candidates.size());
    for (const candidate_trial &candidate : candidates)
    {
        benchmark_record record;
        record.plan_id = plan_id(candidate.analysis.plan);
        record.status = benchmark_status::pending;
        record.verification.independent_plan = check_trial(req, candidate).empty();
        record.verification.ram_bound =
            candidate.analysis.legal && candidate.analysis.temporary_bytes <= req.limits.ram;
        record.peak_scratch_bytes =
            candidate.analysis.temporary_bytes > std::numeric_limits<std::uint64_t>::max()
                ? std::numeric_limits<std::uint64_t>::max()
                : static_cast<std::uint64_t>(candidate.analysis.temporary_bytes);
        record.provenance.compiler = "not_run";
        record.provenance.target = req.target.name;
        record.provenance.runner = "not_run";
        records.push_back(std::move(record));
    }
    return records;
}

[[nodiscard]] std::string summary_json(const candidate_trial &selected,
                                       std::span<const candidate_trial> candidates,
                                       std::span<const benchmark_record> benchmarks,
                                       latency_metric metric, const std::filesystem::path &out_path)
{
    const benchmark_record *measurement =
        benchmark_for_id(benchmarks, plan_id(selected.analysis.plan));
    const bool measured = measurement != nullptr && fully_verified(*measurement) &&
                          measured_latency(*measurement, metric).has_value();
    std::string out;

    out += "{\n  \"selected\": ";
    append_json_string(out, plan_id(selected.analysis.plan));
    out += ",\n  \"acc_bits\": " + std::to_string(selected.analysis.plan.acc_bits);
    out += ",\n  \"tmp_bytes\": ";
    append_wide(out, selected.analysis.temporary_bytes);
    out += ",\n  \"need_bits\": " + std::to_string(selected.analysis.required_bits);
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
            append_indent(out, 2);
            append_json_string(out, points[i]->plan_id);
            out += i + 1 == points.size() ? "\n" : ",\n";
        }
        if (!points.empty())
        {
            append_indent(out, 1);
        }
    }
    else
    {
        const std::vector<const candidate_trial *> points = frontier(candidates);
        if (!points.empty())
        {
            out += '\n';
        }
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            append_indent(out, 2);
            append_json_string(out, plan_id(points[i]->analysis.plan));
            out += i + 1 == points.size() ? "\n" : ",\n";
        }
        if (!points.empty())
        {
            append_indent(out, 1);
        }
    }
    out += "],\n  \"out\": ";
    append_json_string(out, out_path.string());
    out += "\n}\n";
    return out;
}

}

void emit(const std::filesystem::path &out, const request &req, const candidate_trial &selected,
          std::span<const candidate_trial> candidates)
{
    const std::vector<benchmark_record> benchmarks = pending_benchmarks(req, candidates);

    emit(out, req, selected, candidates, benchmarks, latency_metric::nanoseconds);
}

void emit(const std::filesystem::path &out, const request &req, const candidate_trial &selected,
          std::span<const candidate_trial> candidates, std::span<const benchmark_record> benchmarks,
          latency_metric metric)
{
    const auto errors = check_trial(req, selected);

    if (!errors.empty())
    {
        throw explore_error("bad plan: " + join_errors(errors));
    }
    if (!selected.analysis.legal)
    {
        throw explore_error("cannot emit an illegal plan");
    }

    const polynomial_ir graph = lower_ir(req, selected);
    const std::vector<std::string> graph_errors = verify_ir(req, selected, graph);

    if (!graph_errors.empty())
    {
        throw explore_error("bad ir: " + join_errors(graph_errors));
    }

    const std::vector<compiler_plan> compiler_plans = enumerate_compiler_plans(req);
    for (const compiler_plan &plan : compiler_plans)
    {
        const std::vector<std::string> plan_errors = check_compiler_plan(req, plan);
        if (!plan_errors.empty())
        {
            throw explore_error("bad compiler plan: " + join_errors(plan_errors));
        }
    }
    const auto compiled =
        std::find_if(compiler_plans.begin(), compiler_plans.end(),
                     [&selected](const compiler_plan &plan)
                     {
                         return compiler_plan_ready(plan) && plan.has_schoolbook_lowering &&
                                plan.schoolbook_lowering == selected.analysis.plan;
                     });
    if (compiled == compiler_plans.end())
    {
        throw explore_error("selected candidate has no verified compiler plan");
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
    write_text(out / "plan.json", plan_json(req, selected, *compiled, measurement, metric));
    write_text(out / "cands.json", candidates_json(candidates));
    write_text(out / "plans.json", compiler_plans_to_json(compiler_plans));
    write_text(out / "ir.json", ir_to_json(graph));
    write_text(out / "benchmarks.json", benchmarks_to_json(benchmarks, metric));
    write_text(out / "report.html", report_to_html(benchmarks, metric));
}

int run(std::span<const std::string_view> arguments, std::ostream &standard_output,
        std::ostream &standard_error) noexcept
{
    try
    {
        const auto args = parse_arguments(arguments);
        const request req = load_request(args.specification);
        const auto candidates = find(req);
        const candidate_trial *selected = args.has_plan ? &select_plan(candidates, args) : nullptr;
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
                if (forced == nullptr || !fully_verified(*forced) ||
                    !measured_latency(*forced, args.metric).has_value())
                {
                    throw explore_error(
                        "forced plan did not produce a fully verified host "
                        "measurement");
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
