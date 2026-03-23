use pqc_poly_selector::{check_trial, find, frontier, load_request, pick, CandidateTrial, Request};
use serde::Serialize;
use std::ffi::OsString;
use std::fmt;
use std::fs;
use std::io::{self, Write};
use std::path::{Path, PathBuf};

const DEFAULT_OUTPUT_DIRECTORY: &str = "out";

#[derive(Debug)]
struct Arguments
{
    specification: PathBuf,
    output_directory: PathBuf,
    plan: Option<String>,
}

enum ArgumentAction
{
    Run(Arguments),
    Help,
}

#[derive(Debug)]
pub struct ExploreError(String);

impl fmt::Display for ExploreError
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result
    {
        formatter.write_str(&self.0)
    }
}

impl std::error::Error for ExploreError {}

impl From<io::Error> for ExploreError
{
    fn from(error: io::Error) -> Self
    {
        Self(error.to_string())
    }
}

impl From<serde_json::Error> for ExploreError
{
    fn from(error: serde_json::Error) -> Self
    {
        Self(error.to_string())
    }
}

#[derive(Serialize)]
struct Verification
{
    analysis_consistency: &'static str,
    compile: &'static str,
    differential_test: &'static str,
    target_run: &'static str,
}

#[derive(Serialize)]
struct PlanMetadata<'a>
{
    request: &'a Request,
    plan: &'a pqc_poly_selector::SchoolbookPlan,
    analysis: &'a pqc_poly_selector::AnalysisVerdict,
    score: &'a pqc_poly_selector::StaticScore,
    verification: Verification,
    scratch_accounting: &'static str,
    selection: &'static str,
}

#[derive(Serialize)]
struct RunSummary
{
    selected: String,
    acc_bits: u16,
    tmp_bytes: u128,
    need_bits: u16,
    frontier: Vec<String>,
    out: String,
}

fn usage() -> &'static str
{
    "usage: pqc-poly-bench [-h] [-o OUT] [--plan PLAN] spec\n"
}

fn option_value(
    arguments: &mut impl Iterator<Item = OsString>,
    option: &str,
) -> Result<OsString, ExploreError>
{
    arguments
        .next()
        .ok_or_else(|| ExploreError(format!("argument {option}: expected one argument")))
        .and_then(|value|
        {
            if value.is_empty()
            {
                Err(ExploreError(format!("argument {option}: expected one argument")))
            }
            else
            {
                Ok(value)
            }
        })
}

fn parse_arguments<I, S>(arguments: I) -> Result<ArgumentAction, ExploreError>
where
    I: IntoIterator<Item = S>,
    S: Into<OsString>,
{
    let mut arguments = arguments.into_iter().map(Into::into);
    let mut specification = None;
    let mut output_directory = PathBuf::from(DEFAULT_OUTPUT_DIRECTORY);
    let mut plan = None;
    let mut positional_only = false;

    while let Some(argument) = arguments.next()
    {
        if !positional_only && (argument == "-h" || argument == "--help")
        {
            return Ok(ArgumentAction::Help);
        }

        if !positional_only && argument == "--"
        {
            positional_only = true;
            continue;
        }

        if !positional_only && (argument == "-o" || argument == "--out")
        {
            output_directory = PathBuf::from(option_value(&mut arguments, "-o/--out")?);
            continue;
        }

        if !positional_only && argument == "--plan"
        {
            let value = option_value(&mut arguments, "--plan")?;
            plan = Some(
                value
                    .into_string()
                    .map_err(|_| ExploreError("argument --plan must be valid UTF-8".into()))?,
            );
            continue;
        }

        if !positional_only
        {
            if let Some(value) = argument.to_str().and_then(|value| value.strip_prefix("--out="))
            {
                if value.is_empty()
                {
                    return Err(ExploreError("argument --out: expected one argument".into()));
                }

                output_directory = PathBuf::from(value);
                continue;
            }

            if let Some(value) = argument.to_str().and_then(|value| value.strip_prefix("--plan="))
            {
                if value.is_empty()
                {
                    return Err(ExploreError("argument --plan: expected one argument".into()));
                }

                plan = Some(value.into());
                continue;
            }

            if let Some(value) = argument.to_str().and_then(|value| value.strip_prefix("-o"))
                && !value.is_empty()
            {
                let value = value.strip_prefix('=').unwrap_or(value);
                if value.is_empty()
                {
                    return Err(ExploreError("argument -o: expected one argument".into()));
                }

                output_directory = PathBuf::from(value);
                continue;
            }

            if argument.to_string_lossy().starts_with('-')
            {
                return Err(ExploreError(format!(
                    "unrecognized argument: {}",
                    argument.to_string_lossy(),
                )));
            }
        }

        if specification.replace(PathBuf::from(argument)).is_some()
        {
            return Err(ExploreError("expected exactly one request specification".into()));
        }
    }

    let specification = specification
        .ok_or_else(|| ExploreError("the following argument is required: spec".into()))?;

    Ok(ArgumentAction::Run(Arguments
    {
        specification,
        output_directory,
        plan,
    }))
}

