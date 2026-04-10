#include "pqc_poly/mlkem_codegen.hpp"

#include <string_view>

namespace pqc_poly
{
namespace
{

constexpr std::string_view prologue = R"pqc(#include <stdint.h>

#define PQC_MLKEM_N 256
#define PQC_MLKEM_Q 3329
#if defined(__GNUC__) || defined(__clang__)
#define PQC_FORCE_INLINE static inline __attribute__((always_inline))
#else
#define PQC_FORCE_INLINE static inline
#endif

static const int16_t pqc_zetas[128] = {
    -1044, -758, -359, -1517, 1493, 1422, 287, 202, -171, 622, 1577, 182, 962,
    -1202, -1474, 1468, 573, -1325, 264, 383, -829, 1458, -1602, -130, -681,
    1017, 732, 608, -1542, 411, -205, -1571, 1223, 652, -552, 1015, -1293,
    1491, -282, -1544, 516, -8, -320, -666, -1618, -1162, 126, 1469, -853,
    -90, -271, 830, 107, -1421, -247, -951, -398, 961, -1508, -725, 448,
    -1065, 677, -1275, -1103, 430, 555, 843, -1251, 871, 1550, 105, 422,
    587, 177, -235, -291, -460, 1574, 1653, -246, 778, 1159, -147, -777,
    1483, -602, 1119, -1590, 644, -872, 349, 418, 329, -156, -75, 817,
    1097, 603, 610, 1322, -1285, -1465, 384, -1215, -136, 1218, -1335,
    -874, 220, -1187, -1659, -1185, -1530, -1278, 794, -1510, -854, -870,
    478, -108, -308, 996, 991, 958, -1460, 1522, 1628,
};

PQC_FORCE_INLINE int16_t pqc_montgomery_reduce(int32_t a)
{
    const uint16_t inverted = (uint16_t)((uint32_t)(uint16_t)a * UINT32_C(62209));
    const int32_t t = inverted <= INT16_MAX ? (int32_t)inverted : (int32_t)inverted - 65536;
    return (int16_t)((a - t * PQC_MLKEM_Q) >> 16);
}

PQC_FORCE_INLINE int16_t pqc_fqmul(int16_t a, int16_t b)
{
    return pqc_montgomery_reduce((int32_t)a * (int32_t)b);
}

PQC_FORCE_INLINE int16_t pqc_barrett_reduce(int16_t a)
{
    const int32_t t = (INT32_C(20159) * a + (INT32_C(1) << 25)) >> 26;
    return (int16_t)(a - t * PQC_MLKEM_Q);
}

)pqc";

constexpr std::string_view custom_prologue = R"pqc(#include <stdint.h>

#include "fqmul.h"

#define PQC_MLKEM_N 256
#define PQC_MLKEM_Q 3329
#if defined(__GNUC__) || defined(__clang__)
#define PQC_FORCE_INLINE static inline __attribute__((always_inline))
#else
#define PQC_FORCE_INLINE static inline
#endif

static const int16_t pqc_zetas[128] = {
    -1044, -758, -359, -1517, 1493, 1422, 287, 202, -171, 622, 1577, 182, 962,
    -1202, -1474, 1468, 573, -1325, 264, 383, -829, 1458, -1602, -130, -681,
    1017, 732, 608, -1542, 411, -205, -1571, 1223, 652, -552, 1015, -1293,
    1491, -282, -1544, 516, -8, -320, -666, -1618, -1162, 126, 1469, -853,
    -90, -271, 830, 107, -1421, -247, -951, -398, 961, -1508, -725, 448,
    -1065, 677, -1275, -1103, 430, 555, 843, -1251, 871, 1550, 105, 422,
    587, 177, -235, -291, -460, 1574, 1653, -246, 778, 1159, -147, -777,
    1483, -602, 1119, -1590, 644, -872, 349, 418, 329, -156, -75, 817,
    1097, 603, 610, 1322, -1285, -1465, 384, -1215, -136, 1218, -1335,
    -874, 220, -1187, -1659, -1185, -1530, -1278, 794, -1510, -854, -870,
    478, -108, -308, 996, 991, 958, -1460, 1522, 1628,
};

PQC_FORCE_INLINE int16_t pqc_montgomery_reduce(int32_t a)
{
    const uint16_t inverted = (uint16_t)((uint32_t)(uint16_t)a * UINT32_C(62209));
    const int32_t t = inverted <= INT16_MAX ? (int32_t)inverted : (int32_t)inverted - 65536;
    return (int16_t)((a - t * PQC_MLKEM_Q) >> 16);
}

PQC_FORCE_INLINE int16_t pqc_fqmul(int16_t a, int16_t b)
{
    return (int16_t)pqc_mlk_fqmul((uint32_t)(int32_t)a, (uint32_t)(int32_t)b);
}

PQC_FORCE_INLINE int16_t pqc_barrett_reduce(int16_t a)
{
    const int32_t t = (INT32_C(20159) * a + (INT32_C(1) << 25)) >> 26;
    return (int16_t)(a - t * PQC_MLKEM_Q);
}

)pqc";

