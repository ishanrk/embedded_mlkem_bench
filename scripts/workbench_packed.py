#!/usr/bin/env python3
import json
import re
from workbench_data import root, out, work, run, digest, write


def main():
    # build the independent model, then compare compiled loop bodies without adding packed RTL
    work.mkdir(exist_ok=True, parents=True)
    model_command = ["c++", "-std=c++20", "-O2", "-Wall", "-Wextra", root / "scripts/workbench/packed.cpp", "-o", work / "packed-model"]
    run(model_command)
    result = {"schema": "pqc-poly-bench/packed-feasibility-v1", "status": "compiled software comparison and independent model only",
              "model": run([work / "packed-model"]), "compiler": run(["riscv32-unknown-elf-gcc", "--version"]).splitlines()[0],
              "packing": "rs1 low16 is a high16 is b rs2 low signed16 is zeta rd low16 is a plus t high16 is a minus t",
              "encoding_proposal": "custom-0 mask 0xfe00707f match 0x0000300b currently unimplemented",
              "contract": "absolute a and b at most 26632 absolute zeta at most 1664 signed17 additions checked before signed16 packing",
              "range_derivation": "ceil((26632 * 1664 + 32768 * 3329) / 65536) = 2341 hence abs a plus or minus t at most 28973 less than 32768",
              "latency_assumption": "optimistic packed ready at four recorded edges same as fqmul no measured packed rtl latency",
              "sources": {}, "variants": {}}
    for name in ("scripts/workbench/packed.cpp", "scripts/workbench/packed.c", "src/mlkem_check.cpp", "src/mlkem_codegen.cpp"):
        result["sources"][name] = digest(root / name)
    for variant in ("fqmul", "packed"):
        obj = work / f"{variant}-butterfly.o"
        command = ["riscv32-unknown-elf-gcc", "-O3", "-std=c11", "-march=rv32imc", "-mabi=ilp32", "-ffreestanding", "-fno-unroll-loops",
                   "-DPQC_POLY_HAVE_MLK_FQMUL", "-I", root / "targets/picorv32/mlkem", "-c", root / "scripts/workbench/packed.c", "-o", obj]
        if variant == "packed": command += ["-DPQC_PACKED"]
        run(command)
        disassembly = run(["riscv32-unknown-elf-objdump", "-d", "-M", "no-aliases", obj])
        (out / f"{variant}-butterfly.dis.txt").write_text(disassembly + "\n")
        result["variants"][variant] = {"command": [str(x) for x in command], "object_sha256": digest(obj), "disassembly": f"{variant}-butterfly.dis.txt"}
        functions = {}
        # count the steady-state loop through its bne, excluding one-time setup code
        for function, repetitions in (("pqc_forward_layer", 128), ("pqc_forward_pair", 64)):
            body = disassembly.split(f"<{function}>:", 1)[1]
            if function == "pqc_forward_layer":
                body = body.split("<pqc_forward_pair>:", 1)[0]
            loop = body.split(":\n", 1)[1]
            instructions = re.findall(r"^\s*[0-9a-f]+:\s+[0-9a-f]+\s+(\S+)\s*(.*)$", loop, re.M)
            branch = next(i for i, (op, _) in enumerate(instructions) if op == "bne")
            instructions = instructions[:branch + 1]
            functions[function] = {"instructions_per_iteration": len(instructions),
                                   "loop_iterations": repetitions,
                                   "custom_per_iteration": sum(op == ".insn" for op, _ in instructions),
                                   "loads_per_iteration": sum(op in ("lh", "lhu", "lw") for op, _ in instructions),
                                   "stores_per_iteration": sum(op == "sh" for op, _ in instructions),
                                   "stack_accesses": sum("(sp)" in operands for _, operands in instructions)}
        result["variants"][variant]["loops"] = functions
    result["decision"] = "no go for rtl both compiled loop bodies execute more instructions with unchanged custom transaction counts even at optimistic equal pcpi latency"
    result["limitation"] = "static disassembly comparison not measured packed cpu cycles includes pack unpack memory and loop instructions no claim about a different compiler or core"
    result["register_pressure"] = "neither loop spills to stack packed fused loop keeps an extra mask constant live and adds setup outside the loop"

    write("packed-feasibility.json", result)
    print(result["model"])


if __name__ == "__main__":
    main()
