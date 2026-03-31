#include "pqc_poly/compiler_plan.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <limits>
#include <string>
#include <tuple>
#include <utility>

namespace pqc_poly
{
namespace
{

constexpr wide_uint wide_max = ~wide_uint{0};

struct tree_metrics
{
    std::uint16_t depth{0};
    std::uint64_t largest_leaf{0};
};

[[nodiscard]] wide_uint saturating_multiply(wide_uint left, wide_uint right,
                                            bool &saturated) noexcept
{
    if (left != 0 && right > wide_max / left)
    {
        saturated = true;
        return wide_max;
    }
    return left * right;
}

[[nodiscard]] algorithm_tree make_schoolbook_leaf(std::uint64_t degree)
{
    return {algorithm_family::schoolbook, degree, 0, degree, 0, {}};
}

[[nodiscard]] algorithm_tree make_karatsuba_tree(std::uint64_t degree, std::uint16_t depth)
{
    if (depth == 0)
    {
        return make_schoolbook_leaf(degree);
    }

    const std::uint64_t child_degree = degree / 2 + degree % 2;
    algorithm_tree result;
    result.family = algorithm_family::karatsuba;
    result.degree = degree;
    result.recursion_depth = depth;
    result.branches.reserve(3);
    for (unsigned branch = 0; branch < 3; ++branch)
    {
        result.branches.push_back(make_karatsuba_tree(child_degree, depth - 1));
    }
    result.leaf_size = result.branches.front().leaf_size;
    return result;
}

[[nodiscard]] algorithm_tree make_mixed_karatsuba_tree(std::uint64_t degree)
{
    const std::uint64_t child_degree = degree / 2 + degree % 2;
    algorithm_tree result;
    result.family = algorithm_family::mixed_karatsuba;
    result.degree = degree;
    result.recursion_depth = 2;
    result.leaf_size = child_degree;
    result.branches = {
        make_karatsuba_tree(child_degree, 1),
        make_schoolbook_leaf(child_degree),
        make_schoolbook_leaf(child_degree),
    };
    return result;
}

[[nodiscard]] algorithm_tree make_toom_tree(std::uint64_t degree)
{
    const std::uint64_t child_degree = degree / 3 + (degree % 3 != 0);
    algorithm_tree result;
    result.family = algorithm_family::toom_cook;
    result.degree = degree;
    result.recursion_depth = 1;
    result.leaf_size = child_degree;
    result.branches.assign(5, make_schoolbook_leaf(child_degree));
    return result;
}

[[nodiscard]] algorithm_tree make_hybrid_tree(std::uint64_t degree)
{
    const std::uint64_t child_degree = degree / 3 + (degree % 3 != 0);
    algorithm_tree result;
    result.family = algorithm_family::hybrid;
    result.degree = degree;
    result.recursion_depth = 2;
    result.leaf_size = child_degree;
    result.branches = {
        make_karatsuba_tree(child_degree, 1), make_karatsuba_tree(child_degree, 1),
        make_schoolbook_leaf(child_degree),   make_schoolbook_leaf(child_degree),
        make_schoolbook_leaf(child_degree),
    };
    return result;
}

[[nodiscard]] compiler_range_estimate exact_range(const request &req,
                                                  const analysis_verdict &analysis) noexcept
{
    return {
        input_bound(req),
        product_bound(req),
        accumulator_bound(req),
        analysis.accumulator_bound,
        analysis.required_bits,
        true,
        false,
    };
}

[[nodiscard]] compiler_range_estimate provisional_range(const request &req,
                                                        wide_uint growth) noexcept
{
    bool saturated = false;
    const wide_uint output_bound = accumulator_bound(req);
    const wide_uint peak_bound = saturating_multiply(output_bound, growth, saturated);
    return {
        input_bound(req),
        product_bound(req),
        output_bound,
        peak_bound,
        required_signed_bits(peak_bound),
        false,
        saturated,
    };
}

[[nodiscard]] compiler_scratch_estimate provisional_scratch(const request &req,
                                                            std::uint16_t acc_bits,
                                                            wide_uint coefficient_factor) noexcept
{
    const wide_uint coefficients = wide_uint{req.n} * coefficient_factor;
    return {coefficients * (acc_bits / 8), coefficients, false};
}

[[nodiscard]] std::vector<std::string> lowerable_legality_reasons(const analysis_verdict &analysis)
{
    if (analysis.failure_reasons.empty())
    {
        return {"verified range, ram, alias, and target-size checks passed"};
    }

    std::vector<std::string> reasons;
    reasons.reserve(analysis.failure_reasons.size());
    for (const std::string &reason : analysis.failure_reasons)
    {
        if (reason == "ram")
        {
            reasons.emplace_back("exact scratch requirement exceeds the ram limit");
        }
        else if (reason == "acc_width")
        {
            reasons.emplace_back("proven accumulator range exceeds the selected width");
        }
        else if (reason == "alias")
        {
            reasons.emplace_back("the direct-output schedule is not alias safe");
        }
        else if (reason == "size_t")
        {
            reasons.emplace_back("an emitted object exceeds the target address range");
        }
        else
        {
            reasons.emplace_back("the existing legality checker rejected the mapping");
        }
    }
    return reasons;
}

[[nodiscard]] compiler_plan make_lowerable_plan(const request &req, const candidate_trial &trial)
{
    compiler_plan result;
    result.tree.degree = req.n;
    result.tree.recursion_depth = 0;
    result.tree.leaf_size = req.n;
    result.accumulator_bits = trial.analysis.plan.acc_bits;
    result.range = exact_range(req, trial.analysis);
    result.scratch = {
        trial.analysis.temporary_bytes,
        trial.analysis.temporary_bytes / (trial.analysis.plan.acc_bits / 8),
        true,
    };
    result.has_schoolbook_lowering = true;
    result.schoolbook_lowering = trial.analysis.plan;
    result.legal = trial.analysis.legal;
    result.emit_supported = true;
    result.legality_reasons = lowerable_legality_reasons(trial.analysis);

    switch (trial.analysis.plan.sched)
    {
        case schedule::full:
            result.tree.family = algorithm_family::schoolbook;
            result.reduction = reduction_placement::after_convolution;
            result.memory = memory_schedule::full_product;
            result.support_reasons.emplace_back("existing full schoolbook lowering is registered");
            break;
        case schedule::fold:
            result.tree.family = algorithm_family::blocked;
            result.tree.block_size = trial.analysis.plan.block;
            result.reduction = reduction_placement::after_convolution;
            result.memory = memory_schedule::ring_accumulator;
            result.support_reasons.emplace_back(
                "existing blocked ring-accumulator lowering is registered");
            break;
        case schedule::output:
            result.tree.family = algorithm_family::schoolbook;
            result.reduction = reduction_placement::per_output;
            result.memory = memory_schedule::direct_output;
            result.support_reasons.emplace_back(
                "existing direct-output schoolbook lowering is registered");
            break;
    }
    return result;
}

[[nodiscard]] std::string unavailable_support_reason(algorithm_family family)
{
    return "no verified " + std::string(algorithm_family_name(family)) + " lowering is registered";
}

[[nodiscard]] compiler_plan make_capability_blocked_plan(const request &req, algorithm_tree tree,
                                                         reduction_placement reduction,
                                                         memory_schedule memory,
                                                         wide_uint range_growth,
                                                         wide_uint scratch_factor)
{
    compiler_plan result;
    result.tree = std::move(tree);
    result.reduction = reduction;
    result.memory = memory;
    result.accumulator_bits = req.target.acc_bits.front();
    result.range = provisional_range(req, range_growth);
    result.scratch = provisional_scratch(req, result.accumulator_bits, scratch_factor);
    result.legal = false;
    result.emit_supported = false;

    if (result.tree.family == algorithm_family::ntt)
    {
        result.legality_reasons.emplace_back(
            "native-root and transform range proofs are not implemented");
    }
    else if (result.tree.family == algorithm_family::ntt_crt)
    {
        result.legality_reasons.emplace_back(
            "auxiliary-modulus and crt range proofs are not implemented");
    }
    else
    {
        result.legality_reasons.emplace_back(
            "recursive recombination range proofs are not implemented");
    }
    result.legality_reasons.emplace_back("the scratch estimate is provisional");
    if (result.scratch.temporary_bytes > req.limits.ram)
    {
        result.legality_reasons.emplace_back(
            "the provisional scratch estimate exceeds the ram limit");
    }
    if (req.alias == aliasing::may)
    {
        result.legality_reasons.emplace_back("alias safety has not been verified");
    }
    result.support_reasons.push_back(unavailable_support_reason(result.tree.family));
    return result;
}

[[nodiscard]] std::size_t expected_branch_count(algorithm_family family) noexcept
{
    switch (family)
    {
        case algorithm_family::karatsuba:
        case algorithm_family::mixed_karatsuba:
            return 3;
        case algorithm_family::toom_cook:
        case algorithm_family::hybrid:
            return 5;
        case algorithm_family::schoolbook:
        case algorithm_family::blocked:
        case algorithm_family::ntt:
        case algorithm_family::ntt_crt:
            return 0;
    }
    return 0;
}

[[nodiscard]] bool known_family(algorithm_family family) noexcept
{
    switch (family)
    {
        case algorithm_family::schoolbook:
        case algorithm_family::blocked:
        case algorithm_family::karatsuba:
        case algorithm_family::mixed_karatsuba:
        case algorithm_family::toom_cook:
        case algorithm_family::hybrid:
        case algorithm_family::ntt:
        case algorithm_family::ntt_crt:
            return true;
    }
    return false;
}

[[nodiscard]] std::uint64_t expected_child_degree(const algorithm_tree &tree) noexcept
{
    if (tree.family == algorithm_family::karatsuba ||
        tree.family == algorithm_family::mixed_karatsuba)
    {
        return tree.degree / 2 + tree.degree % 2;
    }
    return tree.degree / 3 + (tree.degree % 3 != 0);
}

[[nodiscard]] bool child_family_matches(algorithm_family parent, algorithm_family child) noexcept
{
    const bool leaf = child == algorithm_family::schoolbook || child == algorithm_family::blocked;
    switch (parent)
    {
        case algorithm_family::karatsuba:
            return leaf || child == algorithm_family::karatsuba;
        case algorithm_family::mixed_karatsuba:
            return leaf || child == algorithm_family::karatsuba;
        case algorithm_family::toom_cook:
            return leaf || child == algorithm_family::toom_cook;
        case algorithm_family::hybrid:
            return leaf || child == algorithm_family::karatsuba ||
                   child == algorithm_family::toom_cook;
        case algorithm_family::schoolbook:
        case algorithm_family::blocked:
        case algorithm_family::ntt:
        case algorithm_family::ntt_crt:
            return false;
    }
    return false;
}

[[nodiscard]] tree_metrics check_tree(const algorithm_tree &tree, std::vector<std::string> &errors,
                                      std::size_t nesting, std::size_t &nodes)
{
    ++nodes;
    if (nodes > 4096)
    {
        errors.emplace_back("tree has too many nodes");
        return {};
    }
    if (nesting > 32)
    {
        errors.emplace_back("tree recursion is too deep");
        return {};
    }
    if (tree.degree == 0)
    {
        errors.emplace_back("tree contains a zero-degree node");
    }
    if (!known_family(tree.family))
    {
        errors.emplace_back("tree contains an unknown algorithm family");
    }

    const std::size_t branch_count = expected_branch_count(tree.family);
    if (tree.branches.size() != branch_count)
    {
        errors.emplace_back("tree has the wrong branch count");
    }

    if (branch_count == 0)
    {
        if (tree.recursion_depth != 0)
        {
            errors.emplace_back("terminal node has a recursion depth");
        }
        if (tree.leaf_size != tree.degree)
        {
            errors.emplace_back("terminal node has the wrong leaf size");
        }
        if (tree.family == algorithm_family::blocked)
        {
            if (tree.block_size == 0 || tree.block_size > tree.degree)
            {
                errors.emplace_back("blocked leaf has an invalid block size");
            }
        }
        else if (tree.block_size != 0)
        {
            errors.emplace_back("non-blocked node has a block size");
        }
        return {0, tree.degree};
    }

    if (tree.block_size != 0)
    {
        errors.emplace_back("recursive node has a block size");
    }

    const std::uint64_t child_degree = expected_child_degree(tree);
    std::uint16_t maximum_depth = 0;
    std::uint64_t largest_leaf = 0;
    algorithm_family first_family = algorithm_family::schoolbook;
    bool different_families = false;
    for (std::size_t index = 0; index < tree.branches.size(); ++index)
    {
        const algorithm_tree &branch = tree.branches[index];
        if (branch.degree != child_degree)
        {
            errors.emplace_back("tree branch has the wrong degree");
        }
        if (!child_family_matches(tree.family, branch.family))
        {
            errors.emplace_back("tree branch uses an invalid algorithm family");
        }
        if (index == 0)
        {
            first_family = branch.family;
        }
        else if (branch.family != first_family)
        {
            different_families = true;
        }

        const tree_metrics metrics = check_tree(branch, errors, nesting + 1, nodes);
        maximum_depth = std::max(maximum_depth, metrics.depth);
        largest_leaf = std::max(largest_leaf, metrics.largest_leaf);
    }

    if ((tree.family == algorithm_family::mixed_karatsuba ||
         tree.family == algorithm_family::hybrid) &&
        !different_families)
    {
        errors.emplace_back("mixed tree does not contain different branch strategies");
    }

    const std::uint16_t depth = static_cast<std::uint16_t>(maximum_depth + 1);
    if (tree.recursion_depth != depth)
    {
        errors.emplace_back("tree recursion depth is inconsistent");
    }
    if (tree.leaf_size != largest_leaf)
    {
        errors.emplace_back("tree leaf size is inconsistent");
    }
    return {depth, largest_leaf};
}

[[nodiscard]] bool independent_width_fits(wide_uint bound, std::uint16_t bits) noexcept
{
    if (bits >= 1 && bits <= 127)
    {
        return bound < (wide_uint{1} << (bits - 1));
    }
    if (bits == 128)
    {
        return bound <= ((wide_uint{1} << 127) - 1);
    }
    return false;
}

[[nodiscard]] std::uint16_t independent_required_bits(wide_uint bound) noexcept
{
    if (bound == 0)
    {
        return 1;
    }
    const std::uint64_t high = static_cast<std::uint64_t>(bound >> 64);
    if (high != 0)
    {
        return static_cast<std::uint16_t>(65 + std::bit_width(high));
    }
    return static_cast<std::uint16_t>(1 + std::bit_width(static_cast<std::uint64_t>(bound)));
}

[[nodiscard]] bool independent_size_fits(const request &req,
                                         const schoolbook_plan &lowering) noexcept
{
    const wide_uint size_maximum = req.target.size_bits == 32
                                       ? wide_uint{std::numeric_limits<std::uint32_t>::max()}
                                       : wide_uint{std::numeric_limits<std::uint64_t>::max()};
    const wide_uint object_maximum =
        req.target.size_bits == 32
            ? wide_uint{static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())}
            : wide_uint{static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())};
    const wide_uint count = req.n;
    if (count > size_maximum / 4 || count > object_maximum / 4)
    {
        return false;
    }

