#!/usr/bin/env python3

import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "web/public/evidence"
WORK = ROOT / "build/workbench"
RTL = ROOT / "targets/picorv32/rtl/pqc_pcpi_mlkem.sv"


def run(command, log=None, cwd=ROOT):
    result = subprocess.run(
        [str(value) for value in command],
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=300,
        check=False,
    )
    if log is not None:
        pathlib.Path(log).write_text(result.stdout, encoding="utf-8")
    if result.returncode != 0:
        raise RuntimeError(f"command failed {command}\n{result.stdout[-3000:]}")
    return result.stdout.strip()


def digest(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def write(name, value):
    (OUT / name).write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def refresh_catalog(*inputs):
    catalog_path = OUT / "catalog.json"
    if not catalog_path.exists():
        return
    catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
    for source in inputs:
        path = pathlib.Path(source).resolve()
        catalog["inputs"][path.relative_to(ROOT).as_posix()] = digest(path)
    catalog["artifacts"] = {
        path.name: digest(path)
        for path in OUT.iterdir()
        if path.is_file() and path.name != "catalog.json"
    }
    write("catalog.json", catalog)


def reference(kind, first, second, shift):
    def signed(value, width):
        value &= (1 << width) - 1
        return value - (1 << width) if value >> (width - 1) else value

    if kind == "fsri":
        return ((second << 32 | first) >> shift) & 0xFFFFFFFF
    value = signed(first, 32) if kind == "red32" else signed(first, 16) * signed(second, 16)
    inverse = signed((value & 0xFFFF) * 62209, 16)
    return ((value - inverse * 3329) // 65536) & 0xFFFFFFFF


def check_catalog():
    catalog = json.loads((OUT / "catalog.json").read_text(encoding="utf-8"))
    for name, expected in catalog["inputs"].items():
        if digest(ROOT / name) != expected:
            raise RuntimeError(f"stale workbench input {name}")
    for name, expected in catalog["artifacts"].items():
        if digest(OUT / name) != expected:
            raise RuntimeError(f"changed workbench artifact {name}")
    for entry in catalog["traces"]:
        trace = json.loads((OUT / entry["file"]).read_text(encoding="utf-8"))
        first = int(trace["rs1"], 0)
        second = int(trace["rs2"], 0)
        expected = reference(trace["instruction"], first, second, trace["shift"])
        response = next(value for value in trace["snapshots"] if int(value["ready"], 0))
        if int(response["rd"], 0) != expected:
            raise RuntimeError(f"wrong saved trace result {entry['id']}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--data-only", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.jobs <= 16:
        parser.error("jobs must be between one and sixteen")
    if args.check:
        check_catalog()
        print("workbench evidence matches its source files")
        return

    OUT.mkdir(parents=True, exist_ok=True)
    WORK.mkdir(parents=True, exist_ok=True)
    inputs = [
        "targets/picorv32/rtl/pqc_pcpi_mlkem.sv",
        "targets/picorv32/mlkem/fqmul.h",
        "targets/picorv32/mlkem/red32.h",
        "targets/picorv32/mlkem/fsri.h",
        "scripts/workbench/observe.sv",
        "scripts/workbench/trace.cpp",
        "scripts/workbench_data.py",
        "results/summary.json",
    ]
    for name in ("fqmul", "red32", "fsri"):
        inputs.append(f"targets/picorv32/formal/{name}.sby")
        inputs.append(f"targets/picorv32/formal/{name}_properties.sv")
    catalog = {
        "schema": "pqc-poly-bench/workbench-v2",
        "inputs": {name: digest(ROOT / name) for name in inputs},
        "traces": [],
        "artifacts": {},
    }
    shutil.copyfile(ROOT / "results/summary.json", OUT / "summary.json")
    shutil.copyfile(RTL, OUT / "pqc_pcpi_mlkem.sv")
    for name in ("fqmul", "red32", "fsri"):
        shutil.copyfile(
            ROOT / f"targets/picorv32/mlkem/{name}.h", OUT / f"{name}.h"
        )
        shutil.copyfile(
            ROOT / f"targets/picorv32/formal/{name}.sby",
            OUT / f"{name}.sby",
        )
        shutil.copyfile(
            ROOT / f"targets/picorv32/formal/{name}_properties.sv",
            OUT / f"{name}_properties.sv",
        )

    if not args.data_only:
        if shutil.which("verilator") is None:
            raise RuntimeError("verilator is required to record traces")
        source = OUT / "pqc_pcpi_mlkem.sv"
        for kind, feature in (("fqmul", "FQMUL"), ("red32", "RED32"), ("fsri", "FSRI")):
            directory = WORK / f"trace-{kind}"
            directory.mkdir(parents=True, exist_ok=True)
            command = [
                "verilator",
                "--cc",
                "--exe",
                "--build",
                "--trace",
                "--threads",
                "1",
                "-j",
                str(args.jobs),
                "--Mdir",
                directory,
                "--top-module",
                "pqc_pcpi_observe",
                "--Wno-fatal",
                f"-GENABLE_{feature}=1",
                "-CFLAGS",
                "-std=c++20 -O2",
                source,
                ROOT / "scripts/workbench/observe.sv",
                ROOT / "scripts/workbench/trace.cpp",
            ]
            run(command, directory / "build.log")
            cases = {
                "fqmul": [(0xDEAD8000, 0xBEEF0680, 0), (3328, 3328, 0)],
                "red32": [(0x80000001, 0, 0), (0x7FFFFFFF, 0, 0)],
                "fsri": [(0x89ABCDEF, 0x12345678, shift) for shift in (0, 13, 31)],
            }[kind]
            binary = directory / "Vpqc_pcpi_observe"
            for index, (first, second, shift) in enumerate(cases):
                instruction = {"fqmul": 0xB, "red32": 0x100B, "fsri": 0x200B}[kind]
                instruction |= shift << 25 | 7 << 7 | 5 << 15
                if kind != "red32":
                    instruction |= 6 << 20
                name = f"{kind}-{index}"
                vcd = OUT / f"{name}.vcd"
                snapshots = json.loads(
                    run([binary, hex(instruction), hex(first), hex(second), vcd])
                )
                expected = reference(kind, first, second, shift)
                response = next(value for value in snapshots if int(value["ready"], 0))
                if int(response["rd"], 0) != expected:
                    raise RuntimeError(f"wrong Verilator result {name}")
                write(
                    f"{name}.json",
                    {
                        "schema": "pqc-poly-bench/pcpi-trace-v2",
                        "instruction": kind,
                        "rs1": hex(first),
                        "rs2": hex(second),
                        "shift": shift,
                        "encoding": hex(instruction),
                        "snapshots": snapshots,
                        "response_edge": response["edge"],
                        "source": source.name,
                        "source_sha256": digest(source),
                        "tool": run(["verilator", "--version"]),
                        "vcd": vcd.name,
                    },
                )
                catalog["traces"].append(
                    {"id": name, "instruction": kind, "file": f"{name}.json"}
                )

    catalog["artifacts"] = {
        path.name: digest(path)
        for path in OUT.iterdir()
        if path.is_file() and path.name != "catalog.json"
    }
    write("catalog.json", catalog)
    print(f"exported {len(catalog['traces'])} Verilator traces")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        sys.exit(f"error {error}")
