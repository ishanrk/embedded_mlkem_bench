#include "pqc_poly/target_measurement.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace pqc_poly
{
namespace
{

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
    std::vector<std::pair<std::string, json_value>> members{};
    std::vector<json_value> elements{};
};

[[noreturn]] void fail(std::string_view message)
{
    throw target_measurement_error(std::string(message));
}

class json_parser
{
public:
    explicit json_parser(std::string_view input) noexcept : input_(input)
    {
    }

    [[nodiscard]] json_value parse()
    {
        space();
        json_value value = parse_value(0);
        space();
        if (position_ != input_.size())
        {
            invalid("trailing data");
        }
        return value;
    }

private:
    [[noreturn]] void invalid(std::string_view message) const
    {
        fail("invalid measurement json: " + std::string(message) + " at byte " +
             std::to_string(position_));
    }

    void space() noexcept
    {
        while (position_ < input_.size() &&
               (input_[position_] == ' ' || input_[position_] == '\n' ||
                input_[position_] == '\r' || input_[position_] == '\t'))
        {
            ++position_;
        }
    }

    [[nodiscard]] bool take(char value) noexcept
    {
        if (position_ < input_.size() && input_[position_] == value)
        {
            ++position_;
            return true;
        }
        return false;
    }

    [[nodiscard]] json_value parse_value(std::size_t depth)
    {
        if (depth > 32)
        {
            invalid("nesting too deep");
        }
        if (position_ == input_.size())
        {
            invalid("missing value");
        }
        if (input_[position_] == '{')
        {
            return parse_object(depth);
        }
        if (input_[position_] == '[')
        {
            return parse_array(depth);
        }
        if (input_[position_] == '"')
        {
            return {json_kind::string, parse_string()};
        }
        if (input_.substr(position_, 4) == "true")
        {
            position_ += 4;
            return {json_kind::boolean, "true"};
        }
        if (input_.substr(position_, 5) == "false")
        {
            position_ += 5;
            return {json_kind::boolean, "false"};
        }
        if (input_.substr(position_, 4) == "null")
        {
            position_ += 4;
            return {};
        }
        if (input_[position_] == '-' || std::isdigit(static_cast<unsigned char>(input_[position_])))
        {
            return {json_kind::number, parse_number()};
        }
        invalid("missing value");
    }

    [[nodiscard]] json_value parse_object(std::size_t depth)
    {
        ++position_;
        json_value value;
        value.kind = json_kind::object;
        space();
        if (take('}'))
        {
            return value;
        }
        while (true)
        {
            if (position_ == input_.size() || input_[position_] != '"')
            {
                invalid("missing object key");
            }
            std::string key = parse_string();
            if (std::any_of(value.members.begin(), value.members.end(),
                            [&key](const auto &member) { return member.first == key; }))
            {
                invalid("duplicate object key");
            }
            space();
            if (!take(':'))
            {
                invalid("missing colon");
            }
            space();
            value.members.emplace_back(std::move(key), parse_value(depth + 1));
            space();
            if (take('}'))
            {
                return value;
            }
            if (!take(','))
            {
                invalid("missing object separator");
            }
            space();
        }
    }

    [[nodiscard]] json_value parse_array(std::size_t depth)
    {
        ++position_;
        json_value value;
        value.kind = json_kind::array;
        space();
        if (take(']'))
        {
            return value;
        }
        while (true)
        {
            value.elements.push_back(parse_value(depth + 1));
            space();
            if (take(']'))
            {
                return value;
            }
            if (!take(','))
            {
                invalid("missing array separator");
            }
            space();
        }
    }

    [[nodiscard]] static unsigned hex(char value) noexcept
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

    [[nodiscard]] std::uint32_t hex_quad()
    {
        if (input_.size() - position_ < 4)
        {
            invalid("short unicode escape");
        }
        std::uint32_t result = 0;
        for (unsigned i = 0; i < 4; ++i)
        {
            const unsigned digit = hex(input_[position_++]);
            if (digit == 16)
            {
                invalid("invalid unicode escape");
            }
            result = (result << 4U) | digit;
        }
        return result;
    }

    static void utf8(std::string &out, std::uint32_t value)
    {
        if (value <= 0x7fU)
        {
            out.push_back(static_cast<char>(value));
        }
        else if (value <= 0x7ffU)
        {
            out.push_back(static_cast<char>(0xc0U | (value >> 6U)));
            out.push_back(static_cast<char>(0x80U | (value & 0x3fU)));
        }
        else if (value <= 0xffffU)
        {
            out.push_back(static_cast<char>(0xe0U | (value >> 12U)));
            out.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3fU)));
            out.push_back(static_cast<char>(0x80U | (value & 0x3fU)));
        }
        else
        {
            out.push_back(static_cast<char>(0xf0U | (value >> 18U)));
            out.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3fU)));
            out.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3fU)));
            out.push_back(static_cast<char>(0x80U | (value & 0x3fU)));
        }
    }

    [[nodiscard]] std::string parse_string()
    {
        ++position_;
        std::string out;
        while (position_ < input_.size())
        {
            const char value = input_[position_++];
            if (value == '"')
            {
                return out;
            }
            if (static_cast<unsigned char>(value) < 0x20U)
            {
                invalid("control character in string");
            }
            if (value != '\\')
            {
                out.push_back(value);
                continue;
            }
            if (position_ == input_.size())
            {
                invalid("short escape");
            }
            const char escaped = input_[position_++];
            switch (escaped)
            {
                case '"':
                case '\\':
                case '/':
                    out.push_back(escaped);
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u':
                {
                    std::uint32_t point = hex_quad();
                    if (point >= 0xd800U && point <= 0xdbffU)
                    {
                        if (input_.size() - position_ < 2 || input_[position_] != '\\' ||
                            input_[position_ + 1] != 'u')
                        {
                            invalid("unpaired unicode surrogate");
                        }
                        position_ += 2;
                        const std::uint32_t low = hex_quad();
                        if (low < 0xdc00U || low > 0xdfffU)
                        {
                            invalid("unpaired unicode surrogate");
                        }
                        point = 0x10000U + ((point - 0xd800U) << 10U) + (low - 0xdc00U);
                    }
                    else if (point >= 0xdc00U && point <= 0xdfffU)
                    {
                        invalid("unpaired unicode surrogate");
                    }
                    utf8(out, point);
                    break;
                }
                default:
                    invalid("invalid escape");
            }
        }
        invalid("unterminated string");
    }

    [[nodiscard]] std::string parse_number()
    {
        const std::size_t begin = position_;
        static_cast<void>(take('-'));
        if (position_ == input_.size())
        {
            invalid("short number");
        }
        if (take('0'))
        {
            if (position_ < input_.size() &&
                std::isdigit(static_cast<unsigned char>(input_[position_])))
            {
                invalid("leading zero");
            }
        }
        else
        {
            if (!std::isdigit(static_cast<unsigned char>(input_[position_])))
            {
                invalid("invalid number");
            }
            while (position_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_])))
            {
                ++position_;
            }
        }
        if (take('.'))
        {
            if (position_ == input_.size() ||
                !std::isdigit(static_cast<unsigned char>(input_[position_])))
            {
                invalid("invalid fraction");
            }
            while (position_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_])))
            {
                ++position_;
            }
        }
        return std::string(input_.substr(begin, position_ - begin));
    }

    std::string_view input_;
    std::size_t position_{0};
};

