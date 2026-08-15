#include "pqc_poly/ir.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <utility>

namespace pqc_poly
{
namespace
{

[[nodiscard]] wide_uint magnitude(wide_int value) noexcept
{
    const wide_uint bits = static_cast<wide_uint>(value);
    return value < 0 ? wide_uint{0} - bits : bits;
}

[[nodiscard]] std::uint16_t wide_bit_width(wide_uint value) noexcept
{
    const std::uint64_t high = static_cast<std::uint64_t>(value >> 64);
    if (high != 0)
    {
        return static_cast<std::uint16_t>(64 + std::bit_width(high));
    }
    return static_cast<std::uint16_t>(std::bit_width(static_cast<std::uint64_t>(value)));
}

[[nodiscard]] std::uint16_t interval_width(coefficient_interval range) noexcept
{
    const wide_uint positive = range.upper > 0 ? static_cast<wide_uint>(range.upper) : 0;
    const wide_uint negative = range.lower < 0 ? magnitude(range.lower) : 0;
    const std::uint16_t positive_bits =
        positive == 0 ? 1 : static_cast<std::uint16_t>(1 + wide_bit_width(positive));
    const std::uint16_t negative_bits =
        negative == 0 ? 1 : static_cast<std::uint16_t>(1 + wide_bit_width(negative - 1));
    return std::max(positive_bits, negative_bits);
}

[[nodiscard]] coefficient_interval multiply_interval(coefficient_interval left,
                                                     coefficient_interval right) noexcept
{
    const std::array products{
        left.lower * right.lower,
        left.lower * right.upper,
        left.upper * right.lower,
        left.upper * right.upper,
    };
    return {
        *std::min_element(products.begin(), products.end()),
        *std::max_element(products.begin(), products.end()),
    };
}

[[nodiscard]] coefficient_interval input_interval(const request &req) noexcept
{
    return {
        static_cast<wide_int>(input_lower_bound(req)),
        static_cast<wide_int>(input_upper_bound(req)),
    };
}

[[nodiscard]] coefficient_interval convolution_interval(const request &req) noexcept
{
    const coefficient_interval product =
        multiply_interval(input_interval(req), input_interval(req));
    const wide_int terms = static_cast<wide_int>(req.n);
    return {
        std::min<wide_int>(0, product.lower * terms),
        std::max<wide_int>(0, product.upper * terms),
    };
}

[[nodiscard]] coefficient_interval ring_interval(const request &req) noexcept
{
    const coefficient_interval product =
        multiply_interval(input_interval(req), input_interval(req));
    const wide_int count = static_cast<wide_int>(req.n);
    if (req.op == operation::cyclic_mul)
    {
        return {product.lower * count, product.upper * count};
    }

    // the direct part has at least one term and the wrapped part may have n - 1.
    const auto range_for_direct = [product, count](wide_int direct) noexcept
    {
        const wide_int wrapped = count - direct;
        return coefficient_interval{
            direct * product.lower - wrapped * product.upper,
            direct * product.upper - wrapped * product.lower,
        };
    };
    const coefficient_interval first = range_for_direct(1);
    const coefficient_interval last = range_for_direct(count);
    return {
        std::min(first.lower, last.lower),
        std::max(first.upper, last.upper),
    };
}

[[nodiscard]] ring_wrap expected_wrap(operation op) noexcept
{
    return op == operation::cyclic_mul ? ring_wrap::add : ring_wrap::subtract;
}

[[nodiscard]] reduction_state input_reduction(const request &req) noexcept
{
    return req.input == input_representation::centered ? reduction_state::centered
                                                       : reduction_state::canonical;
}

void add_value(polynomial_ir &graph, ir_value_kind kind, std::uint64_t extent,
               coefficient_interval range, reduction_state reduction, storage_kind storage,
               ir_id scratch_region = invalid_ir_id)
{
    ir_value value;
    value.id = static_cast<ir_id>(graph.values.size());
    value.kind = kind;
    value.extent = extent;
    value.range = range;
    value.required_bits = interval_width(range);
    value.reduction = reduction;
    value.storage = storage;
    value.scratch_region = scratch_region;
    graph.values.push_back(value);
}

void add_operation(polynomial_ir &graph, ir_operation_kind kind, std::vector<ir_id> inputs,
                   ir_id output, ring_wrap wrap, std::uint64_t coefficient_count,
                   std::uint64_t maximum_terms, bool in_place = false)
{
    ir_operation op;
    op.id = static_cast<ir_id>(graph.operations.size());
    op.kind = kind;
    op.inputs = std::move(inputs);
    op.output = output;
    op.wrap = wrap;
    op.coefficient_count = coefficient_count;
    op.maximum_terms = maximum_terms;
    op.accumulator_bits = graph.accumulator_bits;
    op.in_place = in_place;
    for (const ir_id input : op.inputs)
    {
        const ir_id producer = graph.values[input].producer;
        if (producer != invalid_ir_id && std::find(op.dependencies.begin(), op.dependencies.end(),
                                                   producer) == op.dependencies.end())
        {
            op.dependencies.push_back(producer);
        }
    }
    graph.values[output].producer = op.id;
    graph.operations.push_back(std::move(op));
}

void set_last_uses(polynomial_ir &graph) noexcept
{
    // terminal values stay live through their producer; consumers extend this interval.
    for (ir_value &value : graph.values)
    {
        value.last_use = value.producer;
    }
    for (const ir_operation &op : graph.operations)
    {
        for (const ir_id input : op.inputs)
        {
            graph.values[input].last_use = op.id;
        }
    }
}

[[nodiscard]] wide_uint scratch_bytes(const request &req, const schoolbook_plan &plan) noexcept
{
    const wide_uint element_bytes = plan.acc_bits / 8;
    switch (plan.sched)
    {
        case schedule::full:
            return (wide_uint{2} * req.n - 1) * element_bytes;
        case schedule::fold:
            return wide_uint{req.n} * element_bytes;
        case schedule::output:
            return 0;
    }
    return 0;
}

void add_common_inputs(polynomial_ir &graph, const request &req)
{
    const coefficient_interval range = input_interval(req);
    add_value(graph, ir_value_kind::input_a, req.n, range, input_reduction(req),
              storage_kind::input);
    add_operation(graph, ir_operation_kind::bind_input, {}, 0, ring_wrap::none, req.n, 0);

    add_value(graph, ir_value_kind::input_b, req.n, range, input_reduction(req),
              storage_kind::input);
    add_operation(graph, ir_operation_kind::bind_input, {}, 1, ring_wrap::none, req.n, 0);
}

void lower_full(polynomial_ir &graph, const request &req)
{
    const ir_id region = 0;
    add_value(graph, ir_value_kind::zero, 2 * req.n - 1, {0, 0}, reduction_state::unreduced,
              storage_kind::scratch, region);
    add_operation(graph, ir_operation_kind::clear, {}, 2, ring_wrap::none, 2 * req.n - 1, 0);

    add_value(graph, ir_value_kind::convolution, 2 * req.n - 1, convolution_interval(req),
              reduction_state::unreduced, storage_kind::scratch, region);
    add_operation(graph, ir_operation_kind::convolve, {0, 1, 2}, 3, ring_wrap::none, 2 * req.n - 1,
                  req.n, true);

    add_value(graph, ir_value_kind::ring_result, req.n, ring_interval(req),
              reduction_state::unreduced, storage_kind::scratch, region);
    add_operation(graph, ir_operation_kind::fold, {3}, 4, expected_wrap(req.op), req.n, 2, true);

    add_value(graph, ir_value_kind::reduced, req.n, {0, static_cast<wide_int>(req.q - 1)},
              reduction_state::canonical, storage_kind::registers);
    add_operation(graph, ir_operation_kind::reduce, {4}, 5, ring_wrap::none, req.n, 1);

    add_value(graph, ir_value_kind::output, req.n, {0, static_cast<wide_int>(req.q - 1)},
              reduction_state::canonical, storage_kind::output);
    add_operation(graph, ir_operation_kind::write_output, {5}, 6, ring_wrap::none, req.n, 1);

    graph.scratch.push_back({region, 0, graph.peak_scratch_bytes, 64, 2, 5});
}

void lower_fold(polynomial_ir &graph, const request &req)
{
    const ir_id region = 0;
    add_value(graph, ir_value_kind::zero, req.n, {0, 0}, reduction_state::unreduced,
              storage_kind::scratch, region);
    add_operation(graph, ir_operation_kind::clear, {}, 2, ring_wrap::none, req.n, 0);

    add_value(graph, ir_value_kind::ring_result, req.n, ring_interval(req),
              reduction_state::unreduced, storage_kind::scratch, region);
    add_operation(graph, ir_operation_kind::convolve, {0, 1, 2}, 3, expected_wrap(req.op), req.n,
                  req.n, true);

    add_value(graph, ir_value_kind::reduced, req.n, {0, static_cast<wide_int>(req.q - 1)},
              reduction_state::canonical, storage_kind::registers);
    add_operation(graph, ir_operation_kind::reduce, {3}, 4, ring_wrap::none, req.n, 1);

    add_value(graph, ir_value_kind::output, req.n, {0, static_cast<wide_int>(req.q - 1)},
              reduction_state::canonical, storage_kind::output);
    add_operation(graph, ir_operation_kind::write_output, {4}, 5, ring_wrap::none, req.n, 1);

    graph.scratch.push_back({region, 0, graph.peak_scratch_bytes, 64, 2, 4});
}

void lower_output(polynomial_ir &graph, const request &req)
{
    add_value(graph, ir_value_kind::ring_result, req.n, ring_interval(req),
              reduction_state::unreduced, storage_kind::registers);
    add_operation(graph, ir_operation_kind::convolve, {0, 1}, 2, expected_wrap(req.op), req.n,
                  req.n);

    add_value(graph, ir_value_kind::reduced, req.n, {0, static_cast<wide_int>(req.q - 1)},
              reduction_state::canonical, storage_kind::registers);
    add_operation(graph, ir_operation_kind::reduce, {2}, 3, ring_wrap::none, req.n, 1);

    add_value(graph, ir_value_kind::output, req.n, {0, static_cast<wide_int>(req.q - 1)},
              reduction_state::canonical, storage_kind::output);
    add_operation(graph, ir_operation_kind::write_output, {3}, 4, ring_wrap::none, req.n, 1);
}

void add_error(std::vector<std::string> &errors, std::string message)
{
    if (std::find(errors.begin(), errors.end(), message) == errors.end())
    {
        errors.push_back(std::move(message));
    }
}

[[nodiscard]] coefficient_interval verifier_input_interval(const request &req) noexcept
{
    if (req.input == input_representation::canonical)
    {
        return {0, static_cast<wide_int>(req.q - 1)};
    }
    return {
        -static_cast<wide_int>(req.q / 2),
        static_cast<wide_int>((req.q - 1) / 2),
    };
}

[[nodiscard]] coefficient_interval verifier_product_interval(coefficient_interval input) noexcept
{
    const wide_int p0 = input.lower * input.lower;
    const wide_int p1 = input.lower * input.upper;
    const wide_int p2 = input.upper * input.upper;
    return {std::min({p0, p1, p2}), std::max({p0, p1, p2})};
}

[[nodiscard]] coefficient_interval verifier_convolution_interval(const request &req) noexcept
{
    const coefficient_interval product = verifier_product_interval(verifier_input_interval(req));
    const wide_int n = static_cast<wide_int>(req.n);
    return {std::min<wide_int>(0, product.lower * n), std::max<wide_int>(0, product.upper * n)};
}

[[nodiscard]] coefficient_interval verifier_ring_interval(const request &req) noexcept
{
    const coefficient_interval product = verifier_product_interval(verifier_input_interval(req));
    const wide_int n = static_cast<wide_int>(req.n);
    if (req.op == operation::cyclic_mul)
    {
        return {product.lower * n, product.upper * n};
    }

    const wide_int lower_one = product.lower - (n - 1) * product.upper;
    const wide_int upper_one = product.upper - (n - 1) * product.lower;
    const wide_int lower_all = n * product.lower;
    const wide_int upper_all = n * product.upper;
    return {std::min(lower_one, lower_all), std::max(upper_one, upper_all)};
}

[[nodiscard]] std::uint16_t verifier_interval_width(coefficient_interval range) noexcept
{
    for (std::uint16_t bits = 1; bits < 128; ++bits)
    {
        const wide_int magnitude_limit = static_cast<wide_int>(wide_uint{1} << (bits - 1));
        if (range.lower >= -magnitude_limit && range.upper < magnitude_limit)
        {
            return bits;
        }
    }
    return 128;
}

struct expected_value
{
    ir_value_kind kind;
    std::uint64_t extent;
    coefficient_interval range;
    reduction_state reduction;
    storage_kind storage;
    ir_id producer;
    ir_id last_use;
    ir_id scratch_region;
};

struct expected_operation
{
    ir_operation_kind kind;
    std::vector<ir_id> inputs;
    ir_id output;
    std::vector<ir_id> dependencies;
    ring_wrap wrap;
    std::uint64_t coefficient_count;
    std::uint64_t maximum_terms;
    bool in_place;
};

void check_value(const polynomial_ir &graph, std::size_t index, const expected_value &expected,
                 std::vector<std::string> &errors)
{
    if (index >= graph.values.size())
    {
        add_error(errors, "missing value");
        return;
    }
    const ir_value &value = graph.values[index];
    if (value.id != index)
    {
        add_error(errors, "bad value id");
    }
    if (value.kind != expected.kind || value.extent != expected.extent)
    {
        add_error(errors, "bad value shape");
    }
    if (value.range != expected.range ||
        value.required_bits != verifier_interval_width(expected.range))
    {
        add_error(errors, "bad value range");
    }
    if (value.reduction != expected.reduction)
    {
        add_error(errors, "bad reduction state");
    }
    if (value.storage != expected.storage || value.scratch_region != expected.scratch_region)
    {
        add_error(errors, "bad value storage");
    }
    if (value.producer != expected.producer || value.last_use != expected.last_use)
    {
        add_error(errors, "bad value lifetime");
    }
}

void check_operation(const polynomial_ir &graph, std::size_t index,
                     const expected_operation &expected, std::uint16_t accumulator_bits,
                     std::vector<std::string> &errors)
{
    if (index >= graph.operations.size())
    {
        add_error(errors, "missing operation");
        return;
    }
    const ir_operation &op = graph.operations[index];
    if (op.id != index || op.kind != expected.kind || op.output != expected.output)
    {
        add_error(errors, "bad operation shape");
    }
    if (op.inputs != expected.inputs || op.dependencies != expected.dependencies)
    {
        add_error(errors, "bad dependency graph");
    }
    if (op.wrap != expected.wrap)
    {
        add_error(errors, "bad ring wrap");
    }
    if (op.coefficient_count != expected.coefficient_count ||
        op.maximum_terms != expected.maximum_terms || op.accumulator_bits != accumulator_bits ||
        op.in_place != expected.in_place)
    {
        add_error(errors, "bad operation bounds");
    }
}

void verify_structure(const polynomial_ir &graph, std::vector<std::string> &errors)
{
    std::vector<ir_id> recomputed_last_use(graph.values.size(), invalid_ir_id);
    for (std::size_t index = 0; index < graph.operations.size(); ++index)
    {
        const ir_operation &op = graph.operations[index];
        if (op.output >= graph.values.size())
        {
            add_error(errors, "operation output is out of range");
            continue;
        }
        if (graph.values[op.output].producer != index)
        {
            add_error(errors, "producer link is inconsistent");
        }
        recomputed_last_use[op.output] = static_cast<ir_id>(index);
        for (const ir_id input : op.inputs)
        {
            if (input >= graph.values.size())
            {
                add_error(errors, "operation input is out of range");
                continue;
            }
            recomputed_last_use[input] = static_cast<ir_id>(index);
            const ir_id producer = graph.values[input].producer;
            if (producer >= index)
            {
                add_error(errors, "graph is not topological");
            }
            if (std::find(op.dependencies.begin(), op.dependencies.end(), producer) ==
                op.dependencies.end())
            {
                add_error(errors, "missing producer dependency");
            }
        }
        for (const ir_id dependency : op.dependencies)
        {
            if (dependency >= index)
            {
                add_error(errors, "dependency is not topological");
            }
        }
    }
    for (std::size_t index = 0; index < graph.values.size(); ++index)
    {
        if (graph.values[index].last_use != recomputed_last_use[index])
        {
            add_error(errors, "last use is inconsistent");
        }
        if (graph.values[index].range.lower > graph.values[index].range.upper ||
            graph.values[index].required_bits != verifier_interval_width(graph.values[index].range))
        {
            add_error(errors, "invalid value interval");
        }
    }
}

void verify_memory(const request &req, const candidate_trial &trial, const polynomial_ir &graph,
                   std::vector<std::string> &errors)
{
    const wide_uint expected_bytes = [&req, &trial]() noexcept
    {
        const wide_uint element_bytes = trial.analysis.plan.acc_bits / 8;
        if (trial.analysis.plan.sched == schedule::full)
        {
            return (wide_uint{2} * req.n - 1) * element_bytes;
        }
        if (trial.analysis.plan.sched == schedule::fold)
        {
            return wide_uint{req.n} * element_bytes;
        }
        return wide_uint{0};
    }();
    if (graph.peak_scratch_bytes != expected_bytes ||
        graph.peak_scratch_bytes != trial.analysis.temporary_bytes ||
        graph.peak_scratch_bytes > req.limits.ram)
    {
        add_error(errors, "bad peak scratch");
    }

    const bool needs_scratch = trial.analysis.plan.sched != schedule::output;
    if (graph.scratch.size() != static_cast<std::size_t>(needs_scratch))
    {
        add_error(errors, "bad scratch allocation count");
        return;
    }
    if (!needs_scratch || graph.scratch.empty())
    {
        return;
    }

    const scratch_allocation &allocation = graph.scratch[0];
    const ir_id expected_last = trial.analysis.plan.sched == schedule::full ? 5 : 4;
    if (allocation.id != 0 || allocation.offset != 0 || allocation.bytes != expected_bytes ||
        allocation.alignment != 64 || allocation.first_operation != 2 ||
        allocation.last_operation != expected_last)
    {
        add_error(errors, "bad scratch lifetime");
    }
    if (allocation.alignment == 0 || (allocation.alignment & (allocation.alignment - 1)) != 0 ||
        allocation.offset % allocation.alignment != 0 ||
        allocation.offset + allocation.bytes > graph.peak_scratch_bytes)
    {
        add_error(errors, "invalid scratch allocation");
    }
}

void append_wide(std::string &output, wide_uint value)
{
    output += wide_to_string(value);
}

void append_signed_wide(std::string &output, wide_int value)
{
    if (value < 0)
    {
        output.push_back('-');
    }
    append_wide(output, magnitude(value));
}

void append_ids(std::string &output, const std::vector<ir_id> &ids)
{
    output.push_back('[');
    for (std::size_t index = 0; index < ids.size(); ++index)
    {
        if (index != 0)
        {
            output += ", ";
        }
        output += std::to_string(ids[index]);
    }
    output.push_back(']');
}

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
    const std::vector<std::string> errors = check_trial(req, trial);
    if (!errors.empty() || !trial.analysis.legal)
    {
        throw ir_error("cannot lower an invalid or illegal trial");
    }

