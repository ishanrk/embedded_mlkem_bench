#include "pqc_poly/mlkem_plan.hpp"

#include <charconv>

namespace pqc_poly
{
namespace
{

void skip_space(std::string_view input, std::size_t &position) noexcept
{
    while (position < input.size())
    {
        const char c = input[position];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
        {
            break;
        }
        ++position;
    }
}

void expect(std::string_view input, std::size_t &position, char expected)
{
    skip_space(input, position);
    if (position == input.size() || input[position] != expected)
    {
        throw mlkem_error("invalid mlkem spec");
    }
    ++position;
}

}

mlkem_request parse_mlkem_request(std::string_view input)
{
    std::size_t position = 0;
    expect(input, position, '{');
    skip_space(input, position);

    constexpr std::string_view key = "\"scratch_limit\"";
    if (input.substr(position, key.size()) != key)
    {
        throw mlkem_error("mlkem spec must contain only scratch_limit");
    }
    position += key.size();
    expect(input, position, ':');
    skip_space(input, position);

    std::uint64_t scratch_limit = 0;
    const char *const begin = input.data() + position;
    const char *const end = input.data() + input.size();
    const auto parsed = std::from_chars(begin, end, scratch_limit);
    if (parsed.ec != std::errc{} || parsed.ptr == begin)
    {
        throw mlkem_error("invalid scratch_limit");
    }
    position = static_cast<std::size_t>(parsed.ptr - input.data());

    expect(input, position, '}');
    skip_space(input, position);
    if (position != input.size())
    {
        throw mlkem_error("trailing mlkem spec data");
    }

    return {.scratch_limit = scratch_limit};
}

}