[[nodiscard]] const json_value &object_field(const json_value &object, std::string_view key)
{
    if (object.kind != json_kind::object)
    {
        fail("measurement json root must be an object");
    }
    const auto found = std::find_if(object.members.begin(), object.members.end(),
                                    [key](const auto &member) { return member.first == key; });
    if (found == object.members.end())
    {
        fail("missing measurement field: " + std::string(key));
    }
    return found->second;
}

[[nodiscard]] std::string string_field(const json_value &object, std::string_view key)
{
    const json_value &value = object_field(object, key);
    if (value.kind != json_kind::string || value.text.empty())
    {
        fail("measurement field must be a nonempty string: " + std::string(key));
    }
    return value.text;
}

[[nodiscard]] std::uint64_t unsigned_number(const json_value &value, std::string_view name)
{
    if (value.kind != json_kind::number || value.text.empty() || value.text.front() == '-' ||
        value.text.find('.') != std::string::npos)
    {
        fail("measurement field must be an unsigned integer: " + std::string(name));
    }
    std::uint64_t result = 0;
    const auto parsed =
        std::from_chars(value.text.data(), value.text.data() + value.text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.text.data() + value.text.size())
    {
        fail("measurement integer overflow: " + std::string(name));
    }
    return result;
}

[[nodiscard]] std::uint64_t uint_field(const json_value &object, std::string_view key)
{
    return unsigned_number(object_field(object, key), key);
}

