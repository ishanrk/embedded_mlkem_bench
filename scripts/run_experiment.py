#!/usr/bin/env python3

import argparse
import pathlib
import shlex
import shutil
import subprocess
import sys


VARIANTS = ("baseline", "fqmul", "red32", "fsri")
LEVELS = ("512", "768", "1024")
INPUT_COUNT = 30


def run(command, *, cwd=None):
    print(f"+ {shlex.join(str(part) for part in command)}", flush=True)
    subprocess.run(
        [str(part) for part in command],
        cwd=cwd,
        check=True,
    )


def resolve_tool(value, description):
    resolved = shutil.which(value)
    if resolved is None:
        raise RuntimeError(f"{description} is required but was not found: {value}")
    return resolved


def repository_path(root, path):
    return path.resolve() if path.is_absolute() else (root / path).resolve()


def check_runtime_tools(args):
    tools = {}
    if args.pcpi or args.bench or args.synthesis:
        tools["cmake"] = resolve_tool(args.cmake, "CMake")
    if args.formal:
        tools["sby"] = resolve_tool(args.sby, "SymbiYosys")
    if args.synthesis:
        tools["yosys"] = resolve_tool(args.yosys, "Yosys")
        tools["nextpnr"] = resolve_tool(args.nextpnr, "nextpnr-ecp5")
        tools["ecppack"] = resolve_tool(args.ecppack, "ecppack")
    return tools


def configure_or_locate_build(root, build_dir, args, tools):
    if not (args.pcpi or args.bench or args.synthesis):
        build_dir.mkdir(parents=True, exist_ok=True)
        return

    command = [
        tools["cmake"],
        "-S",
        root,
        "-B",
        build_dir,
        "-DCMAKE_BUILD_TYPE=Release",
        "-DPQC_POLY_PICORV32=ON",
        f"-DPQC_POLY_BUILD_JOBS={args.jobs}",
    ]
    if args.generator:
        command.extend(("-G", args.generator))
    command.extend(args.cmake_arg)
    run(command)


def build_required_targets(build_dir, args, tools):
    targets = []
    if args.pcpi:
        targets.append("pqc-picorv32-pcpi-models")
    if args.bench:
        targets.extend(("pqc-picorv32-cpu-models", "pqc-picorv32-firmware"))
    if not targets:
        return
    run(
        [
            tools["cmake"],
            "--build",
            build_dir,
            "--parallel",
            str(args.jobs),
            "--target",
            *targets,
        ]
    )


def require_artifact(path):
    if not path.is_file():
        raise RuntimeError(f"required build artifact is missing: {path}")
    return path


def picorv32_build_dir(build_dir):
    return build_dir / "targets" / "picorv32"


def run_pcpi_tests(build_dir):
    target_dir = picorv32_build_dir(build_dir)
    for variant in VARIANTS:
        model = require_artifact(
            target_dir / f"pcpi-{variant}" / "Vpqc_pcpi_mlkem"
        )
        run([model])


def run_mlkem_benchmarks(build_dir, results_dir):
    target_dir = picorv32_build_dir(build_dir)
    results_dir.mkdir(parents=True, exist_ok=True)
    for level in LEVELS:
        for variant in VARIANTS:
            model = require_artifact(
                target_dir / f"cpu-{variant}" / "Vpqc_picorv32_sim_top"
            )
            firmware = require_artifact(target_dir / f"{variant}-{level}.hex")
            disassembly = require_artifact(target_dir / f"{variant}-{level}.dis")
            result = results_dir / f"{variant}-{level}.json"
            run(
                [
                    model,
                    f"+firmware={firmware}",
                    "--output",
                    result,
                    "--disassembly",
                    disassembly,
                    "--variant",
                    variant,
                    "--level",
                    level,
                    "--inputs",
                    str(INPUT_COUNT),
                ]
            )


def run_formal_checks(root, build_dir, tools):
    source_dir = root / "targets" / "picorv32"
    for instruction in ("fqmul", "red32", "fsri"):
        work = picorv32_build_dir(build_dir) / f"formal-{instruction}"
        work.mkdir(parents=True, exist_ok=True)
        for source in (
            source_dir / "rtl" / "pqc_pcpi_mlkem.sv",
            source_dir / "formal" / f"{instruction}_properties.sv",
            source_dir / "formal" / f"{instruction}.sby",
        ):
            shutil.copy2(source, work / source.name)
        run([tools["sby"], "-f", f"{instruction}.sby"], cwd=work)
        status = work / instruction / "status"
        passed = status.is_file() and status.read_text(encoding="utf-8").startswith(
            "PASS"
        )
        if not passed:
            raise RuntimeError(f"formal job did not produce PASS status: {instruction}")


