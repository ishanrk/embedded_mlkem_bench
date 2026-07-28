#!/usr/bin/env python3
import json
import pathlib
import shutil
import sys
from workbench_data import out, root, work, run, digest, write


def main():
    renderer = root / "web/node_modules/.bin/netlistsvg"
    if not shutil.which("yosys") or not renderer.exists():
        sys.exit("schematic unavailable install yosys and run npm ci in web")
    manifest = {"schema": "pqc-poly-bench/schematic-v1", "variants": {}}
    catalog = json.loads((out / "catalog.json").read_text())
    for variant in dict.fromkeys(t["variant"] for t in catalog["traces"]):
        trace = json.loads((out / next(t["file"] for t in catalog["traces"] if t["variant"] == variant)).read_text())
        source = out / trace["source"]
        target = work / f"{variant}-netlist.json"
        parameters = " ".join(f"-set {k} {v}" for k, v in trace["parameters"].items())
        command = ["yosys", "-p", f"read_verilog -sv {source}; chparam {parameters} pqc_pcpi_mlkem; hierarchy -top pqc_pcpi_mlkem; proc; opt; write_json {target}"]
        run(command, work / f"{variant}-schematic.log")
        svg = f"{variant}-schematic.svg"
        render = [renderer, target, "-o", out / svg]
        run(render)
        manifest["variants"][variant] = {"svg": svg, "source_sha256": digest(source), "parameters": trace["parameters"],
                                         "stage": "Yosys RTLIL after proc and opt with word-level arithmetic cells",
                                         "command": [str(x) for x in command], "render_command": [str(x) for x in render],
                                         "yosys": run(["yosys", "-V"]), "netlistsvg": "1.0.2", "json_sha256": digest(target),
                                         "svg_sha256": digest(out / svg), "source": trace["source"]}
    write("schematics.json", manifest)
    catalog["inputs"]["scripts/workbench_schematic.py"] = digest(pathlib.Path(__file__))
    catalog["artifacts"].update({p.name: digest(p) for p in out.iterdir() if p.is_file() and p.name != "catalog.json"})
    write("catalog.json", catalog)
    print(f"generated {len(manifest['variants'])} word level pcpi schematics")


if __name__ == "__main__":
    main()
