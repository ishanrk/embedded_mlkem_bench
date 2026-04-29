#!/usr/bin/env python3

import argparse
import json
import statistics
from collections import defaultdict
from pathlib import Path

LEVELS = ("512", "768", "1024")
OPS = ("keygen", "encapsulation", "decapsulation")


def read_measurements(path):
    values = defaultdict(lambda: defaultdict(list))
    with path.open() as handle:
        for line in handle:
            row = json.loads(line)
            if row.get("operation") in OPS:
                values[row["plan_id"]][row["operation"]].append(row["calibrated_cycles"])
    return values


def best_plan(values):
    best = None
    for plan, operations in values.items():
        if any(not operations[op] for op in OPS):
            continue
        medians = {op: int(statistics.median(operations[op])) for op in OPS}
        total = sum(medians.values())
        candidate = (total, plan, medians)
        if best is None or candidate[:2] < best[:2]:
            best = candidate
    if best is None:
        raise RuntimeError("no complete plan found")
    total, plan, medians = best
    return {"plan": plan, **medians, "total": total}


def median_synthesis(path):
    data = json.loads(path.read_text())
    seeds = data["seeds"]
    return {
        "lut4": int(statistics.median(row["lut4"] for row in seeds)),
        "flip_flops": int(statistics.median(row["flip_flops"] for row in seeds)),
        "dsp": int(statistics.median(row["dsp"] for row in seeds)),
        "bram": int(statistics.median(row["bram"] for row in seeds)),
        "median_fmax_mhz": statistics.median(row["maximum_frequency_mhz"] for row in seeds),
        "all_seeds_meet_50mhz": all(row["meets_50mhz"] for row in seeds),
    }


def percent_less(reference, value):
    return 100.0 * (reference - value) / reference


def percent_change(reference, value):
    return 100.0 * (value - reference) / reference


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("build", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    root = args.build / "targets" / "picorv32"
    mlkem = root / "mlkem-results"
    red32 = root / "red32-results"
    synth = root / "results"

    levels = {}
    for level in LEVELS:
        portable = best_plan(
            read_measurements(mlkem / f"mlk{level}_portable-project.jsonl")
        )
        software = best_plan(
            read_measurements(mlkem / f"mlkem{level}-software-measurements.jsonl")
        )
        fqmul = best_plan(
            read_measurements(mlkem / f"mlkem{level}-fqmul-measurements.jsonl")
        )
        red = best_plan(
            read_measurements(red32 / f"mlkem{level}-red32-measurements.jsonl")
        )

        software["fewer_cycles_vs_portable_percent"] = percent_less(
            portable["total"], software["total"]
        )
        fqmul["fewer_cycles_vs_portable_percent"] = percent_less(
            portable["total"], fqmul["total"]
        )
        fqmul["fewer_cycles_vs_software_percent"] = percent_less(
            software["total"], fqmul["total"]
        )
        red["fewer_cycles_vs_portable_percent"] = percent_less(
            portable["total"], red["total"]
        )
        red["fewer_cycles_vs_software_percent"] = percent_less(
            software["total"], red["total"]
        )
        levels[level] = {
            "portable": portable,
            "software": software,
            "fqmul": fqmul,
            "red32": red,
        }

    baseline = median_synthesis(synth / "baseline-synthesis.json")
    fqmul_synth = median_synthesis(synth / "fqmul-synthesis.json")
    red32_synth = median_synthesis(synth / "red32-synthesis.json")

    for value in (fqmul_synth, red32_synth):
        value["lut4_change_percent"] = percent_change(baseline["lut4"], value["lut4"])
        value["flip_flop_change_percent"] = percent_change(
            baseline["flip_flops"], value["flip_flops"]
        )
        value["fmax_change_percent"] = percent_change(
            baseline["median_fmax_mhz"], value["median_fmax_mhz"]
        )

    report = {
        "schema": "pqc-poly-bench/current-comparison-v2",
        "measurement_protocol": {
            "plans_per_level": 24,
            "kernel_inputs": 16,
            "complete_operation_inputs": 30,
            "repeats": 3,
            "measurements_per_plan": 510,
            "red32_measurement_records_per_level": 12240,
        },
        "levels": levels,
        "synthesis": {
            "fpga_part": "LFE5U-45F-6BG381C",
            "target_frequency_mhz": 50,
            "seeds": 5,
            "baseline": baseline,
            "fqmul": fqmul_synth,
            "red32": red32_synth,
        },
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