[[nodiscard]] bool bool_field(const json_value &object, std::string_view key)
{
    const json_value &value = object_field(object, key);
    if (value.kind != json_kind::boolean)
    {
        fail("measurement field must be boolean: " + std::string(key));
    }
    return value.text == "true";
}

[[nodiscard]] double double_field(const json_value &object, std::string_view key)
{
    const json_value &value = object_field(object, key);
    if (value.kind != json_kind::number)
    {
        fail("measurement field must be numeric: " + std::string(key));
    }
    std::size_t consumed = 0;
    double result = 0.0;
    try
    {
        result = std::stod(value.text, &consumed);
    }
    catch (const std::exception &)
    {
        fail("invalid measurement number: " + std::string(key));
    }
    if (consumed != value.text.size() || !std::isfinite(result))
    {
        fail("invalid measurement number: " + std::string(key));
    }
    return result;
}

[[nodiscard]] std::vector<std::string> string_array(const json_value &object, std::string_view key)
{
    const json_value &value = object_field(object, key);
    if (value.kind != json_kind::array || value.elements.empty())
    {
        fail("measurement field must be a nonempty string array: " + std::string(key));
    }
    std::vector<std::string> result;
    result.reserve(value.elements.size());
    for (const json_value &element : value.elements)
    {
        if (element.kind != json_kind::string || element.text.empty())
        {
            fail("measurement field must be a nonempty string array: " + std::string(key));
        }
        result.push_back(element.text);
    }
    return result;
}

[[nodiscard]] std::uint64_t checked_add(std::uint64_t left, std::uint64_t right,
                                        std::string_view field)
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
    {
        fail("measurement integer overflow: " + std::string(field));
    }
    return left + right;
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        fail("cannot open target manifest: " + path.string());
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] bool valid_hex(std::string_view value, std::size_t width) noexcept
{
    return value.size() == width &&
           std::all_of(value.begin(), value.end(),
                       [](char digit)
                       { return std::isxdigit(static_cast<unsigned char>(digit)) != 0; });
}

[[nodiscard]] std::vector<std::string_view> lines(std::string_view text)
{
    std::vector<std::string_view> result;
    while (!text.empty())
    {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        result.push_back(line);
        if (end == std::string_view::npos)
        {
            break;
        }
        text.remove_prefix(end + 1);
    }
    return result;
}

[[nodiscard]] std::uint64_t decimal(std::string_view value, std::string_view field)
{
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
    {
        fail("invalid unsigned integer in " + std::string(field));
    }
    return result;
}

