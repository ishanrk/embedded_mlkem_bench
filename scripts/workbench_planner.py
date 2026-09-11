#!/usr/bin/env python3
import json
import pathlib
from workbench_data import out, root, work, run, digest, write


def main():
    # compiles the planner and records execution order from generated forward NTT code
    work.mkdir(exist_ok=True, parents=True)
    command = ["c++", "-std=c++20", "-O2", "-I", root / "include", root / "scripts/workbench/planner.cpp",
               root / "src/mlkem_plan.cpp", root / "src/mlkem_check.cpp", root / "src/mlkem_codegen.cpp", "-o", work / "planner"]
    run(command)
    data = json.loads(run([work / "planner", work / "generated"]))
    data["operation_order"] = {}
    # inserts a recorder before each generated butterfly multiplication
    replacements = {
        "const int16_t t = pqc_fqmul(r[j + length], zeta);": "record(j, j + length, zeta_index - 1, length, 0);",
        "const int16_t t0 = pqc_fqmul(x2, z0);": "record(start + j, start + 2U * length + j, groups + g, 2U * length, 1);",
        "const int16_t t1 = pqc_fqmul(x3, z0);": "record(start + length + j, start + 3U * length + j, groups + g, 2U * length, 1);",
        "const int16_t u1z = pqc_fqmul(u1, z1);": "record(start + j, start + length + j, 2U * groups + 2U * g, length, 1);",
        "const int16_t v1z = pqc_fqmul(v1, z2);": "record(start + 2U * length + j, start + 3U * length + j, 2U * groups + 2U * g + 1U, length, 1);",
    }
    prologue = '''#include <stdio.h>
static void record(unsigned left, unsigned right, unsigned zeta, unsigned length, unsigned fused)
{
    printf("%u %u %u %u %u\\n", left, right, zeta, length, fused);
}
'''
    for traversal in ("stage", "fuse2"):
        source = (work / "generated" / f"{traversal}.c").read_text()
        for needle, extra in replacements.items():
            assert source.count(needle) == 1, needle
            source = source.replace(needle, extra + "\n            " + needle)
        source = prologue + source + "\nint main(void) { int16_t r[256] = {0}; pqc_mlkem_ntt(r); return 0; }\n"
        path = work / f"{traversal}-observed.c"
        path.write_text(source)
        run(["cc", "-std=c11", "-O0", path, "-o", work / traversal])
        events = []
        for line in run([work / traversal]).splitlines():
            left, right, zeta, length, fused = map(int, line.split())
            events.append({"left": left, "right": right, "zeta_index": zeta, "length": length, "fused": bool(fused)})
        # seven layers of 128 butterflies produce 896 events
        assert len(events) == 896
        actual = sorted((e["left"], e["right"], e["zeta_index"]) for e in events)
        expected = sorted((r["left_base"] + j, r["right_base"] + j, r["zeta_index"]) for r in data["forward_records"] for j in range(r["length"]))
        assert actual == expected
        data["operation_order"][traversal] = events
    data["provenance"] = {"command": [str(x) for x in command], "compiler": run(["c++", "--version"]).splitlines()[0],
                          "order": "observed generated forward C execution with zero coefficients only indices shown no live coefficient values",
                          "bounds": "aggregate conservative bounds from existing planner and independent checker not per-stage proofs"}
    write("planner.json", data)
    catalog = json.loads((out / "catalog.json").read_text())
    for name in ("scripts/workbench_planner.py", "scripts/workbench/planner.cpp", "src/mlkem_plan.cpp", "src/mlkem_check.cpp", "src/mlkem_codegen.cpp", "include/pqc_poly/mlkem_plan.hpp"):
        catalog["inputs"][name] = digest(root / name)
    catalog["artifacts"]["planner.json"] = digest(out / "planner.json")
    write("catalog.json", catalog)
    print("exported 144 checked plans 2304 memory checks and both observed forward schedules")


if __name__ == "__main__":
    main()