    polynomial_ir graph;
    graph.ring_operation = req.op;
    graph.n = req.n;
    graph.q = req.q;
    graph.sched = trial.analysis.plan.sched;
    graph.accumulator_bits = trial.analysis.plan.acc_bits;
    graph.block = trial.analysis.plan.block;
    graph.estimated_cost = trial.score.cost;
    graph.peak_scratch_bytes = scratch_bytes(req, trial.analysis.plan);
    add_common_inputs(graph, req);

    switch (graph.sched)
    {
        case schedule::full:
            lower_full(graph, req);
            break;
        case schedule::fold:
            lower_fold(graph, req);
            break;
        case schedule::output:
            lower_output(graph, req);
            break;
    }
    set_last_uses(graph);

    const std::vector<std::string> verification = verify_ir(req, trial, graph);
    if (!verification.empty())
    {
        throw ir_error("internal ir verification failed: " + verification.front());
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
    if (!check_trial(req, trial).empty() || !trial.analysis.legal)
    {
        errors.emplace_back("invalid trial");
        return errors;
    }

    const schoolbook_plan &plan = trial.analysis.plan;
    if (graph.ring_operation != req.op || graph.n != req.n || graph.q != req.q ||
        graph.sched != plan.sched || graph.accumulator_bits != plan.acc_bits ||
        graph.block != plan.block || graph.estimated_cost != trial.score.cost)
    {
        add_error(errors, "bad ir metadata");
    }

    const coefficient_interval input = verifier_input_interval(req);
    const coefficient_interval convolution = verifier_convolution_interval(req);
    const coefficient_interval ring = verifier_ring_interval(req);
    const coefficient_interval reduced{0, static_cast<wide_int>(req.q - 1)};
    const reduction_state input_state = req.input == input_representation::centered
                                            ? reduction_state::centered
                                            : reduction_state::canonical;
    const ring_wrap wrap = req.op == operation::cyclic_mul ? ring_wrap::add : ring_wrap::subtract;

    std::vector<expected_value> values;
    std::vector<expected_operation> operations;
    values.push_back({ir_value_kind::input_a, req.n, input, input_state, storage_kind::input, 0,
                      plan.sched == schedule::output ? 2u : 3u, invalid_ir_id});
    values.push_back({ir_value_kind::input_b, req.n, input, input_state, storage_kind::input, 1,
                      plan.sched == schedule::output ? 2u : 3u, invalid_ir_id});
    operations.push_back(
        {ir_operation_kind::bind_input, {}, 0, {}, ring_wrap::none, req.n, 0, false});
    operations.push_back(
        {ir_operation_kind::bind_input, {}, 1, {}, ring_wrap::none, req.n, 0, false});

    if (plan.sched == schedule::full)
    {
        values.push_back({ir_value_kind::zero,
                          2 * req.n - 1,
                          {0, 0},
                          reduction_state::unreduced,
                          storage_kind::scratch,
                          2,
                          3,
                          0});
        values.push_back({ir_value_kind::convolution, 2 * req.n - 1, convolution,
                          reduction_state::unreduced, storage_kind::scratch, 3, 4, 0});
        values.push_back({ir_value_kind::ring_result, req.n, ring, reduction_state::unreduced,
                          storage_kind::scratch, 4, 5, 0});
        values.push_back({ir_value_kind::reduced, req.n, reduced, reduction_state::canonical,
                          storage_kind::registers, 5, 6, invalid_ir_id});
        values.push_back({ir_value_kind::output, req.n, reduced, reduction_state::canonical,
                          storage_kind::output, 6, 6, invalid_ir_id});
        operations.push_back(
            {ir_operation_kind::clear, {}, 2, {}, ring_wrap::none, 2 * req.n - 1, 0, false});
        operations.push_back({ir_operation_kind::convolve,
                              {0, 1, 2},
                              3,
                              {0, 1, 2},
                              ring_wrap::none,
                              2 * req.n - 1,
                              req.n,
                              true});
        operations.push_back({ir_operation_kind::fold, {3}, 4, {3}, wrap, req.n, 2, true});
        operations.push_back(
            {ir_operation_kind::reduce, {4}, 5, {4}, ring_wrap::none, req.n, 1, false});
        operations.push_back(
            {ir_operation_kind::write_output, {5}, 6, {5}, ring_wrap::none, req.n, 1, false});
    }
    else if (plan.sched == schedule::fold)
    {
        values.push_back({ir_value_kind::zero,
                          req.n,
                          {0, 0},
                          reduction_state::unreduced,
                          storage_kind::scratch,
                          2,
                          3,
                          0});
        values.push_back({ir_value_kind::ring_result, req.n, ring, reduction_state::unreduced,
                          storage_kind::scratch, 3, 4, 0});
        values.push_back({ir_value_kind::reduced, req.n, reduced, reduction_state::canonical,
                          storage_kind::registers, 4, 5, invalid_ir_id});
        values.push_back({ir_value_kind::output, req.n, reduced, reduction_state::canonical,
                          storage_kind::output, 5, 5, invalid_ir_id});
        operations.push_back(
            {ir_operation_kind::clear, {}, 2, {}, ring_wrap::none, req.n, 0, false});
        operations.push_back(
            {ir_operation_kind::convolve, {0, 1, 2}, 3, {0, 1, 2}, wrap, req.n, req.n, true});
        operations.push_back(
            {ir_operation_kind::reduce, {3}, 4, {3}, ring_wrap::none, req.n, 1, false});
        operations.push_back(
            {ir_operation_kind::write_output, {4}, 5, {4}, ring_wrap::none, req.n, 1, false});
    }
    else
    {
        values.push_back({ir_value_kind::ring_result, req.n, ring, reduction_state::unreduced,
                          storage_kind::registers, 2, 3, invalid_ir_id});
        values.push_back({ir_value_kind::reduced, req.n, reduced, reduction_state::canonical,
                          storage_kind::registers, 3, 4, invalid_ir_id});
        values.push_back({ir_value_kind::output, req.n, reduced, reduction_state::canonical,
                          storage_kind::output, 4, 4, invalid_ir_id});
        operations.push_back(
            {ir_operation_kind::convolve, {0, 1}, 2, {0, 1}, wrap, req.n, req.n, false});
        operations.push_back(
            {ir_operation_kind::reduce, {2}, 3, {2}, ring_wrap::none, req.n, 1, false});
        operations.push_back(
            {ir_operation_kind::write_output, {3}, 4, {3}, ring_wrap::none, req.n, 1, false});
    }

    if (graph.values.size() != values.size())
    {
        add_error(errors, "bad value count");
    }
    if (graph.operations.size() != operations.size())
    {
        add_error(errors, "bad operation count");
    }
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        check_value(graph, index, values[index], errors);
    }
    for (std::size_t index = 0; index < operations.size(); ++index)
    {
        check_operation(graph, index, operations[index], plan.acc_bits, errors);
    }
    verify_structure(graph, errors);
    verify_memory(req, trial, graph, errors);
    return errors;
}

std::string ir_to_json(const polynomial_ir &graph)
{
    std::string output;
    output += "{\n";
    output += "  \"version\": 1,\n";
    output += "  \"operation\": \"";
    output += operation_name(graph.ring_operation);
    output += "\",\n";
    output += "  \"n\": " + std::to_string(graph.n) + ",\n";
    output += "  \"q\": " + std::to_string(graph.q) + ",\n";
    output += "  \"schedule\": \"";
    output += schedule_name(graph.sched);
    output += "\",\n";
    output += "  \"accumulator_bits\": " + std::to_string(graph.accumulator_bits) + ",\n";
    output += "  \"block\": " + std::to_string(graph.block) + ",\n";
    output += "  \"estimated_cost\": ";
    append_wide(output, graph.estimated_cost);
    output += ",\n  \"peak_scratch_bytes\": ";
    append_wide(output, graph.peak_scratch_bytes);
    output += ",\n  \"values\": [";
    for (std::size_t index = 0; index < graph.values.size(); ++index)
    {
        const ir_value &value = graph.values[index];
        output += index == 0 ? "\n" : ",\n";
        output += "    {\"id\": " + std::to_string(value.id) + ", \"kind\": \"";
        output += ir_value_kind_name(value.kind);
        output += "\", \"extent\": " + std::to_string(value.extent) + ", \"range\": [";
        append_signed_wide(output, value.range.lower);
        output += ", ";
        append_signed_wide(output, value.range.upper);
        output += "], \"required_bits\": " + std::to_string(value.required_bits);
        output += ", \"reduction\": \"";
        output += reduction_state_name(value.reduction);
        output += "\", \"storage\": \"";
        output += storage_kind_name(value.storage);
        output += "\", \"producer\": " + std::to_string(value.producer);
        output += ", \"last_use\": " + std::to_string(value.last_use);
        output += ", \"scratch_region\": ";
        if (value.scratch_region == invalid_ir_id)
        {
            output += "null}";
        }
        else
        {
            output += std::to_string(value.scratch_region) + "}";
        }
    }
    output += graph.values.empty() ? "]" : "\n  ]";
    output += ",\n  \"operations\": [";
    for (std::size_t index = 0; index < graph.operations.size(); ++index)
    {
        const ir_operation &op = graph.operations[index];
        output += index == 0 ? "\n" : ",\n";
        output += "    {\"id\": " + std::to_string(op.id) + ", \"kind\": \"";
        output += ir_operation_kind_name(op.kind);
        output += "\", \"inputs\": ";
        append_ids(output, op.inputs);
        output += ", \"output\": " + std::to_string(op.output) + ", \"dependencies\": ";
        append_ids(output, op.dependencies);
        output += ", \"wrap\": \"";
        output += ring_wrap_name(op.wrap);
        output += "\", \"coefficient_count\": " + std::to_string(op.coefficient_count);
        output += ", \"maximum_terms\": " + std::to_string(op.maximum_terms);
        output += ", \"accumulator_bits\": " + std::to_string(op.accumulator_bits);
        output += std::string{", \"in_place\": "} + (op.in_place ? "true}" : "false}");
    }
    output += graph.operations.empty() ? "]" : "\n  ]";
    output += ",\n  \"scratch\": [";
    for (std::size_t index = 0; index < graph.scratch.size(); ++index)
    {
        const scratch_allocation &allocation = graph.scratch[index];
        output += index == 0 ? "\n" : ",\n";
        output += "    {\"id\": " + std::to_string(allocation.id) + ", \"offset\": ";
        append_wide(output, allocation.offset);
        output += ", \"bytes\": ";
        append_wide(output, allocation.bytes);
        output += ", \"alignment\": " + std::to_string(allocation.alignment);
        output += ", \"first_operation\": " + std::to_string(allocation.first_operation);
        output += ", \"last_operation\": " + std::to_string(allocation.last_operation) + "}";
    }
    output += graph.scratch.empty() ? "]" : "\n  ]";
    output += "\n}\n";
    return output;
}

}
