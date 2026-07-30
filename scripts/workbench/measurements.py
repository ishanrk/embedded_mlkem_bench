import json
import pathlib
import statistics

# extract the checked-in raw rows used by the web workbench; this does not rerun benchmarks

def export(root, out):
    raw = root / "results/raw/picorv32-step3-fd803594-69d24e37"
    final = json.loads((raw / "fqmul-final-comparison.json").read_text())
    summary = json.loads((root / "results/summary.json").read_text())
    records = [json.loads(line) for line in (raw / "fqmul-measurements.jsonl").read_text().splitlines()]
    data = {"schema": "pqc-poly-bench/workbench-measurements-v1", "levels": {},
            "aggregation": "sum of per-operation medians across 30 inputs with three identical repeats per input",
            "verification": "historical verified flags are not independent local reruns",
            "sources": [str(raw.relative_to(root) / name) for name in
                        ("fqmul-final-comparison.json", "fqmul-measurements.jsonl", "fqmul-synthesis.json")]}
    # re-derive each displayed median so summary drift is caught during export
    for level in final["levels"]:
        key = level["level"]
        data["levels"][key] = {}
        for variant, source_key in (("portable", "portable"), ("software", "software"), ("fqmul", "joint")):
            selection = level[source_key]
            assert sum(selection["operations"].values()) == summary["levels"][key][variant]
            item = {"plan_id": selection["plan_id"], "cycles": selection["operations"],
                    "instructions": {}, "status": "raw evidence available", "examples": {}}
            if variant == "fqmul":
                rows = [r for r in records if r["plan_id"] == selection["plan_id"]]
                path = raw / "fqmul-measurements.jsonl"
            else:
                path = root / "results/raw/picorv32-step2-3f13dce5-69d24e37" / f"{selection['plan_id']}-project.jsonl"
                rows = [json.loads(line) for line in path.read_text().splitlines()]
            item["source"] = str(path.relative_to(root))
            data["sources"].append(item["source"])
            for op in ("keygen", "encapsulation", "decapsulation"):
                selected = [r for r in rows if r.get("operation") == op]
                assert len(selected) == 90
                cycles, instructions = [], []
                for i in range(30):
                    repeated = [r for r in selected if r["input"] == i]
                    assert sorted(r["repeat"] for r in repeated) == [0, 1, 2]
                    assert len({(r["calibrated_cycles"], r["instruction_count"]) for r in repeated}) == 1
                    cycles.append(repeated[0]["calibrated_cycles"])
                    instructions.append(repeated[0]["instruction_count"])
                assert int(statistics.median(cycles)) == selection["operations"][op]
                item["instructions"][op] = int(statistics.median(instructions))
                item["examples"][op] = selected[0]
            data["levels"][key][variant] = item
    synthesis = json.loads((raw / "fqmul-synthesis.json").read_text())
    data["synthesis"] = synthesis
    data["normalized_seeds"] = []
    # failed route records keep their original values but expose nulls to the UI
    for seed in synthesis["fqmul"]["seeds"]:
        failed = seed["maximum_frequency_mhz"] <= 0 or seed["lut4"] <= 0
        data["normalized_seeds"].append({"seed": seed["seed"], "status": "incomplete/failed run" if failed else "raw evidence available",
                                         "lut4": None if failed else seed["lut4"],
                                         "frequency_mhz": None if failed else seed["maximum_frequency_mhz"]})
    (out / "measurements.json").write_text(json.dumps(data, indent=2) + "\n")
    return data["sources"]
