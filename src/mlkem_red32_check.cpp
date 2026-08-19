#include "pqc_poly/mlkem_red32.hpp"

#include <algorithm>

namespace pqc_poly
{
namespace
{

void add_once(std::vector<std::string> &out, std::string_view value)
{
    if (std::find(out.begin(), out.end(), value) == out.end())
    {
        out.emplace_back(value);
    }
}

[[nodiscard]] unsigned checked_k(mlkem_level value) noexcept
{
    switch (value)
    {
        case mlkem_level::mlkem512:
            return 2U;
        case mlkem_level::mlkem768:
            return 3U;
        case mlkem_level::mlkem1024:
            return 4U;
    }
    return 0U;
}

[[nodiscard]] std::string checked_id(const red32_plan &plan)
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
    out += "_xred32";
    return out;
}

void check_forward(std::vector<std::string> &out, std::span<const mlkem_record> records)
{
    if (records.size() != 127U)
    {
        add_once(out, records.size() < 127U ? "missing_butterfly" : "duplicate_butterfly");
    }
    std::size_t index = 0;
    for (unsigned layer = 1; layer <= 7; ++layer)
    {
        const unsigned length = 256U >> layer;
        const unsigned blocks = 1U << (layer - 1U);
        for (unsigned block = 0; block < blocks; ++block)
        {
            if (index >= records.size())
            {
                return;
            }
            const unsigned left = block * 2U * length;
            const mlkem_record &record = records[index++];
            if (record.layer != layer || record.block != block ||
                record.zeta_index != blocks + block)
            {
                add_once(out, "bad_twiddle_schedule");
            }
            if (record.left_base != left || record.right_base != left + length ||
                record.length != length || record.right_base >= 256U)
            {
                add_once(out, "array_index");
            }
        }
    }
}

void check_inverse(std::vector<std::string> &out, std::span<const mlkem_record> records)
{
    if (records.size() != 127U)
    {
        add_once(out, records.size() < 127U ? "missing_butterfly" : "duplicate_butterfly");
    }
    std::size_t index = 0;
    for (unsigned layer = 7; layer != 0; --layer)
    {
        const unsigned length = 256U >> layer;
        const unsigned blocks = 1U << (layer - 1U);
        for (unsigned block = 0; block < blocks; ++block)
        {
            if (index >= records.size())
            {
                return;
            }
            const unsigned left = block * 2U * length;
            const mlkem_record &record = records[index++];
            if (record.layer != layer || record.block != block ||
                record.zeta_index != (1U << layer) - 1U - block)
            {
                add_once(out, "bad_twiddle_schedule");
            }
            if (record.left_base != left || record.right_base != left + length ||
                record.length != length || record.right_base >= 256U)
            {
                add_once(out, "array_index");
            }
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

void check_forward_grouping(std::vector<std::string> &out,
                            std::span<const mlkem_record> records,
                            ntt_traversal traversal)
{
    if (traversal != ntt_traversal::fuse_two_layers)
    {
        return;
    }
    for (const unsigned layer : {1U, 3U, 5U})
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

void check_inverse_grouping(std::vector<std::string> &out,
                            std::span<const mlkem_record> records,
                            intt_traversal traversal)
{
    if (traversal != intt_traversal::fuse_two_layers)
    {
        return;
    }
    for (const unsigned child_layer : {7U, 5U, 3U})
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

std::vector<std::string> check_red32_candidate(const mlkem_request &request,
                                               const red32_candidate &candidate)
{
    std::vector<std::string> out;
    if (candidate.schema != red32_candidate_schema)
    {
        add_once(out, "bad_schema");
    }

    const std::string id = checked_id(candidate.plan);
    if (id.empty() || candidate.id != id)
    {
        add_once(out, "bad_plan_id");
    }

    check_forward(out, candidate.forward_records);
    check_inverse(out, candidate.inverse_records);
    check_forward_grouping(out, candidate.forward_records, candidate.plan.forward);
    check_inverse_grouping(out, candidate.inverse_records, candidate.plan.inverse);

    const unsigned k = checked_k(candidate.plan.level);
    if (k == 0U)
    {
        add_once(out, "level");
    }
    constexpr std::uint64_t montgomery_bound =
        (4096U * 32768U + 32768U * 3329U + 65535U) / 65536U;
    const bool late = candidate.plan.basemul == basemul_schedule::cached_late32;
    const std::uint64_t accumulator =
        static_cast<std::uint64_t>(k) * 2U * (late ? 4096U * 32768U : montgomery_bound);
    const std::uint32_t cache =
        candidate.plan.basemul == basemul_schedule::direct_eager32 ? 0U : k * 128U;
    const std::uint32_t scratch =
        candidate.plan.basemul == basemul_schedule::direct_eager32 ? 0U : k * 256U;
    const std::uint32_t caller = static_cast<std::uint32_t>((2U * k + 1U) * 512U);

    if (candidate.forward_bound != 8U * 3329U ||
        candidate.inverse_lazy_bound != 4U * 3329U)
    {
        add_once(out, "coefficient_bound");
    }
    if (candidate.accumulator_bound != accumulator)
    {
        add_once(out, "accumulator_bound");
    }
    if (candidate.mulcache_coefficients != cache)
    {
        add_once(out, "mulcache_size");
    }
    if (candidate.scratch_bytes != scratch)
    {
        add_once(out, "scratch_size");
    }
    if (candidate.caller_workspace_bytes != caller)
    {
        add_once(out, "caller_workspace_size");
    }
    if (!candidate.ntt_in_place || !candidate.intt_in_place)
    {
        add_once(out, "alias");
    }
    if (!candidate.fixed_loop_structure)
    {
        add_once(out, "control_flow");
    }
    if (!candidate.full_domain_reduction || candidate.reduction_min != -34432 ||
        candidate.reduction_max != 34432)
    {
        add_once(out, "red32_domain");
    }
    if (!candidate.canonical_rs2_zero)
    {
        add_once(out, "red32_encoding");
    }
    if (!candidate.standard_mul_before_reduction)
    {
        add_once(out, "red32_mul");
    }

    std::vector<std::string> expected_rejections;
    if (k == 0U)
    {
        expected_rejections.emplace_back("level");
    }
    if (scratch > request.scratch_limit)
    {
        expected_rejections.emplace_back("scratch_limit");
    }
    if (caller > request.caller_workspace_limit)
    {
        expected_rejections.emplace_back("caller_workspace_limit");
    }
    if (candidate.rejections != expected_rejections ||
        candidate.legal != expected_rejections.empty())
    {
        add_once(out, "legality");
    }
    return out;
}

}