fn pretty_json<T: Serialize>(value: &T) -> Result<String, ExploreError>
{
    let serialized = serde_json::to_string_pretty(value)?;
    let mut text = escape_non_ascii(&serialized);

    text.push('\n');
    Ok(text)
}

fn escape_non_ascii(text: &str) -> String
{
    let mut escaped = String::with_capacity(text.len());

    for character in text.chars()
    {
        let code_point = character as u32;

        if character.is_ascii()
        {
            escaped.push(character);
        }
        else if code_point <= u32::from(u16::MAX)
        {
            push_unicode_escape(&mut escaped, code_point as u16);
        }
        else
        {
            let supplementary = code_point - 0x1_0000;
            let high_surrogate = 0xd800 | (supplementary >> 10) as u16;
            let low_surrogate = 0xdc00 | (supplementary & 0x3ff) as u16;

            push_unicode_escape(&mut escaped, high_surrogate);
            push_unicode_escape(&mut escaped, low_surrogate);
        }
    }

    escaped
}

fn push_unicode_escape(output: &mut String, code_unit: u16)
{
    const HEX_DIGITS: &[u8; 16] = b"0123456789abcdef";

    output.push_str("\\u");
    for shift in [12, 8, 4, 0]
    {
        let digit = usize::from((code_unit >> shift) & 0xf);

        output.push(char::from(HEX_DIGITS[digit]));
    }
}

fn write_text(path: &Path, text: &str) -> Result<(), ExploreError>
{
    fs::write(path, text).map_err(|error|
    {
        ExploreError(format!("could not write {}: {error}", path.display()))
    })
}

pub fn emit(
    output_directory: impl AsRef<Path>,
    request: &Request,
    selected: &CandidateTrial,
    candidates: &[CandidateTrial],
) -> Result<PathBuf, ExploreError>
{
    emit_inner(output_directory.as_ref(), request, selected, candidates)
}

fn emit_inner(
    output_directory: &Path,
    request: &Request,
    selected: &CandidateTrial,
    candidates: &[CandidateTrial],
) -> Result<PathBuf, ExploreError>
{
    let errors = check_trial(request, selected);

    if !errors.is_empty()
    {
        return Err(ExploreError(format!("bad plan: {}", errors.join(", "))));
    }

    if !selected.legal()
    {
        return Err(ExploreError("cannot emit an illegal plan".into()));
    }

    let header = pqc_poly_codegen::generate_header(request);
    let c_source = pqc_poly_codegen::generate_c(request, selected.analysis())
        .map_err(|error| ExploreError(error.to_string()))?;
    let rust_source = pqc_poly_codegen::generate_rust(request, selected.analysis())
        .map_err(|error| ExploreError(error.to_string()))?;
    let metadata = PlanMetadata
    {
        request,
        plan: selected.plan(),
        analysis: selected.analysis(),
        score: selected.score(),
        verification: Verification
        {
            analysis_consistency: "pass",
            compile: "not_run",
            differential_test: "not_run",
            target_run: "not_run",
        },
        scratch_accounting: "explicit arrays only",
        selection: "static bootstrap; no target benchmark",
    };

    fs::create_dir_all(output_directory).map_err(|error|
    {
        ExploreError(format!(
            "could not create {}: {error}",
            output_directory.display(),
        ))
    })?;

    write_text(&output_directory.join("kernel.h"), &header)?;
    write_text(&output_directory.join("kernel.c"), &c_source)?;
    write_text(&output_directory.join("kernel.rs"), &rust_source)?;
    write_text(&output_directory.join("plan.json"), &pretty_json(&metadata)?)?;
    write_text(&output_directory.join("cands.json"), &pretty_json(&candidates)?)?;

    Ok(output_directory.to_path_buf())
}

