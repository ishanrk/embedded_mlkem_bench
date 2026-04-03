#include "pqc_poly/tuning.hpp"

#include "json.hpp"

#include <algorithm>
#include <cstddef>
#include <tuple>

namespace pqc_poly
{
namespace
{

using detail::append_json_string;

[[nodiscard]] bool valid_status(benchmark_status value) noexcept
{
    switch (value)
    {
        case benchmark_status::pending:
        case benchmark_status::rejected:
        case benchmark_status::measured:
            return true;
    }
    return false;
}

[[nodiscard]] bool valid_metric(latency_metric value) noexcept
{
    switch (value)
    {
        case latency_metric::cycles:
        case latency_metric::nanoseconds:
            return true;
    }
    return false;
}

void require_metric(latency_metric metric)
{
    if (!valid_metric(metric))
    {
        throw tuning_error("invalid latency metric");
    }
}

[[nodiscard]] std::vector<std::string> sorted_reasons(const benchmark_record &record)
{
    std::vector<std::string> reasons = record.rejection_reasons;

    std::sort(reasons.begin(), reasons.end());
    return reasons;
}

[[nodiscard]] bool canonical_less(const benchmark_record *left, const benchmark_record *right)
{
    const auto left_key =
        std::tie(left->plan_id, left->status, left->verification.differential_tests,
                 left->verification.plan_check, left->verification.sanitizers,
                 left->verification.ram_check, left->cycles, left->nanoseconds,
                 left->scratch_bytes, left->code_size_bytes, left->provenance.target,
                 left->provenance.compiler, left->provenance.compiler_version,
                 left->provenance.compiler_flags, left->provenance.runner);
    const auto right_key =
        std::tie(right->plan_id, right->status, right->verification.differential_tests,
                 right->verification.plan_check, right->verification.sanitizers,
                 right->verification.ram_check, right->cycles, right->nanoseconds,
                 right->scratch_bytes, right->code_size_bytes, right->provenance.target,
                 right->provenance.compiler, right->provenance.compiler_version,
                 right->provenance.compiler_flags, right->provenance.runner);

    if (left_key != right_key)
    {
        return left_key < right_key;
    }
    return sorted_reasons(*left) < sorted_reasons(*right);
}

[[nodiscard]] std::vector<const benchmark_record *> canonical_records(
    std::span<const benchmark_record> records)
{
    std::vector<const benchmark_record *> ordered;

    ordered.reserve(records.size());
    for (const benchmark_record &record : records)
    {
        validate_benchmark_record(record);
        ordered.push_back(&record);
    }
    std::sort(ordered.begin(), ordered.end(), canonical_less);
    return ordered;
}

void validate_records(std::span<const benchmark_record> records)
{
    for (const benchmark_record &record : records)
    {
        validate_benchmark_record(record);
    }
}

[[nodiscard]] bool eligible(const benchmark_record &record, latency_metric metric) noexcept
{
    return selectable(record) && measured_latency(record, metric).has_value();
}

[[nodiscard]] bool measured_less(const benchmark_record *left, const benchmark_record *right,
                                 latency_metric metric) noexcept
{
    const std::uint64_t left_latency = *measured_latency(*left, metric);
    const std::uint64_t right_latency = *measured_latency(*right, metric);

    return std::tie(left_latency, left->scratch_bytes, left->code_size_bytes, left->plan_id,
                    left->provenance.target, left->provenance.compiler,
                    left->provenance.compiler_version, left->provenance.compiler_flags,
                    left->provenance.runner) <
           std::tie(right_latency, right->scratch_bytes, right->code_size_bytes,
                    right->plan_id, right->provenance.target, right->provenance.compiler,
                    right->provenance.compiler_version, right->provenance.compiler_flags,
                    right->provenance.runner);
}

[[nodiscard]] const benchmark_record *find_winner(std::span<const benchmark_record> records,
                                                  latency_metric metric)
{
    const benchmark_record *winner = nullptr;

    for (const benchmark_record &record : records)
    {
        if (eligible(record, metric) &&
            (winner == nullptr || measured_less(&record, winner, metric)))
        {
            winner = &record;
        }
    }
    return winner;
}

[[nodiscard]] bool dominates(const benchmark_record &left, const benchmark_record &right,
                             latency_metric metric) noexcept
{
    const std::uint64_t left_latency = *measured_latency(left, metric);
    const std::uint64_t right_latency = *measured_latency(right, metric);
    const bool no_worse = left_latency <= right_latency &&
                          left.scratch_bytes <= right.scratch_bytes &&
                          left.code_size_bytes <= right.code_size_bytes;
    const bool strictly_better = left_latency < right_latency ||
                                 left.scratch_bytes < right.scratch_bytes ||
                                 left.code_size_bytes < right.code_size_bytes;

    return no_worse && strictly_better;
}

[[nodiscard]] std::vector<const benchmark_record *> frontier_unchecked(
    std::span<const benchmark_record> records, latency_metric metric)
{
    std::vector<const benchmark_record *> eligible_records;

    eligible_records.reserve(records.size());
    for (const benchmark_record &record : records)
    {
        if (eligible(record, metric))
        {
            eligible_records.push_back(&record);
        }
    }

    std::vector<const benchmark_record *> frontier;
    frontier.reserve(eligible_records.size());
    for (const benchmark_record *candidate : eligible_records)
    {
        const bool dominated_by_other =
            std::any_of(eligible_records.begin(), eligible_records.end(),
                        [candidate, metric](const benchmark_record *other)
                        { return other != candidate && dominates(*other, *candidate, metric); });
        if (!dominated_by_other)
        {
            frontier.push_back(candidate);
        }
    }
    std::sort(frontier.begin(), frontier.end(),
              [metric](const benchmark_record *left, const benchmark_record *right)
              { return measured_less(left, right, metric); });
    return frontier;
}

void append_optional_number(std::string &out, const std::optional<std::uint64_t> &value)
{
    out += value ? std::to_string(*value) : "null";
}

void append_json_record(std::string &out, const benchmark_record &record)
{
    const std::vector<std::string> reasons = sorted_reasons(record);

    out += "    {\n      \"plan_id\": ";
    append_json_string(out, record.plan_id);
    out += ",\n      \"status\": ";
    append_json_string(out, benchmark_status_name(record.status));
    out += ",\n      \"selectable\": ";
    out += selectable(record) ? "true" : "false";
    out += ",\n      \"verification\": {\n        \"differential_tests\": ";
    out += record.verification.differential_tests ? "true" : "false";
    out += ",\n        \"plan_check\": ";
    out += record.verification.plan_check ? "true" : "false";
    out += ",\n        \"sanitizers\": ";
    out += record.verification.sanitizers ? "true" : "false";
    out += ",\n        \"ram_check\": ";
    out += record.verification.ram_check ? "true" : "false";
    out += "\n      },\n      \"metrics\": {\n        \"nanoseconds\": ";
    append_optional_number(out, record.nanoseconds);
    out += ",\n        \"cycles\": ";
    append_optional_number(out, record.cycles);
    out += ",\n        \"scratch_bytes\": " + std::to_string(record.scratch_bytes);
    out += ",\n        \"code_size_bytes\": " + std::to_string(record.code_size_bytes);
    out += "\n      },\n      \"provenance\": {\n        \"compiler\": ";
    append_json_string(out, record.provenance.compiler);
    out += ",\n        \"compiler_version\": ";
    append_json_string(out, record.provenance.compiler_version);
    out += ",\n        \"compiler_flags\": ";
    append_json_string(out, record.provenance.compiler_flags);
    out += ",\n        \"target\": ";
    append_json_string(out, record.provenance.target);
    out += ",\n        \"runner\": ";
    append_json_string(out, record.provenance.runner);
    out += "\n      },\n      \"rejection_reasons\": [";
    for (std::size_t index = 0; index < reasons.size(); ++index)
    {
        if (index != 0)
        {
            out += ", ";
        }
        append_json_string(out, reasons[index]);
    }
    out += "]\n    }";
}

}

std::string_view benchmark_status_name(benchmark_status value) noexcept
{
    switch (value)
    {
        case benchmark_status::pending:
            return "pending";
        case benchmark_status::rejected:
            return "rejected";
        case benchmark_status::measured:
            return "measured";
    }
    return "invalid";
}

std::string_view latency_metric_name(latency_metric value) noexcept
{
    switch (value)
    {
        case latency_metric::cycles:
            return "cycles";
        case latency_metric::nanoseconds:
            return "nanoseconds";
    }
    return "invalid";
}

bool selectable(const benchmark_record &record) noexcept
{
    return record.status == benchmark_status::measured && record.verification.differential_tests &&
           record.verification.plan_check && record.verification.sanitizers &&
           record.verification.ram_check;
}

void validate_benchmark_record(const benchmark_record &record)
{
    if (record.plan_id.empty())
    {
        throw tuning_error("benchmark plan id must not be empty");
    }
    if (!valid_status(record.status))
    {
        throw tuning_error("invalid benchmark status for plan " + record.plan_id);
    }
    if (record.provenance.compiler.empty())
    {
        throw tuning_error("benchmark compiler must not be empty for plan " + record.plan_id);
    }
    if (record.provenance.target.empty())
    {
        throw tuning_error("benchmark target must not be empty for plan " + record.plan_id);
    }
    if ((record.cycles && *record.cycles == 0) || (record.nanoseconds && *record.nanoseconds == 0))
    {
        throw tuning_error("benchmark latency must be positive for plan " + record.plan_id);
    }
    if (std::any_of(record.rejection_reasons.begin(), record.rejection_reasons.end(),
                    [](const std::string &reason) { return reason.empty(); }))
    {
        throw tuning_error("benchmark rejection reason must not be empty for plan " +
                           record.plan_id);
    }

    switch (record.status)
    {
        case benchmark_status::pending:
            if (record.cycles || record.nanoseconds || !record.rejection_reasons.empty())
            {
                throw tuning_error("pending benchmark has terminal data for plan " +
                                   record.plan_id);
            }
            break;
        case benchmark_status::rejected:
            if (record.cycles || record.nanoseconds || record.rejection_reasons.empty())
            {
                throw tuning_error("rejected benchmark has invalid terminal data for plan " +
                                   record.plan_id);
            }
            break;
        case benchmark_status::measured:
            if ((!record.cycles && !record.nanoseconds) || !record.rejection_reasons.empty() ||
                record.code_size_bytes == 0)
            {
                throw tuning_error("measured benchmark has invalid terminal data for plan " +
                                   record.plan_id);
            }
            break;
    }
}

std::optional<std::uint64_t> measured_latency(const benchmark_record &record,
                                              latency_metric metric) noexcept
{
    switch (metric)
    {
        case latency_metric::cycles:
            return record.cycles;
        case latency_metric::nanoseconds:
            return record.nanoseconds;
    }
    return std::nullopt;
}

const benchmark_record &pick_measured(std::span<const benchmark_record> records,
                                      latency_metric metric)
{
    require_metric(metric);
    validate_records(records);

    const benchmark_record *winner = find_winner(records, metric);

    if (winner == nullptr)
    {
        throw tuning_error("no selectable " + std::string(latency_metric_name(metric)) +
                           " benchmark");
    }
    return *winner;
}

std::vector<const benchmark_record *> measured_frontier(std::span<const benchmark_record> records,
                                                        latency_metric metric)
{
    require_metric(metric);
    validate_records(records);
    return frontier_unchecked(records, metric);
}

std::string benchmarks_to_json(std::span<const benchmark_record> records, latency_metric metric)
{
    require_metric(metric);

    const std::vector<const benchmark_record *> ordered = canonical_records(records);
    const benchmark_record *winner = find_winner(records, metric);
    const std::vector<const benchmark_record *> frontier = frontier_unchecked(records, metric);
    std::string out;

    out.reserve(1024 + records.size() * 640);
    out += "{\n  \"schema\": \"pqc-poly-bench/benchmarks-v2\",\n  \"metric\": ";
    append_json_string(out, latency_metric_name(metric));
    out +=
        ",\n  \"verification_scope\": \"plan consistency, differential tests, dynamic "
        "sanitizers, and scratch accounting; not formal verification or target execution\",\n"
        "  \"selected\": ";
    if (winner == nullptr)
    {
        out += "null";
    }
    else
    {
        append_json_string(out, winner->plan_id);
    }
    out += ",\n  \"frontier\": [";
    for (std::size_t index = 0; index < frontier.size(); ++index)
    {
        if (index != 0)
        {
            out += ", ";
        }
        append_json_string(out, frontier[index]->plan_id);
    }
    out += "],\n  \"records\": ";
    if (ordered.empty())
    {
        out += "[]\n}\n";
        return out;
    }
    out += "[\n";
    for (std::size_t index = 0; index < ordered.size(); ++index)
    {
        append_json_record(out, *ordered[index]);
        out += index + 1 == ordered.size() ? "\n" : ",\n";
    }
    out += "  ]\n}\n";
    return out;
}

}