    const wide_uint bytes = lowering.acc_bits / 8;
    if (bytes == 0)
    {
        return false;
    }
    if (lowering.sched == schedule::full)
    {
        const wide_uint coefficients = 2 * count - 1;
        return coefficients <= size_maximum / bytes && coefficients <= object_maximum / bytes;
    }
    if (lowering.sched == schedule::fold)
    {
        return count <= size_maximum / bytes && count <= object_maximum / bytes;
    }
    return true;
}

[[nodiscard]] wide_uint independent_scratch(const request &req,
                                            const schoolbook_plan &lowering) noexcept
{
    const wide_uint bytes = lowering.acc_bits / 8;
    if (lowering.sched == schedule::full)
    {
        return (2 * wide_uint{req.n} - 1) * bytes;
    }
    if (lowering.sched == schedule::fold)
    {
        return wide_uint{req.n} * bytes;
    }
    return 0;
}

[[nodiscard]] bool independent_lowering_legal(const request &req,
                                              const schoolbook_plan &lowering) noexcept
{
    const wide_uint scratch = independent_scratch(req, lowering);
    const wide_uint magnitude =
        req.input == input_representation::canonical ? wide_uint{req.q - 1} : wide_uint{req.q / 2};
    const wide_uint bound = wide_uint{req.n} * magnitude * magnitude;
    const bool alias_safe = lowering.sched != schedule::output;
    return scratch <= req.limits.ram && independent_width_fits(bound, lowering.acc_bits) &&
           (req.alias == aliasing::no || alias_safe) && independent_size_fits(req, lowering);
}

