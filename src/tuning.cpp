#include "pqc_poly/tuning.hpp"

#include <algorithm>
#include <cstddef>
#include <tuple>

namespace pqc_poly
{
namespace
{

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

void append_hex_escape(std::string &out, std::uint16_t value)
{
    constexpr std::string_view digits = "0123456789abcdef";

    out += "\\u";
    for (const unsigned shift : {12U, 8U, 4U, 0U})
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
    // ascii output makes reports byte stable across locale and filesystem settings
    out += '"';

    for (std::size_t index = 0; index < value.size();)
    {
        const auto first = static_cast<std::uint8_t>(value[index]);

        if (first < 0x80U)
        {
            ++index;
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
        std::uint32_t minimum = 0;

        if ((first & 0xe0U) == 0xc0U)
        {
            code_point = first & 0x1fU;
            width = 2;
            minimum = 0x80U;
        }
        else if ((first & 0xf0U) == 0xe0U)
        {
            code_point = first & 0x0fU;
            width = 3;
            minimum = 0x800U;
        }
        else if ((first & 0xf8U) == 0xf0U)
        {
            code_point = first & 0x07U;
            width = 4;
            minimum = 0x10000U;
        }

        bool valid = width != 0 && index + width <= value.size();
        for (std::size_t offset = 1; valid && offset < width; ++offset)
        {
            const auto next = static_cast<std::uint8_t>(value[index + offset]);

            if ((next & 0xc0U) != 0x80U)
            {
                valid = false;
            }
            else
            {
                code_point = (code_point << 6U) | (next & 0x3fU);
            }
        }

        valid = valid && code_point >= minimum && code_point <= 0x10ffffU &&
                !(code_point >= 0xd800U && code_point <= 0xdfffU);
        if (!valid)
        {
            append_hex_escape(out, first);
            ++index;
            continue;
        }

        append_code_point(out, code_point);
        index += width;
    }

    out += '"';
}

void append_html_text(std::string &out, std::string_view value)
{
    constexpr std::string_view digits = "0123456789abcdef";

    for (const char character : value)
    {
        const auto byte = static_cast<std::uint8_t>(character);

        switch (character)
        {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&#39;";
                break;
            default:
                if (byte < 0x20U && character != '\n' && character != '\r' && character != '\t')
                {
                    out += "&#x";
                    out += digits[byte >> 4U];
                    out += digits[byte & 0xfU];
                    out += ';';
                }
                else
                {
                    out += character;
                }
                break;
        }
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
                 left->verification.independent_plan, left->verification.memory_safety,
                 left->verification.ram_bound, left->cycles, left->nanoseconds,
                 left->peak_scratch_bytes, left->code_size_bytes, left->provenance.target,
                 left->provenance.compiler, left->provenance.compiler_version,
                 left->provenance.compiler_flags, left->provenance.runner);
    const auto right_key =
        std::tie(right->plan_id, right->status, right->verification.differential_tests,
                 right->verification.independent_plan, right->verification.memory_safety,
                 right->verification.ram_bound, right->cycles, right->nanoseconds,
                 right->peak_scratch_bytes, right->code_size_bytes, right->provenance.target,
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

[[nodiscard]] bool eligible(const benchmark_record &record, latency_metric metric) noexcept
{
    return fully_verified(record) && measured_latency(record, metric).has_value();
}

[[nodiscard]] bool measured_less(const benchmark_record *left, const benchmark_record *right,
                                 latency_metric metric) noexcept
{
    const std::uint64_t left_latency = *measured_latency(*left, metric);
    const std::uint64_t right_latency = *measured_latency(*right, metric);

    return std::tie(left_latency, left->peak_scratch_bytes, left->code_size_bytes, left->plan_id,
                    left->provenance.target, left->provenance.compiler,
                    left->provenance.compiler_version, left->provenance.compiler_flags,
                    left->provenance.runner) <
           std::tie(right_latency, right->peak_scratch_bytes, right->code_size_bytes,
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
                          left.peak_scratch_bytes <= right.peak_scratch_bytes &&
                          left.code_size_bytes <= right.code_size_bytes;
    const bool strictly_better = left_latency < right_latency ||
                                 left.peak_scratch_bytes < right.peak_scratch_bytes ||
                                 left.code_size_bytes < right.code_size_bytes;

    return no_worse && strictly_better;
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
    out += ",\n      \"fully_verified\": ";
    out += fully_verified(record) ? "true" : "false";
    out += ",\n      \"verification\": {\n        \"differential_tests\": ";
    out += record.verification.differential_tests ? "true" : "false";
    out += ",\n        \"independent_plan\": ";
    out += record.verification.independent_plan ? "true" : "false";
    out += ",\n        \"memory_safety\": ";
    out += record.verification.memory_safety ? "true" : "false";
    out += ",\n        \"ram_bound\": ";
    out += record.verification.ram_bound ? "true" : "false";
    out += "\n      },\n      \"measurement\": {\n        \"nanoseconds\": ";
    append_optional_number(out, record.nanoseconds);
    out += ",\n        \"cycles\": ";
    append_optional_number(out, record.cycles);
    out += ",\n        \"peak_scratch_bytes\": " + std::to_string(record.peak_scratch_bytes);
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

void append_number_cell(std::string &out, const std::optional<std::uint64_t> &value)
{
    out += "<td>";
    out += value ? std::to_string(*value) : "n/a";
    out += "</td>";
}

void append_verification_cell(std::string &out, const verification_status &verification)
{
    out += "<td>differential: ";
    out += verification.differential_tests ? "pass" : "fail";
    out += "<br>plan: ";
    out += verification.independent_plan ? "pass" : "fail";
    out += "<br>memory: ";
    out += verification.memory_safety ? "pass" : "fail";
    out += "<br>ram: ";
    out += verification.ram_bound ? "pass" : "fail";
    out += "</td>";
}

void append_record_row(std::string &out, const benchmark_record &record)
{
    out += "<tr><td><code>";
    append_html_text(out, record.plan_id);
    out += "</code></td><td>";
    append_html_text(out, benchmark_status_name(record.status));
    out += "</td><td>";
    out += fully_verified(record) ? "pass" : "fail";
    out += "</td>";
    append_verification_cell(out, record.verification);
    append_number_cell(out, record.cycles);
    append_number_cell(out, record.nanoseconds);
    out += "<td>" + std::to_string(record.peak_scratch_bytes) + "</td><td>" +
           std::to_string(record.code_size_bytes) + "</td><td>";
    append_html_text(out, record.provenance.target);
    out += "</td><td>";
    append_html_text(out, record.provenance.compiler);
    if (!record.provenance.compiler_version.empty())
    {
        out += ' ';
        append_html_text(out, record.provenance.compiler_version);
    }
    out += "</td><td>";
    append_html_text(out, record.provenance.compiler_flags);
    out += "</td><td>";
    append_html_text(out, record.provenance.runner);
    out += "</td><td>";

    const std::vector<std::string> reasons = sorted_reasons(record);
    for (std::size_t index = 0; index < reasons.size(); ++index)
    {
        if (index != 0)
        {
            out += "; ";
        }
        append_html_text(out, reasons[index]);
    }
    out += "</td></tr>\n";
}

void append_table_header(std::string &out)
{
    out +=
        "<thead><tr><th>plan</th><th>status</th><th>verified</th><th>checks</th>"
        "<th>cycles</th><th>nanoseconds</th><th>scratch bytes</th>"
        "<th>code bytes</th><th>target</th><th>compiler</th><th>flags</th><th>runner</th>"
        "<th>rejection reasons</th></tr></thead>\n<tbody>\n";
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

bool fully_verified(const benchmark_record &record) noexcept
{
    return record.status == benchmark_status::measured && record.verification.differential_tests &&
           record.verification.independent_plan && record.verification.memory_safety &&
           record.verification.ram_bound;
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
    static_cast<void>(canonical_records(records));

    const benchmark_record *winner = find_winner(records, metric);

    if (winner == nullptr)
    {
        throw tuning_error("no fully verified " + std::string(latency_metric_name(metric)) +
                           " benchmark");
    }
    return *winner;
}

std::vector<const benchmark_record *> measured_frontier(std::span<const benchmark_record> records,
                                                        latency_metric metric)
{
    require_metric(metric);
    static_cast<void>(canonical_records(records));

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
        const bool dominated =
            std::any_of(eligible_records.begin(), eligible_records.end(),
                        [candidate, metric](const benchmark_record *other)
                        { return other != candidate && dominates(*other, *candidate, metric); });

        if (!dominated)
        {
            frontier.push_back(candidate);
        }
    }
    std::sort(frontier.begin(), frontier.end(),
              [metric](const benchmark_record *left, const benchmark_record *right)
              { return measured_less(left, right, metric); });
    return frontier;
}

std::string benchmarks_to_json(std::span<const benchmark_record> records, latency_metric metric)
{
    require_metric(metric);

    const std::vector<const benchmark_record *> ordered = canonical_records(records);
    const benchmark_record *winner = find_winner(records, metric);
    const std::vector<const benchmark_record *> frontier = measured_frontier(records, metric);
    std::string out;

    out.reserve(1024 + records.size() * 640);
    out += "{\n  \"schema\": \"pqc-poly-bench/benchmarks-v1\",\n  \"metric\": ";
    append_json_string(out, latency_metric_name(metric));
    out +=
        ",\n  \"verification_scope\": \"differential tests, independent plan and ir checks, "
        "dynamic sanitizers, and ram accounting; not cbmc or real target execution\",\n"
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

std::string report_to_html(std::span<const benchmark_record> records, latency_metric metric)
{
    require_metric(metric);

    const std::vector<const benchmark_record *> ordered = canonical_records(records);
    const benchmark_record *winner = find_winner(records, metric);
    const std::vector<const benchmark_record *> frontier = measured_frontier(records, metric);
    std::string out;

    out.reserve(2048 + records.size() * 480);
    out +=
        "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
        "<title>pqc polynomial benchmark report</title>\n"
        "<style>body{font-family:system-ui,sans-serif;line-height:1.45;margin:2rem;"
        "color:#18202a}main{max-width:100rem;margin:auto}table{border-collapse:collapse;"
        "width:100%;margin-block:1rem 2rem}th,td{border:1px solid #ccd3da;"
        "padding:.45rem;text-align:left;vertical-align:top}th{background:#eef2f5}"
        "code{font-family:ui-monospace,monospace}.muted{color:#5b6570}</style>\n"
        "</head>\n<body>\n<main>\n<h1>pqc polynomial benchmark report</h1>\n<p>"
        "optimization metric: <code>";
    append_html_text(out, latency_metric_name(metric));
    out +=
        "</code></p>\n<p class=\"muted\">verified here means differential tests, independent "
        "plan and ir checks, dynamic sanitizers, and ram accounting. it does not mean cbmc "
        "or real target execution.</p>\n<section>\n<h2>selected implementation</h2>\n";
    if (winner == nullptr)
    {
        out += "<p class=\"muted\">no fully verified measurement is available.</p>\n";
    }
    else
    {
        out += "<p><code>";
        append_html_text(out, winner->plan_id);
        out += "</code> at " + std::to_string(*measured_latency(*winner, metric)) + ' ';
        append_html_text(out, latency_metric_name(metric));
        out += ", " + std::to_string(winner->peak_scratch_bytes) + " scratch bytes, and " +
               std::to_string(winner->code_size_bytes) + " code bytes.</p>\n";
    }
    out += "</section>\n<section>\n<h2>measured pareto frontier</h2>\n<table>\n";
    append_table_header(out);
    for (const benchmark_record *record : frontier)
    {
        append_record_row(out, *record);
    }
    out +=
        "</tbody>\n</table>\n</section>\n<section>\n<h2>all benchmark records</h2>\n"
        "<table>\n";
    append_table_header(out);
    for (const benchmark_record *record : ordered)
    {
        append_record_row(out, *record);
    }
    out += "</tbody>\n</table>\n</section>\n</main>\n</body>\n</html>\n";
    return out;
}

}
