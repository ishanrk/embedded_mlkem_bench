#!/usr/bin/env python3

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RAW = ROOT / "results" / "raw" / "picorv32-step2-3f13dce5-69d24e37"
STEP3 = ROOT / "results" / "raw" / "picorv32-step3-fd803594-69d24e37" / "fqmul-final-comparison.json"
CURRENT = ROOT / "results" / "current-comparison.json"
CASES = {"512": (2, 9, 4), "768": (3, 12, 4), "1024": (4, 15, 4)}


def peak(path, operation):
    value = 0
    for line in path.read_text().splitlines():
        row = json.loads(line)
        if row.get("operation") == operation:
            value = max(value, row["calibrated_cycles"])
    if value == 0:
        raise RuntimeError(operation)
    return value


def main():
    current = json.loads(CURRENT.read_text())
    step3 = json.loads(STEP3.read_text())
    portable = {
        row["level"]: row["portable"]["complete_total_median_cycles"]
        for row in step3["levels"]
    }
    levels = {}
    for level, (k, base_calls, cache_calls) in CASES.items():
        path = RAW / f"mlk{level}_ffuse2_ifuse2_rpair_bcachelate_xnone-project.jsonl"
        base = peak(path, f"base_dot_k{k}")
        cache = peak(path, "mulcache")
        software = current["levels"][level]["software"]["total"]
        removed = base_calls * base + cache_calls * cache
        zero = software - removed
        levels[level] = {
            "k": k,
            "base_dot_calls": base_calls,
            "mulcache_calls": cache_calls,
            "peak_base_dot_cycles": base,
            "peak_mulcache_cycles": cache,
            "software_cycles": software,
            "portable_cycles": portable[level],
            "zero_cost_cycles": zero,
            "max_gain_vs_software_percent": 100.0 * removed / software,
            "max_gain_vs_portable_percent": 100.0 * (portable[level] - zero) / portable[level],
            "passes_10_percent": removed * 10 >= software,
        }
    print(json.dumps({"schema": "pqc-poly-bench/basmul2-bound-v1", "levels": levels}, indent=2))


if __name__ == "__main__":
    main()
