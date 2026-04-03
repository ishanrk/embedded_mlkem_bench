#include "pqc_poly/selector.hpp"

#include "json.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>
#include <tuple>
#include <utility>

namespace pqc_poly
{
namespace
{

using detail::append_indent;
using detail::append_json_string;

constexpr std::uint64_t max_coefficient_count = std::numeric_limits<std::uint64_t>::max() / 4;
constexpr wide_uint wide_max = ~wide_uint{0};

enum class json_kind
{
    object,
    array,
    string,
    number,
    boolean,
    null_value,
};

struct json_value
{
    json_kind kind{json_kind::null_value};
    std::string text{};
    std::vector<std::string> keys{};
    std::vector<json_value> values{};
};

class json_parser
{
public:
    explicit json_parser(std::string_view input) noexcept : input_(input)
    {
    }

    [[nodiscard]] json_value parse()
    {
        skip_space();
        json_value value = parse_value(0);
        skip_space();
        if (position_ != input_.size())
        {
            fail("trailing data");
        }
        return value;
    }

private:
    [[noreturn]] void fail(std::string_view message) const
    {
        throw spec_error("invalid request: " + std::string(message) + " at byte " +
                         std::to_string(position_));
    }

    void skip_space() noexcept
    {
        while (position_ < input_.size())
        {
            const char value = input_[position_];
            if (value != ' ' && value != '\n' && value != '\r' && value != '\t')
            {
                break;
            }
            ++position_;
        }
    }

    [[nodiscard]] bool consume(char wanted) noexcept
    {
        if (position_ < input_.size() && input_[position_] == wanted)
        {
            ++position_;
            return true;
        }
        return false;
    }

    [[nodiscard]] json_value parse_value(std::size_t depth)
    {
        if (depth > 64)
        {
            fail("nesting is too deep");
        }
        if (position_ == input_.size())
        {
            fail("expected a value");
        }

        switch (input_[position_])
        {
            case '{':
                return parse_object(depth);
            case '[':
                return parse_array(depth);
            case '"':
            {
                json_value value;
                value.kind = json_kind::string;
                value.text = parse_string();
                return value;
            }
            case 't':
                parse_literal("true");
                return json_value{json_kind::boolean};
            case 'f':
                parse_literal("false");
                return json_value{json_kind::boolean};
            case 'n':
                parse_literal("null");
                return json_value{};
            default:
                if (input_[position_] == '-' ||
                    (input_[position_] >= '0' && input_[position_] <= '9'))
                {
                    json_value value;
                    value.kind = json_kind::number;
                    value.text = parse_number();
                    return value;
                }
                fail("expected a value");
        }
    }

    [[nodiscard]] json_value parse_object(std::size_t depth)
    {
        ++position_;
        json_value result;
        result.kind = json_kind::object;
        skip_space();
        if (consume('}'))
        {
            return result;
        }

        while (true)
        {
            if (position_ == input_.size() || input_[position_] != '"')
            {
                fail("expected an object key");
            }
            std::string key = parse_string();
            if (std::find(result.keys.begin(), result.keys.end(), key) != result.keys.end())
            {
                fail("duplicate object key");
            }
            result.keys.push_back(std::move(key));
            skip_space();
            if (!consume(':'))
            {
                fail("expected ':'");
            }
            skip_space();
            result.values.push_back(parse_value(depth + 1));
            skip_space();
            if (consume('}'))
            {
                return result;
            }
            if (!consume(','))
            {
                fail("expected ',' or '}'");
            }
            skip_space();
        }
    }

    [[nodiscard]] json_value parse_array(std::size_t depth)
    {
        ++position_;
        json_value result;
        result.kind = json_kind::array;
        skip_space();
        if (consume(']'))
        {
            return result;
        }

        while (true)
        {
            result.values.push_back(parse_value(depth + 1));
            skip_space();
            if (consume(']'))
            {
                return result;
            }
            if (!consume(','))
            {
                fail("expected ',' or ']'");
            }
            skip_space();
        }
    }

    [[nodiscard]] static unsigned hex_digit(char value) noexcept
    {
        if (value >= '0' && value <= '9')
        {
            return static_cast<unsigned>(value - '0');
        }
        if (value >= 'a' && value <= 'f')
        {
            return static_cast<unsigned>(value - 'a' + 10);
        }
        if (value >= 'A' && value <= 'F')
        {
            return static_cast<unsigned>(value - 'A' + 10);
        }
        return 16;
    }

    [[nodiscard]] std::uint32_t parse_hex_quad()
    {
        if (input_.size() - position_ < 4)
        {
            fail("short unicode escape");
        }

        std::uint32_t result = 0;
        for (unsigned index = 0; index < 4; ++index)
        {
            const unsigned digit = hex_digit(input_[position_++]);
            if (digit == 16)
            {
                fail("invalid unicode escape");
            }
            result = (result << 4) | digit;
        }
        return result;
    }

    static void append_utf8(std::string &output, std::uint32_t code_point)
    {
        if (code_point <= 0x7f)
        {
            output.push_back(static_cast<char>(code_point));
        }
        else if (code_point <= 0x7ff)
        {
            output.push_back(static_cast<char>(0xc0 | (code_point >> 6)));
            output.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
        }
        else if (code_point <= 0xffff)
        {
            output.push_back(static_cast<char>(0xe0 | (code_point >> 12)));
            output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
        }
        else
        {
            output.push_back(static_cast<char>(0xf0 | (code_point >> 18)));
            output.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
        }
    }