constexpr std::string_view forward_helpers =
    R"pqc(PQC_FORCE_INLINE void pqc_ntt_layer(int16_t r[256], unsigned length, unsigned zeta_index)
{
    for (unsigned start = 0; start < 256; start += 2U * length)
    {
        const int16_t zeta = pqc_zetas[zeta_index++];
        for (unsigned j = start; j < start + length; ++j)
        {
            const int16_t t = pqc_fqmul(r[j + length], zeta);
            const int16_t left = r[j];
            r[j] = (int16_t)(left + t);
            r[j + length] = (int16_t)(left - t);
        }
    }
}

PQC_FORCE_INLINE void pqc_ntt_pair(int16_t r[256], unsigned length, unsigned groups)
{
    for (unsigned g = 0; g < groups; ++g)
    {
        const int16_t z0 = pqc_zetas[groups + g];
        const int16_t z1 = pqc_zetas[2U * groups + 2U * g];
        const int16_t z2 = pqc_zetas[2U * groups + 2U * g + 1U];
        const unsigned start = 4U * length * g;
        for (unsigned j = 0; j < length; ++j)
        {
            const int16_t x0 = r[start + j];
            const int16_t x1 = r[start + length + j];
            const int16_t x2 = r[start + 2U * length + j];
            const int16_t x3 = r[start + 3U * length + j];
            const int16_t t0 = pqc_fqmul(x2, z0);
            const int16_t t1 = pqc_fqmul(x3, z0);
            const int16_t u0 = (int16_t)(x0 + t0);
            const int16_t u1 = (int16_t)(x1 + t1);
            const int16_t v0 = (int16_t)(x0 - t0);
            const int16_t v1 = (int16_t)(x1 - t1);
            const int16_t u1z = pqc_fqmul(u1, z1);
            const int16_t v1z = pqc_fqmul(v1, z2);
            r[start + j] = (int16_t)(u0 + u1z);
            r[start + length + j] = (int16_t)(u0 - u1z);
            r[start + 2U * length + j] = (int16_t)(v0 + v1z);
            r[start + 3U * length + j] = (int16_t)(v0 - v1z);
        }
    }
}

)pqc";

constexpr std::string_view inverse_helpers =
    R"pqc(PQC_FORCE_INLINE void pqc_intt_layer(int16_t r[256], unsigned length, unsigned zeta_index,
                           int reduce_sum)
{
    for (unsigned start = 0; start < 256; start += 2U * length)
    {
        const int16_t zeta = pqc_zetas[zeta_index--];
        for (unsigned j = start; j < start + length; ++j)
        {
            const int16_t left = r[j];
            const int16_t right = r[j + length];
            const int16_t sum = (int16_t)(left + right);
            r[j] = reduce_sum != 0 ? pqc_barrett_reduce(sum) : sum;
            r[j + length] = pqc_fqmul((int16_t)(right - left), zeta);
        }
    }
}

PQC_FORCE_INLINE void pqc_intt_pair(int16_t r[256], unsigned length, unsigned groups,
                          int reduce_each)
{
    const unsigned top = 2U * groups - 1U;
    for (unsigned h = 0; h < groups / 2U; ++h)
    {
        const int16_t z0 = pqc_zetas[top - 2U * h];
        const int16_t z1 = pqc_zetas[top - (2U * h + 1U)];
        const int16_t z2 = pqc_zetas[top - groups - h];
        const unsigned start = 4U * length * h;
        for (unsigned j = 0; j < length; ++j)
        {
            const int16_t x0 = r[start + j];
            const int16_t x1 = r[start + length + j];
            const int16_t x2 = r[start + 2U * length + j];
            const int16_t x3 = r[start + 3U * length + j];
            int16_t s0 = (int16_t)(x0 + x1);
            int16_t s1 = (int16_t)(x2 + x3);
            const int16_t d0 = pqc_fqmul((int16_t)(x1 - x0), z0);
            const int16_t d1 = pqc_fqmul((int16_t)(x3 - x2), z1);
            if (reduce_each != 0)
            {
                s0 = pqc_barrett_reduce(s0);
                s1 = pqc_barrett_reduce(s1);
            }
            r[start + j] = pqc_barrett_reduce((int16_t)(s0 + s1));
            r[start + length + j] = pqc_barrett_reduce((int16_t)(d0 + d1));
            r[start + 2U * length + j] = pqc_fqmul((int16_t)(s1 - s0), z2);
            r[start + 3U * length + j] = pqc_fqmul((int16_t)(d1 - d0), z2);
        }
    }
}

)pqc";

