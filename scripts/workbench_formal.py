#!/usr/bin/env python3

import pathlib
import shutil

from workbench_data import OUT, ROOT, WORK, digest, refresh_catalog, run, write


def main():
    if shutil.which("sby") is None:
        raise RuntimeError("sby is required for formal evidence")
    rtl = ROOT / "targets/picorv32/rtl/pqc_pcpi_mlkem.sv"
    result = {
        "schema": "pqc-poly-bench/formal-checks-v2",
        "scope": "bounded direct PCPI arithmetic checks",
        "source_sha256": digest(rtl),
        "tool": run(["sby", "--version"]),
        "checks": [],
    }
    for name, depth in (("fqmul", 8), ("red32", 7), ("fsri", 3)):
        directory = WORK / f"formal-{name}"
        directory.mkdir(parents=True, exist_ok=True)
        for source in (
            rtl,
            ROOT / f"targets/picorv32/formal/{name}.sby",
            ROOT / f"targets/picorv32/formal/{name}_properties.sv",
        ):
            shutil.copyfile(source, directory / source.name)
        run(["sby", "-f", f"{name}.sby"], directory / "run.log", directory)
        status = (directory / name / "status").read_text(encoding="utf-8").strip()
        result["checks"].append(
            {"instruction": name, "depth": depth, "result": status}
        )
    OUT.mkdir(parents=True, exist_ok=True)
    write("formal-checks.json", result)
    refresh_catalog(pathlib.Path(__file__))


if __name__ == "__main__":
    main()