    void append_raw_utf8(std::string &output)
    {
        const auto lead = static_cast<unsigned char>(input_[position_]);
        unsigned length = 0;
        std::uint32_t code_point = 0;
        std::uint32_t minimum = 0;

        if ((lead & 0xe0) == 0xc0)
        {
            length = 2;
            code_point = lead & 0x1f;
            minimum = 0x80;
        }
        else if ((lead & 0xf0) == 0xe0)
        {
            length = 3;
            code_point = lead & 0x0f;
            minimum = 0x800;
        }
        else if ((lead & 0xf8) == 0xf0)
        {
            length = 4;
            code_point = lead & 0x07;
            minimum = 0x10000;
        }
        else
        {
            fail("invalid utf-8");
        }

        if (input_.size() - position_ < length)
        {
            fail("short utf-8 sequence");
        }
        for (unsigned index = 1; index < length; ++index)
        {
            const auto continuation = static_cast<unsigned char>(input_[position_ + index]);
            if ((continuation & 0xc0) != 0x80)
            {
                fail("invalid utf-8 continuation");
            }
            code_point = (code_point << 6) | (continuation & 0x3f);
        }
        if (code_point < minimum || code_point > 0x10ffff ||
            (code_point >= 0xd800 && code_point <= 0xdfff))
        {
            fail("invalid utf-8 code point");
        }

        output.append(input_.substr(position_, length));
        position_ += length;
    }

    [[nodiscard]] std::string parse_string()
    {
        ++position_;
        std::string result;

        while (position_ < input_.size())
        {
            const auto value = static_cast<unsigned char>(input_[position_++]);
            if (value == '"')
            {
                return result;
            }
            if (value < 0x20)
            {
                fail("unescaped control character");
            }
            if (value >= 0x80)
            {
                --position_;
                append_raw_utf8(result);
                continue;
            }
            if (value != '\\')
            {
                result.push_back(static_cast<char>(value));
                continue;
            }
            if (position_ == input_.size())
            {
                fail("short escape");
            }

            const char escaped = input_[position_++];
            switch (escaped)
            {
                case '"':
                case '\\':
                case '/':
                    result.push_back(escaped);
                    break;
                case 'b':
                    result.push_back('\b');
                    break;
                case 'f':
                    result.push_back('\f');
                    break;
                case 'n':
                    result.push_back('\n');
                    break;
                case 'r':
                    result.push_back('\r');
                    break;
                case 't':
                    result.push_back('\t');
                    break;
                case 'u':
                {
                    std::uint32_t code_point = parse_hex_quad();
                    if (code_point >= 0xd800 && code_point <= 0xdbff)
                    {
                        if (input_.size() - position_ < 2 || input_[position_] != '\\' ||
                            input_[position_ + 1] != 'u')
                        {
                            fail("unpaired high surrogate");
                        }
                        position_ += 2;
                        const std::uint32_t low = parse_hex_quad();
                        if (low < 0xdc00 || low > 0xdfff)
                        {
                            fail("unpaired high surrogate");
                        }
                        code_point = 0x10000 + ((code_point - 0xd800) << 10) + (low - 0xdc00);
                    }
                    else if (code_point >= 0xdc00 && code_point <= 0xdfff)
                    {
                        fail("unpaired low surrogate");
                    }
                    append_utf8(result, code_point);
                    break;
                }
                default:
                    fail("invalid escape");
            }
        }

        fail("unterminated string");
    }