fn select_plan<'a>(
    candidates: &'a [CandidateTrial],
    requested_plan: Option<&str>,
) -> Result<&'a CandidateTrial, ExploreError>
{
    if let Some(requested_plan) = requested_plan
    {
        let selected = candidates
            .iter()
            .find(|candidate| candidate.id() == requested_plan)
            .ok_or_else(|| ExploreError(format!("unknown plan: {requested_plan}")))?;

        if !selected.legal()
        {
            return Err(ExploreError(format!(
                "plan is illegal: {}",
                selected.failure_reasons().join(", "),
            )));
        }

        Ok(selected)
    }
    else
    {
        pick(candidates).map_err(|error| ExploreError(error.to_string()))
    }
}

fn execute(arguments: Arguments) -> Result<RunSummary, ExploreError>
{
    let request = load_request(&arguments.specification)
        .map_err(|error| ExploreError(error.to_string()))?;
    let candidates = find(&request);
    let selected = select_plan(&candidates, arguments.plan.as_deref())?;
    let output_directory = emit_inner(
        &arguments.output_directory,
        &request,
        selected,
        &candidates,
    )?;
    let frontier = frontier(&candidates);

    let frontier_ids = frontier
        .iter()
        .map(|candidate| candidate.id())
        .collect();

    Ok(RunSummary
    {
        selected: selected.id(),
        acc_bits: selected.accumulator_bits(),
        tmp_bytes: selected.temporary_bytes(),
        need_bits: selected.required_bits(),
        frontier: frontier_ids,
        out: output_directory.display().to_string(),
    })
}

fn run_with_writers<I, S>(arguments: I, stdout: &mut impl Write, stderr: &mut impl Write) -> i32
where
    I: IntoIterator<Item = S>,
    S: Into<OsString>,
{
    match parse_arguments(arguments)
    {
        Ok(ArgumentAction::Help) =>
        {
            let _ = stdout.write_all(usage().as_bytes());
            0
        }
        Ok(ArgumentAction::Run(arguments)) => match execute(arguments)
        {
            Ok(summary) => match pretty_json(&summary)
            {
                Ok(text) =>
                {
                    if stdout.write_all(text.as_bytes()).is_ok()
                    {
                        0
                    }
                    else
                    {
                        2
                    }
                }
                Err(error) =>
                {
                    let _ = writeln!(stderr, "error: {error}");
                    2
                }
            },
            Err(error) =>
            {
                let _ = writeln!(stderr, "error: {error}");
                2
            }
        },
        Err(error) =>
        {
            let _ = stderr.write_all(usage().as_bytes());
            let _ = writeln!(stderr, "error: {error}");
            2
        }
    }
}

pub fn run<I, S>(arguments: I) -> i32
where
    I: IntoIterator<Item = S>,
    S: Into<OsString>,
{
    run_with_writers(arguments, &mut io::stdout().lock(), &mut io::stderr().lock())
}

#[cfg(test)]
mod tests
{
    use super::*;
    use std::collections::BTreeSet;
    use std::ffi::OsStr;
    use std::sync::atomic::{AtomicU64, Ordering};

    static NEXT_DIRECTORY: AtomicU64 = AtomicU64::new(0);

    struct TestDirectory
    {
        path: PathBuf,
    }

    impl TestDirectory
    {
        fn new(label: &str) -> Self
        {
            let number = NEXT_DIRECTORY.fetch_add(1, Ordering::Relaxed);
            let path = std::env::temp_dir().join(format!(
                "pqc-poly-explore-{}-{label}-{number}",
                std::process::id(),
            ));

            fs::create_dir(&path).expect("test directory should be created");
            Self { path }
        }

        fn join(&self, path: impl AsRef<Path>) -> PathBuf
        {
            self.path.join(path)
        }