void append_forward(std::string &out, const mlkem_plan &plan)
{
    out += "void pqc_mlkem_ntt(int16_t r[256])\n{\n";
    if (plan.forward == ntt_traversal::stage_major)
    {
        out +=
            "    pqc_ntt_layer(r, 128, 1);\n"
            "    pqc_ntt_layer(r, 64, 2);\n"
            "    pqc_ntt_layer(r, 32, 4);\n"
            "    pqc_ntt_layer(r, 16, 8);\n"
            "    pqc_ntt_layer(r, 8, 16);\n"
            "    pqc_ntt_layer(r, 4, 32);\n"
            "    pqc_ntt_layer(r, 2, 64);\n";
    }
    else
    {
        out +=
            "    pqc_ntt_pair(r, 64, 1);\n"
            "    pqc_ntt_pair(r, 16, 4);\n"
            "    pqc_ntt_pair(r, 4, 16);\n"
            "    pqc_ntt_layer(r, 2, 64);\n";
    }
    out += "}\n\n";
}

void append_inverse(std::string &out, const mlkem_plan &plan)
{
    const bool each = plan.inverse_reduction == intt_sum_reduction::every_layer;
    out +=
        "void pqc_mlkem_intt(int16_t r[256])\n{\n"
        "    for (unsigned j = 0; j < 256; ++j)\n"
        "    {\n"
        "        r[j] = pqc_fqmul(r[j], 1441);\n"
        "    }\n";
    if (plan.inverse == intt_traversal::stage_major)
    {
        const char *first = each ? "1" : "0";
        out += "    pqc_intt_layer(r, 2, 127, ";
        out += first;
        out +=
            ");\n    pqc_intt_layer(r, 4, 63, 1);\n"
            "    pqc_intt_layer(r, 8, 31, ";
        out += first;
        out +=
            ");\n    pqc_intt_layer(r, 16, 15, 1);\n"
            "    pqc_intt_layer(r, 32, 7, ";
        out += first;
        out +=
            ");\n    pqc_intt_layer(r, 64, 3, 1);\n"
            "    pqc_intt_layer(r, 128, 1, 1);\n";
    }
    else
    {
        const char *reduce = each ? "1" : "0";
        out += "    pqc_intt_pair(r, 2, 64, ";
        out += reduce;
        out += ");\n    pqc_intt_pair(r, 8, 16, ";
        out += reduce;
        out += ");\n    pqc_intt_pair(r, 32, 4, ";
        out += reduce;
        out += ");\n    pqc_intt_layer(r, 128, 1, 1);\n";
    }
    out += "}\n\n";
}