[[nodiscard]] wide_uint provisional_range_growth(const compiler_plan &plan) noexcept
{
    switch (plan.tree.family)
    {
        case algorithm_family::karatsuba:
            return wide_uint{1} << std::min<std::uint16_t>(plan.tree.recursion_depth, 16);
        case algorithm_family::mixed_karatsuba:
            return 4;
        case algorithm_family::toom_cook:
            return 9;
        case algorithm_family::hybrid:
            return 12;
        case algorithm_family::ntt:
            return 2;
        case algorithm_family::ntt_crt:
            return 4;
        case algorithm_family::schoolbook:
        case algorithm_family::blocked:
            return 1;
    }
    return 1;
}

[[nodiscard]] wide_uint provisional_scratch_factor(const compiler_plan &plan) noexcept
{
    switch (plan.tree.family)
    {
        case algorithm_family::karatsuba:
            return 2 + plan.tree.recursion_depth;
        case algorithm_family::mixed_karatsuba:
            return 4;
        case algorithm_family::toom_cook:
            return 6;
        case algorithm_family::hybrid:
            return 6;
        case algorithm_family::ntt:
            return 2;
        case algorithm_family::ntt_crt:
            return 6;
        case algorithm_family::schoolbook:
        case algorithm_family::blocked:
            return 0;
    }
    return 0;
}

