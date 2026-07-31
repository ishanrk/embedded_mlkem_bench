#!/usr/bin/env python3

import pathlib
import shutil

from workbench_data import OUT, ROOT, WORK, digest, refresh_catalog, run, write


def main():
    renderer = ROOT / "web/node_modules/.bin/netlistsvg"
    if shutil.which("yosys") is None or not renderer.exists():
        raise RuntimeError("yosys and netlistsvg are required for schematics")
    source = ROOT / "targets/picorv32/rtl/pqc_pcpi_mlkem.sv"
    result = {"schema": "pqc-poly-bench/schematics-v2", "variants": {}}
    OUT.mkdir(parents=True, exist_ok=True)
    WORK.mkdir(parents=True, exist_ok=True)
    for name, parameter in (
        ("fqmul", "ENABLE_FQMUL"),
        ("red32", "ENABLE_RED32"),
        ("fsri", "ENABLE_FSRI"),
    ):
        netlist = WORK / f"{name}-netlist.json"
        svg = OUT / f"{name}-schematic.svg"
        command = [
            "yosys",
            "-p",
            f"read_verilog -sv {source}; chparam -set {parameter} 1 pqc_pcpi_mlkem; hierarchy -top pqc_pcpi_mlkem; proc; opt; write_json {netlist}",
        ]
        run(command, WORK / f"{name}-schematic.log")
        run([renderer, netlist, "-o", svg])
        result["variants"][name] = {
            "svg": svg.name,
            "source_sha256": digest(source),
            "netlist_sha256": digest(netlist),
            "svg_sha256": digest(svg),
            "stage": "Yosys word level logic before technology mapping",
        }
    write("schematics.json", result)
    refresh_catalog(pathlib.Path(__file__))


if __name__ == "__main__":
    main()