void append_base(std::string &out, const mlkem_plan &plan)
{
    const unsigned k = mlkem_k(plan.level);
    out += "void pqc_mlkem_mulcache_one(int16_t *cache, const int16_t b[256])\n{\n";
    if (plan.basemul == basemul_schedule::direct_eager32)
    {
        out += "    (void)cache;\n    (void)b;\n";
    }
    else
    {
        out +=
            "    for (unsigned i = 0; i < 64; ++i)\n"
            "    {\n"
            "        cache[2U * i] = pqc_fqmul(b[4U * i + 1U], pqc_zetas[64U + i]);\n"
            "        cache[2U * i + 1U] =\n"
            "            pqc_fqmul(b[4U * i + 3U], (int16_t)-pqc_zetas[64U + i]);\n"
            "    }\n";
    }
    out += "}\n\n";

    out += "void pqc_mlkem_mulcache(int16_t *cache, const int16_t b[" + std::to_string(k * 256U) +
           "])\n{\n";
    if (plan.basemul == basemul_schedule::direct_eager32)
    {
        out += "    (void)cache;\n    (void)b;\n";
    }
    else
    {
        out += "    for (unsigned lane = 0; lane < " + std::to_string(k) +
               "; ++lane)\n"
               "    {\n"
               "        pqc_mlkem_mulcache_one(cache + lane * 128U, b + lane * 256U);\n"
               "    }\n";
    }
    out += "}\n\n";

    out += "void pqc_mlkem_basemul(int16_t r[256], const int16_t a[" + std::to_string(k * 256U) +
           "], const int16_t b[" + std::to_string(k * 256U) + "], const int16_t *cache)\n{\n";
    if (plan.basemul == basemul_schedule::direct_eager32)
    {
        out += "    (void)cache;\n";
    }
    out +=
        "    for (unsigned i = 0; i < 128; ++i)\n"
        "    {\n"
        "        int32_t t0 = 0;\n"
        "        int32_t t1 = 0;\n"
        "        for (unsigned lane = 0; lane < " +
        std::to_string(k) +
        "; ++lane)\n"
        "        {\n"
        "            const unsigned p = lane * 256U + 2U * i;\n";
    if (plan.basemul == basemul_schedule::cached_late32)
    {
        out +=
            "            const int16_t gb = cache[lane * 128U + i];\n"
            "            t0 += (int32_t)a[p + 1U] * gb + (int32_t)a[p] * b[p];\n"
            "            t1 += (int32_t)a[p] * b[p + 1U] + (int32_t)a[p + 1U] * b[p];\n";
    }
    else
    {
        if (plan.basemul == basemul_schedule::cached_eager32)
        {
            out += "            const int16_t gb = cache[lane * 128U + i];\n";
        }
        else
        {
            out +=
                "            const int16_t gamma = (i & 1U) == 0U\n"
                "                                      ? pqc_zetas[64U + i / 2U]\n"
                "                                      : (int16_t)-pqc_zetas[64U + i / 2U];\n"
                "            const int16_t gb = pqc_fqmul(b[p + 1U], gamma);\n";
        }
        out +=
            "            t0 += pqc_fqmul(a[p + 1U], gb) + pqc_fqmul(a[p], b[p]);\n"
            "            t1 += pqc_fqmul(a[p], b[p + 1U]) + pqc_fqmul(a[p + 1U], b[p]);\n";
    }
    out += "        }\n";
    if (plan.basemul == basemul_schedule::cached_late32)
    {
        out +=
            "        r[2U * i] = pqc_montgomery_reduce(t0);\n"
            "        r[2U * i + 1U] = pqc_montgomery_reduce(t1);\n";
    }
    else
    {
        out +=
            "        r[2U * i] = (int16_t)t0;\n"
            "        r[2U * i + 1U] = (int16_t)t1;\n";
    }
    out += "    }\n}\n\n";
}

void append_declarations(std::string &out, const mlkem_plan &plan)
{
    const unsigned k = mlkem_k(plan.level);
    out +=
        "void pqc_mlkem_ntt(int16_t r[256]);\n"
        "void pqc_mlkem_intt(int16_t r[256]);\n"
        "void pqc_mlkem_tomont(int16_t r[256]);\n"
        "void pqc_mlkem_mulcache_one(int16_t *cache, const int16_t b[256]);\n"
        "void pqc_mlkem_mulcache(int16_t *cache, const int16_t b[" +
        std::to_string(k * 256U) +
        "]);\n"
        "void pqc_mlkem_basemul(int16_t r[256], const int16_t a[" +
        std::to_string(k * 256U) + "], const int16_t b[" + std::to_string(k * 256U) +
        "], const int16_t *cache);\n\n";
}

}

std::string generate_mlkem_backend(const mlkem_request &request, const mlkem_candidate &candidate)
{
    const std::vector<std::string> errors = check_mlkem_plan(request, candidate);
    if (!errors.empty() || !candidate.legal)
    {
        throw mlkem_error("cannot generate rejected mlkem plan");
    }

    std::string out;
    out.reserve(16384);
    out += candidate.plan.instruction == mlkem_instruction::fqmul ? custom_prologue : prologue;
    append_declarations(out, candidate.plan);
    out += forward_helpers;
    out += inverse_helpers;
    append_forward(out, candidate.plan);
    append_inverse(out, candidate.plan);
    append_base(out, candidate.plan);
    out += R"pqc(void pqc_mlkem_tomont(int16_t r[256])
{
    for (unsigned i = 0; i < 256; ++i)
    {
        r[i] = pqc_fqmul(r[i], 1353);
    }
}
)pqc";
    return out;
}

}