[[nodiscard]] bool provisional_schedule_matches(const compiler_plan &plan) noexcept
{
    switch (plan.tree.family)
    {
        case algorithm_family::karatsuba:
            return plan.memory == memory_schedule::reuse_branches &&
                   ((plan.tree.recursion_depth == 1 &&
                     plan.reduction == reduction_placement::after_leaf) ||
                    (plan.tree.recursion_depth > 1 &&
                     plan.reduction == reduction_placement::after_recursion_level));
        case algorithm_family::mixed_karatsuba:
            return plan.reduction == reduction_placement::after_recursion_level &&
                   plan.memory == memory_schedule::recompute;
        case algorithm_family::toom_cook:
            return plan.reduction == reduction_placement::after_leaf &&
                   plan.memory == memory_schedule::separate_branches;
        case algorithm_family::hybrid:
            return plan.reduction == reduction_placement::after_recursion_level &&
                   plan.memory == memory_schedule::reuse_branches;
        case algorithm_family::ntt:
            return plan.reduction == reduction_placement::after_transform &&
                   plan.memory == memory_schedule::in_place_transform;
        case algorithm_family::ntt_crt:
            return plan.reduction == reduction_placement::after_crt &&
                   plan.memory == memory_schedule::crt_streaming;
        case algorithm_family::schoolbook:
        case algorithm_family::blocked:
            return false;
    }
    return false;
}

