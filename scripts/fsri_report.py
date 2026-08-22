#!/usr/bin/env python3

import argparse
import json
import math
import re
import statistics
from collections import defaultdict
from pathlib import Path

LEVELS = ("512", "768", "1024")
OPS = ("keygen", "encapsulation", "decapsulation")


def measurements(path):
    values = defaultdict(list)
    plans = set()
    with path.open(encoding="utf-8") as handle:
        for line in handle:
            row = json.loads(line)
            operation = row.get("operation")
            if operation in OPS:
                values[operation].append(row["calibrated_cycles"])
                plans.add(row["plan_id"])
    if len(plans) != 1 or any(not values[operation] for operation in OPS):
        raise RuntimeError(f"incomplete measurements: {path}")
    medians = {
        operation: int(statistics.median(values[operation])) for operation in OPS
    }
    return {"plan": plans.pop(), **medians, "total": sum(medians.values())}


def synthesis(path):
    rows = json.loads(path.read_text(encoding="utf-8"))["seeds"]
    if len(rows) != 5:
        raise RuntimeError(f"invalid synthesis seed count: {path}")
    return {
        "lut4": int(statistics.median(row["lut4"] for row in rows)),
        "flip_flops": int(statistics.median(row["flip_flops"] for row in rows)),
        "dsp": int(statistics.median(row["dsp"] for row in rows)),
        "bram": int(statistics.median(row["bram"] for row in rows)),
        "median_fmax_mhz": statistics.median(
            row["maximum_frequency_mhz"] for row in rows
        ),
        "all_seeds_meet_50mhz": all(row["meets_50mhz"] for row in rows),
    }


def fewer(reference, value):
    return 100.0 * (reference - value) / reference


def change(reference, value):
    return 100.0 * (value - reference) / reference


def instruction_count(path):
    count = 0
    for raw in re.findall(
        r"^\s*[0-9a-f]+:\s+([0-9a-f]{8})\s", path.read_text(encoding="utf-8"), re.M
    ):
        word = int(raw, 16)
        if word & 0xC000707F == 0x0000200B:
            count += 1
    return count


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("build", type=Path)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    reference = json.loads(args.reference.read_text(encoding="utf-8"))
    model = json.loads(args.model.read_text(encoding="utf-8"))
    levels = {}
    speedups = []
    counts = []
    for level in LEVELS:
        result = measurements(
            args.build / "fsri-results" / f"mlkem{level}-fsri-measurements.jsonl"
        )
        software = reference["levels"][level]["software"]
        portable = reference["levels"][level]["portable"]
        fqmul = reference["levels"][level]["fqmul"]
        count = instruction_count(
            args.build
            / f"mlk{level}_ffuse2_ifuse2_rpair_bcachelate_xfsri.dis"
        )
        gain = fewer(software["total"], result["total"])
        result.update(
            {
                "fewer_cycles_vs_portable_percent": fewer(
                    portable["total"], result["total"]
                ),
                "fewer_cycles_vs_software_percent": gain,
                "fewer_cycles_vs_fqmul_percent": fewer(
                    fqmul["total"], result["total"]
                ),
                "model_two_cycle_gain_percent": model["levels"][level][
                    "latency_sweep"
                ]["2"]["gain_percent"],
                "static_fsri_instructions": count,
            }
        )
        levels[level] = result
        speedups.append(software["total"] / result["total"])
        counts.append(count)

    if any(count != 116 for count in counts):
        raise RuntimeError(f"unexpected fsri instruction counts: {counts}")

    baseline = synthesis(args.build / "results" / "baseline-synthesis.json")
    fsri = synthesis(args.build / "results" / "fsri-synthesis.json")
    fqmul = reference["synthesis"]["fqmul"]
    fsri.update(
        {
            "lut4_change_vs_baseline_percent": change(
                baseline["lut4"], fsri["lut4"]
            ),
            "flip_flop_change_vs_baseline_percent": change(
                baseline["flip_flops"], fsri["flip_flops"]
            ),
            "fmax_change_vs_baseline_percent": change(
                baseline["median_fmax_mhz"], fsri["median_fmax_mhz"]
            ),
            "lut4_change_vs_fqmul_percent": change(fqmul["lut4"], fsri["lut4"]),
            "fmax_change_vs_fqmul_percent": change(
                fqmul["median_fmax_mhz"], fsri["median_fmax_mhz"]
            ),
        }
    )

    report = {
        "schema": "pqc-poly-bench/fsri-comparison-v1",
        "levels": levels,
        "three_level_geometric_mean_speedup": math.prod(speedups) ** (1.0 / 3.0),
        "synthesis": {
            "baseline": baseline,
            "fsri": fsri,
            "fqmul_reference": fqmul,
        },
        "gates": {
            "static_instruction_count_passed": all(count == 116 for count in counts),
            "all_levels_faster_than_software": all(
                levels[level]["fewer_cycles_vs_software_percent"] > 0.0
                for level in LEVELS
            ),
            "all_seeds_meet_50mhz": fsri["all_seeds_meet_50mhz"],
            "fewer_lut4_than_fqmul": fsri["lut4"] < fqmul["lut4"],
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
