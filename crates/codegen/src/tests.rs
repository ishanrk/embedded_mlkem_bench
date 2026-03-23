use pqc_poly_selector::{
    Aliasing,
    AnalysisVerdict,
    InputRepresentation,
    Operation,
    OutputRepresentation,
    Request,
    Schedule,
    SchoolbookPlan,
    find,
    pick,
};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicU64, Ordering};

use super::*;

fn request(operation: Operation, aliasing: Aliasing) -> Request
{
    Request::from_parts(
        operation,
        8,
        17,
        InputRepresentation::Centered,
        OutputRepresentation::Canonical,
        aliasing,
        "test".into(),
        32,
        32,
        vec![32],
        1_000,
    )
    .unwrap()
}

fn verdict(schedule: Schedule, block_size: u64) -> AnalysisVerdict
{
    let temporary_bytes = match schedule
    {
        Schedule::Full => 60,
        Schedule::Fold => 32,
        Schedule::Output => 0,
    };
    let additions = match schedule
    {
        Schedule::Full => 71,
        Schedule::Fold | Schedule::Output => 64,
    };

    AnalysisVerdict
    {
        plan: SchoolbookPlan::new(schedule, 32, block_size),
        temporary_bytes,
        alias_safe: schedule != Schedule::Output,
        accumulator_bound: 512,
        required_bits: 11,
        multiplications: 64,
        additions,
        reductions: 8,
        legal: true,
        failure_reasons: Vec::new(),
    }
}

fn tail_request(operation: Operation) -> Request
{
    Request::from_parts(
        operation,
        5,
        17,
        InputRepresentation::Centered,
        OutputRepresentation::Canonical,
        Aliasing::No,
        "compiled-test".into(),
        32,
        32,
        vec![32],
        1_000,
    )
    .unwrap()
}

fn widening_request() -> Request
{
    Request::from_parts(
        Operation::NegacyclicMul,
        2,
        i32::MAX as u32,
        InputRepresentation::Canonical,
        OutputRepresentation::Canonical,
        Aliasing::No,
        "compiled-i64-test".into(),
        32,
        32,
        vec![64],
        1_000,
    )
    .unwrap()
}

fn mlkem_request() -> Request
{
    Request::from_parts(
        Operation::NegacyclicMul,
        256,
        3_329,
        InputRepresentation::Centered,
        OutputRepresentation::Canonical,
        Aliasing::No,
        "rv32im".into(),
        32,
        32,
        vec![32, 64],
        4_096,
    )
    .unwrap()
}

fn reference_product(operation: Operation, left: &[i32], right: &[i32], modulus: i32) -> Vec<i32>
{
    let coefficient_count = left.len();
    let mut accumulators = vec![0i128; coefficient_count];

    for (left_index, left_coefficient) in left.iter().enumerate()
    {
        for (right_index, right_coefficient) in right.iter().enumerate()
        {
            let product = i128::from(*left_coefficient) * i128::from(*right_coefficient);
            let unfolded_index = left_index + right_index;

            if unfolded_index < coefficient_count
            {
                accumulators[unfolded_index] += product;
            }
            else
            {
                let output_index = unfolded_index - coefficient_count;
                match operation
                {
                    Operation::CyclicMul => accumulators[output_index] += product,
                    Operation::NegacyclicMul => accumulators[output_index] -= product,
                }
            }
        }
    }

    accumulators
        .into_iter()
        .map(|value| value.rem_euclid(i128::from(modulus)) as i32)
        .collect()
}

fn comma_separated(values: &[i32]) -> String
{
    values
        .iter()
        .map(i32::to_string)
        .collect::<Vec<_>>()
        .join(", ")
}

static NEXT_TEMPORARY_DIRECTORY: AtomicU64 = AtomicU64::new(0);

struct TemporaryDirectory
{
    path: PathBuf,
}

