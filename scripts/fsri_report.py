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
EXPECTED_STATIC_FSRI = 116


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


def hardware_gates(baseline, candidate):
    lut4 = change(baseline["lut4"], candidate["lut4"]) <= 5.0
    flip_flops = change(baseline["flip_flops"], candidate["flip_flops"]) <= 5.0
    fmax = change(baseline["median_fmax_mhz"], candidate["median_fmax_mhz"]) >= -2.0
    dsp = candidate["dsp"] == baseline["dsp"]
    bram = candidate["bram"] == baseline["bram"]
    seeds = candidate["all_seeds_meet_50mhz"]
    return {
        "all_seeds_meet_50mhz": seeds,
        "lut4_budget_passed": lut4,
        "flip_flop_budget_passed": flip_flops,
        "fmax_budget_passed": fmax,
        "dsp_budget_passed": dsp,
        "bram_budget_passed": bram,
        "hardware_budget_passed": all((seeds, lut4, flip_flops, fmax, dsp, bram)),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("build", type=Path)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--combinational", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    reference = json.loads(args.reference.read_text(encoding="utf-8"))
    combinational = json.loads(args.combinational.read_text(encoding="utf-8"))
    model = json.loads(args.model.read_text(encoding="utf-8"))
    model_cpi = model["pico_rv32_cpi"]["multiplier_reuse_pcpi_instruction"]
    levels = {}
    speedups = []
    combinational_speedups = []
    fqmul_speedups = []
    red32_speedups = []
    counts = []
    operation_gates = []
    for level in LEVELS:
        result = measurements(
            args.build / "fsri-results" / f"mlkem{level}-fsri-measurements.jsonl"
        )
        software = reference["levels"][level]["software"]
        portable = reference["levels"][level]["portable"]
        fqmul = reference["levels"][level]["fqmul"]
        red32 = reference["levels"][level]["red32"]
        fast = combinational["levels"][level]
        count = instruction_count(
            args.build / f"mlk{level}_ffuse2_ifuse2_rpair_bcachelate_xfsri.dis"
        )
        prediction = model["levels"][level]["multiplier_reuse_prediction"]
        gain = fewer(software["total"], result["total"])
        fast_gain = fewer(software["total"], fast["total"])
        result.update(
            {
                "fewer_cycles_vs_portable_percent": fewer(
                    portable["total"], result["total"]
                ),
                "fewer_cycles_vs_software_percent": gain,
                "fewer_cycles_vs_combinational_percent": fewer(
                    fast["total"], result["total"]
                ),
                "fewer_cycles_vs_fqmul_percent": fewer(
                    fqmul["total"], result["total"]
                ),
                "fewer_cycles_vs_red32_percent": fewer(
                    red32["total"], result["total"]
                ),
                "retained_combinational_gain_percent": 100.0 * gain / fast_gain,
                "model_multiplier_reuse_pcpi_cpi": model_cpi,
                "model_multiplier_reuse_gain_percent": prediction[
                    "gain_vs_software_percent"
                ],
                "static_fsri_instructions": count,
            }
        )
        levels[level] = result
        speedups.append(software["total"] / result["total"])
        combinational_speedups.append(software["total"] / fast["total"])
        fqmul_speedups.append(software["total"] / fqmul["total"])
        red32_speedups.append(software["total"] / red32["total"])
        counts.append(count)
        operation_gates.extend(
            result[operation] <= 1.02 * software[operation] for operation in OPS
        )

    if any(count != EXPECTED_STATIC_FSRI for count in counts):
        raise RuntimeError(f"unexpected fsri instruction counts: {counts}")

    baseline = synthesis(args.build / "results" / "baseline-synthesis.json")
    fsri = synthesis(args.build / "results" / "fsri-synthesis.json")
    fast = combinational["synthesis"]
    fqmul = reference["synthesis"]["fqmul"]
    red32 = reference["synthesis"]["red32"]
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
            "lut4_change_vs_combinational_percent": change(
                fast["lut4"], fsri["lut4"]
            ),
            "flip_flop_change_vs_combinational_percent": change(
                fast["flip_flops"], fsri["flip_flops"]
            ),
            "fmax_change_vs_combinational_percent": change(
                fast["median_fmax_mhz"], fsri["median_fmax_mhz"]
            ),
            "lut4_change_vs_fqmul_percent": change(fqmul["lut4"], fsri["lut4"]),
            "fmax_change_vs_fqmul_percent": change(
                fqmul["median_fmax_mhz"], fsri["median_fmax_mhz"]
            ),
            "lut4_change_vs_red32_percent": change(red32["lut4"], fsri["lut4"]),
            "fmax_change_vs_red32_percent": change(
                red32["median_fmax_mhz"], fsri["median_fmax_mhz"]
            ),
        }
    )

    static_count_passed = all(count == EXPECTED_STATIC_FSRI for count in counts)
    all_levels_faster_than_software = all(
        levels[level]["fewer_cycles_vs_software_percent"] > 0.0 for level in LEVELS
    )
    all_levels_faster_than_combinational = all(
        levels[level]["fewer_cycles_vs_combinational_percent"] > 0.0
        for level in LEVELS
    )
    all_levels_faster_than_fqmul = all(
        levels[level]["fewer_cycles_vs_fqmul_percent"] > 0.0 for level in LEVELS
    )
    all_levels_faster_than_red32 = all(
        levels[level]["fewer_cycles_vs_red32_percent"] > 0.0 for level in LEVELS
    )
    no_operation_regression = all(operation_gates)
    gates = hardware_gates(baseline, fsri)
    budget_feasible_candidate = (
        static_count_passed
        and all_levels_faster_than_software
        and no_operation_regression
        and gates["hardware_budget_passed"]
    )
    strict_cycle_winner = all_levels_faster_than_fqmul and all_levels_faster_than_red32

    report = {
        "schema": "pqc-poly-bench/fsri-comparison-v3",
        "architecture": "multiplier_reuse",
        "levels": levels,
        "three_level_geometric_mean_speedup": math.prod(speedups) ** (1.0 / 3.0),
        "reference_geometric_mean_speedup": {
            "combinational_fsri": math.prod(combinational_speedups) ** (1.0 / 3.0),
            "fqmul": math.prod(fqmul_speedups) ** (1.0 / 3.0),
            "red32": math.prod(red32_speedups) ** (1.0 / 3.0),
        },
        "synthesis": {
            "baseline": baseline,
            "fsri": fsri,
            "combinational_fsri_reference": fast,
            "fqmul_reference": fqmul,
            "red32_reference": red32,
        },
        "gates": {
            "static_instruction_count_passed": static_count_passed,
            "all_levels_faster_than_software": all_levels_faster_than_software,
            "all_levels_faster_than_combinational": all_levels_faster_than_combinational,
            "all_levels_faster_than_fqmul": all_levels_faster_than_fqmul,
            "all_levels_faster_than_red32": all_levels_faster_than_red32,
            "no_operation_regression_over_two_percent": no_operation_regression,
            **gates,
            "fewer_lut4_than_combinational": fsri["lut4"] < fast["lut4"],
            "fewer_lut4_than_fqmul": fsri["lut4"] < fqmul["lut4"],
            "fewer_lut4_than_red32": fsri["lut4"] < red32["lut4"],
            "strict_cycle_winner": strict_cycle_winner,
            "budget_feasible_candidate": budget_feasible_candidate,
            "merge_candidate": budget_feasible_candidate,
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
