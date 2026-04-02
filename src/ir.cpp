#include "pqc_poly/ir.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <span>

namespace pqc_poly
{
namespace
{

[[nodiscard]] wide_uint magnitude(wide_int value) noexcept
{
    const wide_uint bits = static_cast<wide_uint>(value);
    return value < 0 ? wide_uint{0} - bits : bits;
}

[[nodiscard]] std::uint16_t interval_width(coefficient_interval range) noexcept
{
    const wide_uint positive = range.upper > 0 ? static_cast<wide_uint>(range.upper) : 0;
    const wide_uint negative = range.lower < 0 ? magnitude(range.lower) : 0;
    const auto bits = [](wide_uint value) noexcept
    {
        const std::uint64_t high = static_cast<std::uint64_t>(value >> 64U);
        return high == 0 ? std::bit_width(static_cast<std::uint64_t>(value))
                         : 64 + std::bit_width(high);
    };
    return static_cast<std::uint16_t>(std::max(positive == 0 ? 1 : 1 + bits(positive),
                                               negative == 0 ? 1 : 1 + bits(negative - 1)));
}

[[nodiscard]] coefficient_interval input_interval(const request &req) noexcept
{
    return {static_cast<wide_int>(input_lower_bound(req)),
            static_cast<wide_int>(input_upper_bound(req))};
}

[[nodiscard]] coefficient_interval product_interval(coefficient_interval input) noexcept
{
    const std::array products{
        input.lower * input.lower,
        input.lower * input.upper,
        input.upper * input.upper,
    };
    return {*std::min_element(products.begin(), products.end()),
            *std::max_element(products.begin(), products.end())};
}

[[nodiscard]] coefficient_interval convolution_interval(const request &req) noexcept
{
    const coefficient_interval product = product_interval(input_interval(req));
    const wide_int n = static_cast<wide_int>(req.n);
    return {std::min<wide_int>(0, product.lower * n), std::max<wide_int>(0, product.upper * n)};
}

[[nodiscard]] coefficient_interval ring_interval(const request &req) noexcept
{
    const coefficient_interval product = product_interval(input_interval(req));
    const wide_int n = static_cast<wide_int>(req.n);
    if (req.op == operation::cyclic_mul)
    {
        return {product.lower * n, product.upper * n};
    }

    const coefficient_interval first{
        product.lower - (n - 1) * product.upper,
        product.upper - (n - 1) * product.lower,
    };
    const coefficient_interval last{n * product.lower, n * product.upper};
    return {std::min(first.lower, last.lower), std::max(first.upper, last.upper)};
}

[[nodiscard]] ring_wrap wrap_for(const request &req) noexcept
{
    return req.op == operation::cyclic_mul ? ring_wrap::add : ring_wrap::subtract;
}

[[nodiscard]] reduction_state input_state(const request &req) noexcept
{
    return req.input == input_representation::centered ? reduction_state::centered
                                                       : reduction_state::canonical;
}

void add_stage(polynomial_ir &graph, ir_operation_kind operation_kind, ir_value_kind value_kind,
               coefficient_interval range, std::uint64_t extent, std::uint64_t maximum_terms,
               reduction_state reduction, storage_kind storage, ring_wrap wrap,
               std::uint8_t dependencies, bool in_place = false)
{
    const ir_id id = graph.stage_count;
    ir_stage &stage = graph.stages[id];

    stage = {operation_kind,
             value_kind,
             range,
             extent,
             maximum_terms,
             interval_width(range),
             reduction,
             storage,
             wrap,
             dependencies,
             id,
             in_place};
    for (ir_id dependency = 0; dependency < id; ++dependency)
    {
        if ((dependencies & static_cast<std::uint8_t>(1U << dependency)) != 0)
        {
            graph.stages[dependency].last_use = id;
        }
    }
    ++graph.stage_count;
}

void lower_stages(polynomial_ir &graph, const request &req)
{
    const coefficient_interval input = input_interval(req);
    const coefficient_interval reduced{0, static_cast<wide_int>(req.q - 1)};
    add_stage(graph, ir_operation_kind::bind_input, ir_value_kind::input_a, input, req.n, 0,
              input_state(req), storage_kind::input, ring_wrap::none, 0);
    add_stage(graph, ir_operation_kind::bind_input, ir_value_kind::input_b, input, req.n, 0,
              input_state(req), storage_kind::input, ring_wrap::none, 0);

    if (graph.sched != schedule::output)
    {
        const std::uint64_t extent = graph.sched == schedule::full ? 2 * req.n - 1 : req.n;
        add_stage(graph, ir_operation_kind::clear, ir_value_kind::zero, {0, 0}, extent, 0,
                  reduction_state::unreduced, storage_kind::scratch, ring_wrap::none, 0);
        graph.scratch_alignment = 64;
        graph.scratch_first_use = 2;
    }

    if (graph.sched == schedule::full)
    {
        add_stage(graph, ir_operation_kind::convolve, ir_value_kind::convolution,
                  convolution_interval(req), 2 * req.n - 1, req.n, reduction_state::unreduced,
                  storage_kind::scratch, ring_wrap::none, 0x07U, true);
        add_stage(graph, ir_operation_kind::fold, ir_value_kind::ring_result, ring_interval(req),
                  req.n, 2, reduction_state::unreduced, storage_kind::scratch, wrap_for(req), 0x08U,
                  true);
        graph.scratch_last_use = 5;
    }
    else
    {
        const std::uint8_t dependencies =
            static_cast<std::uint8_t>(graph.sched == schedule::fold ? 0x07U : 0x03U);
        add_stage(graph, ir_operation_kind::convolve, ir_value_kind::ring_result,
                  ring_interval(req), req.n, req.n, reduction_state::unreduced,
                  graph.sched == schedule::fold ? storage_kind::scratch : storage_kind::registers,
                  wrap_for(req), dependencies, graph.sched == schedule::fold);
        if (graph.sched == schedule::fold)
        {
            graph.scratch_last_use = 4;
        }
    }

    const ir_id accumulated = static_cast<ir_id>(graph.stage_count - 1);
    add_stage(graph, ir_operation_kind::reduce, ir_value_kind::reduced, reduced, req.n, 1,
              reduction_state::canonical, storage_kind::registers, ring_wrap::none,
              static_cast<std::uint8_t>(1U << accumulated));
    const ir_id reduced_id = static_cast<ir_id>(graph.stage_count - 1);
    add_stage(graph, ir_operation_kind::write_output, ir_value_kind::output, reduced, req.n, 1,
              reduction_state::canonical, storage_kind::output, ring_wrap::none,
              static_cast<std::uint8_t>(1U << reduced_id));
}

[[nodiscard]] std::span<const ir_operation_kind> expected_operations(schedule value) noexcept
{
    static constexpr std::array full{
        ir_operation_kind::bind_input,   ir_operation_kind::bind_input, ir_operation_kind::clear,
        ir_operation_kind::convolve,     ir_operation_kind::fold,       ir_operation_kind::reduce,
        ir_operation_kind::write_output,
    };
    static constexpr std::array fold{
        ir_operation_kind::bind_input, ir_operation_kind::bind_input,
        ir_operation_kind::clear,      ir_operation_kind::convolve,
        ir_operation_kind::reduce,     ir_operation_kind::write_output,
    };
    static constexpr std::array output{
        ir_operation_kind::bind_input, ir_operation_kind::bind_input,   ir_operation_kind::convolve,
        ir_operation_kind::reduce,     ir_operation_kind::write_output,
    };

    if (value == schedule::full)
    {
        return full;
    }
    return value == schedule::fold ? std::span<const ir_operation_kind>{fold}
                                   : std::span<const ir_operation_kind>{output};
}

[[nodiscard]] std::uint16_t verifier_width(coefficient_interval range) noexcept
{
    for (std::uint16_t bits = 1; bits < 128; ++bits)
    {
        const wide_int limit = static_cast<wide_int>(wide_uint{1} << (bits - 1));
        if (range.lower >= -limit && range.upper < limit)
        {
            return bits;
        }
    }
    return 128;
}

void add_error(std::vector<std::string> &errors, std::string_view error)
{
    if (std::find(errors.begin(), errors.end(), error) == errors.end())
    {
        errors.emplace_back(error);
    }
}

void append_signed(std::string &out, wide_int value)
{
    if (value < 0)
    {
        out += '-';
    }
    out += wide_to_string(magnitude(value));
}

void append_dependencies(std::string &out, std::uint8_t mask)
{
    out += '[';
    bool first = true;
    for (ir_id id = 0; id < maximum_ir_stages; ++id)
    {
        if ((mask & static_cast<std::uint8_t>(1U << id)) != 0)
        {
            out += first ? "" : ", ";
            out += std::to_string(id);
            first = false;
        }
    }
    out += ']';
}

}

std::string_view ir_operation_kind_name(ir_operation_kind value) noexcept
{
    switch (value)
    {
        case ir_operation_kind::bind_input:
            return "bind_input";
        case ir_operation_kind::clear:
            return "clear";
        case ir_operation_kind::convolve:
            return "convolve";
        case ir_operation_kind::fold:
            return "fold";
        case ir_operation_kind::reduce:
            return "reduce";
        case ir_operation_kind::write_output:
            return "write_output";
    }
    return "unknown";
}

std::string_view ir_value_kind_name(ir_value_kind value) noexcept
{
    switch (value)
    {
        case ir_value_kind::input_a:
            return "input_a";
        case ir_value_kind::input_b:
            return "input_b";
        case ir_value_kind::zero:
            return "zero";
        case ir_value_kind::convolution:
            return "convolution";
        case ir_value_kind::ring_result:
            return "ring_result";
        case ir_value_kind::reduced:
            return "reduced";
        case ir_value_kind::output:
            return "output";
    }
    return "unknown";
}

std::string_view reduction_state_name(reduction_state value) noexcept
{
    switch (value)
    {
        case reduction_state::centered:
            return "centered";
        case reduction_state::canonical:
            return "canonical";
        case reduction_state::unreduced:
            return "unreduced";
    }
    return "unknown";
}

std::string_view storage_kind_name(storage_kind value) noexcept
{
    switch (value)
    {
        case storage_kind::input:
            return "input";
        case storage_kind::scratch:
            return "scratch";
        case storage_kind::registers:
            return "registers";
        case storage_kind::output:
            return "output";
    }
    return "unknown";
}

std::string_view ring_wrap_name(ring_wrap value) noexcept
{
    switch (value)
    {
        case ring_wrap::none:
            return "none";
        case ring_wrap::add:
            return "add";
        case ring_wrap::subtract:
            return "subtract";
    }
    return "unknown";
}

polynomial_ir lower_ir(const request &req, const candidate_trial &trial)
{
    validate_request(req);
    if (!trial.analysis.legal || !check_trial(req, trial).empty())
    {
        throw ir_error("cannot lower an invalid or illegal trial");
    }

    polynomial_ir graph;
    graph.ring_operation = req.op;
    graph.sched = trial.analysis.plan.sched;
    graph.n = req.n;
    graph.q = req.q;
    graph.block = trial.analysis.plan.block;
    graph.accumulator_bits = trial.analysis.plan.acc_bits;
    graph.estimated_cost = trial.score.cost;
    graph.peak_scratch_bytes = trial.analysis.temporary_bytes;
    lower_stages(graph, req);

    const std::vector<std::string> errors = verify_ir(req, trial, graph);
    if (!errors.empty())
    {
        throw ir_error("internal ir verification failed: " + errors.front());
    }
    return graph;
}

std::vector<std::string> verify_ir(const request &req, const candidate_trial &trial,
                                   const polynomial_ir &graph)
{
    std::vector<std::string> errors;
    try
    {
        validate_request(req);
    }
    catch (const spec_error &)
    {
        errors.emplace_back("invalid request");
        return errors;
    }
    if (!trial.analysis.legal || !check_trial(req, trial).empty())
    {
        errors.emplace_back("invalid trial");
        return errors;
    }

    const schoolbook_plan &plan = trial.analysis.plan;
    if (graph.ring_operation != req.op || graph.sched != plan.sched || graph.n != req.n ||
        graph.q != req.q || graph.block != plan.block || graph.accumulator_bits != plan.acc_bits ||
        graph.estimated_cost != trial.score.cost)
    {
        add_error(errors, "bad ir metadata");
    }
    if (graph.peak_scratch_bytes != trial.analysis.temporary_bytes ||
        graph.peak_scratch_bytes > req.limits.ram)
    {
        add_error(errors, "bad peak scratch");
    }

    const bool scratch = plan.sched != schedule::output;
    const ir_id scratch_last = plan.sched == schedule::full ? 5 : 4;
    if (graph.scratch_alignment != (scratch ? 64U : 0U) ||
        graph.scratch_first_use != (scratch ? 2U : invalid_ir_id) ||
        graph.scratch_last_use != (scratch ? scratch_last : invalid_ir_id))
    {
        add_error(errors, "bad scratch lifetime");
    }

    const std::span<const ir_operation_kind> operations = expected_operations(plan.sched);
    if (graph.stage_count != operations.size())
    {
        add_error(errors, "bad graph size");
        return errors;
    }

    std::array<ir_id, maximum_ir_stages> last_use{};
    for (ir_id id = 0; id < graph.stage_count; ++id)
    {
        last_use[id] = id;
        const ir_stage &stage = graph.stages[id];
        const ir_operation_kind operation_kind = operations[id];
        const ir_value_kind value_kind =
            operation_kind == ir_operation_kind::bind_input
                ? (id == 0 ? ir_value_kind::input_a : ir_value_kind::input_b)
            : operation_kind == ir_operation_kind::clear ? ir_value_kind::zero
            : operation_kind == ir_operation_kind::convolve
                ? (plan.sched == schedule::full ? ir_value_kind::convolution
                                                : ir_value_kind::ring_result)
            : operation_kind == ir_operation_kind::fold   ? ir_value_kind::ring_result
            : operation_kind == ir_operation_kind::reduce ? ir_value_kind::reduced
                                                          : ir_value_kind::output;
        const coefficient_interval range =
            value_kind == ir_value_kind::input_a || value_kind == ir_value_kind::input_b
                ? input_interval(req)
            : value_kind == ir_value_kind::zero        ? coefficient_interval{0, 0}
            : value_kind == ir_value_kind::convolution ? convolution_interval(req)
            : value_kind == ir_value_kind::ring_result
                ? ring_interval(req)
                : coefficient_interval{0, static_cast<wide_int>(req.q - 1)};
        const bool stage_scratch = value_kind == ir_value_kind::zero ||
                                   value_kind == ir_value_kind::convolution ||
                                   (value_kind == ir_value_kind::ring_result && scratch);
        const std::uint64_t extent =
            plan.sched == schedule::full &&
                    (value_kind == ir_value_kind::zero || value_kind == ir_value_kind::convolution)
                ? 2 * req.n - 1
                : req.n;
        const std::uint8_t dependencies = operation_kind == ir_operation_kind::convolve
                                              ? static_cast<std::uint8_t>(scratch ? 0x07U : 0x03U)
                                          : operation_kind == ir_operation_kind::fold ||
                                                  operation_kind == ir_operation_kind::reduce ||
                                                  operation_kind == ir_operation_kind::write_output
                                              ? static_cast<std::uint8_t>(1U << (id - 1))
                                              : std::uint8_t{0};

        if (stage.operation != operation_kind || stage.value != value_kind ||
            stage.extent != extent)
        {
            add_error(errors, "bad stage shape");
        }
        if (stage.range != range || stage.required_bits != verifier_width(range))
        {
            add_error(errors, "bad value range");
        }
        if (stage.dependencies != dependencies)
        {
            add_error(errors, "bad dependency graph");
        }
        for (std::size_t dependency = 0; dependency < maximum_ir_stages; ++dependency)
        {
            if (dependency < id &&
                (dependencies & static_cast<std::uint8_t>(1U << dependency)) != 0)
            {
                last_use[dependency] = id;
            }
        }

        const reduction_state reduction =
            value_kind == ir_value_kind::input_a || value_kind == ir_value_kind::input_b
                ? input_state(req)
            : value_kind == ir_value_kind::reduced || value_kind == ir_value_kind::output
                ? reduction_state::canonical
                : reduction_state::unreduced;
        const storage_kind storage =
            value_kind == ir_value_kind::input_a || value_kind == ir_value_kind::input_b
                ? storage_kind::input
            : value_kind == ir_value_kind::output ? storage_kind::output
            : stage_scratch                       ? storage_kind::scratch
                                                  : storage_kind::registers;
        const ring_wrap wrap =
            operation_kind == ir_operation_kind::fold ||
                    (operation_kind == ir_operation_kind::convolve && plan.sched != schedule::full)
                ? wrap_for(req)
                : ring_wrap::none;
        const std::uint64_t terms = operation_kind == ir_operation_kind::convolve ? req.n
                                    : operation_kind == ir_operation_kind::fold   ? 2U
                                    : operation_kind == ir_operation_kind::reduce ||
                                            operation_kind == ir_operation_kind::write_output
                                        ? 1U
                                        : 0U;
        const bool in_place = operation_kind == ir_operation_kind::fold ||
                              (operation_kind == ir_operation_kind::convolve && scratch);
        if (stage.reduction != reduction || stage.storage != storage || stage.wrap != wrap ||
            stage.maximum_terms != terms || stage.in_place != in_place)
        {
            add_error(errors, "bad stage properties");
        }
    }

    for (ir_id id = 0; id < graph.stage_count; ++id)
    {
        if (graph.stages[id].last_use != last_use[id])
        {
            add_error(errors, "bad value lifetime");
        }
    }
    return errors;
}

std::string ir_to_json(const polynomial_ir &graph)
{
    std::string out;
    out += "{\n  \"version\": 2,\n  \"operation\": \"";
    out += operation_name(graph.ring_operation);
    out += "\",\n  \"n\": " + std::to_string(graph.n) + ",\n  \"q\": " + std::to_string(graph.q) +
           ",\n  \"schedule\": \"";
    out += schedule_name(graph.sched);
    out += "\",\n  \"accumulator_bits\": " + std::to_string(graph.accumulator_bits) +
           ",\n  \"block\": " + std::to_string(graph.block) +
           ",\n  \"estimated_cost\": " + wide_to_string(graph.estimated_cost) +
           ",\n  \"scratch\": ";
    if (graph.peak_scratch_bytes == 0)
    {
        out += "null";
    }
    else
    {
        out += "{\"bytes\": " + wide_to_string(graph.peak_scratch_bytes) +
               ", \"alignment\": " + std::to_string(graph.scratch_alignment) +
               ", \"first_use\": " + std::to_string(graph.scratch_first_use) +
               ", \"last_use\": " + std::to_string(graph.scratch_last_use) + "}";
    }
    out += ",\n  \"operations\": [";
    for (ir_id id = 0; id < graph.stage_count; ++id)
    {
        const ir_stage &stage = graph.stages[id];
        out += id == 0 ? "\n" : ",\n";
        out += "    {\"id\": " + std::to_string(id) + ", \"operation\": \"";
        out += ir_operation_kind_name(stage.operation);
        out += "\", \"value\": \"";
        out += ir_value_kind_name(stage.value);
        out += "\", \"extent\": " + std::to_string(stage.extent) + ", \"range\": [";
        append_signed(out, stage.range.lower);
        out += ", ";
        append_signed(out, stage.range.upper);
        out +=
            "], \"required_bits\": " + std::to_string(stage.required_bits) + ", \"reduction\": \"";
        out += reduction_state_name(stage.reduction);
        out += "\", \"storage\": \"";
        out += storage_kind_name(stage.storage);
        out += "\", \"dependencies\": ";
        append_dependencies(out, stage.dependencies);
        out += ", \"last_use\": " + std::to_string(stage.last_use) + ", \"wrap\": \"";
        out += ring_wrap_name(stage.wrap);
        out += "\", \"maximum_terms\": " + std::to_string(stage.maximum_terms) +
               ", \"in_place\": " + (stage.in_place ? "true}" : "false}");
    }
    out += graph.stage_count == 0 ? "]\n}\n" : "\n  ]\n}\n";
    return out;
}

}