    [[nodiscard]] std::string parse_number()
    {
        const std::size_t start = position_;
        static_cast<void>(consume('-'));
        if (position_ == input_.size())
        {
            fail("short number");
        }
        if (consume('0'))
        {
            if (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9')
            {
                fail("leading zero in number");
            }
        }
        else
        {
            if (input_[position_] < '1' || input_[position_] > '9')
            {
                fail("invalid number");
            }
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9')
            {
                ++position_;
            }
        }
        if (consume('.'))
        {
            if (position_ == input_.size() || input_[position_] < '0' || input_[position_] > '9')
            {
                fail("invalid fraction");
            }
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9')
            {
                ++position_;
            }
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E'))
        {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-'))
            {
                ++position_;
            }
            if (position_ == input_.size() || input_[position_] < '0' || input_[position_] > '9')
            {
                fail("invalid exponent");
            }
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9')
            {
                ++position_;
            }
        }
        return std::string(input_.substr(start, position_ - start));
    }

    void parse_literal(std::string_view literal)
    {
        if (input_.substr(position_, literal.size()) != literal)
        {
            fail("invalid literal");
        }
        position_ += literal.size();
    }

    std::string_view input_;
    std::size_t position_{0};
};

[[nodiscard]] const json_value *find_member(const json_value &object, std::string_view key) noexcept
{
    for (std::size_t index = 0; index < object.keys.size(); ++index)
    {
        if (object.keys[index] == key)
        {
            return &object.values[index];
        }
    }
    return nullptr;
}

[[nodiscard]] const json_value &require_member(const json_value &object, std::string_view key)
{
    if (const json_value *value = find_member(object, key))
    {
        return *value;
    }
    throw spec_error("missing request field: " + std::string(key));
}

void reject_unknown_fields(const json_value &object, std::span<const std::string_view> allowed,
                           std::string_view location)
{
    // strict shapes catch misspelled constraints before they can change selection.
    std::vector<std::string> unknown;
    for (const std::string &key : object.keys)
    {
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
        {
            unknown.push_back(key);
        }
    }
    std::sort(unknown.begin(), unknown.end());
    unknown.erase(std::unique(unknown.begin(), unknown.end()), unknown.end());
    if (unknown.empty())
    {
        return;
    }

    std::string message = "unknown " + std::string(location) + " field: ";
    for (std::size_t index = 0; index < unknown.size(); ++index)
    {
        if (index != 0)
        {
            message += ", ";
        }
        message += unknown[index];
    }
    throw spec_error(message);
}

[[noreturn]] void invalid_field(std::string_view field)
{
    throw spec_error("invalid request: invalid " + std::string(field));
}

[[nodiscard]] std::string get_string(const json_value &value, std::string_view field)
{
    if (value.kind != json_kind::string)
    {
        invalid_field(field);
    }
    return value.text;
}

[[nodiscard]] std::uint64_t get_u64(const json_value &value, std::string_view field)
{
    if (value.kind != json_kind::number || value.text.empty() || value.text.front() == '-' ||
        value.text.find_first_of(".eE") != std::string::npos)
    {
        invalid_field(field);
    }

    std::uint64_t result = 0;
    const char *first = value.text.data();
    const char *last = first + value.text.size();
    const auto parsed = std::from_chars(first, last, result);
    if (parsed.ec != std::errc{} || parsed.ptr != last)
    {
        invalid_field(field);
    }
    return result;
}

[[nodiscard]] std::uint16_t get_u16(const json_value &value, std::string_view field)
{
    const std::uint64_t result = get_u64(value, field);
    if (result > std::numeric_limits<std::uint16_t>::max())
    {
        invalid_field(field);
    }
    return static_cast<std::uint16_t>(result);
}

[[nodiscard]] operation parse_operation(const json_value &value)
{
    const std::string name = get_string(value, "op");
    if (name == "negacyclic_mul")
    {
        return operation::negacyclic_mul;
    }
    if (name == "cyclic_mul")
    {
        return operation::cyclic_mul;
    }
    invalid_field("op");
}

[[nodiscard]] input_representation parse_input(const json_value &value)
{
    const std::string name = get_string(value, "input");
    if (name == "centered")
    {
        return input_representation::centered;
    }
    if (name == "canonical")
    {
        return input_representation::canonical;
    }
    invalid_field("input");
}

[[nodiscard]] output_representation parse_output(const json_value &value)
{
    if (get_string(value, "output") == "canonical")
    {
        return output_representation::canonical;
    }
    invalid_field("output");
}

[[nodiscard]] aliasing parse_aliasing(const json_value &value)
{
    const std::string name = get_string(value, "alias");
    if (name == "no")
    {
        return aliasing::no;
    }
    if (name == "may")
    {
        return aliasing::may;
    }
    invalid_field("alias");
}

[[nodiscard]] wide_uint checked_add(wide_uint left, wide_uint right)
{
    // validated sizes fit, but checked primitives keep that proof local and reviewable.
    if (right > wide_max - left)
    {
        throw std::overflow_error("exact analysis overflow");
    }
    return left + right;
}

[[nodiscard]] wide_uint checked_multiply(wide_uint left, wide_uint right)
{
    if (left != 0 && right > wide_max / left)
    {
        throw std::overflow_error("exact analysis overflow");
    }
    return left * right;
}

[[nodiscard]] std::vector<schoolbook_plan> make_plans_unchecked(const request &req)
{
    std::vector<schoolbook_plan> candidates;
    candidates.reserve(req.target.acc_bits.size() * 7);

    for (const std::uint16_t acc_bits : req.target.acc_bits)
    {
        // this order is part of the artifact contract, even though ranking is explicit.
        candidates.push_back({schedule::full, acc_bits, 0});

        std::array<std::uint64_t, 5> blocks{
            std::min<std::uint64_t>(req.n, 4),
            std::min<std::uint64_t>(req.n, 8),
            std::min<std::uint64_t>(req.n, 16),
            std::min<std::uint64_t>(req.n, 32),
            req.n,
        };
        std::sort(blocks.begin(), blocks.end());
        const auto end = std::unique(blocks.begin(), blocks.end());
        for (auto block = blocks.begin(); block != end; ++block)
        {
            candidates.push_back({schedule::fold, acc_bits, *block});
        }

        candidates.push_back({schedule::output, acc_bits, 0});
    }
    return candidates;
}

[[nodiscard]] wide_uint scratch_bytes(const request &req, const schoolbook_plan &plan) noexcept
{
    const wide_uint count = req.n;
    const wide_uint acc_bytes = plan.acc_bits / 8;
    switch (plan.sched)
    {
        case schedule::full:
            return (2 * count - 1) * acc_bytes;
        case schedule::fold:
            return count * acc_bytes;
        case schedule::output:
            return 0;
    }
    return 0;
}

[[nodiscard]] bool plan_alias_safe(const schoolbook_plan &plan) noexcept
{
    return plan.sched != schedule::output;
}

[[nodiscard]] bool target_size_is_legal(const request &req, const schoolbook_plan &plan) noexcept
{
    // both size_t and signed pointer differences must represent every emitted object.
    const wide_uint size_maximum = req.target.size_bits == 32
                                       ? wide_uint{std::numeric_limits<std::uint32_t>::max()}
                                       : wide_uint{std::numeric_limits<std::uint64_t>::max()};
    const wide_uint object_size_maximum =
        req.target.size_bits == 32
            ? wide_uint{static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())}
            : wide_uint{static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())};
    const wide_uint count = req.n;
    if (count > size_maximum / 4 || count > object_size_maximum / 4)
    {
        return false;
    }