impl TemporaryDirectory
{
    fn new(label: &str) -> Self
    {
        let sequence = NEXT_TEMPORARY_DIRECTORY.fetch_add(1, Ordering::Relaxed);
        let path = std::env::temp_dir().join(format!(
            "pqc-poly-codegen-{label}-{}-{sequence}",
            std::process::id(),
        ));

        fs::create_dir(&path).unwrap();
        Self { path }
    }

    fn path(&self) -> &Path
    {
        &self.path
    }
}

impl Drop for TemporaryDirectory
{
    fn drop(&mut self)
    {
        let _ = fs::remove_dir_all(&self.path);
    }
}

fn run_checked(command: &mut Command, description: &str)
{
    let output = command.output().unwrap_or_else(|error|
    {
        panic!("could not run {description}: {error}");
    });

    assert!(
        output.status.success(),
        "{description} failed with {}\nstdout:\n{}\nstderr:\n{}",
        output.status,
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr),
    );
}

fn compile_and_run_c_harness(
    directory: &Path,
    request: &Request,
    analysis: &AnalysisVerdict,
    harness: &str,
    compiler_arguments: &[&str],
    runner_description: &str,
)
{
    fs::write(directory.join("kernel.h"), generate_header(request)).unwrap();
    fs::write(directory.join("kernel.c"), generate_c(request, analysis).unwrap()).unwrap();
    fs::write(directory.join("main.c"), harness).unwrap();

    let executable = directory.join("c-kernel-test");
    let mut compiler = Command::new("cc");
    compiler
        .current_dir(directory)
        .args(["-std=c11", "-Wall", "-Wextra", "-Werror"])
        .args(compiler_arguments)
        .args(["kernel.c", "main.c", "-o"])
        .arg(&executable);
    run_checked(&mut compiler, "generated C compilation");

    let mut runner = Command::new(&executable);
    runner.current_dir(directory);
    run_checked(&mut runner, runner_description);
}

fn c_differential_harness(
    left: &[i32],
    right: &[i32],
    expected: &[i32],
) -> String
{
    format!(
        r#"#include "kernel.h"

#include <stdint.h>

int main(void)
{{
    int32_t left[POLYSEL_N] = {{{}}};
    int32_t right[POLYSEL_N] = {{{}}};
    int32_t result[POLYSEL_N] = {{0}};
    const int32_t expected[POLYSEL_N] = {{{}}};

    polysel_mul(result, left, right);
    for (size_t index = 0; index < POLYSEL_N; index++)
    {{
        if (result[index] != expected[index])
        {{
            return 1;
        }}
    }}
    return 0;
}}
"#,
        comma_separated(left),
        comma_separated(right),
        comma_separated(expected),
    )
}

fn compile_and_run_c(
    directory: &Path,
    request: &Request,
    analysis: &AnalysisVerdict,
    left: &[i32],
    right: &[i32],
    expected: &[i32],
)
{
    let harness = c_differential_harness(left, right, expected);

    compile_and_run_c_harness(
        directory,
        request,
        analysis,
        &harness,
        &["-O2"],
        "generated C differential test",
    );
}

fn compile_and_run_c_aliasing(
    directory: &Path,
    request: &Request,
    analysis: &AnalysisVerdict,
    left: &[i32],
    right: &[i32],
    expected: &[i32],
    same_expected: &[i32],
)
{
    let harness = format!(
        r#"#include "kernel.h"

#include <stdint.h>

static int equal(const int32_t *actual, const int32_t *expected)
{{
    for (size_t index = 0; index < POLYSEL_N; index++)
    {{
        if (actual[index] != expected[index])
        {{
            return 0;
        }}
    }}
    return 1;
}}

int main(void)
{{
    const int32_t expected[POLYSEL_N] = {{{}}};
    const int32_t same_expected[POLYSEL_N] = {{{}}};
    int32_t left_result[POLYSEL_N] = {{{}}};
    int32_t right_for_left[POLYSEL_N] = {{{}}};

    polysel_mul(left_result, left_result, right_for_left);
    if (!equal(left_result, expected))
    {{
        return 1;
    }}

    int32_t left_for_right[POLYSEL_N] = {{{}}};
    int32_t right_result[POLYSEL_N] = {{{}}};

    polysel_mul(right_result, left_for_right, right_result);
    if (!equal(right_result, expected))
    {{
        return 2;
    }}

    int32_t all_same[POLYSEL_N] = {{{}}};

    polysel_mul(all_same, all_same, all_same);
    if (!equal(all_same, same_expected))
    {{
        return 3;
    }}

    return 0;
}}
"#,
        comma_separated(expected),
        comma_separated(same_expected),
        comma_separated(left),
        comma_separated(right),
        comma_separated(left),
        comma_separated(right),
        comma_separated(left),
    );

    compile_and_run_c_harness(
        directory,
        request,
        analysis,
        &harness,
        &["-O2"],
        "generated C aliasing differential test",
    );
}

