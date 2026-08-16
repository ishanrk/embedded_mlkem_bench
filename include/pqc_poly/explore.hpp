#ifndef PQC_POLY_EXPLORE_HPP
#define PQC_POLY_EXPLORE_HPP

#include <iosfwd>
#include <span>
#include <stdexcept>
#include <string_view>

namespace pqc_poly
{

class explore_error final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

int run(std::span<const std::string_view> arguments, std::ostream &standard_output,
        std::ostream &standard_error) noexcept;

}

#endif
