#ifndef PQC_POLY_JSON_HPP
#define PQC_POLY_JSON_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace pqc_poly::detail
{

inline void append_hex_quad(std::string &out, std::uint16_t value)
{
    constexpr std::string_view digits = "0123456789abcdef";

    out += "\\u";
    for (const unsigned shift : {12U, 8U, 4U, 0U})
    {
        out += digits[(value >> shift) & 0xfU];
    }
}

inline bool decode_utf8(std::string_view input, std::size_t &position,
                        std::uint32_t &code_point) noexcept
{
    const auto lead = static_cast<std::uint8_t>(input[position]);
    unsigned width = 0;
    std::uint32_t minimum = 0;

    if ((lead & 0xe0U) == 0xc0U)
    {
        width = 2;
        code_point = lead & 0x1fU;
        minimum = 0x80U;
    }
    else if ((lead & 0xf0U) == 0xe0U)
    {
        width = 3;
        code_point = lead & 0x0fU;
        minimum = 0x800U;
    }
    else if ((lead & 0xf8U) == 0xf0U)
    {
        width = 4;
        code_point = lead & 0x07U;
        minimum = 0x10000U;
    }
    else
    {
        return false;
    }

    if (input.size() - position < width)
    {
        return false;
    }
    for (unsigned index = 1; index < width; ++index)
    {
        const auto byte = static_cast<std::uint8_t>(input[position + index]);
        if ((byte & 0xc0U) != 0x80U)
        {
            return false;
        }
        code_point = (code_point << 6U) | (byte & 0x3fU);
    }
    if (code_point < minimum || code_point > 0x10ffffU ||
        (code_point >= 0xd800U && code_point <= 0xdfffU))
    {
        return false;
    }

    position += width;
    return true;
}

inline void append_json_string(std::string &out, std::string_view value)
{
    // ascii output keeps artifacts deterministic across host locales
    out += '"';
    for (std::size_t position = 0; position < value.size();)
    {
        const auto byte = static_cast<std::uint8_t>(value[position]);
        if (byte >= 0x80U)
        {
            std::uint32_t code_point = 0;
            if (!decode_utf8(value, position, code_point))
            {
                append_hex_quad(out, byte);
                ++position;
                continue;
            }
            if (code_point <= 0xffffU)
            {
                append_hex_quad(out, static_cast<std::uint16_t>(code_point));
            }
            else
            {
                code_point -= 0x10000U;
                append_hex_quad(out, static_cast<std::uint16_t>(0xd800U | (code_point >> 10U)));
                append_hex_quad(out, static_cast<std::uint16_t>(0xdc00U | (code_point & 0x3ffU)));
            }
            continue;
        }

        ++position;
        switch (byte)
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
                if (byte < 0x20U)
                {
                    append_hex_quad(out, byte);
                }
                else
                {
                    out += static_cast<char>(byte);
                }
                break;
        }
    }
    out += '"';
}

}

#endif
