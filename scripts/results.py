#!/usr/bin/env python3

import argparse
import json
import pathlib
import statistics
import sys


VARIANTS = ("baseline", "fqmul", "red32", "fsri")
LEVELS = ("512", "768", "1024")
PINS = {
    "mlkem_native": "69d24e37b8a04c6050ec55bc84a4228d7051bb4b",
    "picorv32": "a473fc8fca393771d83b0ffcf0b14db3393339d8",
}


def read_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"cannot read {path}: {error}") from error


def percent_change(value, baseline):
    return round((value / baseline - 1.0) * 100.0, 4)


def pending_summary():
    measurements = {}
    hardware = {}
    for variant in VARIANTS:
        measurements[variant] = {}
        for level in LEVELS:
            measurements[variant][level] = {
                "keygen_cycles": None,
                "encapsulation_cycles": None,
                "decapsulation_cycles": None,
                "total_cycles": None,
                "percent_change_vs_baseline": None,
                "verified": False,
            }
        hardware[variant] = {
            "lut4": None,
            "flip_flops": None,
            "dsp": None,
            "bram": None,
            "fmax_by_seed_mhz": [],
            "median_fmax_mhz": None,
            "meets_50mhz_by_seed": [],
            "fmax_change_vs_baseline_percent": None,
        }
    return {
        "schema": "pqc-poly-bench/summary-v2",
        "status": "pending fair rerun",
        "platform": "PicoRV32 RTL simulation and LFE5U 45F 6BG381C routing",
        "pins": PINS,
        "baseline": "pinned mlkem native with the fixed upstream arithmetic schedule and project RV32M PCPI multiplier",
        "measurements": measurements,
        "hardware": hardware,
    }


def load_run(input_dir, variant, level):
    path = input_dir / f"{variant}-{level}.json"
    data = read_json(path)
    if data.get("schema") != "pqc-poly-bench/mlkem-run-v2":
        raise RuntimeError(f"wrong run schema in {path}")
    if data.get("variant") != variant or data.get("level") != level:
        raise RuntimeError(f"wrong run identity in {path}")
    if data.get("verified") is not True:
        raise RuntimeError(f"unverified run in {path}")
    instruction_count = data.get("custom_instruction_count")
    if variant == "baseline" and instruction_count != 0:
        raise RuntimeError(f"baseline contains a custom instruction in {path}")
    if variant != "baseline" and not isinstance(instruction_count, int):
        raise RuntimeError(f"missing instruction count in {path}")
    if variant != "baseline" and instruction_count <= 0:
        raise RuntimeError(f"custom instruction absent in {path}")
    cycles = data.get("cycles", {})
    values = {}
    for operation in ("keygen", "encapsulation", "decapsulation"):
        value = cycles.get(operation, {}).get("median")
        if not isinstance(value, int) or value <= 0:
            raise RuntimeError(f"invalid {operation} cycles in {path}")
        values[operation] = value
    if cycles.get("total") != sum(values.values()):
        raise RuntimeError(f"wrong cycle total in {path}")
    return data, values


def load_synthesis(input_dir, variant):
    path = input_dir / f"{variant}-synthesis.json"
    data = read_json(path)
    if data.get("schema") != "pqc-poly-bench/synthesis-v2":
        raise RuntimeError(f"wrong synthesis schema in {path}")
    seeds = data.get("seeds")
    if not isinstance(seeds, list) or [seed.get("seed") for seed in seeds] != [1, 2, 3, 4, 5]:
        raise RuntimeError(f"synthesis seeds are incomplete in {path}")
    for seed in seeds:
        if seed.get("status") != "complete" or not isinstance(seed.get("meets_50mhz"), bool):
            raise RuntimeError(f"failed synthesis seed in {path}")
    result = {}
    for field in ("lut4", "flip_flops", "dsp", "bram"):
        values = [seed.get(field) for seed in seeds]
        if not all(isinstance(value, int) and value >= 0 for value in values):
            raise RuntimeError(f"invalid {field} in {path}")
        if len(set(values)) != 1:
            raise RuntimeError(f"routed {field} count changed between seeds in {path}")
        result[field] = values[0]
    frequencies = [seed.get("maximum_frequency_mhz") for seed in seeds]
    if not all(isinstance(value, (int, float)) and value > 0 for value in frequencies):
        raise RuntimeError(f"invalid frequency in {path}")
    result["fmax_by_seed_mhz"] = frequencies
    result["median_fmax_mhz"] = statistics.median(frequencies)
    result["meets_50mhz_by_seed"] = [seed["meets_50mhz"] for seed in seeds]
    return result


def complete_summary(input_dir):
    result = pending_summary()
    result["status"] = "complete"
    checksums = {level: set() for level in LEVELS}
    for variant in VARIANTS:
        for level in LEVELS:
            run, values = load_run(input_dir, variant, level)
            checksums[level].add(run.get("output_checksum"))
            result["measurements"][variant][level] = {
                "keygen_cycles": values["keygen"],
                "encapsulation_cycles": values["encapsulation"],
                "decapsulation_cycles": values["decapsulation"],
                "total_cycles": sum(values.values()),
                "percent_change_vs_baseline": None,
                "verified": True,
            }
    for level, values in checksums.items():
        if len(values) != 1 or None in values:
            raise RuntimeError(f"variant output mismatch for ML KEM {level}")
    for level in LEVELS:
        baseline = result["measurements"]["baseline"][level]["total_cycles"]
        for variant in VARIANTS:
            measurement = result["measurements"][variant][level]
            measurement["percent_change_vs_baseline"] = percent_change(
                measurement["total_cycles"], baseline
            )

    for variant in VARIANTS:
        result["hardware"][variant] = load_synthesis(input_dir, variant)
    baseline_fmax = result["hardware"]["baseline"]["median_fmax_mhz"]
    for variant in VARIANTS:
        hardware = result["hardware"][variant]
        hardware["fmax_change_vs_baseline_percent"] = percent_change(
            hardware["median_fmax_mhz"], baseline_fmax
        )
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--allow-missing", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    try:
        summary = complete_summary(args.input)
    except RuntimeError:
        if not args.allow_missing:
            raise
        summary = pending_summary()
    content = json.dumps(summary, indent=2) + "\n"
    if args.check:
        if args.output.read_text(encoding="utf-8") != content:
            raise RuntimeError(f"stale summary {args.output}")
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(content, encoding="utf-8")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError) as error:
        sys.exit(f"error: {error}")