        fn write_request(&self, name: &str, request: serde_json::Value) -> PathBuf
        {
            let path = self.join(name);

            fs::write(&path, pretty_json(&request).unwrap()).unwrap();
            path
        }
    }

    impl Drop for TestDirectory
    {
        fn drop(&mut self)
        {
            let _ = fs::remove_dir_all(&self.path);
        }
    }

    fn request(alias: &str, ram: u64) -> serde_json::Value
    {
        serde_json::json!({
            "op": "negacyclic_mul",
            "n": 8,
            "q": 17,
            "input": "centered",
            "output": "canonical",
            "alias": alias,
            "target": {
                "name": "test",
                "word_bits": 32,
                "size_bits": 32,
                "acc_bits": 32,
            },
            "limits": { "ram": ram },
        })
    }

    fn captured_run<I, S>(arguments: I) -> (i32, String, String)
    where
        I: IntoIterator<Item = S>,
        S: Into<OsString>,
    {
        let mut stdout = Vec::new();
        let mut stderr = Vec::new();
        let status = run_with_writers(arguments, &mut stdout, &mut stderr);

        (
            status,
            String::from_utf8(stdout).unwrap(),
            String::from_utf8(stderr).unwrap(),
        )
    }

    #[test]
    fn parser_uses_reference_defaults_and_accepts_option_forms()
    {
        let ArgumentAction::Run(arguments) = parse_arguments(["request.json"]).unwrap() else
        {
            panic!("request should run");
        };

        assert_eq!(arguments.specification, Path::new("request.json"));
        assert_eq!(arguments.output_directory, Path::new("out"));
        assert_eq!(arguments.plan, None);

        let ArgumentAction::Run(arguments) = parse_arguments([
            "--out=generated files",
            "--plan=sb_out_i32",
            "request.json",
        ])
        .unwrap()
        else
        {
            panic!("request should run");
        };

        assert_eq!(arguments.output_directory, Path::new("generated files"));
        assert_eq!(arguments.plan.as_deref(), Some("sb_out_i32"));

        let ArgumentAction::Run(arguments) =
            parse_arguments(["-o=generated files", "request.json"]).unwrap()
        else
        {
            panic!("request should run");
        };

        assert_eq!(arguments.output_directory, Path::new("generated files"));
    }

