#!/usr/bin/env python3
import argparse
import json
from workbench_data import root, work, out, run, digest, write


def main():
    # rebuilds the PCPI test for every instruction and implementation setting
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 16:
        parser.error("jobs must be between one and sixteen")
    results = {"schema": "pqc-poly-bench/pcpi-checks-v1", "source_sha256": digest(root / "targets/picorv32/rtl/pqc_pcpi_mlkem.sv"),
               "tool": run(["verilator", "--version"]), "compiler": run(["g++", "--version"]).splitlines()[0],
               "repository_sha": run(["git", "rev-parse", "HEAD"]), "driver_sha256": digest(root / "scripts/workbench/pcpi.cpp"), "checks": []}
    for name, feature, impl in (("disabled", 0, 0), ("fqmul", 1, 0), ("red32", 2, 0), ("reuse", 3, 0), ("sliced", 3, 1), ("direct", 3, 2)):
        directory = work / f"check-{name}"
        directory.mkdir(parents=True, exist_ok=True)
        parameters = {"ENABLE_FQMUL": int(feature == 1), "ENABLE_RED32": int(feature == 2), "ENABLE_FSRI": int(feature == 3), "FSRI_IMPL": impl}
        command = ["verilator", "--cc", "--exe", "--build", "-j", str(args.jobs), "--threads", "1", "--Mdir", directory,
                   "--top-module", "pqc_pcpi_mlkem", "--Wno-fatal", "-CFLAGS", f"-std=c++20 -O2 -DPQC_FEATURE={feature} -DPQC_FSRI_IMPL={impl}",
                   *[f"-G{k}={v}" for k, v in parameters.items()], root / "targets/picorv32/rtl/pqc_pcpi_mlkem.sv", root / "scripts/workbench/pcpi.cpp"]
        run(command, directory / "build.log")
        message = run([directory / "Vpqc_pcpi_mlkem"])
        # records the Verilator command beside each test result
        results["checks"].append({"name": name, "parameters": parameters, "command": [str(x) for x in command], "result": message})
        print(name, message, flush=True)
    write("pcpi-checks.json", results)


if __name__ == "__main__":
    main()