void append_indent(std::string &output, std::size_t count)
{
    output.append(count, ' ');
}

void append_json_string(std::string &output, std::string_view value)
{
    constexpr char hex[] = "0123456789abcdef";
    output.push_back('"');
    for (const char character : value)
    {
        const auto byte = static_cast<unsigned char>(character);

        switch (byte)
        {
            case '"':
                output += "\\\"";
                break;
            case '\\':
                output += "\\\\";
                break;
            case '\b':
                output += "\\b";
                break;
            case '\f':
                output += "\\f";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                if (byte < 0x20)
                {
                    output += "\\u00";
                    output.push_back(hex[byte >> 4]);
                    output.push_back(hex[byte & 0x0f]);
                }
                else
                {
                    output.push_back(static_cast<char>(byte));
                }
                break;
        }
    }
    output.push_back('"');
}

void append_string_array(std::string &output, const std::vector<std::string> &values,
                         std::size_t indent)
{
    if (values.empty())
    {
        output += "[]";
        return;
    }

    output += "[\n";
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        append_indent(output, indent + 2);
        append_json_string(output, values[index]);
        output += index + 1 == values.size() ? "\n" : ",\n";
    }
    append_indent(output, indent);
    output.push_back(']');
}

void append_tree_json(std::string &output, const algorithm_tree &tree, std::size_t indent)
{
    output += "{\n";
    append_indent(output, indent + 2);
    output += "\"family\": ";
    append_json_string(output, algorithm_family_name(tree.family));
    output += ",\n";
    append_indent(output, indent + 2);
    output += "\"degree\": " + std::to_string(tree.degree) + ",\n";
    append_indent(output, indent + 2);
    output += "\"recursion_depth\": " + std::to_string(tree.recursion_depth) + ",\n";
    append_indent(output, indent + 2);
    output += "\"leaf_size\": " + std::to_string(tree.leaf_size) + ",\n";
    append_indent(output, indent + 2);
    output += "\"block_size\": " + std::to_string(tree.block_size) + ",\n";
    append_indent(output, indent + 2);
    output += "\"branches\": [";
    if (!tree.branches.empty())
    {
        output.push_back('\n');
        for (std::size_t index = 0; index < tree.branches.size(); ++index)
        {
            append_indent(output, indent + 4);
            append_tree_json(output, tree.branches[index], indent + 4);
            output += index + 1 == tree.branches.size() ? "\n" : ",\n";
        }
        append_indent(output, indent + 2);
    }
    output += "]\n";
    append_indent(output, indent);
    output.push_back('}');
}

