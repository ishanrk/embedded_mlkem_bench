#!/usr/bin/env python3
import json
import shutil
import subprocess
from workbench_data import root, work, out, run, digest, write


def main():
    # rerun only the FSRI block-level depth-8 task for reuse and sliced implementations
    results = {"schema": "pqc-poly-bench/bounded-check-v1", "tool": run(["sby", "--version"]),
               "yosys": run(["yosys", "-V"]), "scope": "pcpi bmc depth 8 only rvfi depth 22 not rerun",
               "assumptions": "existing fsri_properties harness fixed arbitrary operands valid fsri encoding unsupported non multiply encoding optional reset cancellation",
               "source_sha256": digest(root / "targets/picorv32/rtl/pqc_pcpi_mlkem.sv"), "checks": []}
    for name, impl in (("reuse", 0), ("sliced", 1)):
        directory = work / f"formal-{name}"
        directory.mkdir(parents=True, exist_ok=True)
        for file in ("fsri.sby", "fsri_properties.sv", "rvfi_fsri_monitor.sv"):
            shutil.copyfile(root / "targets/picorv32/formal" / file, directory / file)
        for file in ("pqc_pcpi_mlkem.sv", "pqc_picorv32_core_top.sv"):
            shutil.copyfile(root / "targets/picorv32/rtl" / file, directory / file)
        shutil.copyfile(root / "build/picorv32-sim/_deps/picorv32-src/picorv32.v", directory / "picorv32.v")
        harness = directory / "fsri_properties.sv"
        # generated harness selects the implementation without editing repository RTL
        harness.write_text(harness.read_text().replace(".ENABLE_FSRI(1'b1)", f".ENABLE_FSRI(1'b1), .FSRI_IMPL({impl})"))
        command = ["sby", "-f", "fsri.sby", "pcpi"]
        try:
            completed = subprocess.run(command, cwd=directory, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=120)
            (directory / "run.log").write_text(completed.stdout)
            status = (directory / "fsri_pcpi/status").read_text().strip() if (directory / "fsri_pcpi/status").exists() else "ERROR"
        except subprocess.TimeoutExpired:
            status = "TIMEOUT"
        results["checks"].append({"variant": name, "task": "pcpi", "bound": 8, "engine": "abc bmc3", "result": status,
                                   "command": command, "cwd": str(directory), "harness_sha256": digest(harness)})
        print(name, status, flush=True)
    write("bounded-checks.json", results)


if __name__ == "__main__":
    main()