fn compile_and_run_rust_harness(
    directory: &Path,
    request: &Request,
    analysis: &AnalysisVerdict,
    harness: &str,
    runner_description: &str,
)
{
    fs::write(
        directory.join("kernel.rs"),
        generate_rust(request, analysis).unwrap(),
    )
    .unwrap();
    fs::write(directory.join("main.rs"), harness).unwrap();

    let library = directory.join("libgenerated_kernel.rlib");
    let mut library_compiler = Command::new("rustc");
    library_compiler
        .current_dir(directory)
        .args([
            "--edition=2024",
            "--crate-name=generated_kernel",
            "--crate-type=rlib",
            "kernel.rs",
            "-o",
        ])
        .arg(&library);
    run_checked(&mut library_compiler, "generated no_std Rust compilation");

    let executable = directory.join("rust-kernel-test");
    let mut harness_compiler = Command::new("rustc");
    harness_compiler
        .current_dir(directory)
        .args(["--edition=2024", "main.rs", "--extern"])
        .arg(format!("generated_kernel={}", library.display()))
        .arg("-o")
        .arg(&executable);
    run_checked(&mut harness_compiler, "generated Rust harness compilation");

    let mut runner = Command::new(&executable);
    runner.current_dir(directory);
    run_checked(&mut runner, runner_description);
}

fn compile_and_run_rust(
    directory: &Path,
    request: &Request,
    analysis: &AnalysisVerdict,
    left: &[i32],
    right: &[i32],
    expected: &[i32],
)
{
    let harness = format!(
            r#"extern crate generated_kernel;

fn main()
{{
    let left = [{}];
    let right = [{}];
    let mut result = [0i32; {}];
    let expected = [{}];

    unsafe
    {{
        generated_kernel::polysel_mul(
            result.as_mut_ptr(),
            left.as_ptr(),
            right.as_ptr(),
        );
    }}
    assert_eq!(result, expected);
}}
"#,
            comma_separated(left),
            comma_separated(right),
            expected.len(),
            comma_separated(expected),
        );

    compile_and_run_rust_harness(
        directory,
        request,
        analysis,
        &harness,
        "generated Rust differential test",
    );
}

fn compile_and_run_rust_aliasing(
    directory: &Path,
    request: &Request,
    analysis: &AnalysisVerdict,
    left: &[i32],
    right: &[i32],
    expected: &[i32],
    same_expected: &[i32],
)
{
    let harness = format!(
        r#"extern crate generated_kernel;

fn main()
{{
    let expected = [{}];
    let same_expected = [{}];
    let mut left_result = [{}];
    let right_for_left = [{}];
    let left_result_pointer = left_result.as_mut_ptr();

    unsafe
    {{
        generated_kernel::polysel_mul(
            left_result_pointer,
            left_result_pointer.cast_const(),
            right_for_left.as_ptr(),
        );
    }}
    assert_eq!(left_result, expected);

    let left_for_right = [{}];
    let mut right_result = [{}];
    let right_result_pointer = right_result.as_mut_ptr();

    unsafe
    {{
        generated_kernel::polysel_mul(
            right_result_pointer,
            left_for_right.as_ptr(),
            right_result_pointer.cast_const(),
        );
    }}
    assert_eq!(right_result, expected);

    let mut all_same = [{}];
    let all_same_pointer = all_same.as_mut_ptr();

    unsafe
    {{
        generated_kernel::polysel_mul(
            all_same_pointer,
            all_same_pointer.cast_const(),
            all_same_pointer.cast_const(),
        );
    }}
    assert_eq!(all_same, same_expected);
}}
"#,
        comma_separated(expected),
        comma_separated(same_expected),
        comma_separated(left),
        comma_separated(right),
        comma_separated(left),
        comma_separated(right),
        comma_separated(left),
    );

    compile_and_run_rust_harness(
        directory,
        request,
        analysis,
        &harness,
        "generated Rust aliasing differential test",
    );
}

