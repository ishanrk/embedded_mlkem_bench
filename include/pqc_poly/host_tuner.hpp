#ifndef PQC_POLY_HOST_TUNER_HPP
#define PQC_POLY_HOST_TUNER_HPP

#include "pqc_poly/selector.hpp"
#include "pqc_poly/tuning.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace pqc_poly
{

struct host_tuning_options
{
    std::size_t samples{5};
    std::size_t iterations{16};
};

[[nodiscard]] std::vector<benchmark_record> tune_on_host(
    const request &req, std::span<const candidate_trial> candidates,
    const host_tuning_options &options = {});

}

#endif
