use std::process::Command;

#[test]
fn binary_prints_help()
{
    let output = Command::new(env!("CARGO_BIN_EXE_pqc-poly-bench"))
        .arg("--help")
        .output()
        .expect("pqc-poly-bench should run");

    assert!(output.status.success());
    assert_eq!(
        String::from_utf8(output.stdout).unwrap(),
        "usage: pqc-poly-bench [-h] [-o OUT] [--plan PLAN] spec\n",
    );
    assert!(output.stderr.is_empty());
}

#[test]
fn binary_reports_missing_specification()
{
    let output = Command::new(env!("CARGO_BIN_EXE_pqc-poly-bench"))
        .output()
        .expect("pqc-poly-bench should run");

    assert_eq!(output.status.code(), Some(2));
    assert!(output.stdout.is_empty());
    assert!(
        String::from_utf8(output.stderr)
            .unwrap()
            .contains("error: the following argument is required: spec"),
    );
}