#[test]
fn header_is_byte_identical_to_the_reference_shape()
{
    let generated = generate_header(&request(Operation::NegacyclicMul, Aliasing::No));
    let expected = r#"#ifndef POLYSEL_KERNEL_H
#define POLYSEL_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#define POLYSEL_N ((size_t)UINT64_C(8))
#define POLYSEL_Q INT32_C(17)

/*
 * input coeffs: [-8, 8]
 * output coeffs: [0, 16]
 * alias: r is disjoint from a and b; a and b may overlap
 */

void polysel_mul(int32_t *r, const int32_t *a, const int32_t *b);

#endif
"#;

    assert_eq!(generated, expected);
}

#[test]
fn full_c_source_is_byte_identical_to_the_reference()
{
    let generated = generate_c(
        &request(Operation::NegacyclicMul, Aliasing::No),
        &verdict(Schedule::Full, 0),
    )
    .unwrap();
    let expected = r#"#include "kernel.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

/* plan sb_full_i32; raw bound 512; explicit scratch 60 bytes */
typedef int32_t acc_t;

_Static_assert(512LL <= INT32_MAX, "accumulator too small");

static int32_t modq(acc_t x)
{
    acc_t y = x % (acc_t)POLYSEL_Q;
    y += (y < 0) * (acc_t)POLYSEL_Q;
    return (int32_t)y;
}

void polysel_mul(int32_t *restrict r, const int32_t *a, const int32_t *b)
{
    acc_t t[2 * POLYSEL_N - 1] = {0};

    for (size_t i = 0; i < POLYSEL_N; i++) {
        for (size_t j = 0; j < POLYSEL_N; j++)
            t[i + j] += (acc_t)a[i] * (acc_t)b[j];
    }

    for (size_t i = 0; i + 1 < POLYSEL_N; i++)
        t[i] -= t[i + POLYSEL_N];

    for (size_t i = 0; i < POLYSEL_N; i++)
        r[i] = modq(t[i]);
}
"#;

    assert_eq!(generated, expected);
}

#[test]
fn cyclic_and_negacyclic_wrapping_use_opposite_signs_in_every_schedule()
{
    let cyclic_request = request(Operation::CyclicMul, Aliasing::No);
    let negacyclic_request = request(Operation::NegacyclicMul, Aliasing::No);

    for (schedule, block_size, cyclic_line, negacyclic_line) in
    [
        (Schedule::Full, 0, "t[i] += t[i + POLYSEL_N];", "t[i] -= t[i + POLYSEL_N];"),
        (
            Schedule::Fold,
            4,
            "t[j - (POLYSEL_N - i)] += v;",
            "t[j - (POLYSEL_N - i)] -= v;",
        ),
        (
            Schedule::Output,
            0,
            "s += (acc_t)a[i] * (acc_t)b[POLYSEL_N - (i - k)];",
            "s -= (acc_t)a[i] * (acc_t)b[POLYSEL_N - (i - k)];",
        ),
    ]
    {
        let analysis = verdict(schedule, block_size);
        let cyclic = generate_c(&cyclic_request, &analysis).unwrap();
        let negacyclic = generate_c(&negacyclic_request, &analysis).unwrap();

        assert!(cyclic.contains(cyclic_line));
        assert!(!cyclic.contains(negacyclic_line));
        assert!(negacyclic.contains(negacyclic_line));
        assert!(!negacyclic.contains(cyclic_line));
    }
}

