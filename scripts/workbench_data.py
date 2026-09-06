#!/usr/bin/env python3
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
from workbench.measurements import export as export_measurements

root = pathlib.Path(__file__).resolve().parents[1]
out = root / "web/public/evidence"
work = root / "build/workbench"
historical = "1b1d01aaaffe48a0bfff3cdc096cca526f8a40ca"
rtl = "targets/picorv32/rtl/pqc_pcpi_mlkem.sv"


def run(command, log=None):
    result = subprocess.run([str(x) for x in command], cwd=root, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=300)
    if log:
        pathlib.Path(log).write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f"command failed {command}\n{result.stderr[-3000:]}")
    return result.stdout.strip()


def digest(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def write(name, value):
    (out / name).write_text(json.dumps(value, indent=2) + "\n")


def reference(kind, a, b, shift):
    def signed(value, width):
        value &= (1 << width) - 1
        return value - (1 << width) if value >> (width - 1) else value
    if kind == "fsri":
        return ((b << 32 | a) >> shift) & 0xffffffff
    t = signed(a, 32) if kind == "red32" else signed(a, 16) * signed(b, 16)
    u = signed((t & 65535) * 62209, 16)
    return ((t - u * 3329) // 65536) & 0xffffffff


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--data-only", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.jobs <= 16:
        parser.error("jobs must be between one and sixteen")
    out.mkdir(parents=True, exist_ok=True)
    work.mkdir(parents=True, exist_ok=True)
    if args.check:
        catalog = json.loads((out / "catalog.json").read_text())
        for name, expected in catalog["inputs"].items():
            if digest(root / name) != expected:
                raise RuntimeError(f"stale generated evidence for {name}")
        for name, expected in catalog["artifacts"].items():
            if digest(out / name) != expected:
                raise RuntimeError(f"changed generated artifact {name}")
        for entry in catalog["traces"]:
            trace = json.loads((out / entry["file"]).read_text())
            a, b = (int(trace[k], 0) for k in ("rs1", "rs2"))
            assert int(trace["snapshots"][-1]["rd"], 0) == reference(trace["instruction"], a, b, trace["shift"])
        print("source hashes artifact hashes and rtl arithmetic agree")
        return
    inputs = [rtl, "scripts/workbench/observe.sv", "scripts/workbench/trace.cpp",
              "scripts/workbench_data.py", "scripts/workbench/measurements.py", "results/summary.json"]
    catalog = {"schema": "pqc-poly-bench/workbench-v1", "repository_sha": run(["git", "rev-parse", "HEAD"]),
               "dirty": bool(run(["git", "status", "--porcelain", "--untracked-files=normal"])),
               "inputs": {p: digest(root / p) for p in inputs}, "traces": [],
               "execution": "recorded native verilator playback", "wasm": "unavailable emcc not installed",
               "upstream": json.loads((root / "results/summary.json").read_text())["platform"]}
    shutil.copyfile(root / "results/summary.json", out / "summary.json")
    for name in ("fqmul", "red32", "fsri"):
        shutil.copyfile(root / f"targets/picorv32/mlkem/{name}.h", out / f"{name}.h")
        catalog["inputs"][f"targets/picorv32/mlkem/{name}.h"] = digest(out / f"{name}.h")
    for name in export_measurements(root, out):
        catalog["inputs"][name] = digest(root / name)
    for variant, kind, revision in (("fqmul", "fqmul", None), ("red32", "red32", None),
                                  ("fsri_multiplier_reuse", "fsri", None),
                                  ("fsri_combinational", "fsri", historical)):
        source = out / f"{variant}.sv"
        source.write_text(run(["git", "show", f"{revision}:{rtl}"]) + "\n" if revision else (root / rtl).read_text())
        if args.data_only or not shutil.which("verilator"):
            continue
        directory = work / variant
        directory.mkdir(exist_ok=True)
        command = ["verilator", "--cc", "--exe", "--build", "--trace", "--threads", "1", "-j", str(args.jobs),
                   "--Mdir", directory, "--top-module", "pqc_pcpi_observe", "--Wno-fatal",
                   f"-GENABLE_{kind.upper()}=1", "-CFLAGS", "-std=c++20 -O2", source,
                   root / "scripts/workbench/observe.sv", root / "scripts/workbench/trace.cpp"]
        run(command, directory / "build.log")
        binary = directory / "Vpqc_pcpi_observe"
        cases = [(0xdead8000, 0xbeef0680, 0), (3328, 3328, 0)] if kind == "fqmul" else (
            [(0x80000001, 0, 0), (0x7fffffff, 0, 0)] if kind == "red32" else
            [(0x89abcdef, 0x12345678, shift) for shift in (0, 13, 31)])
        for index, (a, b, shift) in enumerate(cases):
            name = f"{variant}-{index}"
            encoding = {"fqmul": 0xb, "red32": 0x100b, "fsri": 0x200b}[kind] | (shift << 25) | (7 << 7) | (5 << 15)
            if kind != "red32":
                encoding |= 6 << 20
            trace_command = [binary, hex(encoding), hex(a), hex(b), out / f"{name}.vcd"]
            snapshots = json.loads(run(trace_command))
            expected = reference(kind, a, b, shift)
            assert int(snapshots[-1]["rd"], 0) == expected, name
            write(f"{name}.json", {"schema": "pqc-poly-bench/pcpi-trace-v1", "variant": variant,
                  "instruction": kind, "rs1": hex(a), "rs2": hex(b), "shift": shift,
                  "encoding": hex(encoding), "expected": hex(expected), "snapshots": snapshots,
                  "accepted_edge": 1,
                  "response_edge": next(s["edge"] for s in snapshots if int(s["ready"], 0)),
                  "sampling": "edge zero is settled request before first rising edge then settled post rising edge snapshots",
                  "latency": "first settled ready assertion indexed from request at edge zero direct is combinational at zero and can be consumed at edge one sequential snapshots include capture edge cpu issue and retirement excluded",
                  "source": source.name, "source_sha256": digest(source), "source_revision": revision,
                  "parameters": {f"ENABLE_{k.upper()}": int(k == kind) for k in ("fqmul", "red32", "fsri")},
                  "tool": run(["verilator", "--version"]), "build_command": [str(x) for x in command],
                  "command": [str(x) for x in trace_command], "binary_sha256": digest(binary),
                  "status": "locally reproduced run", "vcd": f"{name}.vcd"})
            catalog["traces"].append({"id": name, "variant": variant, "file": f"{name}.json"})
    catalog["artifacts"] = {p.name: digest(p) for p in out.iterdir() if p.is_file() and p.name != "catalog.json"}
    write("catalog.json", catalog)
    print(f"exported {len(catalog['traces'])} native rtl traces")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, AssertionError) as error:
        sys.exit(str(error))