    const wide_uint acc_bytes = plan.acc_bits / 8;
    if (acc_bytes == 0)
    {
        return false;
    }
    switch (plan.sched)
    {
        case schedule::full:
        {
            const wide_uint acc_count = 2 * count - 1;
            return acc_count <= size_maximum / acc_bytes &&
                   acc_count <= object_size_maximum / acc_bytes;
        }
        case schedule::fold:
            return count <= size_maximum / acc_bytes && count <= object_size_maximum / acc_bytes;
        case schedule::output:
            return true;
    }
    return false;
}

struct operation_counts
{
    wide_uint multiplications;
    wide_uint additions;
    wide_uint reductions;
};

[[nodiscard]] operation_counts count_operations(const request &req,
                                                const schoolbook_plan &plan) noexcept
{
    const wide_uint count = req.n;
    const wide_uint multiplications = count * count;
    wide_uint additions = multiplications;
    if (plan.sched == schedule::full)
    {
        additions += count - 1;
    }
    else if (plan.sched == schedule::output)
    {
        additions += 3 * count;
    }
    return {multiplications, additions, count};
}

[[nodiscard]] wide_uint estimate_cost(const request &req, const schoolbook_plan &plan,
                                      const operation_counts &counts)
{
    const bool wide = plan.acc_bits > req.target.word_bits;
    wide_uint cost = checked_multiply(wide ? 10 : 4, counts.multiplications);
    cost = checked_add(cost, counts.additions);
    cost = checked_add(cost, checked_multiply(wide ? 14 : 8, counts.reductions));

    if (plan.sched == schedule::fold)
    {
        const wide_uint block = plan.block;
        if (block == 0)
        {
            throw std::invalid_argument("fold block must be nonzero");
        }
        const wide_uint tiles = (wide_uint{req.n} + block - 1) / block;
        cost = checked_add(cost, counts.multiplications / 4);
        cost = checked_add(cost, checked_multiply(2, checked_multiply(tiles, tiles)));
    }
    else if (plan.sched == schedule::output)
    {
        cost = checked_add(cost, counts.multiplications / 2);
    }
    return cost;
}

[[nodiscard]] candidate analyze_unchecked(const request &req, const schoolbook_plan &plan)
{
    plan_analysis analysis;
    analysis.plan = plan;
    analysis.scratch_bytes = scratch_bytes(req, plan);
    analysis.alias_safe = plan_alias_safe(plan);
    analysis.accumulator_bound = pqc_poly::accumulator_bound(req);
    analysis.required_bits = required_signed_bits(analysis.accumulator_bound);

    const operation_counts counts = count_operations(req, plan);
    analysis.multiplications = counts.multiplications;
    analysis.additions = counts.additions;
    analysis.reductions = counts.reductions;

    if (analysis.scratch_bytes > req.limits.ram)
    {
        analysis.rejections.emplace_back("ram");
    }
    if (!signed_width_fits(analysis.accumulator_bound, plan.acc_bits))
    {
        analysis.rejections.emplace_back("acc_width");
    }
    if (req.alias == aliasing::may && !analysis.alias_safe)
    {
        analysis.rejections.emplace_back("alias");
    }
    if (!target_size_is_legal(req, plan))
    {
        analysis.rejections.emplace_back("size_t");
    }
    analysis.legal = analysis.rejections.empty();

    return {
        std::move(analysis),
        estimate_cost(req, plan, counts),
    };
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

[[nodiscard]] bool independent_target_size_is_legal(const request &req,
                                                    const plan_analysis &analysis) noexcept
{
    const wide_uint size_maximum = req.target.size_bits == 32
                                       ? wide_uint{std::numeric_limits<std::uint32_t>::max()}
                                       : wide_uint{std::numeric_limits<std::uint64_t>::max()};
    const wide_uint object_size_maximum =
        req.target.size_bits == 32
            ? wide_uint{static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())}
            : wide_uint{static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())};
    const wide_uint count = req.n;
    if (count > size_maximum / 4 || count > object_size_maximum / 4)
    {
        return false;
    }

