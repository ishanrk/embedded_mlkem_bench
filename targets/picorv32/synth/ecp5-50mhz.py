#!/usr/bin/env python3

import argparse
import json
import os
import pathlib
import re
import shlex
import subprocess
import sys


def run(command, *, environment=None, output=None):
    completed = subprocess.run(
        command,
        check=False,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if output is not None:
        output.write_text(completed.stdout, encoding="utf-8")
    if completed.returncode != 0:
        raise RuntimeError(f"command failed: {shlex.join(command)}")
    return completed.stdout.strip()


def capture(command, output):
    completed = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    output.write_text(completed.stdout, encoding="utf-8")
    return completed


def version(command):
    lines = run(command).splitlines()
    if not lines:
        raise RuntimeError(f"version command produced no output: {shlex.join(command)}")
    return lines[-1]


def report_command(command, replacements):
    result = shlex.join(command)
    for source, replacement in sorted(replacements, key=lambda value: len(value[0]), reverse=True):
        result = result.replace(source, replacement)
    return result


def resource_count(log, name):
    matches = re.findall(rf"{name}:\s*([0-9]+)\s*/", log)
    if not matches:
        raise RuntimeError(f"nextpnr log lacks {name}")
    return int(matches[-1])


def resource_counts(log):
    return {
        "lut4": resource_count(log, "Total LUT4s"),
        "flip_flops": resource_count(log, "Total DFFs"),
        "dsp": resource_count(log, "MULT18X18D") + resource_count(log, "ALU54B"),
        "bram": resource_count(log, "DP16KD"),
    }


def maximum_frequency(log):
    matches = re.findall(r"Max frequency[^:]*:\s*([0-9]+(?:\.[0-9]+)?)\s*MHz", log)
    if not matches:
        raise RuntimeError("nextpnr log lacks maximum frequency")
    return float(matches[-1])


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--yosys", required=True)
    parser.add_argument("--nextpnr", required=True)
    parser.add_argument("--ecppack", required=True)
    parser.add_argument("--picorv32", required=True)
    parser.add_argument("--pcpi", required=True)
    parser.add_argument("--core", required=True)
    parser.add_argument("--script", required=True)
    parser.add_argument("--work", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--enable-fqmul", action="store_true")
    return parser.parse_args()


def main():
    args = parse_args()
    work = pathlib.Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    replacements = [
        (str(pathlib.Path(__file__).resolve().parents[3]), "${PROJECT_SOURCE_DIR}"),
        (str(pathlib.Path(args.yosys).resolve().parents[1]), "${PQC_OSS_CAD_SUITE_ROOT}"),
    ]
    netlist_path = work / "core.json"
    yosys_log = work / "yosys.log"
    environment = os.environ.copy()
    environment.update(
        {
            "PICORV32_SOURCE": str(pathlib.Path(args.picorv32).resolve()),
            "PQC_PCPI_SOURCE": str(pathlib.Path(args.pcpi).resolve()),
            "PQC_CORE_SOURCE": str(pathlib.Path(args.core).resolve()),
            "STOCK_MUL": "0",
            "ENABLE_FQMUL": "1" if args.enable_fqmul else "0",
            "SYNTH_JSON": str(netlist_path.resolve()),
        }
    )
    yosys_command = [args.yosys, "-c", str(pathlib.Path(args.script).resolve())]
    run(yosys_command, environment=environment, output=yosys_log)

    seeds = []
    all_pass = True
    for seed in range(1, 6):
        config = work / f"seed-{seed}.config"
        bitstream = work / f"seed-{seed}.bit"
        log_path = work / f"seed-{seed}.log"
        command = [
            args.nextpnr,
            "--json",
            str(netlist_path),
            "--textcfg",
            str(config),
            "--45k",
            "--package",
            "CABGA381",
            "--speed",
            "6",
            "--freq",
            "50",
            "--lpf-allow-unconstrained",
            "--seed",
            str(seed),
        ]
        route = capture(command, log_path)
        try:
            frequency = maximum_frequency(route.stdout)
            counts = resource_counts(route.stdout)
        except RuntimeError:
            frequency = 0.0
            counts = {"lut4": 0, "flip_flops": 0, "dsp": 0, "bram": 0}
        pack_returncode = -1
        pack_command = [args.ecppack, str(config), str(bitstream)]
        if config.exists():
            pack = capture(pack_command, work / f"seed-{seed}-pack.log")
            pack_returncode = pack.returncode
        passed = route.returncode == 0 and pack_returncode == 0 and frequency >= 50.0
        all_pass = all_pass and passed
        seeds.append(
            {
                "seed": seed,
                **counts,
                "maximum_frequency_mhz": frequency,
                "meets_50mhz": passed,
                "command": report_command(command, replacements),
                "ecppack_command": report_command(pack_command, replacements),
                "nextpnr_returncode": route.returncode,
                "ecppack_returncode": pack_returncode,
            }
        )

    result = {
        "fpga_part": "LFE5U-45F-6BG381C",
        "target_frequency_mhz": 50,
        "period_ns": 20,
        "fqmul_enabled": args.enable_fqmul,
        "yosys_version": version([args.yosys, "-V"]),
        "nextpnr_version": version([args.nextpnr, "--version"]),
        "ecppack_version": version([args.ecppack, "--version"]),
        "yosys_command": report_command(yosys_command, replacements),
        "seeds": seeds,
    }
    pathlib.Path(args.output).write_text(
        json.dumps(result, indent=2, sort_keys=False) + "\n", encoding="utf-8"
    )
    return 0 if all_pass else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(2)