void append_compiler_plan_json(std::string &output, const compiler_plan &plan, std::size_t indent)
{
    output += "{\n";
    append_indent(output, indent + 2);
    output += "\"id\": ";
    append_json_string(output, compiler_plan_id(plan));
    output += ",\n";
    append_indent(output, indent + 2);
    output += "\"tree\": ";
    append_tree_json(output, plan.tree, indent + 2);
    output += ",\n";
    append_indent(output, indent + 2);
    output += "\"reduction\": ";
    append_json_string(output, reduction_placement_name(plan.reduction));
    output += ",\n";
    append_indent(output, indent + 2);
    output += "\"memory\": ";
    append_json_string(output, memory_schedule_name(plan.memory));
    output += ",\n";
    append_indent(output, indent + 2);
    output += "\"acc_bits\": " + std::to_string(plan.accumulator_bits) + ",\n";
    append_indent(output, indent + 2);
    output += "\"range\": {\n";
    append_indent(output, indent + 4);
    output += "\"input_magnitude\": " + wide_to_string(plan.range.input_magnitude) + ",\n";
    append_indent(output, indent + 4);
    output += "\"product_bound\": " + wide_to_string(plan.range.product_bound) + ",\n";
    append_indent(output, indent + 4);
    output +=
        "\"output_accumulator_bound\": " + wide_to_string(plan.range.output_accumulator_bound) +
        ",\n";
    append_indent(output, indent + 4);
    output += "\"peak_intermediate_bound\": " + wide_to_string(plan.range.peak_intermediate_bound) +
              ",\n";
    append_indent(output, indent + 4);
    output += "\"required_bits\": " + std::to_string(plan.range.required_bits) + ",\n";
    append_indent(output, indent + 4);
    output += std::string{"\"proven\": "} + (plan.range.proven ? "true,\n" : "false,\n");
    append_indent(output, indent + 4);
    output += std::string{"\"saturated\": "} + (plan.range.saturated ? "true\n" : "false\n");
    append_indent(output, indent + 2);
    output += "},\n";
    append_indent(output, indent + 2);
    output += "\"scratch\": {\n";
    append_indent(output, indent + 4);
    output += "\"temporary_bytes\": " + wide_to_string(plan.scratch.temporary_bytes) + ",\n";
    append_indent(output, indent + 4);
    output += "\"peak_live_coefficients\": " + wide_to_string(plan.scratch.peak_live_coefficients) +
              ",\n";
    append_indent(output, indent + 4);
    output += std::string{"\"exact\": "} + (plan.scratch.exact ? "true\n" : "false\n");
    append_indent(output, indent + 2);
    output += "},\n";
    append_indent(output, indent + 2);
    output += std::string{"\"legal\": "} + (plan.legal ? "true,\n" : "false,\n");
    append_indent(output, indent + 2);
    output += std::string{"\"emit_supported\": "} + (plan.emit_supported ? "true,\n" : "false,\n");
    append_indent(output, indent + 2);
    output += "\"legality_reasons\": ";
    append_string_array(output, plan.legality_reasons, indent + 2);
    output += ",\n";
    append_indent(output, indent + 2);
    output += "\"support_reasons\": ";
    append_string_array(output, plan.support_reasons, indent + 2);
    output += ",\n";
    append_indent(output, indent + 2);
    output += "\"schoolbook_lowering\": ";
    if (plan.has_schoolbook_lowering)
    {
        output += "{\n";
        append_indent(output, indent + 4);
        output += "\"schedule\": ";
        append_json_string(output, schedule_name(plan.schoolbook_lowering.sched));
        output += ",\n";
        append_indent(output, indent + 4);
        output += "\"acc_bits\": " + std::to_string(plan.schoolbook_lowering.acc_bits) + ",\n";
        append_indent(output, indent + 4);
        output += "\"block\": " + std::to_string(plan.schoolbook_lowering.block) + "\n";
        append_indent(output, indent + 2);
        output.push_back('}');
    }
    else
    {
        output += "null";
    }
    output.push_back('\n');
    append_indent(output, indent);
    output.push_back('}');
}

}

std::string_view algorithm_family_name(algorithm_family value) noexcept
{
    switch (value)
    {
        case algorithm_family::schoolbook:
            return "schoolbook";
        case algorithm_family::blocked:
            return "blocked";
        case algorithm_family::karatsuba:
            return "karatsuba";
        case algorithm_family::mixed_karatsuba:
            return "mixed_karatsuba";
        case algorithm_family::toom_cook:
            return "toom_cook";
        case algorithm_family::hybrid:
            return "hybrid";
        case algorithm_family::ntt:
            return "ntt";
        case algorithm_family::ntt_crt:
            return "ntt_crt";
    }
    return "unknown";
}

std::string_view reduction_placement_name(reduction_placement value) noexcept
{
    switch (value)
    {
        case reduction_placement::after_convolution:
            return "after_convolution";
        case reduction_placement::per_output:
            return "per_output";
        case reduction_placement::after_leaf:
            return "after_leaf";
        case reduction_placement::after_recursion_level:
            return "after_recursion_level";
        case reduction_placement::after_transform:
            return "after_transform";
        case reduction_placement::after_crt:
            return "after_crt";
    }
    return "unknown";
}

std::string_view memory_schedule_name(memory_schedule value) noexcept
{
    switch (value)
    {
        case memory_schedule::full_product:
            return "full_product";
        case memory_schedule::ring_accumulator:
            return "ring_accumulator";
        case memory_schedule::direct_output:
            return "direct_output";
        case memory_schedule::reuse_branches:
            return "reuse_branches";
        case memory_schedule::separate_branches:
            return "separate_branches";
        case memory_schedule::recompute:
            return "recompute";
        case memory_schedule::in_place_transform:
            return "in_place_transform";
        case memory_schedule::crt_streaming:
            return "crt_streaming";
    }
    return "unknown";
}