    const wide_uint acc_bytes = analysis.plan.acc_bits / 8;
    if (acc_bytes == 0)
    {
        return false;
    }
    switch (analysis.plan.sched)
    {
        case schedule::full:
        {
            const wide_uint acc_count = 2 * count - 1;
            return acc_count <= size_maximum / acc_bytes &&
                   acc_count <= object_size_maximum / acc_bytes;
        }
        case schedule::fold:
            return count <= size_maximum / acc_bytes && count <= object_size_maximum / acc_bytes;
        case schedule::output:
            return true;
    }
    return false;
}

void append_plan_json(std::string &output, const schoolbook_plan &plan, std::size_t indent)
{
    output += "{\n";
    append_indent(output, indent + 2);
    output += "\"id\": ";
    append_json_string(output, plan_id(plan));
    output += ",\n";
    append_indent(output, indent + 2);
    output += "\"algorithm\": \"schoolbook\",\n";
    append_indent(output, indent + 2);
    output += "\"schedule\": ";
    append_json_string(output, schedule_name(plan.sched));
    output += ",\n";
    append_indent(output, indent + 2);
    output += "\"acc_bits\": " + std::to_string(plan.acc_bits) + ",\n";
    append_indent(output, indent + 2);
    output += "\"block\": " + std::to_string(plan.block) + "\n";
    append_indent(output, indent);
    output += '}';
}

void append_rejections(std::string &output, const std::vector<std::string> &reasons,
                       std::size_t indent)
{
    if (reasons.empty())
    {
        output += "[]";
        return;
    }
    output += "[\n";
    for (std::size_t index = 0; index < reasons.size(); ++index)
    {
        append_indent(output, indent + 2);
        append_json_string(output, reasons[index]);
        output += index + 1 == reasons.size() ? "\n" : ",\n";
    }
    append_indent(output, indent);
    output += ']';
}

void append_analysis_json(std::string &output, const plan_analysis &analysis, std::size_t indent)
{
    output += "{\n";
    append_indent(output, indent + 2);
    output += "\"scratch_bytes\": " + wide_to_string(analysis.scratch_bytes) + ",\n";
    append_indent(output, indent + 2);
    output += std::string{"\"alias_safe\": "} + (analysis.alias_safe ? "true,\n" : "false,\n");
    append_indent(output, indent + 2);
    output += "\"accumulator_bound\": " + wide_to_string(analysis.accumulator_bound) + ",\n";
    append_indent(output, indent + 2);
    output += "\"required_bits\": " + std::to_string(analysis.required_bits) + ",\n";
    append_indent(output, indent + 2);
    output += "\"multiplications\": " + wide_to_string(analysis.multiplications) + ",\n";
    append_indent(output, indent + 2);
    output += "\"additions\": " + wide_to_string(analysis.additions) + ",\n";
    append_indent(output, indent + 2);
    output += "\"reductions\": " + wide_to_string(analysis.reductions) + ",\n";
    append_indent(output, indent + 2);
    output += std::string{"\"legal\": "} + (analysis.legal ? "true,\n" : "false,\n");
    append_indent(output, indent + 2);
    output += "\"rejections\": ";
    append_rejections(output, analysis.rejections, indent + 2);
    output += '\n';
    append_indent(output, indent);
    output += '}';
}

void append_candidate_json(std::string &output, const candidate &candidate,
                           std::size_t indent)
{
    output += "{\n";
    append_indent(output, indent + 2);
    output += "\"plan\": ";
    append_plan_json(output, candidate.analysis.plan, indent + 2);
    output += ",\n";
    append_indent(output, indent + 2);
    output += "\"analysis\": ";
    append_analysis_json(output, candidate.analysis, indent + 2);
    output += ",\n";
    append_indent(output, indent + 2);
    output += "\"estimated_cost\": " + wide_to_string(candidate.estimated_cost);
    output += '\n';
    append_indent(output, indent);
    output += '}';
}

}

selection_error::selection_error() : std::runtime_error("no legal plan")
{
}

std::string_view operation_name(operation value) noexcept
{
    switch (value)
    {
        case operation::negacyclic_mul:
            return "negacyclic_mul";
        case operation::cyclic_mul:
            return "cyclic_mul";
    }
    return "unknown";
}

std::string_view input_name(input_representation value) noexcept
{
    switch (value)
    {
        case input_representation::centered:
            return "centered";
        case input_representation::canonical:
            return "canonical";
    }
    return "unknown";
}

std::string_view output_name(output_representation value) noexcept
{
    switch (value)
    {
        case output_representation::canonical:
            return "canonical";
    }
    return "unknown";
}

std::string_view aliasing_name(aliasing value) noexcept
{
    switch (value)
    {
        case aliasing::no:
            return "no";
        case aliasing::may:
            return "may";
    }
    return "unknown";
}

std::string_view schedule_name(schedule value) noexcept
{
    switch (value)
    {
        case schedule::full:
            return "sb_full";
        case schedule::fold:
            return "sb_fold";
        case schedule::output:
            return "sb_out";
    }
    return "unknown";
}