#[test]
fn rust_kernel_is_standalone_no_std_and_uses_raw_pointers()
{
    let analysis = verdict(Schedule::Fold, 4);
    let first = generate_rust(
        &request(Operation::NegacyclicMul, Aliasing::May),
        &analysis,
    )
    .unwrap();
    let second = generate_rust(
        &request(Operation::NegacyclicMul, Aliasing::May),
        &analysis,
    )
    .unwrap();

    assert_eq!(first, second);
    assert!(first.starts_with("#![no_std]\n"));
    assert!(first.contains("result: *mut i32"));
    assert!(first.contains("left: *const i32"));
    assert!(first.contains("right: *const i32"));
    assert!(first.contains("result, left, and right may overlap"));
    assert!(first.contains("temporary[right_index - (POLYSEL_N - left_index)] -= product;"));
}

#[test]
fn inconsistent_verdict_is_rejected_before_generation()
{
    let request = request(Operation::NegacyclicMul, Aliasing::No);
    let mut analysis = verdict(Schedule::Full, 0);
    analysis.accumulator_bound += 1;

    let error = generate_c(&request, &analysis).unwrap_err();

    assert!(matches!(error, CodegenError::BadPlan(_)));
    assert!(error.to_string().contains("bad range"));
}

#[test]
fn legal_alias_contract_controls_restrict_and_illegal_output_is_gated()
{
    let request = request(Operation::NegacyclicMul, Aliasing::May);
    let safe_analysis = verdict(Schedule::Fold, 4);
    let source = generate_c(&request, &safe_analysis).unwrap();

    assert!(generate_header(&request).contains("alias: r, a, and b may overlap"));
    assert!(source.contains(
        "void polysel_mul(int32_t *r, const int32_t *a, const int32_t *b)",
    ));
    assert!(!source.contains("*restrict r"));

    let mut unsafe_analysis = verdict(Schedule::Output, 0);
    unsafe_analysis.legal = false;
    unsafe_analysis.failure_reasons.push("alias".into());

    assert_eq!(
        generate_rust(&request, &unsafe_analysis),
        Err(CodegenError::IllegalPlan),
    );
}

#[test]
fn generated_c_and_rust_kernels_compile_and_match_the_reference()
{
    let left = [8, -8, 7, -7, 1];
    let right = [-8, 3, 8, 0, -2];

    for operation in [Operation::NegacyclicMul, Operation::CyclicMul]
    {
        let request = tail_request(operation);
        let expected = reference_product(operation, &left, &right, 17);
        let trials = find(&request);
        let legal_ids: Vec<_> = trials
            .iter()
            .filter(|trial| trial.legal())
            .map(|trial| trial.id())
            .collect();

        assert_eq!(
            legal_ids,
            [
                "sb_full_i32",
                "sb_fold_b4_i32",
                "sb_fold_b5_i32",
                "sb_out_i32",
            ],
        );

        for trial in trials.iter().filter(|trial| trial.legal())
        {
            let label = format!("{:?}-{}", operation, trial.id());
            let c_directory = TemporaryDirectory::new(&format!("c-{label}"));
            let rust_directory = TemporaryDirectory::new(&format!("rust-{label}"));

            compile_and_run_c(
                c_directory.path(),
                &request,
                trial.analysis(),
                &left,
                &right,
                &expected,
            );
            compile_and_run_rust(
                rust_directory.path(),
                &request,
                trial.analysis(),
                &left,
                &right,
                &expected,
            );
        }
    }
}

