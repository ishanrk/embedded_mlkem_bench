# pqc-poly-bench

`pqc-poly-bench` is a constraint-driven polynomial-kernel selector for PQC
experiments. The primary ring is NTRU-HPS-2048-509,
`Z_2048[x] / (x^509 - 1)`, on an RV32IM target.

The current checkpoint separates two concerns that must not be conflated:

- `formula` contains independently tested NTRU implementations of Karatsuba,
  Toom-Cook, NTT, and TMVP.
- `selector` parses a target request, enumerates schoolbook execution schedules,
  proves their range, memory, aliasing, and target-size legality, ranks the legal
  candidates deterministically, and retains an independent checker as an
  emission gate.

The formula implementations are not yet placed in the selector's candidate
pool. Each needs its own independently checked range, scratch-memory, and cost
model before a comparison would be meaningful.

## Operations and schedules

The request layer supports both polynomial wrap conventions explicitly:

- `cyclic_mul` for `Z_q[x] / (x^n - 1)`, including NTRU-HPS-2048-509
- `negacyclic_mul` for `Z_q[x] / (x^n + 1)`, including the ML-KEM architecture
  example

The first selectable family is schoolbook multiplication with three schedules:

- `sb_full`: form a `2n - 1` convolution buffer, then fold it
- `sb_fold`: accumulate directly into an `n`-coefficient buffer using a chosen
  tile size
- `sb_out`: compute one output coefficient at a time without explicit scratch

`alias: "no"` means the output is disjoint from the inputs; the two inputs may
still be the same polynomial. `alias: "may"` permits output overlap too, so
`sb_out` is rejected for that contract.

## Run

Select and emit the NTRU schedule:

```bash
cargo run -p pqc-poly-explore -- examples/ntruhps2048509.json -o out
```

The ML-KEM-shaped negacyclic example uses the same pipeline:

```bash
cargo run -p pqc-poly-explore -- examples/mlkem.json -o out
```

Use `--plan PLAN_ID` to emit a particular legal candidate instead of the static
winner. Each run writes reproducible artifacts:

- `kernel.c` and `kernel.h`: portable C lowering
- `kernel.rs`: the equivalent standalone `no_std` Rust lowering
- `plan.json`: selected plan, analysis, score, and verification status
- `cands.json`: every candidate, including rejected candidates and reasons

## Layout

- `crates/ring`: fixed NTRU-HPS-2048-509 semantics and the exact reference
  multiplication
- `crates/formula`: current Karatsuba, Toom-Cook, NTT, and TMVP kernels
- `crates/selector`: strict request schema, plan records, bounds, candidate
  search, static ranking, Pareto frontier, and independent checks
- `crates/codegen`: checked C and `no_std` Rust lowering
- `crates/explore`: deterministic artifact emission and command-line interface
- `examples`: cyclic NTRU and negacyclic ML-KEM request files

## Current limits

- The score model is a deterministic bootstrap, not a claim about target speed.
- `tmp_bytes` counts explicit algorithm scratch, not compiler spills or caller
  input/output storage.
- `% q` is functionally tested but is not yet a target constant-time proof.
- Accumulator storage is selected as signed 32 or 64 bit; input and output
  coefficients remain signed 32 bit in generated kernels.
- Target-size checks bound every modeled input, output, and scratch object by
  the target's signed pointer-offset limit as well as its unsigned size limit.
- Host compilation validates generated-kernel semantics, not RV32 cycle counts.

## Test

```bash
cargo test --workspace --all-targets
```

The tests cover strict request parsing, exact ranking and frontier results,
independent-checker mutations, cyclic and negacyclic wrap behavior, alias-safe
plans, generated-code compilation, and differential comparison with simple
references.

Algorithm references:

1. [Karatsuba](https://en.wikipedia.org/wiki/Karatsuba_algorithm)
2. [Toom-Cook](https://eprint.iacr.org/2023/678)
3. [NTT](https://eprint.iacr.org/2024/585.pdf)
4. [TMVP](https://open.metu.edu.tr/handle/11511/98549)