[[nodiscard]] std::optional<std::uint64_t> frame_bytes(std::span<const stack_frame> frames,
                                                       std::string_view name) noexcept
{
    std::optional<std::uint64_t> result;
    for (const stack_frame &frame : frames)
    {
        if (frame.function == name)
        {
            result = std::max(result.value_or(0), frame.bytes);
        }
    }
    if (result)
    {
        return result;
    }

    const std::size_t suffix = name.rfind('.');
    if (suffix == std::string_view::npos || suffix + 1 == name.size() ||
        !std::all_of(name.begin() + static_cast<std::ptrdiff_t>(suffix + 1), name.end(),
                     [](char value)
                     { return std::isdigit(static_cast<unsigned char>(value)) != 0; }))
    {
        return std::nullopt;
    }
    const std::string_view base = name.substr(0, suffix);
    if (!base.ends_with(".constprop") && !base.ends_with(".isra") && !base.ends_with(".part"))
    {
        return std::nullopt;
    }
    for (const stack_frame &frame : frames)
    {
        if (frame.function == base)
        {
            result = std::max(result.value_or(0), frame.bytes);
        }
    }
    return result;
}

struct call_graph
{
    std::map<std::string, std::vector<std::string>, std::less<>> calls{};
    std::set<std::string, std::less<>> indirect{};
};

[[nodiscard]] std::string_view trim(std::string_view value) noexcept
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
    {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
    {
        value.remove_suffix(1);
    }
    return value;
}

[[nodiscard]] call_graph parse_calls(std::string_view disassembly)
{
    call_graph graph;
    std::string current;
    for (const std::string_view raw : lines(disassembly))
    {
        const std::string_view line = trim(raw);
        const std::size_t left = line.find('<');
        const std::size_t header = line.find(">:");
        if (left != std::string_view::npos && header != std::string_view::npos && left < header)
        {
            current = std::string(line.substr(left + 1, header - left - 1));
            graph.calls.try_emplace(current);
            continue;
        }
        if (current.empty())
        {
            continue;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos)
        {
            continue;
        }
        const std::string_view instruction = line.substr(colon + 1);
        const bool indirect_opcode = instruction.find("jalr") != std::string_view::npos ||
                                     instruction.find("\tjr") != std::string_view::npos;
        const bool direct_opcode = instruction.find("\tjal") != std::string_view::npos ||
                                   instruction.find("\tc.jal") != std::string_view::npos ||
                                   instruction.find("\tcall") != std::string_view::npos ||
                                   instruction.find("\ttail") != std::string_view::npos ||
                                   instruction.find("\tj\t") != std::string_view::npos;
        if (!indirect_opcode && !direct_opcode)
        {
            continue;
        }
        const std::size_t target_left = instruction.find('<');
        const std::size_t target_right = instruction.find('>', target_left);
        if (target_left == std::string_view::npos || target_right == std::string_view::npos)
        {
            graph.indirect.insert(current);
            continue;
        }
        const std::string_view target =
            instruction.substr(target_left + 1, target_right - target_left - 1);
        if (target.find("+0x") != std::string_view::npos ||
            target.find("-0x") != std::string_view::npos)
        {
            continue;
        }
        graph.calls[current].emplace_back(target);
    }
    return graph;
}

[[nodiscard]] std::optional<std::uint64_t> call_bound(
    std::span<const stack_frame> frames, const call_graph &graph, const std::string &name,
    std::set<std::string, std::less<>> &active,
    std::map<std::string, std::uint64_t, std::less<>> &memo)
{
    if (const auto known = memo.find(name); known != memo.end())
    {
        return known->second;
    }
    const std::optional<std::uint64_t> frame = frame_bytes(frames, name);
    if (!frame || graph.indirect.contains(name) || !active.insert(name).second)
    {
        return std::nullopt;
    }

    std::uint64_t child_max = 0;
    if (const auto found = graph.calls.find(name); found != graph.calls.end())
    {
        for (const std::string &child : found->second)
        {
            const std::optional<std::uint64_t> child_bound =
                call_bound(frames, graph, child, active, memo);
            if (!child_bound)
            {
                active.erase(name);
                return std::nullopt;
            }
            child_max = std::max(child_max, *child_bound);
        }
    }
    active.erase(name);
    const std::uint64_t result = checked_add(*frame, child_max, "callchain stack");
    memo.emplace(name, result);
    return result;
}

}

