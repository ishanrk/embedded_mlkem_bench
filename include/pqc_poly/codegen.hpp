#pragma once

#include "pqc_poly/selector.hpp"

#include <stdexcept>
#include <string>

namespace pqc_poly
{

class codegen_error final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::string generate_header(const request &req, const candidate &selected);

[[nodiscard]] std::string generate_source(const request &req, const candidate &selected);

}
