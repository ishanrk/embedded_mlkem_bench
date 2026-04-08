#ifndef PQC_POLY_MLKEM_CODEGEN_HPP
#define PQC_POLY_MLKEM_CODEGEN_HPP

#include "pqc_poly/mlkem_plan.hpp"

#include <string>

namespace pqc_poly
{

[[nodiscard]] std::string generate_mlkem_backend(const mlkem_request &request,
                                                 const mlkem_candidate &candidate);

}

#endif