picorv32_manifest load_picorv32_manifest(const std::filesystem::path &path)
{
    const json_value root = json_parser(read_file(path)).parse();
    picorv32_manifest result{
        .repository_sha = string_field(root, "repository_sha"),
        .picorv32_sha = string_field(root, "picorv32_sha"),
        .riscv_toolchain_release = string_field(root, "riscv_toolchain_release"),
        .oss_cad_suite_release = string_field(root, "oss_cad_suite_release"),
        .gcc_version = string_field(root, "gcc_version"),
        .binutils_version = string_field(root, "binutils_version"),
        .verilator_version = string_field(root, "verilator_version"),
        .yosys_version = string_field(root, "yosys_version"),
        .nextpnr_version = string_field(root, "nextpnr_version"),
        .solver_version = string_field(root, "solver_version"),
        .cmake_version = string_field(root, "cmake_version"),
        .ninja_version = string_field(root, "ninja_version"),
        .python_version = string_field(root, "python_version"),
        .compiler_flags = string_array(root, "compiler_flags"),
        .linker_flags = string_array(root, "linker_flags"),
        .core_parameters = string_array(root, "core_parameters"),
        .fpga_part = string_field(root, "fpga_part"),
    };

    const json_value &seeds = object_field(root, "synthesis_seeds");
    if (seeds.kind != json_kind::array || seeds.elements.empty())
    {
        fail("measurement field must be a nonempty integer array: synthesis_seeds");
    }
    for (const json_value &seed : seeds.elements)
    {
        const std::uint64_t value = unsigned_number(seed, "synthesis_seeds");
        if (value > std::numeric_limits<std::uint32_t>::max())
        {
            fail("measurement integer overflow: synthesis_seeds");
        }
        result.synthesis_seeds.push_back(static_cast<std::uint32_t>(value));
    }

    const json_value &archives = object_field(root, "source_archives");
    if (archives.kind != json_kind::array || archives.elements.empty())
    {
        fail("measurement field must be a nonempty array: source_archives");
    }
    for (const json_value &archive : archives.elements)
    {
        result.source_archives.push_back(
            {.name = string_field(archive, "name"), .sha256 = string_field(archive, "sha256")});
        if (!valid_hex(result.source_archives.back().sha256, 64))
        {
            fail("source archive sha256 must contain 64 hexadecimal digits");
        }
    }

    const json_value &container = object_field(root, "container_digest");
    if (container.kind == json_kind::string && !container.text.empty())
    {
        result.container_digest = container.text;
    }
    else if (container.kind != json_kind::null_value)
    {
        fail("container_digest must be null or a nonempty string");
    }

    if (!valid_hex(result.repository_sha, 40) || !valid_hex(result.picorv32_sha, 40))
    {
        fail("repository shas must contain 40 hexadecimal digits");
    }
    if (result.picorv32_sha != "a473fc8fca393771d83b0ffcf0b14db3393339d8" ||
        result.riscv_toolchain_release != "2026.07.15" ||
        result.oss_cad_suite_release != "2026-07-29" || result.fpga_part != "LFE5U-45F-6BG381C" ||
        result.synthesis_seeds != std::vector<std::uint32_t>({1, 2, 3, 4, 5}))
    {
        fail("target manifest does not describe the pinned baseline");
    }
    return result;
}

