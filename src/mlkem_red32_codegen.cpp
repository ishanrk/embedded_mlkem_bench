#include "pqc_poly/mlkem_codegen.hpp"

#include <string_view>

namespace pqc_poly
{
namespace
{

void replace_once(std::string &source, std::string_view before, std::string_view after)
{
    const std::size_t position = source.find(before);
    if (position == std::string::npos || source.find(before, position + before.size()) != std::string::npos)
    {
        throw mlkem_error("red32 codegen template changed");
    }
    source.replace(position, before.size(), after);
}

}

std::string generate_red32_backend(const mlkem_request &request,
                                   const red32_candidate &candidate)
{
    if (!candidate.legal || !check_red32_candidate(request, candidate).empty())
    {
        throw mlkem_error("cannot generate rejected red32 plan");
    }

    const mlkem_plan plan = red32_schedule_plan(candidate.plan);
    const mlkem_candidate software = analyze_mlkem_plan(request, plan);
    std::string source = generate_mlkem_backend(request, software);

    replace_once(source, "#include <stdint.h>\n\n", "#include <stdint.h>\n\n#include \"red32.h\"\n\n");
    replace_once(
        source,
        "PQC_FORCE_INLINE int16_t pqc_montgomery_reduce(int32_t a)\n"
        "{\n"
        "    const uint16_t inverted = (uint16_t)((uint32_t)(uint16_t)a * UINT32_C(62209));\n"
        "    const int32_t t = inverted <= INT16_MAX ? (int32_t)inverted : (int32_t)inverted - 65536;\n"
        "    return (int16_t)((a - t * PQC_MLKEM_Q) >> 16);\n"
        "}\n",
        "PQC_FORCE_INLINE int16_t pqc_montgomery_reduce(int32_t a)\n"
        "{\n"
        "    return (int16_t)pqc_mlk_red32((uint32_t)a);\n"
        "}\n");
    replace_once(
        source,
        "PQC_FORCE_INLINE int16_t pqc_fqmul(int16_t a, int16_t b)\n"
        "{\n"
        "    return pqc_montgomery_reduce((int32_t)a * (int32_t)b);\n"
        "}\n",
        "PQC_FORCE_INLINE int16_t pqc_fqmul(int16_t a, int16_t b)\n"
        "{\n"
        "    return (int16_t)pqc_mlk_fqmul_red32(a, b);\n"
        "}\n");
    return source;
}

}