std::string compiler_plan_id(const compiler_plan &plan)
{
    if (plan.has_schoolbook_lowering)
    {
        return plan_id(plan.schoolbook_lowering);
    }

    std::string result{algorithm_family_name(plan.tree.family)};
    result += "_d" + std::to_string(plan.tree.recursion_depth);
    result += "_l" + std::to_string(plan.tree.leaf_size);
    result += "_" + std::string(reduction_placement_name(plan.reduction));
    result += "_" + std::string(memory_schedule_name(plan.memory));
    result += "_i" + std::to_string(plan.accumulator_bits);
    return result;
}

bool compiler_plan_ready(const compiler_plan &plan) noexcept
{
    return plan.legal && plan.emit_supported && plan.has_schoolbook_lowering;
}

std::vector<compiler_plan> enumerate_compiler_plans(const request &req)
{
    validate_request(req);
    const std::vector<candidate_trial> lowerable = find(req);
    std::vector<compiler_plan> plans;
    plans.reserve(lowerable.size() + 7);
    for (const candidate_trial &trial : lowerable)
    {
        plans.push_back(make_lowerable_plan(req, trial));
    }

    plans.push_back(make_capability_blocked_plan(req, make_karatsuba_tree(req.n, 1),
                                                 reduction_placement::after_leaf,
                                                 memory_schedule::reuse_branches, 2, 3));
    plans.push_back(make_capability_blocked_plan(req, make_karatsuba_tree(req.n, 2),
                                                 reduction_placement::after_recursion_level,
                                                 memory_schedule::reuse_branches, 4, 4));
    plans.push_back(make_capability_blocked_plan(req, make_mixed_karatsuba_tree(req.n),
                                                 reduction_placement::after_recursion_level,
                                                 memory_schedule::recompute, 4, 4));
    plans.push_back(make_capability_blocked_plan(req, make_toom_tree(req.n),
                                                 reduction_placement::after_leaf,
                                                 memory_schedule::separate_branches, 9, 6));
    plans.push_back(make_capability_blocked_plan(req, make_hybrid_tree(req.n),
                                                 reduction_placement::after_recursion_level,
                                                 memory_schedule::reuse_branches, 12, 6));

    algorithm_tree ntt_tree{
        algorithm_family::ntt, req.n, 0, req.n, 0, {},
    };
    plans.push_back(make_capability_blocked_plan(req, std::move(ntt_tree),
                                                 reduction_placement::after_transform,
                                                 memory_schedule::in_place_transform, 2, 2));

    algorithm_tree crt_tree{
        algorithm_family::ntt_crt, req.n, 0, req.n, 0, {},
    };
    plans.push_back(make_capability_blocked_plan(req, std::move(crt_tree),
                                                 reduction_placement::after_crt,
                                                 memory_schedule::crt_streaming, 4, 6));
    return plans;
}