def run_synthesis(root, build_dir, results_dir, tools):
    target_source = root / "targets" / "picorv32"
    picorv32 = require_artifact(build_dir / "_deps" / "picorv32-src" / "picorv32.v")
    synthesis_script = root / "scripts" / "ecp5-50mhz.py"
    results_dir.mkdir(parents=True, exist_ok=True)
    for variant in VARIANTS:
        command = [
            sys.executable,
            synthesis_script,
            "--yosys",
            tools["yosys"],
            "--nextpnr",
            tools["nextpnr"],
            "--ecppack",
            tools["ecppack"],
            "--picorv32",
            picorv32,
            "--pcpi",
            target_source / "rtl" / "pqc_pcpi_mlkem.sv",
            "--core",
            target_source / "rtl" / "pqc_picorv32_core_top.sv",
            "--script",
            target_source / "synth" / "core.ys",
            "--work",
            picorv32_build_dir(build_dir) / f"synthesis-{variant}",
            "--output",
            results_dir / f"{variant}-synthesis.json",
        ]
        if variant != "baseline":
            command.append(f"--enable-{variant}")
        run(command, cwd=root)


def generate_summary(root, results_dir):
    run(
        [
            sys.executable,
            root / "scripts" / "results.py",
            "--input",
            results_dir,
            "--output",
            results_dir / "summary.json",
        ],
        cwd=root,
    )


def positive_integer(value):
    parsed = int(value)
    if parsed < 1:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return parsed


def parse_args():
    parser = argparse.ArgumentParser(
        description="Build and run the pinned PicoRV32 ML-KEM experiment."
    )
    stages = parser.add_argument_group("experiment stages")
    stages.add_argument("--pcpi", action="store_true", help="run direct PCPI tests")
    stages.add_argument("--bench", action="store_true", help="run all 12 ML-KEM benchmarks")
    stages.add_argument("--formal", action="store_true", help="run the three bounded formal jobs")
    stages.add_argument("--synthesis", action="store_true", help="run four ECP5 synthesis flows")
    stages.add_argument(
        "--summary",
        action="store_true",
        help="combine existing benchmark and synthesis JSON",
    )
    stages.add_argument("--all", action="store_true", help="run every stage")
    parser.add_argument(
        "--build-dir", type=pathlib.Path, default=pathlib.Path("build/picorv32")
    )
    parser.add_argument("--results-dir", type=pathlib.Path)
    parser.add_argument("--jobs", type=positive_integer, default=4)
    parser.add_argument("--generator", help="CMake generator for a new build directory")
    parser.add_argument(
        "--cmake-arg",
        action="append",
        default=[],
        help="additional CMake configure argument; may be repeated",
    )
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--sby", default="sby")
    parser.add_argument("--yosys", default="yosys")
    parser.add_argument("--nextpnr", default="nextpnr-ecp5")
    parser.add_argument("--ecppack", default="ecppack")
    args = parser.parse_args()
    if args.all:
        args.pcpi = True
        args.bench = True
        args.formal = True
        args.synthesis = True
        args.summary = True
    if not any((args.pcpi, args.bench, args.formal, args.synthesis, args.summary)):
        parser.error("select at least one stage or use --all")
    return args


def main():
    args = parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    build_dir = repository_path(root, args.build_dir)
    results_dir = (
        repository_path(root, args.results_dir)
        if args.results_dir is not None
        else picorv32_build_dir(build_dir) / "results"
    )
    tools = check_runtime_tools(args)
    configure_or_locate_build(root, build_dir, args, tools)
    build_required_targets(build_dir, args, tools)
    if args.pcpi:
        run_pcpi_tests(build_dir)
    if args.bench:
        run_mlkem_benchmarks(build_dir, results_dir)
    if args.formal:
        run_formal_checks(root, build_dir, tools)
    if args.synthesis:
        run_synthesis(root, build_dir, results_dir, tools)
    if args.summary:
        generate_summary(root, results_dir)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError, ValueError) as error:
        sys.exit(f"error: {error}")