std::vector<stack_frame> parse_stack_usage(std::string_view text)
{
    std::vector<stack_frame> result;
    for (const std::string_view raw : lines(text))
    {
        const std::string_view line = trim(raw);
        if (line.empty())
        {
            continue;
        }
        const std::size_t first_tab = line.find('\t');
        const std::size_t second_tab = line.find('\t', first_tab + 1);
        if (first_tab == std::string_view::npos || second_tab == std::string_view::npos ||
            line.find('\t', second_tab + 1) != std::string_view::npos)
        {
            fail("malformed gcc stack usage record");
        }
        const std::string_view location = line.substr(0, first_tab);
        const std::string_view size = line.substr(first_tab + 1, second_tab - first_tab - 1);
        const std::string_view kind = line.substr(second_tab + 1);

        std::size_t function_start = std::string_view::npos;
        for (std::size_t first = location.find(':'); first != std::string_view::npos;
             first = location.find(':', first + 1))
        {
            const std::size_t second = location.find(':', first + 1);
            if (second == std::string_view::npos)
            {
                break;
            }
            const std::size_t third = location.find(':', second + 1);
            if (third == std::string_view::npos)
            {
                break;
            }
            const std::string_view line_number = location.substr(first + 1, second - first - 1);
            const std::string_view column = location.substr(second + 1, third - second - 1);
            if (!line_number.empty() && !column.empty() &&
                std::all_of(line_number.begin(), line_number.end(),
                            [](char value)
                            { return std::isdigit(static_cast<unsigned char>(value)) != 0; }) &&
                std::all_of(column.begin(), column.end(),
                            [](char value)
                            { return std::isdigit(static_cast<unsigned char>(value)) != 0; }))
            {
                function_start = third + 1;
                break;
            }
        }
        if (function_start == std::string_view::npos || function_start == location.size())
        {
            fail("malformed gcc stack usage location");
        }
        if (kind != "static" && kind != "dynamic,bounded")
        {
            fail("unbounded or unknown gcc stack frame: " +
                 std::string(location.substr(function_start)));
        }
        result.push_back({.function = std::string(location.substr(function_start)),
                          .bytes = decimal(size, "gcc stack usage"),
                          .bounded_dynamic = kind == "dynamic,bounded"});
    }
    if (result.empty())
    {
        fail("gcc stack usage input contains no records");
    }
    return result;
}

std::optional<std::uint64_t> compute_callchain_stack_bound(std::span<const stack_frame> frames,
                                                           std::string_view disassembly,
                                                           std::string_view root)
{
    if (root.empty())
    {
        fail("callchain root must not be empty");
    }
    const call_graph graph = parse_calls(disassembly);
    std::set<std::string, std::less<>> active;
    std::map<std::string, std::uint64_t, std::less<>> memo;
    return call_bound(frames, graph, std::string(root), active, memo);
}

code_size_measurement parse_elf_size(std::string_view text)
{
    code_size_measurement result;
    bool found = false;
    for (const std::string_view raw : lines(text))
    {
        std::istringstream input{std::string(raw)};
        std::string section;
        std::string size;
        std::string address;
        std::string extra;
        if (!(input >> section >> size >> address) || (input >> extra))
        {
            continue;
        }
        if (section == ".text" || section.starts_with(".text."))
        {
            const std::uint64_t bytes = decimal(size, "elf section size");
            result.code_text_bytes = checked_add(result.code_text_bytes, bytes, "code text");
        }
        else if (section == ".rodata" || section.starts_with(".rodata.") || section == ".srodata" ||
                 section.starts_with(".srodata."))
        {
            const std::uint64_t bytes = decimal(size, "elf section size");
            result.code_rodata_bytes = checked_add(result.code_rodata_bytes, bytes, "code rodata");
        }
        else if (section == ".data" || section.starts_with(".data.") || section == ".sdata" ||
                 section.starts_with(".sdata."))
        {
            const std::uint64_t bytes = decimal(size, "elf section size");
            result.data_bytes = checked_add(result.data_bytes, bytes, "data");
        }
        else if (section == ".bss" || section.starts_with(".bss.") || section == ".sbss" ||
                 section.starts_with(".sbss."))
        {
            const std::uint64_t bytes = decimal(size, "elf section size");
            result.bss_bytes = checked_add(result.bss_bytes, bytes, "bss");
        }
        else
        {
            continue;
        }
        found = true;
    }
    if (!found)
    {
        fail("elf size input contains no measured sections");
    }
    result.allocated_flash_bytes = checked_add(
        checked_add(result.code_text_bytes, result.code_rodata_bytes, "allocated flash"),
        result.data_bytes, "allocated flash");
    return result;
}

