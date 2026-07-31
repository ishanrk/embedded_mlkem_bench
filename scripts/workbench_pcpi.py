#!/usr/bin/env python3

import argparse
import pathlib

from workbench_data import OUT, ROOT, WORK, digest, refresh_catalog, run, write


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 16:
        parser.error("jobs must be between one and sixteen")

    rtl = ROOT / "targets/picorv32/rtl/pqc_pcpi_mlkem.sv"
    driver = ROOT / "targets/picorv32/sim/pcpi_test.cpp"
    result = {
        "schema": "pqc-poly-bench/pcpi-checks-v2",
        "source_sha256": digest(rtl),
        "driver_sha256": digest(driver),
        "tool": run(["verilator", "--version"]),
        "checks": [],
    }
    for name, index, parameter in (
        ("baseline", 0, None),
        ("fqmul", 1, "-GENABLE_FQMUL=1"),
        ("red32", 2, "-GENABLE_RED32=1"),
        ("fsri", 3, "-GENABLE_FSRI=1"),
    ):
        directory = WORK / f"check-{name}"
        directory.mkdir(parents=True, exist_ok=True)
        command = [
            "verilator",
            "--cc",
            "--exe",
            "--build",
            "-j",
            str(args.jobs),
            "--Mdir",
            directory,
            "--top-module",
            "pqc_pcpi_mlkem",
            "--Wno-fatal",
            "-CFLAGS",
            f"-std=c++20 -O2 -DPQC_TEST_VARIANT={index}",
        ]
        if parameter is not None:
            command.append(parameter)
        command.extend((rtl, driver))
        run(command, directory / "build.log")
        run([directory / "Vpqc_pcpi_mlkem"])
        result["checks"].append(
            {"variant": name, "result": "pass", "command": [str(value) for value in command]}
        )
    OUT.mkdir(parents=True, exist_ok=True)
    write("pcpi-checks.json", result)
    refresh_catalog(pathlib.Path(__file__))


if __name__ == "__main__":
    main()