request parse_request(std::string_view json)
{
    const json_value root = json_parser(json).parse();
    if (root.kind != json_kind::object)
    {
        throw spec_error("request must be an object");
    }

    constexpr std::array request_fields{
        std::string_view{"op"},     std::string_view{"n"},      std::string_view{"q"},
        std::string_view{"input"},  std::string_view{"output"}, std::string_view{"alias"},
        std::string_view{"target"}, std::string_view{"limits"},
    };
    constexpr std::array target_fields{
        std::string_view{"name"},
        std::string_view{"word_bits"},
        std::string_view{"size_bits"},
        std::string_view{"acc_bits"},
    };
    constexpr std::array limit_fields{std::string_view{"ram"}};

    // parse general json, then enforce the small request schema here
    reject_unknown_fields(root, request_fields, "request");
    const json_value *target = find_member(root, "target");
    const json_value *limits = find_member(root, "limits");
    if ((target != nullptr && target->kind != json_kind::object) ||
        (limits != nullptr && limits->kind != json_kind::object))
    {
        throw spec_error("target and limits must be objects");
    }
    if (target != nullptr)
    {
        reject_unknown_fields(*target, target_fields, "target");
    }
    if (limits != nullptr)
    {
        reject_unknown_fields(*limits, limit_fields, "limits");
    }

    request req;
    req.op = parse_operation(require_member(root, "op"));
    req.n = get_u64(require_member(root, "n"), "n");
    const std::uint64_t modulus = get_u64(require_member(root, "q"), "q");
    if (modulus > std::numeric_limits<std::uint32_t>::max())
    {
        invalid_field("q");
    }
    req.q = static_cast<std::uint32_t>(modulus);

    if (const json_value *value = find_member(root, "input"))
    {
        req.input = parse_input(*value);
    }
    if (const json_value *value = find_member(root, "output"))
    {
        req.output = parse_output(*value);
    }
    if (const json_value *value = find_member(root, "alias"))
    {
        req.alias = parse_aliasing(*value);
    }

    if (target != nullptr)
    {
        if (const json_value *value = find_member(*target, "name"))
        {
            req.target.name = get_string(*value, "target.name");
        }
        if (const json_value *value = find_member(*target, "word_bits"))
        {
            req.target.word_bits = get_u16(*value, "target.word_bits");
        }
        if (const json_value *value = find_member(*target, "size_bits"))
        {
            req.target.size_bits = get_u16(*value, "target.size_bits");
        }
        if (const json_value *value = find_member(*target, "acc_bits"))
        {
            req.target.acc_bits.clear();
            if (value->kind != json_kind::array)
            {
                invalid_field("target.acc_bits");
            }
            req.target.acc_bits.reserve(value->values.size());
            for (const json_value &entry : value->values)
            {
                req.target.acc_bits.push_back(get_u16(entry, "target.acc_bits"));
            }
            std::sort(req.target.acc_bits.begin(), req.target.acc_bits.end());
            req.target.acc_bits.erase(
                std::unique(req.target.acc_bits.begin(), req.target.acc_bits.end()),
                req.target.acc_bits.end());
        }
    }

    if (limits != nullptr)
    {
        if (const json_value *value = find_member(*limits, "ram"))
        {
            req.limits.ram = get_u64(*value, "limits.ram");
        }
    }
    validate_request(req);
    return req;
}

request load_request(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw spec_error("could not read " + path.string());
    }
    std::string json{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
    if (!input.eof() && input.fail())
    {
        throw spec_error("could not read " + path.string());
    }
    return parse_request(json);
}

