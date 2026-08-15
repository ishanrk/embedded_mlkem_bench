#ifndef PQC_POLY_EXPLORE_HPP
#define PQC_POLY_EXPLORE_HPP

#include "pqc_poly/selector.hpp"
#include "pqc_poly/tuning.hpp"

#include <filesystem>
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

void emit(const std::filesystem::path &out, const request &req, const candidate_trial &selected,
          std::span<const candidate_trial> candidates);

void emit(const std::filesystem::path &out, const request &req, const candidate_trial &selected,
          std::span<const candidate_trial> candidates, std::span<const benchmark_record> benchmarks,
          latency_metric metric);

int run(std::span<const std::string_view> arguments, std::ostream &standard_output,
        std::ostream &standard_error) noexcept;

}

#endif
