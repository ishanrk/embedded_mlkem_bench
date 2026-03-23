use pqc_poly_selector::{Aliasing, AnalysisVerdict, Request, Schedule};

use crate::{wrap_sign, WrapSign};

pub fn generate_header(request: &Request) -> String
{
    let alias_contract = match request.aliasing()
    {
        Aliasing::No => "r is disjoint from a and b; a and b may overlap",
        Aliasing::May => "r, a, and b may overlap",
    };

    format!(
        r#"#ifndef POLYSEL_KERNEL_H
#define POLYSEL_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#define POLYSEL_N ((size_t)UINT64_C({}))
#define POLYSEL_Q INT32_C({})

/*
 * input coeffs: [{}, {}]
 * output coeffs: [0, {}]
 * alias: {}
 */

void polysel_mul(int32_t *r, const int32_t *a, const int32_t *b);

#endif
"#,
        request.coefficient_count(),
        request.modulus(),
        request.input_lower_bound(),
        request.input_upper_bound(),
        request.modulus() - 1,
        alias_contract,
    )
}

pub(crate) fn generate_source(request: &Request, verdict: &AnalysisVerdict) -> String
{
    let accumulator_type = if verdict.accumulator_bits() == 32
    {
        "int32_t"
    }
    else
    {
        "int64_t"
    };
    let accumulator_limit = if verdict.accumulator_bits() == 32
    {
        "INT32_MAX"
    }
    else
    {
        "INT64_MAX"
    };
    let body = match verdict.schedule()
    {
        Schedule::Full => generate_full_body(request),
        Schedule::Fold => generate_fold_body(request, verdict.block_size()),
        Schedule::Output => generate_output_body(request),
    };
    let arguments = match request.aliasing()
    {
        Aliasing::No => "int32_t *restrict r, const int32_t *a, const int32_t *b",
        Aliasing::May => "int32_t *r, const int32_t *a, const int32_t *b",
    };

    format!(
        r#"#include "kernel.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

/* plan {}; raw bound {}; explicit scratch {} bytes */
typedef {} acc_t;

_Static_assert({}LL <= {}, "accumulator too small");

static int32_t modq(acc_t x)
{{
    acc_t y = x % (acc_t)POLYSEL_Q;
    y += (y < 0) * (acc_t)POLYSEL_Q;
    return (int32_t)y;
}}

void polysel_mul({})
{{
{}}}
"#,
        verdict.id(),
        verdict.accumulator_bound,
        verdict.temporary_bytes,
        accumulator_type,
        verdict.accumulator_bound,
        accumulator_limit,
        arguments,
        body,
    )
}

fn generate_full_body(request: &Request) -> String
{
    let fold_operator = match wrap_sign(request.operation())
    {
        WrapSign::Add => "+=",
        WrapSign::Subtract => "-=",
    };

    format!(
        r#"    acc_t t[2 * POLYSEL_N - 1] = {{0}};

    for (size_t i = 0; i < POLYSEL_N; i++) {{
        for (size_t j = 0; j < POLYSEL_N; j++)
            t[i + j] += (acc_t)a[i] * (acc_t)b[j];
    }}

    for (size_t i = 0; i + 1 < POLYSEL_N; i++)
        t[i] {} t[i + POLYSEL_N];

    for (size_t i = 0; i < POLYSEL_N; i++)
        r[i] = modq(t[i]);
"#,
        fold_operator,
    )
}

fn generate_fold_body(request: &Request, block: u64) -> String
{
    let fold_operator = match wrap_sign(request.operation())
    {
        WrapSign::Add => "+=",
        WrapSign::Subtract => "-=",
    };

    format!(
        r#"    acc_t t[POLYSEL_N] = {{0}};

    for (size_t ii = 0; ii < POLYSEL_N;) {{
        size_t ie = POLYSEL_N - ii < {block} ? POLYSEL_N : ii + {block};
        for (size_t jj = 0; jj < POLYSEL_N;) {{
            size_t je = POLYSEL_N - jj < {block} ? POLYSEL_N : jj + {block};
            for (size_t i = ii; i < ie; i++) {{
                for (size_t j = jj; j < je; j++) {{
                    acc_t v = (acc_t)a[i] * (acc_t)b[j];
                    if (j < POLYSEL_N - i)
                        t[i + j] += v;
                    else
                        t[j - (POLYSEL_N - i)] {fold_operator} v;
                }}
            }}
            jj = je;
        }}
        ii = ie;
    }}

    for (size_t i = 0; i < POLYSEL_N; i++)
        r[i] = modq(t[i]);
"#
    )
}

fn generate_output_body(request: &Request) -> String
{
    let fold_operator = match wrap_sign(request.operation())
    {
        WrapSign::Add => "+=",
        WrapSign::Subtract => "-=",
    };

    format!(
        r#"    for (size_t k = 0; k < POLYSEL_N; k++) {{
        acc_t s = 0;
        for (size_t i = 0; i <= k; i++)
            s += (acc_t)a[i] * (acc_t)b[k - i];
        for (size_t i = k + 1; i < POLYSEL_N; i++)
            s {} (acc_t)a[i] * (acc_t)b[POLYSEL_N - (i - k)];
        r[k] = modq(s);
    }}
"#,
        fold_operator,
    )
}