    #[test]
    fn json_uses_python_compatible_ascii_escaping()
    {
        let text = pretty_json(&serde_json::json!({ "name": "rv32-μ-🚀" })).unwrap();

        assert!(text.is_ascii());
        assert!(text.contains(r#""rv32-\u03bc-\ud83d\ude80""#));
    }

    #[test]
    fn bundled_examples_select_legal_full_schedules()
    {
        let examples = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../examples");
        let cases = [
            (
                "mlkem.json",
                pqc_poly_selector::Operation::NegacyclicMul,
                "sb_full_i32",
            ),
            (
                "ntruhps2048509.json",
                pqc_poly_selector::Operation::CyclicMul,
                "sb_full_i32",
            ),
        ];

        for (name, operation, selected_id) in cases
        {
            let request = load_request(examples.join(name)).unwrap();
            let candidates = find(&request);
            let selected = pick(candidates.iter()).unwrap();

            assert_eq!(request.operation(), operation);
            assert_eq!(selected.id(), selected_id);
        }
    }

    #[test]
    fn run_emits_deterministic_artifacts()
    {
        let directory = TestDirectory::new("deterministic");
        let specification = directory.write_request("request.json", request("no", 60));
        let first = directory.join("first");
        let second = directory.join("second output");
        let (first_status, first_stdout, first_stderr) = captured_run([
            specification.as_os_str(),
            OsStr::new("-o"),
            first.as_os_str(),
        ]);
        let (second_status, _, second_stderr) = captured_run([
            specification.as_os_str(),
            OsStr::new("--out"),
            second.as_os_str(),
        ]);

        assert_eq!(first_status, 0, "{first_stderr}");
        assert_eq!(second_status, 0, "{second_stderr}");
        assert!(first_stderr.is_empty());
        assert!(second_stderr.is_empty());

        let summary: serde_json::Value = serde_json::from_str(&first_stdout).unwrap();

        assert_eq!(summary["selected"], "sb_full_i32");
        assert_eq!(summary["acc_bits"], 32);
        assert_eq!(summary["tmp_bytes"], 60);
        assert_eq!(summary["need_bits"], 11);
        assert_eq!(
            summary["frontier"],
            serde_json::json!(["sb_out_i32", "sb_fold_b8_i32", "sb_full_i32"]),
        );

        let expected_files = BTreeSet::from([
            "cands.json".to_owned(),
            "kernel.c".to_owned(),
            "kernel.h".to_owned(),
            "kernel.rs".to_owned(),
            "plan.json".to_owned(),
        ]);
        let actual_files: BTreeSet<String> = fs::read_dir(&first)
            .unwrap()
            .map(|entry| entry.unwrap().file_name().into_string().unwrap())
            .collect();

        assert_eq!(actual_files, expected_files);

        for name in expected_files
        {
            assert_eq!(fs::read(first.join(&name)).unwrap(), fs::read(second.join(name)).unwrap());
        }

        let metadata: serde_json::Value =
            serde_json::from_slice(&fs::read(first.join("plan.json")).unwrap()).unwrap();

        assert_eq!(metadata["plan"]["id"], "sb_full_i32");
        assert_eq!(metadata["verification"]["analysis_consistency"], "pass");
        assert_eq!(metadata["analysis"]["legal"], true);

        let rust_source = fs::read_to_string(first.join("kernel.rs")).unwrap();

        assert!(rust_source.contains("#![no_std]"));
    }

    #[test]
    fn run_honors_legal_plan_override()
    {
        let directory = TestDirectory::new("override");
        let specification = directory.write_request("request.json", request("no", 60));
        let output = directory.join("output");
        let (status, stdout, stderr) = captured_run([
            specification.as_os_str(),
            OsStr::new("--plan"),
            OsStr::new("sb_out_i32"),
            OsStr::new("-o"),
            output.as_os_str(),
        ]);

        assert_eq!(status, 0, "{stderr}");
        assert!(stderr.is_empty());
        assert_eq!(
            serde_json::from_str::<serde_json::Value>(&stdout).unwrap()["selected"],
            "sb_out_i32",
        );

        let metadata: serde_json::Value =
            serde_json::from_slice(&fs::read(output.join("plan.json")).unwrap()).unwrap();

        assert_eq!(metadata["plan"]["id"], "sb_out_i32");
    }

    #[test]
    fn run_reports_unknown_illegal_and_invalid_requests()
    {
        let directory = TestDirectory::new("errors");
        let normal = directory.write_request("normal.json", request("no", 60));
        let aliasing = directory.write_request("aliasing.json", request("may", 60));
        let invalid = directory.write_request(
            "invalid.json",
            serde_json::json!({
                "op": "negacyclic_mul",
                "n": 8,
                "q": 17,
                "ran": 60,
            }),
        );

        let (status, _, stderr) = captured_run([
            normal.as_os_str(),
            OsStr::new("--plan"),
            OsStr::new("missing"),
        ]);

        assert_eq!(status, 2);
        assert!(stderr.contains("error: unknown plan: missing"));

        let (status, _, stderr) = captured_run([
            aliasing.as_os_str(),
            OsStr::new("--plan"),
            OsStr::new("sb_out_i32"),
        ]);

        assert_eq!(status, 2);
        assert!(stderr.contains("error: plan is illegal: alias"));

        let (status, _, stderr) = captured_run([invalid.as_os_str()]);

        assert_eq!(status, 2);
        assert!(stderr.starts_with("error: "));
        assert!(stderr.contains("ran"));

        let (status, _, stderr) = captured_run::<[&str; 0], &str>([]);

        assert_eq!(status, 2);
        assert!(stderr.contains("error: the following argument is required: spec"));

        let (status, _, stderr) = captured_run(["--unknown"]);

        assert_eq!(status, 2);
        assert!(stderr.contains("error: unrecognized argument: --unknown"));
    }

    #[test]
    fn help_is_successful()
    {
        let (status, stdout, stderr) = captured_run(["--help"]);

        assert_eq!(status, 0);
        assert_eq!(stdout, usage());
        assert!(stderr.is_empty());
    }
}