code_size_measurement parse_code_size_measurement(std::string_view text)
{
    const json_value root = json_parser(text).parse();
    code_size_measurement result{
        .code_text_bytes = uint_field(root, "code_text_bytes"),
        .code_rodata_bytes = uint_field(root, "code_rodata_bytes"),
        .data_bytes = uint_field(root, "data_bytes"),
        .bss_bytes = uint_field(root, "bss_bytes"),
        .allocated_flash_bytes = uint_field(root, "allocated_flash_bytes"),
    };
    if (result.allocated_flash_bytes !=
        checked_add(
            checked_add(result.code_text_bytes, result.code_rodata_bytes, "allocated flash"),
            result.data_bytes, "allocated flash"))
    {
        fail("inconsistent allocated flash measurement");
    }
    return result;
}

stack_measurement parse_stack_measurement(std::string_view text)
{
    const json_value root = json_parser(text).parse();
    stack_measurement result{
        .explicit_scratch_bytes = uint_field(root, "explicit_scratch_bytes"),
        .caller_working_bytes = uint_field(root, "caller_working_bytes"),
        .compiler_frame_bytes = uint_field(root, "compiler_frame_bytes"),
        .runtime_stack_high_water_bytes = uint_field(root, "runtime_stack_high_water_bytes"),
        .raw_stack_high_water_bytes = uint_field(root, "raw_stack_high_water_bytes"),
        .raw_wrapper_high_water_bytes = uint_field(root, "raw_wrapper_high_water_bytes"),
    };
    const json_value &callchain = object_field(root, "compiler_callchain_bound_bytes");
    if (callchain.kind == json_kind::number)
    {
        result.compiler_callchain_bound_bytes =
            unsigned_number(callchain, "compiler_callchain_bound_bytes");
    }
    else if (callchain.kind != json_kind::null_value)
    {
        fail("compiler callchain bound must be unsigned or null");
    }
    if (result.raw_stack_high_water_bytes < result.raw_wrapper_high_water_bytes ||
        result.runtime_stack_high_water_bytes !=
            result.raw_stack_high_water_bytes - result.raw_wrapper_high_water_bytes)
    {
        fail("inconsistent stack measurement");
    }
    return result;
}

cycle_measurement parse_simulation_measurement(std::string_view text)
{
    const json_value root = json_parser(text).parse();
    const std::uint64_t reported_calibrated = uint_field(root, "calibrated_cycles");
    cycle_measurement result{
        .begin_cycle = uint_field(root, "begin_cycle"),
        .end_cycle = uint_field(root, "end_cycle"),
        .marker_overhead_cycles = uint_field(root, "marker_overhead_cycles"),
        .terminated = bool_field(root, "terminated"),
        .trapped = bool_field(root, "trapped"),
    };
    if (result.end_cycle < result.begin_cycle)
    {
        fail("simulation end cycle precedes begin cycle");
    }
    const std::uint64_t raw = result.end_cycle - result.begin_cycle;
    if (result.marker_overhead_cycles > raw)
    {
        fail("simulation marker overhead exceeds measured region");
    }
    result.calibrated_cycles = raw - result.marker_overhead_cycles;
    if (reported_calibrated != result.calibrated_cycles)
    {
        fail("inconsistent calibrated cycle result");
    }
    return result;
}