void validate_request(const request &req)
{
    if (req.n < 2)
    {
        throw spec_error("n must be at least 2");
    }
    if (req.n > max_coefficient_count)
    {
        // this cap keeps every cost term below the exact 128-bit model ceiling.
        throw spec_error("n is too large for exact analysis");
    }
    if (req.q < 2 || req.q > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
    {
        throw spec_error("q must fit a positive int32_t");
    }
    if (operation_name(req.op) == "unknown")
    {
        throw spec_error("invalid operation");
    }
    if (input_name(req.input) == "unknown")
    {
        throw spec_error("invalid input representation");
    }
    if (output_name(req.output) == "unknown")
    {
        throw spec_error("invalid output representation");
    }
    if (aliasing_name(req.alias) == "unknown")
    {
        throw spec_error("invalid aliasing contract");
    }
    if (req.target.name.empty())
    {
        throw spec_error("target name must be a nonempty string");
    }
    if (req.target.word_bits < 8 || req.target.word_bits > 64)
    {
        throw spec_error("word_bits must be in [8, 64]");
    }
    if (req.target.size_bits != 32 && req.target.size_bits != 64)
    {
        throw spec_error("size_bits must be 32 or 64");
    }
    if (req.target.acc_bits.empty())
    {
        throw spec_error("acc_bits must be nonempty");
    }
    if (std::any_of(req.target.acc_bits.begin(), req.target.acc_bits.end(),
                    [](std::uint16_t bits) { return bits != 32 && bits != 64; }))
    {
        throw spec_error("acc_bits entries must be 32 or 64 for now");
    }
    if (!std::is_sorted(req.target.acc_bits.begin(), req.target.acc_bits.end()) ||
        std::adjacent_find(req.target.acc_bits.begin(), req.target.acc_bits.end()) !=
            req.target.acc_bits.end())
    {
        throw spec_error("acc_bits must be sorted and unique");
    }

    if (req.target.word_bits != 64)
    {
        const std::int64_t signed_lower = -(std::int64_t{1} << (req.target.word_bits - 1));
        const std::int64_t signed_upper = (std::int64_t{1} << (req.target.word_bits - 1)) - 1;
        if (input_lower_bound(req) < signed_lower || input_upper_bound(req) > signed_upper)
        {
            throw spec_error("input representatives do not fit target word_bits");
        }
    }
}

std::int64_t input_lower_bound(const request &req) noexcept
{
    if (req.input == input_representation::canonical)
    {
        return 0;
    }
    return -(static_cast<std::int64_t>(req.q) / 2);
}

std::int64_t input_upper_bound(const request &req) noexcept
{
    if (req.input == input_representation::canonical)
    {
        return static_cast<std::int64_t>(req.q) - 1;
    }
    return (static_cast<std::int64_t>(req.q) - 1) / 2;
}

std::uint64_t input_bound(const request &req) noexcept
{
    const std::int64_t lower = input_lower_bound(req);
    const std::int64_t upper = input_upper_bound(req);
    const std::uint64_t lower_magnitude = static_cast<std::uint64_t>(-lower);
    const std::uint64_t upper_magnitude = static_cast<std::uint64_t>(upper);
    return std::max(lower_magnitude, upper_magnitude);
}

wide_uint product_bound(const request &req) noexcept
{
    const wide_uint bound = input_bound(req);
    return bound * bound;
}

wide_uint accumulator_bound(const request &req) noexcept
{
    return wide_uint{req.n} * product_bound(req);
}

std::uint16_t required_signed_bits(wide_uint bound) noexcept
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

bool signed_width_fits(wide_uint bound, std::uint16_t bits) noexcept
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

std::string plan_id(const schoolbook_plan &plan)
{
    std::string result{schedule_name(plan.sched)};
    if (plan.block != 0)
    {
        result += "_b";
        result += std::to_string(plan.block);
    }
    result += "_i";
    result += std::to_string(plan.acc_bits);
    return result;
}

std::string wide_to_string(wide_uint value)
{
    if (value == 0)
    {
        return "0";
    }

    std::array<char, 39> buffer{};
    auto cursor = buffer.end();
    while (value != 0)
    {
        const unsigned digit = static_cast<unsigned>(value % 10);
        *--cursor = static_cast<char>('0' + digit);
        value /= 10;
    }
    return {cursor, buffer.end()};
}

std::string request_to_json(const request &req)
{
    validate_request(req);
    std::string output;
    output.reserve(384);
    output += "{\n  \"op\": ";
    append_json_string(output, operation_name(req.op));
    output += ",\n  \"n\": " + std::to_string(req.n) + ",\n  \"q\": " + std::to_string(req.q) +
              ",\n  \"input\": ";
    append_json_string(output, input_name(req.input));
    output += ",\n  \"output\": ";
    append_json_string(output, output_name(req.output));
    output += ",\n  \"alias\": ";
    append_json_string(output, aliasing_name(req.alias));
    output += ",\n  \"target\": {\n    \"name\": ";
    append_json_string(output, req.target.name);
    output += ",\n    \"word_bits\": " + std::to_string(req.target.word_bits) +
              ",\n    \"size_bits\": " + std::to_string(req.target.size_bits) +
              ",\n    \"acc_bits\": [";
    if (!req.target.acc_bits.empty())
    {
        output += '\n';
        for (std::size_t index = 0; index < req.target.acc_bits.size(); ++index)
        {
            output += "      " + std::to_string(req.target.acc_bits[index]);
            output += index + 1 == req.target.acc_bits.size() ? "\n" : ",\n";
        }
        output += "    ";
    }
    output +=
        "]\n  },\n  \"limits\": {\n    \"ram\": " + std::to_string(req.limits.ram) + "\n  }\n}\n";
    return output;
}

std::string candidate_to_json(const candidate &candidate)
{
    std::string output;
    output.reserve(512);
    append_candidate_json(output, candidate, 0);
    output += '\n';
    return output;
}

std::string candidates_to_json(std::span<const candidate> candidates)
{
    std::string output;
    output.reserve(candidates.size() * 512);
    output += '[';
    if (!candidates.empty())
    {
        output += '\n';
        for (std::size_t index = 0; index < candidates.size(); ++index)
        {
            append_indent(output, 2);
            append_candidate_json(output, candidates[index], 2);
            output += index + 1 == candidates.size() ? "\n" : ",\n";
        }
    }
    output += "]\n";
    return output;
}

std::vector<candidate> find_candidates(const request &req)
{
    validate_request(req);
    const std::vector<schoolbook_plan> plans = make_plans_unchecked(req);
    std::vector<candidate> candidates;
    candidates.reserve(plans.size());
    for (const schoolbook_plan &plan : plans)
    {
        candidates.push_back(analyze_unchecked(req, plan));
    }
    return candidates;
}

const candidate &pick_static(std::span<const candidate> candidates)
{
    const candidate *selected = nullptr;
    for (const candidate &candidate : candidates)
    {
        if (!candidate.analysis.legal)
        {
            continue;
        }
        // cost, scratch, then stable id makes selection independent of input order.
        if (selected == nullptr ||
            std::tuple{
                candidate.estimated_cost,
                candidate.analysis.scratch_bytes,
                plan_id(candidate.analysis.plan),
            } < std::tuple{
                    selected->estimated_cost,
                    selected->analysis.scratch_bytes,
                    plan_id(selected->analysis.plan),
                })
        {
            selected = &candidate;
        }
    }
    if (selected == nullptr)
    {
        throw selection_error{};
    }
    return *selected;
}

std::vector<const candidate *> static_frontier(std::span<const candidate> candidates)
{
    std::vector<const candidate *> legal;
    legal.reserve(candidates.size());
    for (const candidate &candidate : candidates)
    {
        if (candidate.analysis.legal)
        {
            legal.push_back(&candidate);
        }
    }

    std::vector<const candidate *> result;
    result.reserve(legal.size());
    for (const candidate *point : legal)
    {
        const bool dominated = std::any_of(
            legal.begin(), legal.end(),
            [point](const candidate *other)
            {
                return other->analysis.scratch_bytes <= point->analysis.scratch_bytes &&
                       other->estimated_cost <= point->estimated_cost &&
                       (other->analysis.scratch_bytes < point->analysis.scratch_bytes ||
                        other->estimated_cost < point->estimated_cost);
            });
        if (!dominated)
        {
            result.push_back(point);
        }
    }

    std::sort(result.begin(), result.end(),
              [](const candidate *left, const candidate *right)
              {
                  return std::tuple{
                             left->analysis.scratch_bytes,
                             left->estimated_cost,
                             plan_id(left->analysis.plan),
                         } < std::tuple{
                                 right->analysis.scratch_bytes,
                                 right->estimated_cost,
                                 plan_id(right->analysis.plan),
                             };
              });
    return result;
}

namespace
{

[[nodiscard]] std::vector<std::string> check_analysis(const request &req,
                                                      const plan_analysis &analysis)
{
    // repeat the math instead of trusting the values produced during search
    validate_request(req);
    std::vector<std::string> errors;
    if (analysis.plan.sched == schedule::fold &&
        (analysis.plan.block < 1 || analysis.plan.block > req.n))
    {
        errors.emplace_back("bad block");
    }
    if (analysis.plan.sched != schedule::fold && analysis.plan.block != 0)
    {
        errors.emplace_back("unexpected block");
    }
    if ((analysis.plan.acc_bits != 32 && analysis.plan.acc_bits != 64) ||
        std::find(req.target.acc_bits.begin(), req.target.acc_bits.end(), analysis.plan.acc_bits) ==
            req.target.acc_bits.end())
    {
        errors.emplace_back("bad acc type");
    }

    const wide_uint representative_bound =
        req.input == input_representation::canonical ? wide_uint{req.q - 1} : wide_uint{req.q / 2};
    const wide_uint acc_bound = wide_uint{req.n} * representative_bound * representative_bound;
    const std::uint16_t required_bits = independent_required_bits(acc_bound);
    if (analysis.accumulator_bound != acc_bound || analysis.required_bits != required_bits)
    {
        errors.emplace_back("bad range");
    }

    const wide_uint count = req.n;
    const wide_uint acc_bytes = analysis.plan.acc_bits / 8;
    const wide_uint multiplications = count * count;
    wide_uint scratch = 0;
    bool alias_safe = false;
    wide_uint additions = multiplications;
    switch (analysis.plan.sched)
    {
        case schedule::full:
            scratch = (2 * count - 1) * acc_bytes;
            alias_safe = true;
            additions = multiplications + count - 1;
            break;
        case schedule::fold:
            scratch = count * acc_bytes;
            alias_safe = true;
            break;
        case schedule::output:
            additions += 3 * count;
            break;
    }

    if (analysis.scratch_bytes != scratch)
    {
        errors.emplace_back("bad ram");
    }
    if (analysis.alias_safe != alias_safe)
    {
        errors.emplace_back("bad alias flag");
    }
    if (analysis.multiplications != multiplications || analysis.additions != additions ||
        analysis.reductions != count)
    {
        errors.emplace_back("bad op count");
    }

    std::vector<std::string> rejections;
    if (scratch > req.limits.ram)
    {
        rejections.emplace_back("ram");
    }
    if (!independent_width_fits(acc_bound, analysis.plan.acc_bits))
    {
        rejections.emplace_back("acc_width");
    }
    if (req.alias == aliasing::may && !alias_safe)
    {
        rejections.emplace_back("alias");
    }
    if (!independent_target_size_is_legal(req, analysis))
    {
        rejections.emplace_back("size_t");
    }
    if (analysis.legal != rejections.empty() || analysis.rejections != rejections)
    {
        errors.emplace_back("bad legality");
    }
    return errors;
}

}

std::vector<std::string> check_candidate(const request &req, const candidate &selected)
{
    std::vector<std::string> errors = check_analysis(req, selected.analysis);
    const wide_uint count = req.n;
    const wide_uint multiplications = count * count;
    wide_uint additions = multiplications;
    if (selected.analysis.plan.sched == schedule::full)
    {
        additions += count - 1;
    }
    else if (selected.analysis.plan.sched == schedule::output)
    {
        additions += 3 * count;
    }
    const bool wide = selected.analysis.plan.acc_bits > req.target.word_bits;
    wide_uint cost = checked_multiply(wide ? 10 : 4, multiplications);
    cost = checked_add(cost, additions);
    cost = checked_add(cost, checked_multiply(wide ? 14 : 8, count));

    if (selected.analysis.plan.sched == schedule::fold && selected.analysis.plan.block >= 1 &&
        selected.analysis.plan.block <= req.n)
    {
        const wide_uint block = selected.analysis.plan.block;
        const wide_uint tiles = (count + block - 1) / block;
        cost = checked_add(cost, multiplications / 4);
        cost = checked_add(cost, checked_multiply(2, checked_multiply(tiles, tiles)));
    }
    else if (selected.analysis.plan.sched == schedule::output)
    {
        cost = checked_add(cost, multiplications / 2);
    }

    if (selected.estimated_cost != cost)
    {
        errors.emplace_back("bad score");
    }
    return errors;
}

}
