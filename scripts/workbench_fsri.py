#!/usr/bin/env python3
import argparse
import json
import pathlib
import shutil
import subprocess
from workbench_data import root, work, out, run, write


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--route", action="store_true")
    parser.add_argument("--variant", choices=("baseline", "reuse", "sliced", "direct", "all"), default="all")
    parser.add_argument("--picorv32", type=pathlib.Path, default=root / "build/picorv32-sim/_deps/picorv32-src/picorv32.v")
    args = parser.parse_args()
    upstream = [args.picorv32]
    if not args.picorv32.is_file():
        raise RuntimeError("configure the existing picorv32 preset to fetch its pinned source first")
    for name in ("baseline", "reuse", "sliced", "direct") if args.variant == "all" else (args.variant,):
        directory = work / f"area-{name}"
        directory.mkdir(parents=True, exist_ok=True)
        command = ["python3", root / "targets/picorv32/synth/ecp5-50mhz.py",
                   "--yosys", shutil.which("yosys"), "--nextpnr", shutil.which("nextpnr-ecp5"), "--ecppack", shutil.which("ecppack"),
                   "--picorv32", upstream[0], "--pcpi", root / "targets/picorv32/rtl/pqc_pcpi_mlkem.sv",
                   "--core", root / "targets/picorv32/rtl/pqc_picorv32_core_top.sv", "--script", root / "targets/picorv32/synth/core.ys",
                   "--work", directory, "--output", out / f"fsri-{name}-{'route' if args.route else 'area'}.json"]
        if name != "baseline":
            command += ["--enable-fsri", "--fsri-impl", name]
        command += ["--seeds", "1"] if args.route else ["--area-only"]
        if any(item is None for item in command):
            raise RuntimeError("source the local fpga toolchain setup before running this experiment")
        result = subprocess.run([str(x) for x in command], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (directory / "experiment.log").write_text(result.stdout)
        if result.returncode not in (0, 1) or not pathlib.Path(command[command.index("--output") + 1]).exists():
            raise RuntimeError(result.stdout[-2000:])
        print(name, "routing complete" if args.route else "area screen complete", "return", result.returncode, flush=True)


if __name__ == "__main__":
    main()