#[test]
fn generated_alias_safe_c_and_rust_kernels_support_every_promised_overlap()
{
    let request = request(Operation::NegacyclicMul, Aliasing::May);
    let left = [1, -2, 3, -4, 2, 0, 1, -1];
    let right = [2, 1, -3, 1, 0, 2, -1, 4];
    let expected = reference_product(request.operation(), &left, &right, 17);
    let same_expected = reference_product(request.operation(), &left, &left, 17);
    let trials = find(&request);
    let legal_ids: Vec<_> = trials
        .iter()
        .filter(|trial| trial.legal())
        .map(|trial| trial.id())
        .collect();

    assert_eq!(
        legal_ids,
        ["sb_full_i32", "sb_fold_b4_i32", "sb_fold_b8_i32"],
    );
    assert!(trials.iter().filter(|trial| trial.legal()).all(|trial|
    {
        trial.alias_safe()
    }));
    assert!(trials.iter().any(|trial|
    {
        trial.schedule() == Schedule::Output
            && !trial.legal()
            && trial.failure_reasons() == ["alias"]
    }));

    for trial in trials.iter().filter(|trial| trial.legal())
    {
        let c_directory = TemporaryDirectory::new(&format!("c-alias-{}", trial.id()));
        let rust_directory = TemporaryDirectory::new(&format!("rust-alias-{}", trial.id()));

        compile_and_run_c_aliasing(
            c_directory.path(),
            &request,
            trial.analysis(),
            &left,
            &right,
            &expected,
            &same_expected,
        );
        compile_and_run_rust_aliasing(
            rust_directory.path(),
            &request,
            trial.analysis(),
            &left,
            &right,
            &expected,
            &same_expected,
        );
    }
}

#[test]
fn generated_c_and_rust_widen_before_canonical_i32_multiplication()
{
    let request = widening_request();
    let coefficient = i32::MAX - 1;
    let input = [coefficient, coefficient];
    let expected = reference_product(
        request.operation(),
        &input,
        &input,
        request.modulus() as i32,
    );
    let trials = find(&request);
    let legal_ids: Vec<_> = trials
        .iter()
        .filter(|trial| trial.legal())
        .map(|trial| trial.id())
        .collect();

    assert_eq!(expected, [0, 2]);
    assert_eq!(
        legal_ids,
        ["sb_full_i64", "sb_fold_b2_i64", "sb_out_i64"],
    );

    for trial in trials.iter().filter(|trial| trial.legal())
    {
        assert_eq!(trial.accumulator_bits(), 64);
        assert!(generate_c(&request, trial.analysis()).unwrap().contains(
            "typedef int64_t acc_t;",
        ));
        assert!(generate_rust(&request, trial.analysis()).unwrap().contains(
            "type Accumulator = i64;",
        ));

        let c_directory = TemporaryDirectory::new(&format!("c-i64-{}", trial.id()));
        let rust_directory = TemporaryDirectory::new(&format!("rust-i64-{}", trial.id()));

        compile_and_run_c(
            c_directory.path(),
            &request,
            trial.analysis(),
            &input,
            &input,
            &expected,
        );
        compile_and_run_rust(
            rust_directory.path(),
            &request,
            trial.analysis(),
            &input,
            &input,
            &expected,
        );
    }
}

#[test]
fn selected_mlkem_c_kernel_is_undefined_behavior_free_at_centered_extremes()
{
    let request = mlkem_request();
    let trials = find(&request);
    let selected = pick(trials.iter()).unwrap();
    let left: Vec<_> = (0..request.coefficient_count())
        .map(|index| if index & 1 == 0 { -1_664 } else { 1_664 })
        .collect();
    let right: Vec<_> = (0..request.coefficient_count())
        .map(|index| if index & 2 == 0 { -1_664 } else { 1_664 })
        .collect();
    let expected = reference_product(
        request.operation(),
        &left,
        &right,
        request.modulus() as i32,
    );
    let harness = c_differential_harness(&left, &right, &expected);
    let directory = TemporaryDirectory::new("c-mlkem-ubsan");

    assert_eq!(selected.id(), "sb_full_i32");
    compile_and_run_c_harness(
        directory.path(),
        &request,
        selected.analysis(),
        &harness,
        &[
            "-O1",
            "-fsanitize=undefined",
            "-fno-sanitize-recover=all",
        ],
        "generated C ML-KEM UBSan differential test",
    );
}
