use pqc_poly_selector::{Aliasing, AnalysisVerdict, Request, Schedule};

use crate::{wrap_sign, WrapSign};

pub(crate) fn generate_source(request: &Request, verdict: &AnalysisVerdict) -> String
{
    let accumulator_type = if verdict.accumulator_bits() == 32
    {
        "i32"
    }
    else
    {
        "i64"
    };
    let alias_contract = match request.aliasing()
    {
        Aliasing::No => "result must be disjoint from left and right; left and right may overlap",
        Aliasing::May => "result, left, and right may overlap",
    };
    let body = match verdict.schedule()
    {
        Schedule::Full => generate_full_body(request),
        Schedule::Fold => generate_fold_body(request, verdict.block_size()),
        Schedule::Output => generate_output_body(request),
    };

    format!(
        r#"#![no_std]

pub const POLYSEL_N: usize = {};
pub const POLYSEL_Q: i32 = {};

/* plan {}; raw bound {}; explicit scratch {} bytes */
type Accumulator = {};

const _: () = assert!({}u128 <= {}::MAX as u128, "accumulator too small");

#[inline]
fn reduce_modulo_q(value: Accumulator) -> i32
{{
    let mut reduced = value % POLYSEL_Q as Accumulator;
    reduced += (reduced < 0) as Accumulator * POLYSEL_Q as Accumulator;
    reduced as i32
}}

/// Multiplies two polynomials using the selected schoolbook schedule.
///
/// # Safety
///
/// Each pointer must address `POLYSEL_N` initialized `i32` coefficients. The
/// result pointer must be writable, and {}. Input coefficients must be in
/// [{}, {}].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn polysel_mul(
    result: *mut i32,
    left: *const i32,
    right: *const i32,
)
{{
{}}}
"#,
        request.coefficient_count(),
        request.modulus(),
        verdict.id(),
        verdict.accumulator_bound,
        verdict.temporary_bytes,
        accumulator_type,
        verdict.accumulator_bound,
        accumulator_type,
        alias_contract,
        request.input_lower_bound(),
        request.input_upper_bound(),
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
        r#"    let mut temporary = [0 as Accumulator; 2 * POLYSEL_N - 1];

    for left_index in 0..POLYSEL_N
    {{
        for right_index in 0..POLYSEL_N
        {{
            let left_coefficient = unsafe
            {{
                left.add(left_index).read()
            }} as Accumulator;
            let right_coefficient = unsafe
            {{
                right.add(right_index).read()
            }} as Accumulator;
            temporary[left_index + right_index] += left_coefficient * right_coefficient;
        }}
    }}

    for coefficient_index in 0..POLYSEL_N - 1
    {{
        temporary[coefficient_index] {} temporary[coefficient_index + POLYSEL_N];
    }}

    for coefficient_index in 0..POLYSEL_N
    {{
        let coefficient = reduce_modulo_q(temporary[coefficient_index]);
        unsafe
        {{
            result.add(coefficient_index).write(coefficient);
        }}
    }}
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
        r#"    let mut temporary = [0 as Accumulator; POLYSEL_N];
    let mut left_block_start = 0usize;

    while left_block_start < POLYSEL_N
    {{
        let left_block_end = if POLYSEL_N - left_block_start < {block}
        {{
            POLYSEL_N
        }}
        else
        {{
            left_block_start + {block}
        }};
        let mut right_block_start = 0usize;

        while right_block_start < POLYSEL_N
        {{
            let right_block_end = if POLYSEL_N - right_block_start < {block}
            {{
                POLYSEL_N
            }}
            else
            {{
                right_block_start + {block}
            }};

            for left_index in left_block_start..left_block_end
            {{
                for right_index in right_block_start..right_block_end
                {{
                    let left_coefficient = unsafe
                    {{
                        left.add(left_index).read()
                    }} as Accumulator;
                    let right_coefficient = unsafe
                    {{
                        right.add(right_index).read()
                    }} as Accumulator;
                    let product = left_coefficient * right_coefficient;

                    if right_index < POLYSEL_N - left_index
                    {{
                        temporary[left_index + right_index] += product;
                    }}
                    else
                    {{
                        temporary[right_index - (POLYSEL_N - left_index)] {fold_operator} product;
                    }}
                }}
            }}

            right_block_start = right_block_end;
        }}

        left_block_start = left_block_end;
    }}

    for coefficient_index in 0..POLYSEL_N
    {{
        let coefficient = reduce_modulo_q(temporary[coefficient_index]);
        unsafe
        {{
            result.add(coefficient_index).write(coefficient);
        }}
    }}
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
        r#"    for output_index in 0..POLYSEL_N
    {{
        let mut accumulator = 0 as Accumulator;

        for left_index in 0..=output_index
        {{
            let left_coefficient = unsafe
            {{
                left.add(left_index).read()
            }} as Accumulator;
            let right_index = output_index - left_index;
            let right_coefficient = unsafe
            {{
                right.add(right_index).read()
            }} as Accumulator;
            accumulator += left_coefficient * right_coefficient;
        }}
        for left_index in output_index + 1..POLYSEL_N
        {{
            let left_coefficient = unsafe
            {{
                left.add(left_index).read()
            }} as Accumulator;
            let right_index = POLYSEL_N - (left_index - output_index);
            let right_coefficient = unsafe
            {{
                right.add(right_index).read()
            }} as Accumulator;
            accumulator {} left_coefficient * right_coefficient;
        }}

        unsafe
        {{
            result.add(output_index).write(reduce_modulo_q(accumulator));
        }}
    }}
"#,
        fold_operator,
    )
}