std::vector<mlkem_cycle_measurement> parse_mlkem_cycle_measurements(std::string_view text)
{
    std::vector<mlkem_cycle_measurement> result;
    for (const std::string_view line : lines(text))
    {
        if (trim(line).empty())
        {
            continue;
        }
        const json_value root = json_parser(line).parse();
        if (string_field(root, "schema") != "pqc-poly-bench/mlkem-measurement-v1")
        {
            fail("invalid mlkem measurement schema");
        }
        const std::uint64_t input = uint_field(root, "input");
        const std::uint64_t repeat = uint_field(root, "repeat");
        if (input > std::numeric_limits<std::uint32_t>::max() ||
            repeat > std::numeric_limits<std::uint32_t>::max())
        {
            fail("measurement integer overflow");
        }
        mlkem_cycle_measurement record{
            .plan_id = string_field(root, "plan_id"),
            .level = string_field(root, "level"),
            .operation = string_field(root, "operation"),
            .multiplier = string_field(root, "multiplier"),
            .input = static_cast<std::uint32_t>(input),
            .repeat = static_cast<std::uint32_t>(repeat),
            .begin_cycle = uint_field(root, "begin_cycle"),
            .end_cycle = uint_field(root, "end_cycle"),
            .marker_overhead_cycles = uint_field(root, "marker_overhead_cycles"),
            .calibrated_cycles = uint_field(root, "calibrated_cycles"),
            .instruction_count = uint_field(root, "instruction_count"),
        };
        const bool full_operation = record.operation == "keygen" ||
                                    record.operation == "encapsulation" ||
                                    record.operation == "decapsulation";
        const bool kernel = record.operation == "forward_ntt" ||
                            record.operation == "inverse_ntt" || record.operation == "mulcache" ||
                            record.operation == "poly_tomont" ||
                            record.operation == "base_dot_k2" ||
                            record.operation == "base_dot_k3" || record.operation == "base_dot_k4";
        if ((record.level != "512" && record.level != "768" && record.level != "1024") ||
            (!kernel && !full_operation) ||
            (record.multiplier != "project" && record.multiplier != "stock" &&
             record.multiplier != "fqmul") ||
            record.repeat >= 3U || record.input >= (full_operation ? 30U : 16U) ||
            record.end_cycle < record.begin_cycle ||
            record.end_cycle - record.begin_cycle < record.marker_overhead_cycles ||
            record.calibrated_cycles !=
                record.end_cycle - record.begin_cycle - record.marker_overhead_cycles)
        {
            fail("invalid mlkem measurement record");
        }
        result.push_back(std::move(record));
    }
    if (result.empty())
    {
        fail("empty mlkem measurement set");
    }
    return result;
}

synthesis_measurement parse_synthesis_measurement(std::string_view text)
{
    const json_value root = json_parser(text).parse();
    synthesis_measurement result{
        .yosys_version = string_field(root, "yosys_version"),
        .nextpnr_version = string_field(root, "nextpnr_version"),
    };
    const json_value &seeds = object_field(root, "seeds");
    if (seeds.kind != json_kind::array || seeds.elements.empty())
    {
        fail("synthesis seeds must be a nonempty array");
    }
    for (const json_value &seed : seeds.elements)
    {
        const std::uint64_t number = uint_field(seed, "seed");
        if (number > std::numeric_limits<std::uint32_t>::max())
        {
            fail("measurement integer overflow: seed");
        }
        synthesis_seed parsed{
            .seed = static_cast<std::uint32_t>(number),
            .lut4 = uint_field(seed, "lut4"),
            .flip_flops = uint_field(seed, "flip_flops"),
            .dsp = uint_field(seed, "dsp"),
            .bram = uint_field(seed, "bram"),
            .maximum_frequency_mhz = double_field(seed, "maximum_frequency_mhz"),
            .meets_50mhz = bool_field(seed, "meets_50mhz"),
            .command = string_field(seed, "command"),
        };
        if (parsed.maximum_frequency_mhz < 0.0 ||
            parsed.meets_50mhz != (parsed.maximum_frequency_mhz >= 50.0))
        {
            fail("inconsistent synthesis frequency result");
        }
        result.seeds.push_back(std::move(parsed));
    }
    return result;
}

}