std::vector<std::string> check_compiler_plan(const request &req, const compiler_plan &plan)
{
    validate_request(req);
    std::vector<std::string> errors;
    if (plan.tree.degree != req.n)
    {
        errors.emplace_back("root degree does not match the request");
    }
    std::size_t nodes = 0;
    static_cast<void>(check_tree(plan.tree, errors, 0, nodes));

    if (plan.legality_reasons.empty())
    {
        errors.emplace_back("plan has no legality explanation");
    }
    if (plan.support_reasons.empty())
    {
        errors.emplace_back("plan has no support explanation");
    }
    if (compiler_plan_ready(plan) !=
        (plan.legal && plan.emit_supported && plan.has_schoolbook_lowering))
    {
        errors.emplace_back("ready state is inconsistent");
    }

    const wide_uint magnitude = input_bound(req);
    const wide_uint product = wide_uint{magnitude} * magnitude;
    const wide_uint output_bound = wide_uint{req.n} * product;
    if (plan.range.input_magnitude != magnitude || plan.range.product_bound != product ||
        plan.range.output_accumulator_bound != output_bound)
    {
        errors.emplace_back("range baseline is inconsistent");
    }

    if (plan.has_schoolbook_lowering)
    {
        const schoolbook_plan &lowering = plan.schoolbook_lowering;
        const bool supported_family = plan.tree.family == algorithm_family::schoolbook ||
                                      plan.tree.family == algorithm_family::blocked;
        if (!supported_family || !plan.emit_supported)
        {
            errors.emplace_back("schoolbook lowering has an invalid support state");
        }
        if (plan.accumulator_bits != lowering.acc_bits)
        {
            errors.emplace_back("lowering accumulator width is inconsistent");
        }
        if (std::find(req.target.acc_bits.begin(), req.target.acc_bits.end(), lowering.acc_bits) ==
            req.target.acc_bits.end())
        {
            errors.emplace_back("lowering uses an unavailable accumulator type");
        }

        bool known_lowering_schedule = true;
        reduction_placement expected_reduction = reduction_placement::after_convolution;
        memory_schedule expected_memory = memory_schedule::full_product;
        algorithm_family expected_family = algorithm_family::schoolbook;
        std::uint64_t expected_block = 0;
        switch (lowering.sched)
        {
            case schedule::full:
                break;
            case schedule::fold:
                expected_family = algorithm_family::blocked;
                expected_memory = memory_schedule::ring_accumulator;
                expected_block = lowering.block;
                break;
            case schedule::output:
                expected_reduction = reduction_placement::per_output;
                expected_memory = memory_schedule::direct_output;
                break;
            default:
                known_lowering_schedule = false;
                errors.emplace_back("lowering uses an unknown schedule");
                break;
        }
        if (!known_lowering_schedule || plan.tree.family != expected_family ||
            plan.tree.block_size != expected_block || plan.reduction != expected_reduction ||
            plan.memory != expected_memory)
        {
            errors.emplace_back("schoolbook lowering mapping is inconsistent");
        }

        const wide_uint scratch = independent_scratch(req, lowering);
        if (!plan.range.proven || plan.range.saturated ||
            plan.range.peak_intermediate_bound != output_bound ||
            plan.range.required_bits != independent_required_bits(output_bound))
        {
            errors.emplace_back("schoolbook range proof is inconsistent");
        }
        const wide_uint accumulator_bytes = lowering.acc_bits / 8;
        if (!plan.scratch.exact || accumulator_bytes == 0 ||
            plan.scratch.temporary_bytes != scratch ||
            (accumulator_bytes != 0 &&
             plan.scratch.peak_live_coefficients != scratch / accumulator_bytes))
        {
            errors.emplace_back("schoolbook scratch proof is inconsistent");
        }
        if (plan.legal != independent_lowering_legal(req, lowering))
        {
            errors.emplace_back("schoolbook legality is inconsistent");
        }
    }
    else
    {
        if (plan.tree.family == algorithm_family::schoolbook ||
            plan.tree.family == algorithm_family::blocked)
        {
            errors.emplace_back("lowerable family is missing its mapping");
        }
        if (plan.emit_supported || plan.legal)
        {
            errors.emplace_back("capability-blocked plan is marked runnable");
        }
        if (plan.range.proven || plan.scratch.exact)
        {
            errors.emplace_back("provisional analysis is marked exact");
        }
        if (plan.accumulator_bits != req.target.acc_bits.front())
        {
            errors.emplace_back("provisional plan uses a noncanonical accumulator width");
        }
        if (!provisional_schedule_matches(plan))
        {
            errors.emplace_back("provisional reduction or memory schedule is inconsistent");
        }

        bool saturated = false;
        const wide_uint expected_peak =
            saturating_multiply(output_bound, provisional_range_growth(plan), saturated);
        const wide_uint coefficient_count = wide_uint{req.n} * provisional_scratch_factor(plan);
        const wide_uint expected_scratch = coefficient_count * (plan.accumulator_bits / 8);
        if (plan.range.peak_intermediate_bound != expected_peak ||
            plan.range.required_bits != independent_required_bits(expected_peak) ||
            plan.range.saturated != saturated)
        {
            errors.emplace_back("provisional range estimate is inconsistent");
        }
        if (plan.scratch.temporary_bytes != expected_scratch ||
            plan.scratch.peak_live_coefficients != coefficient_count)
        {
            errors.emplace_back("provisional scratch estimate is inconsistent");
        }
    }
    return errors;
}

std::string compiler_plan_to_json(const compiler_plan &plan)
{
    std::string output;
    output.reserve(2048);
    append_compiler_plan_json(output, plan, 0);
    output.push_back('\n');
    return output;
}

std::string compiler_plans_to_json(std::span<const compiler_plan> plans)
{
    std::string output;
    output.reserve(plans.size() * 2048);
    output.push_back('[');
    if (!plans.empty())
    {
        output.push_back('\n');
        for (std::size_t index = 0; index < plans.size(); ++index)
        {
            append_indent(output, 2);
            append_compiler_plan_json(output, plans[index], 2);
            output += index + 1 == plans.size() ? "\n" : ",\n";
        }
    }
    output += "]\n";
    return output;
}

}
