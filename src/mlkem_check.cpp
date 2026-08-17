#include "pqc_poly/mlkem_plan.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace pqc_poly
{
namespace
{

using wide_int = __int128;

struct interval
{
    wide_int low;
    wide_int high;
};

[[nodiscard]] wide_int magnitude(wide_int value)
{
    return value < 0 ? -value : value;
}

[[nodiscard]] wide_int bound(interval value)
{
    return std::max(magnitude(value.low), magnitude(value.high));
}

[[nodiscard]] bool add_interval(interval left, interval right, interval &out)
{
    return !__builtin_add_overflow(left.low, right.low, &out.low) &&
           !__builtin_add_overflow(left.high, right.high, &out.high);
}

[[nodiscard]] bool multiply_interval(interval left, interval right, interval &out)
{
    std::array<wide_int, 4> products{};
    if (__builtin_mul_overflow(left.low, right.low, &products[0]) ||
        __builtin_mul_overflow(left.low, right.high, &products[1]) ||
        __builtin_mul_overflow(left.high, right.low, &products[2]) ||
        __builtin_mul_overflow(left.high, right.high, &products[3]))
    {
        return false;
    }
    const auto limits = std::minmax_element(products.begin(), products.end());
    out = {*limits.first, *limits.second};
    return true;
}

[[nodiscard]] bool montgomery_interval(interval input, interval &out)
{
    constexpr wide_int q = 3329;
    constexpr wide_int r = 65536;
    constexpr wide_int qinv = 62209;
    constexpr wide_int input_limit = static_cast<wide_int>(INT32_MAX) - 32768 * q;
    if (qinv >= r || input.low <= -input_limit || input.high >= input_limit)
    {
        return false;
    }
    const wide_int input_bound = bound(input);
    const wide_int output_bound = (input_bound + 32768 * q + r - 1) / r;
    out = {-output_bound, output_bound};
    return true;
}

[[nodiscard]] bool field_multiply_interval(interval left, interval right, interval &out)
{
    interval product{};
    return multiply_interval(left, right, product) && montgomery_interval(product, out) &&
           bound(out) < 3329;
}

[[nodiscard]] bool forward_intervals(wide_int &largest)
{
    constexpr wide_int q = 3329;
    const interval zeta{-q / 2, q / 2};
    interval coefficient{-(q - 1), q - 1};
    largest = bound(coefficient);
    for (unsigned layer = 0; layer < 7; ++layer)
    {
        interval product{};
        interval reduced{};
        interval next{};
        if (!multiply_interval(coefficient, zeta, product) ||
            !montgomery_interval(product, reduced) ||
            !add_interval(coefficient, {-reduced.high, reduced.high}, next))
        {
            return false;
        }
        coefficient = next;
        largest = std::max(largest, bound(coefficient));
    }
    return true;
}

[[nodiscard]] bool inverse_intervals(intt_sum_reduction reduction, wide_int &largest_lazy,
                                     bool &montgomery_ok, bool &barrett_ok)
{
    constexpr wide_int q = 3329;
    const interval zeta{-q / 2, q / 2};
    interval scaled_product{};
    interval coefficient{};
    if (!multiply_interval({-8 * q, 8 * q}, {1441, 1441}, scaled_product) ||
        !montgomery_interval(scaled_product, coefficient))
    {
        return false;
    }
    largest_lazy = 0;
    montgomery_ok = true;
    barrett_ok = true;
    for (unsigned length = 2; length <= 128; length <<= 1U)
    {
        interval sum{};
        interval difference{};
        interval product{};
        interval reduced_difference{};
        if (!add_interval(coefficient, coefficient, sum) ||
            !add_interval(coefficient, {-coefficient.high, -coefficient.low}, difference) ||
            !multiply_interval(difference, zeta, product) ||
            !montgomery_interval(product, reduced_difference))
        {
            montgomery_ok = false;
            return false;
        }
        largest_lazy = std::max(largest_lazy, bound(sum));
        const bool reduce_sum = reduction == intt_sum_reduction::every_layer || length == 4U ||
                                length == 16U || length == 64U || length == 128U;
        if (reduce_sum)
        {
            if (sum.low < INT16_MIN || sum.high > INT16_MAX || bound(sum) > 4 * q)
            {
                barrett_ok = false;
            }
            sum = {-(q - 1), q - 1};
        }
        const wide_int next_bound = std::max(bound(sum), bound(reduced_difference));
        if (next_bound > INT16_MAX)
        {
            barrett_ok = false;
        }
        coefficient = {-next_bound, next_bound};
    }
    return true;
}

[[nodiscard]] bool base_intervals(unsigned k, bool late, wide_int &accumulator, bool &montgomery_ok)
{
    const interval narrow{-4096, 4096};
    const interval coefficient{INT16_MIN, INT16_MAX};
    interval product{};
    if (!multiply_interval(narrow, coefficient, product))
    {
        return false;
    }
    if (late)
    {
        interval pair{};
        interval total{0, 0};
        if (!add_interval(product, product, pair))
        {
            return false;
        }
        for (unsigned lane = 0; lane < k; ++lane)
        {
            if (!add_interval(total, pair, total))
            {
                return false;
            }
        }
        accumulator = bound(total);
        interval ignored{};
        montgomery_ok = montgomery_interval(total, ignored);
        return true;
    }

    interval reduced{};
    montgomery_ok = montgomery_interval(product, reduced);
    if (!montgomery_ok)
    {
        return true;
    }
    interval total{0, 0};
    for (unsigned term = 0; term < 2U * k; ++term)
    {
        if (!add_interval(total, reduced, total))
        {
            return false;
        }
    }
    accumulator = bound(total);
    return true;
}

void add_once(std::vector<std::string> &out, std::string_view value)
{
    if (std::find(out.begin(), out.end(), value) == out.end())
    {
        out.emplace_back(value);
    }
}

[[nodiscard]] std::string checked_id(const mlkem_plan &plan)
{
    std::string out = "mlk";
    switch (plan.level)
    {
        case mlkem_level::mlkem512:
            out += "512";
            break;
        case mlkem_level::mlkem768:
            out += "768";
            break;
        case mlkem_level::mlkem1024:
            out += "1024";
            break;
        default:
            return {};
    }
    switch (plan.forward)
    {
        case ntt_traversal::stage_major:
            out += "_fstage";
            break;
        case ntt_traversal::fuse_two_layers:
            out += "_ffuse2";
            break;
        default:
            return {};
    }
    switch (plan.inverse)
    {
        case intt_traversal::stage_major:
            out += "_istage";
            break;
        case intt_traversal::fuse_two_layers:
            out += "_ifuse2";
            break;
        default:
            return {};
    }
    switch (plan.inverse_reduction)
    {
        case intt_sum_reduction::every_layer:
            out += "_reach";
            break;
        case intt_sum_reduction::after_layer_pair:
            out += "_rpair";
            break;
        default:
            return {};
    }
    switch (plan.basemul)
    {
        case basemul_schedule::cached_late32:
            out += "_bcachelate";
            break;
        case basemul_schedule::cached_eager32:
            out += "_bcacheeager";
            break;
        case basemul_schedule::direct_eager32:
            out += "_bdirecteager";
            break;
        default:
            return {};
    }
    switch (plan.instruction)
    {
        case mlkem_instruction::none:
            out += "_xnone";
            break;
        case mlkem_instruction::fqmul:
            out += "_xfqmul";
            break;
        default:
            return {};
    }
    return out;
}

[[nodiscard]] unsigned checked_k(mlkem_level level) noexcept
{
    switch (level)
    {
        case mlkem_level::mlkem512:
            return 2;
        case mlkem_level::mlkem768:
            return 3;
        case mlkem_level::mlkem1024:
            return 4;
        default:
            return 0;
    }
}

[[nodiscard]] std::vector<mlkem_record> checked_forward()
{
    std::vector<mlkem_record> out;
    for (unsigned layer = 1; layer < 8; ++layer)
    {
        const unsigned length = 256U / (1U << layer);
        const unsigned count = 256U / (2U * length);
        for (unsigned block = 0; block < count; ++block)
        {
            const unsigned left = block * 2U * length;
            out.push_back(
                {static_cast<std::uint16_t>(layer), static_cast<std::uint16_t>(block),
                 static_cast<std::uint16_t>(count + block), static_cast<std::uint16_t>(left),
                 static_cast<std::uint16_t>(left + length), static_cast<std::uint16_t>(length)});
        }
    }
    return out;
}

[[nodiscard]] std::vector<mlkem_record> checked_inverse()
{
    std::vector<mlkem_record> out;
    for (unsigned length = 2; length <= 128; length *= 2)
    {
        const unsigned layer = 8U;
        unsigned shift = 0;
        for (unsigned value = length; value > 1; value >>= 1U)
        {
            ++shift;
        }
        const unsigned logical_layer = layer - shift;
        const unsigned count = 256U / (2U * length);
        for (unsigned block = 0; block < count; ++block)
        {
            const unsigned left = block * 2U * length;
            out.push_back(
                {static_cast<std::uint16_t>(logical_layer), static_cast<std::uint16_t>(block),
                 static_cast<std::uint16_t>(2U * count - 1U - block),
                 static_cast<std::uint16_t>(left), static_cast<std::uint16_t>(left + length),
                 static_cast<std::uint16_t>(length)});
        }
    }
    return out;
}

void check_records(std::vector<std::string> &out, std::span<const mlkem_record> actual,
                   std::span<const mlkem_record> expected)
{
    if (actual.size() < expected.size())
    {
        add_once(out, "missing_butterfly");
    }
    if (actual.size() > expected.size())
    {
        add_once(out, "duplicate_butterfly");
    }
    const std::size_t count = std::min(actual.size(), expected.size());
    for (std::size_t i = 0; i < count; ++i)
    {
        if (actual[i].zeta_index != expected[i].zeta_index ||
            actual[i].layer != expected[i].layer || actual[i].block != expected[i].block)
        {
            add_once(out, "bad_twiddle_schedule");
        }
        if (actual[i].left_base != expected[i].left_base ||
            actual[i].right_base != expected[i].right_base ||
            actual[i].length != expected[i].length || actual[i].right_base >= 256 ||
            actual[i].left_base >= 256)
        {
            add_once(out, "array_index");
        }
    }
}

[[nodiscard]] const mlkem_record *record(std::span<const mlkem_record> records, unsigned layer,
                                         unsigned block)
{
    const auto found = std::find_if(records.begin(), records.end(),
                                    [layer, block](const mlkem_record &value)
                                    { return value.layer == layer && value.block == block; });
    return found == records.end() ? nullptr : &*found;
}

void check_forward_grouping(std::vector<std::string> &out, std::span<const mlkem_record> records,
                            ntt_traversal traversal)
{
    if (traversal != ntt_traversal::fuse_two_layers)
    {
        return;
    }
    for (unsigned layer : {1U, 3U, 5U})
    {
        const unsigned blocks = 1U << (layer - 1U);
        for (unsigned block = 0; block < blocks; ++block)
        {
            const mlkem_record *parent = record(records, layer, block);
            const mlkem_record *left = record(records, layer + 1U, 2U * block);
            const mlkem_record *right = record(records, layer + 1U, 2U * block + 1U);
            if (parent == nullptr || left == nullptr || right == nullptr ||
                parent->length != 2U * left->length || left->length != right->length ||
                parent->left_base != left->left_base || parent->right_base != right->left_base)
            {
                add_once(out, "bad_twiddle_schedule");
            }
        }
    }
}

void check_inverse_grouping(std::vector<std::string> &out, std::span<const mlkem_record> records,
                            intt_traversal traversal)
{
    if (traversal != intt_traversal::fuse_two_layers)
    {
        return;
    }
    for (unsigned child_layer : {7U, 5U, 3U})
    {
        const unsigned parent_layer = child_layer - 1U;
        const unsigned blocks = 1U << (parent_layer - 1U);
        for (unsigned block = 0; block < blocks; ++block)
        {
            const mlkem_record *parent = record(records, parent_layer, block);
            const mlkem_record *left = record(records, child_layer, 2U * block);
            const mlkem_record *right = record(records, child_layer, 2U * block + 1U);
            if (parent == nullptr || left == nullptr || right == nullptr ||
                parent->length != 2U * left->length || left->length != right->length ||
                parent->left_base != left->left_base || parent->right_base != right->left_base)
            {
                add_once(out, "bad_twiddle_schedule");
            }
        }
    }
}

}

std::vector<std::string> check_mlkem_plan(const mlkem_request &request,
                                          const mlkem_candidate &candidate)
{
    std::vector<std::string> out;
    if (candidate.schema != "pqc-poly-bench/mlkem-plan-v1")
    {
        add_once(out, "bad_schema");
    }

    const std::string id = checked_id(candidate.plan);
    if (id.empty() || candidate.id != id)
    {
        add_once(out, "bad_plan_id");
    }

    const std::vector<mlkem_record> forward = checked_forward();
    const std::vector<mlkem_record> inverse = checked_inverse();
    check_records(out, candidate.forward_records, forward);
    check_records(out, candidate.inverse_records, inverse);
    check_forward_grouping(out, candidate.forward_records, candidate.plan.forward);
    check_inverse_grouping(out, candidate.inverse_records, candidate.plan.inverse);

    constexpr std::uint32_t q = 3329;
    constexpr std::uint32_t forward_bound = 8U * q;
    constexpr std::uint32_t inverse_bound = 4U * q;
    wide_int largest_forward = 0;
    if (!forward_intervals(largest_forward))
    {
        add_once(out, "analysis_overflow");
    }
    if (candidate.forward_bound != forward_bound || largest_forward >= INT16_MAX ||
        largest_forward >= forward_bound)
    {
        add_once(out, "coefficient_storage_overflow");
    }
    wide_int largest_inverse = 0;
    bool inverse_montgomery = false;
    bool inverse_barrett = false;
    if (!inverse_intervals(candidate.plan.inverse_reduction, largest_inverse, inverse_montgomery,
                           inverse_barrett))
    {
        add_once(out, "analysis_overflow");
    }
    if (!inverse_montgomery)
    {
        add_once(out, "montgomery_input_range");
    }
    if (largest_inverse > INT16_MAX)
    {
        add_once(out, "coefficient_storage_overflow");
    }
    if (candidate.inverse_lazy_bound != inverse_bound || largest_inverse >= inverse_bound ||
        !inverse_barrett)
    {
        add_once(out, "barrett_input_range");
    }

    const unsigned k = checked_k(candidate.plan.level);
    const bool late = candidate.plan.basemul == basemul_schedule::cached_late32;
    wide_int accumulator = 0;
    bool base_montgomery = false;
    if (k == 0 || !base_intervals(k, late, accumulator, base_montgomery))
    {
        add_once(out, "analysis_overflow");
    }
    if (!base_montgomery)
    {
        add_once(out, "montgomery_input_range");
    }
    const wide_int accumulator_limit =
        late ? static_cast<wide_int>(INT32_MAX) : static_cast<wide_int>(INT16_MAX);
    if (accumulator >= accumulator_limit || accumulator < 0 ||
        static_cast<wide_int>(candidate.accumulator_bound) != accumulator)
    {
        add_once(out, "accumulator_overflow");
    }

    const bool direct = candidate.plan.basemul == basemul_schedule::direct_eager32;
    const std::uint32_t cache = direct ? 0U : k * 128U;
    const std::uint32_t scratch = cache * 2U;
    if (candidate.mulcache_coefficients != cache)
    {
        add_once(out, "array_index");
    }
    if (candidate.scratch_bytes != scratch || scratch > request.scratch_limit)
    {
        add_once(out, "scratch_limit");
    }
    const std::uint32_t caller = static_cast<std::uint32_t>((2U * k + 1U) * 512U);
    if (candidate.caller_workspace_bytes != caller || caller > request.caller_workspace_limit)
    {
        add_once(out, "caller_workspace_limit");
    }
    if (!candidate.ntt_in_place || !candidate.intt_in_place)
    {
        add_once(out, "alias");
    }
    if (!candidate.fixed_loop_structure)
    {
        add_once(out, "constant_time_structure");
    }

    std::vector<std::string> rejection;
    if (scratch > request.scratch_limit)
    {
        rejection.emplace_back("scratch_limit");
    }
    if (caller > request.caller_workspace_limit)
    {
        rejection.emplace_back("caller_workspace_limit");
    }
    if (candidate.plan.instruction == mlkem_instruction::fqmul)
    {
        interval result{};
        constexpr interval zeta{-1664, 1664};
        constexpr interval ntt_coefficient{-8 * 3329, 8 * 3329};
        constexpr interval base_coefficient{-4096, 4096};
        if (!field_multiply_interval(ntt_coefficient, zeta, result) ||
            !field_multiply_interval(ntt_coefficient, {1353, 1353}, result) ||
            !field_multiply_interval(base_coefficient, base_coefficient, result))
        {
            add_once(out, "fqmul_output_range");
        }
    }
    if (candidate.rejections != rejection || candidate.legal != rejection.empty())
    {
        for (const std::string &reason : rejection)
        {
            add_once(out, reason);
        }
        if (rejection.empty())
        {
            add_once(out, "analysis_overflow");
        }
    }
    return out;
}

}
