#!/usr/bin/env python3

import importlib.util
import json
import pathlib
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("pqc_results", ROOT / "scripts/results.py")
RESULTS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RESULTS)


def write(path, value):
    path.write_text(json.dumps(value), encoding="utf-8")


def make_files(directory):
    for variant_index, variant in enumerate(RESULTS.VARIANTS):
        for level_index, level in enumerate(RESULTS.LEVELS):
            base = 1000 + level_index * 100
            values = {
                "keygen": base + variant_index * 10,
                "encapsulation": base + 100 + variant_index * 10,
                "decapsulation": base + 200 + variant_index * 10,
            }
            write(
                directory / f"{variant}-{level}.json",
                {
                    "schema": "pqc-poly-bench/mlkem-run-v2",
                    "variant": variant,
                    "level": level,
                    "verified": True,
                    "output_checksum": 1234 + level_index,
                    "custom_instruction_count": 0 if variant == "baseline" else 3,
                    "cycles": {
                        **{name: {"median": value} for name, value in values.items()},
                        "total": sum(values.values()),
                    },
                },
            )
        write(
            directory / f"{variant}-synthesis.json",
            {
                "schema": "pqc-poly-bench/synthesis-v2",
                "seeds": [
                    {
                        "seed": seed,
                        "status": "complete",
                        "meets_50mhz": True,
                        "lut4": 3000 + variant_index,
                        "flip_flops": 1000 + variant_index,
                        "dsp": 4,
                        "bram": 0,
                        "maximum_frequency_mhz": 60.0 + seed + variant_index,
                    }
                    for seed in range(1, 6)
                ],
            },
        )


def main():
    # complete fixtures exercise validation totals and baseline changes
    with tempfile.TemporaryDirectory() as temporary:
        directory = pathlib.Path(temporary)
        make_files(directory)
        summary = RESULTS.complete_summary(directory)
        assert summary["status"] == "complete"
        assert summary["measurements"]["baseline"]["512"]["total_cycles"] == 3300
        assert summary["measurements"]["fqmul"]["512"]["percent_change_vs_baseline"] > 0
        assert summary["hardware"]["fsri"]["median_fmax_mhz"] == 66.0

        broken = json.loads((directory / "fqmul-512.json").read_text())
        broken["output_checksum"] = 9999
        write(directory / "fqmul-512.json", broken)
        try:
            RESULTS.complete_summary(directory)
        except RuntimeError:
            pass
        else:
            raise AssertionError("output mismatch was accepted")

    pending = RESULTS.pending_summary()
    assert pending["status"] == "pending fair rerun"
    assert pending["measurements"]["red32"]["1024"]["total_cycles"] is None


if __name__ == "__main__":
    main()
